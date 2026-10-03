/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2024 ValveResourceFormat Contributors
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filesystem_exporter.h"
#include "globalvariables.h"
#include "output.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "metadata_stringifier.h"
#include "format_float.h"
#include "dumpers/module_metadata/module_metadata.h"
#include <string_view>

namespace Dumpers::Schemas::FilesystemExporter
{

static std::map<std::string, int> g_unknownMetadataCounts;

static std::string CommentBlock(std::string str)
{
	size_t pos = 0;
	while ((pos = str.find('\n', pos)) != std::string::npos)
	{
		str.replace(pos, 1, "\n//");
		pos += 3;
	}

	return str;
}

static void OutputMetadataEntry(const IntermediateMetadata& entry, std::ofstream& output, bool tabulate)
{
	// KV3 defaults are written as comments after the fields
	if (entry.name == "MGetKV3ClassDefaults")
		return;

	output << (tabulate ? "\t" : "") << "// " << entry.name;

	if (entry.hasValue)
	{
		if (entry.stringValue)
		{
			output << " = " << CommentBlock(*entry.stringValue);
		}
		else
		{
			g_unknownMetadataCounts[entry.name]++;
			output << " (UNKNOWN FOR PARSER)";
		}
	}

	output << "\n";
}

// Writes a KV3 default on one line, with floats like FormatFloat and NaN or infinity unquoted
static void WriteDefault(const nlohmann::json& value, std::string& out)
{
	if (value.is_number_float())
	{
		out += FormatFloat(value.get<double>());
	}
	else if (value.is_string() && ModuleMetadata::IsNonFiniteFloat(value.get_ref<const std::string&>()))
	{
		out += value.get_ref<const std::string&>();
	}
	else if (value.is_structured())
	{
		out += value.is_object() ? "{ " : "[ ";

		for (auto it = value.begin(); it != value.end(); ++it)
		{
			if (it != value.begin())
				out += ", ";

			if (value.is_object())
				out += nlohmann::json(it.key()).dump() + ": ";

			WriteDefault(*it, out);
		}

		out += value.is_object() ? " }" : " ]";
	}
	else
	{
		out += value.dump();
	}
}

// Zero, false, empty, null, or a container of only those
static bool IsZeroDefault(const nlohmann::json& value)
{
	if (value.is_structured())
		return std::all_of(value.begin(), value.end(), [](const nlohmann::json& item) { return IsZeroDefault(item); });

	if (value.is_number())
		return value.get<double>() == 0.0;

	if (value.is_boolean())
		return !value.get<bool>();

	if (value.is_string())
		return value.get_ref<const std::string&>().empty();

	return value.is_null();
}

static const nlohmann::json* FindMetadataValue(const std::vector<IntermediateMetadata>& metadata, std::string_view name)
{
	for (const auto& entry : metadata)
	{
		if (entry.name == name)
			return entry.jsonValue ? &*entry.jsonValue : nullptr;
	}

	return nullptr;
}

// Defaults are keyed by the KV3 transfer name when a field has one
static const nlohmann::json* FindFieldDefault(const nlohmann::json& defaults, const IntermediateSchemaClassField& field)
{
	for (const auto& entry : field.metadata)
	{
		if (entry.name == "MKV3TransferName" && entry.stringValue && entry.stringValue->size() >= 2)
		{
			// String metadata is written quoted
			auto it = defaults.find(entry.stringValue->substr(1, entry.stringValue->size() - 2));
			if (it != defaults.end())
				return &*it;
		}
	}

	auto it = defaults.find(field.name);
	return it != defaults.end() ? &*it : nullptr;
}

// The file for a class or enum, recorded so outdated files can be removed
static std::filesystem::path GetOutputPath(const std::filesystem::path& schemaPath, const std::string& module, std::string name, std::map<std::string, std::unordered_set<std::string>>& foundFiles)
{
	// Some classes have :: in them which we can't save.
	std::replace(name.begin(), name.end(), ':', '_');
	auto [files, isNewModule] = foundFiles.try_emplace(module);
	files->second.insert(name);

	if (isNewModule)
		std::filesystem::create_directories(schemaPath / module);

	return (schemaPath / module / name).replace_extension(".h");
}

static bool DumpClasses(const std::vector<IntermediateSchemaClass>& classes, const std::filesystem::path& schemaPath, std::map<std::string, std::unordered_set<std::string>>& foundFiles)
{
	for (const auto& intermediateClass : classes)
	{
		const auto path = GetOutputPath(schemaPath, intermediateClass.module, intermediateClass.name, foundFiles);
		std::ofstream output(path);

		// Output metadata entries as comments before the class definition
		for (const auto& metadata : intermediateClass.metadata)
		{
			OutputMetadataEntry(metadata, output, false);
		}

		output << "class " << intermediateClass.name;
		Globals::stringsIgnoreStream << intermediateClass.name << "\n";

		bool wroteBase = false;
		for (const auto& parent : intermediateClass.parents)
		{
			if (!wroteBase)
			{
				output << " : public " << parent.name;
				wroteBase = true;
			}
			else
			{
				output << ", public " << parent.name;
			}
		}

		output << "\n{\n";

		// KV3 defaults are written after the fields they belong to rather than as one block before the class
		auto defaults = FindMetadataValue(intermediateClass.metadata, "MGetKV3ClassDefaults");
		if (defaults && !defaults->is_object())
			defaults = nullptr;

		for (const auto& field : intermediateClass.fields)
		{
			// Output metadata entries as comments before the field definition
			for (const auto& metadata : field.metadata)
			{
				OutputMetadataEntry(metadata, output, true);
			}

			output << "\t" << field.type->m_sTypeName.String() << " " << field.name << ";";

			auto value = defaults ? FindFieldDefault(*defaults, field) : nullptr;
			if (value && !IsZeroDefault(*value))
			{
				std::string text;
				WriteDefault(*value, text);
				output << " // = " << text;
			}

			output << "\n";
			Globals::stringsIgnoreStream << field.name << "\n";
		}

		output << "};\n";

		if (!CloseOutput(output, path))
			return false;
	}

	return true;
}

static bool DumpEnums(const std::vector<IntermediateSchemaEnum>& enums, const std::filesystem::path& schemaPath, std::map<std::string, std::unordered_set<std::string>>& foundFiles)
{
	for (const auto& intermediateEnum : enums)
	{
		const auto path = GetOutputPath(schemaPath, intermediateEnum.module, intermediateEnum.name, foundFiles);
		std::ofstream output(path);

		for (const auto& metadata : intermediateEnum.metadata)
		{
			OutputMetadataEntry(metadata, output, false);
		}

		output << "enum " << intermediateEnum.name << " : " << (intermediateEnum.stringAlignment.has_value() ? *intermediateEnum.stringAlignment : "unknown alignment type") << "\n{\n";
		Globals::stringsIgnoreStream << intermediateEnum.name << "\n";

		for (const auto& member : intermediateEnum.members)
		{
			// Output metadata entries as comments before the field definition
			for (const auto& metadata : member.metadata)
			{
				OutputMetadataEntry(metadata, output, true);
			}

			output << "\t" << member.name << " = " << member.value << ",\n";
			Globals::stringsIgnoreStream << member.name << "\n";
		}

		output << "};\n";

		if (!CloseOutput(output, path))
			return false;
	}

	return true;
}

bool Dump(const std::vector<IntermediateSchemaEnum>& enums, const std::vector<IntermediateSchemaClass>& classes)
{
	const auto schemaPath = Globals::outputPath / "schemas";
	std::map<std::string, std::unordered_set<std::string>> foundFiles;
	std::filesystem::create_directories(schemaPath);

	if (!DumpClasses(classes, schemaPath, foundFiles) || !DumpEnums(enums, schemaPath, foundFiles))
		return false;

	for (const auto& [name, count] : g_unknownMetadataCounts)
		spdlog::warn("Metadata '{}' is not in metadatalist.h ({} usages), value {}", name, count, g_unknownMetadataSamples[name]);

	std::unordered_set<std::string> modules;
	for (const auto& [module, files] : foundFiles)
		modules.insert(module);

	// Collected first, as folders are renamed and removed
	std::vector<std::filesystem::directory_entry> entries(std::filesystem::directory_iterator(schemaPath), {});

	for (const auto& entry : entries)
	{
		if (!entry.is_directory())
			continue;

		// A folder whose name differs only in case is renamed to its module
		const auto module = GetKeptOutputName(entry, modules);
		if (!module)
		{
			spdlog::info("Removing orphan schema folder {}", entry.path().generic_string());
			std::filesystem::remove_all(entry.path());
			continue;
		}

		RemoveOrphanFiles(schemaPath / *module, foundFiles.at(*module), "schema");
	}

	return true;
}

} // namespace Dumpers::Schemas::FilesystemExporter
