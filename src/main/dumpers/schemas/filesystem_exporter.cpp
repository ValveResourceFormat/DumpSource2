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

		for (const auto& field : intermediateClass.fields)
		{
			// Output metadata entries as comments before the field definition
			for (const auto& metadata : field.metadata)
			{
				OutputMetadataEntry(metadata, output, true);
			}

			output << "\t" << field.type->m_sTypeName.String() << " " << field.name << ";\n";
			Globals::stringsIgnoreStream << field.name << "\n";
		}

		for (const auto& field : intermediateClass.staticFields)
		{
			for (const auto& metadata : field.metadata)
			{
				OutputMetadataEntry(metadata, output, true);
			}

			output << "\tstatic " << field.type->m_sTypeName.String() << " " << field.name << ";\n";
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

	for (const auto& entry : std::filesystem::directory_iterator(schemaPath))
	{
		auto projectName = entry.path().filename().string();
		auto files = foundFiles.find(projectName);

		if (entry.is_directory() && files == foundFiles.end())
		{
			spdlog::info("Removing orphan schema folder {}", entry.path().generic_string());
			std::filesystem::remove_all(entry.path());
		}
		else if (files != foundFiles.end())
		{
			RemoveOrphanFiles(entry.path(), files->second, "schema");
		}
	}

	return true;
}

} // namespace Dumpers::Schemas::FilesystemExporter
