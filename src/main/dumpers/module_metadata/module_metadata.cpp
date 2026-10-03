/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2026 ValveResourceFormat Contributors
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

#include "module_metadata.h"
#include "sections.h"
#include "gamedata.h"
#include "globalvariables.h"
#include "modules.h"
#include "output.h"
#include "utils/common.h"
#include "utils/module.h"
#include <algorithm>
#include <map>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace Dumpers::ModuleMetadata
{

// Reads the module's metadata KV3 into kv3, which is null if it has none. Returns false if the module can't be asked for it
// or failed to build it.
static bool ExtractModuleMetadata(const CModule& module, void*& kv3)
{
	typedef void* (*ExtractModuleMetadataFn)(SimpleCUtlString& str);
	auto extractModuleMetadataFn = (ExtractModuleMetadataFn)dlsym(module.m_hModule, "ExtractModuleMetadata");

	if (!extractModuleMetadataFn)
	{
		spdlog::critical("{} does not export ExtractModuleMetadata", module.m_pszModule);
		return false;
	}

	SimpleCUtlString additional_info;
	kv3 = extractModuleMetadataFn(additional_info);

#ifdef _WIN32
	// The metadata providers in the module build it, and modules without any return an empty kv3.
	// Null means a provider failed, like Pulse when its bindings assert, with the error in additional_info.
	if (!kv3)
	{
		spdlog::critical("Metadata of {} failed to build: {}", module.m_pszModule, additional_info.Get() ? additional_info.Get() : "no error given");
		return false;
	}
#else
	// Modules without metadata return null on Linux
	if (!kv3 && additional_info.Get())
		spdlog::debug("{} has no metadata: {}", module.m_pszModule, additional_info.Get());
#endif

	return true;
}

// How SaveKV3AsJSON writes NaN and infinity, in this order so -nan is not matched as nan
static constexpr std::string_view g_NonFiniteFloats[] = { "-nan", "nan", "-inf", "inf" };

bool IsNonFiniteFloat(std::string_view text)
{
	return std::find(std::begin(g_NonFiniteFloats), std::end(g_NonFiniteFloats), text) != std::end(g_NonFiniteFloats);
}

// SaveKV3AsJSON writes NaN and infinity as bare words, which JSON has no value for, so they are quoted into strings
static std::string QuoteNonFiniteFloats(std::string_view text)
{
	std::string result;
	bool inString = false;

	for (size_t i = 0; i < text.size(); i++)
	{
		if (inString)
		{
			if (text[i] == '\\')
				result += text[i++];
			else if (text[i] == '"')
				inString = false;
		}
		else if (text[i] == '"')
		{
			inString = true;
		}
		else
		{
			// Outside strings, only numbers and true/false/null are bare, none of which contain these
			auto nonFinite = std::find_if(std::begin(g_NonFiniteFloats), std::end(g_NonFiniteFloats), [&](std::string_view word) { return text.substr(i, word.size()) == word; });
			if (nonFinite != std::end(g_NonFiniteFloats))
			{
				result += '"';
				result += *nonFinite;
				result += '"';
				i += nonFinite->size() - 1;
				continue;
			}
		}

		result += text[i];
	}

	return result;
}

nlohmann::ordered_json KV3ToJSON(void* kv3)
{
	typedef bool (*SaveKV3AsJSONFn)(void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);
	static auto saveKV3AsJSON = Modules::tier0->GetSymbol<SaveKV3AsJSONFn>(GameData::g_SaveKV3AsJSONSymbol);

	SimpleCUtlString err, buf;
	if (!saveKV3AsJSON(kv3, err, buf) || !buf.Get())
		return nlohmann::ordered_json::value_t::discarded;

	return nlohmann::ordered_json::parse(QuoteNonFiniteFloats(buf.Get()), nullptr, false);
}

// Read once per module, both the metadata and entities dumpers use it
const nlohmann::ordered_json& GetJSON(const CModule& module)
{
	static std::unordered_map<std::string, nlohmann::ordered_json> cache;

	auto [it, inserted] = cache.try_emplace(module.m_pszModule);
	if (!inserted)
		return it->second;

	auto& json = it->second;

	void* kv3;
	if (!ExtractModuleMetadata(module, kv3))
	{
		json = nlohmann::ordered_json::value_t::discarded;
	}
	else if (kv3)
	{
		json = KV3ToJSON(kv3);
		if (json.is_discarded())
			spdlog::critical("Failed to convert {} metadata to JSON", module.m_pszModule);
	}

	return json;
}

// A folder per module with a text file per section, and unhandled.json for keys no section writer knows
bool Dump()
{
	std::unordered_set<std::string> foundModules;
	const auto outputPath = Globals::outputPath / "module_metadata";
	bool failed = false;

	for (const auto& module : Modules::allModules)
	{
		spdlog::trace("Dumping metadata for {}", module.m_pszModule);

		// The other modules are still written, but the files of failed ones are kept as they were
		const auto& metadata = GetJSON(module);
		if (metadata.is_discarded())
		{
			failed = true;
			continue;
		}

		// Modules without metadata providers have none
		if (metadata.is_null())
			continue;

		if (!metadata.is_object())
		{
			spdlog::critical("Module metadata of {} is not an object", module.m_pszModule);
			failed = true;
			continue;
		}

		std::map<std::string, std::string> files;
		auto unhandled = nlohmann::ordered_json::object();
		if (!WriteSections(module.m_pszModule, metadata, files, unhandled))
		{
			failed = true;
			continue;
		}

		if (!unhandled.empty())
		{
			std::vector<std::string> keys;
			for (const auto& [key, value] : unhandled.items())
				keys.push_back(key);

			spdlog::warn("Module metadata of {} has sections or keys that are not written as text, writing them to unhandled.json: {}", module.m_pszModule, fmt::join(keys, ", "));
			files["unhandled.json"] = unhandled.dump(1, '\t', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
		}

		// Like the entity datamaps and schema class descriptions, which are in other dumps
		if (files.empty())
			continue;

		const auto moduleFileName = GetModuleFileName(module.m_pszModule);
		foundModules.insert(moduleFileName);

		const auto modulePath = outputPath / moduleFileName;
		std::filesystem::create_directories(modulePath);

		std::unordered_set<std::string> fileNames;
		for (const auto& [fileName, text] : files)
		{
			const auto path = modulePath / fileName;
			std::ofstream output(path);
			output << text;

			if (!CloseOutput(output, path))
				return false;

			fileNames.insert(std::filesystem::path(fileName).stem().string());
		}

		RemoveOrphanFiles(modulePath, fileNames, "metadata");
	}

	spdlog::info("Wrote module metadata for {} modules", foundModules.size());

	if (failed)
	{
		spdlog::critical("Not removing orphan module metadata files, see above");
		return false;
	}

	if (!std::filesystem::is_directory(outputPath))
		return true;

	// Files are from before metadata was written as a folder per module, like client.kv3
	std::vector<std::filesystem::directory_entry> entries(std::filesystem::directory_iterator(outputPath), {});
	for (const auto& entry : entries)
	{
		if (entry.is_directory() && GetKeptOutputName(entry, foundModules))
			continue;

		spdlog::info("Removing orphan metadata {}", entry.path().generic_string());
		std::filesystem::remove_all(entry.path());
	}

	return true;
}

} // namespace Dumpers::ModuleMetadata
