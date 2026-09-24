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
#include "gamedata.h"
#include "globalvariables.h"
#include "modules.h"
#include "output.h"
#include "utils/common.h"
#include "utils/module.h"
#include "keyvalues3.h"
#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace Dumpers::ModuleMetadata
{

// Reads the module's metadata KV3 into kv3, which is null if it has none. Returns false if the module can't be asked for it.
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

	// Modules without metadata return null on Linux (and an empty kv3 on Windows)
	if (!kv3)
	{
		if (additional_info.Get())
			spdlog::debug("{} has no metadata: {}", module.m_pszModule, additional_info.Get());
	}
	else if (additional_info.Get())
	{
		spdlog::warn("{} has additional_info {}", module.m_pszModule, additional_info.Get());
	}

	return true;
}

// SaveKV3AsJSON writes NaN and infinity as bare words, which JSON has no value for, so they are quoted into strings
static std::string QuoteNonFiniteFloats(std::string_view text)
{
	// In this order so -nan is not matched as nan
	constexpr std::string_view nonFiniteFloats[] = { "-nan", "nan", "-inf", "inf" };

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
			auto nonFinite = std::find_if(std::begin(nonFiniteFloats), std::end(nonFiniteFloats), [&](std::string_view word) { return text.substr(i, word.size()) == word; });
			if (nonFinite != std::end(nonFiniteFloats))
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

nlohmann::ordered_json GetJSON(const CModule& module)
{
	void* kv3;
	if (!ExtractModuleMetadata(module, kv3))
		return nlohmann::ordered_json::value_t::discarded;

	if (!kv3)
		return nullptr;

	auto json = KV3ToJSON(kv3);
	if (json.is_discarded())
		spdlog::critical("Failed to convert {} metadata to JSON", module.m_pszModule);

	return json;
}

bool Dump()
{
	typedef bool (*SaveKV3Text_ToStringFn)(KV3ID_t const&, void* kv3, SimpleCUtlString& err, SimpleCUtlString& str, uint flags);
	static auto saveKV3Text_ToString = Modules::tier0->GetSymbol<SaveKV3Text_ToStringFn>(GameData::g_SaveKV3TextToStringSymbol);

	std::unordered_set<std::string> foundModules;
	const auto outputPath = Globals::outputPath / "module_metadata";

	for (const auto& module : Modules::allModules)
	{
		spdlog::trace("Dumping metadata for {}", module.m_pszModule);

		void* kv3;
		if (!ExtractModuleMetadata(module, kv3))
			return false;

		if (!kv3)
			continue;

		SimpleCUtlString err, buf;
		if (!saveKV3Text_ToString(g_KV3Encoding_Text, kv3, err, buf, KV3_SAVE_TEXT_NONE) || !buf.Get())
		{
			spdlog::critical("Failed to convert {} metadata to KV3 text: {}", module.m_pszModule, err.Get() ? err.Get() : "");
			return false;
		}

		auto sanitizedModuleName = std::string(module.m_pszModule);
		std::replace(sanitizedModuleName.begin(), sanitizedModuleName.end(), '/', '_');
		foundModules.insert(sanitizedModuleName);

		std::filesystem::create_directories(outputPath);

		const auto path = (outputPath / sanitizedModuleName).replace_extension(".kv3");
		std::ofstream output(path);
		output << buf.Get() << "\n";

		if (!CloseOutput(output, path))
			return false;
	}

	spdlog::info("Wrote module metadata for {} modules", foundModules.size());

	if (!std::filesystem::is_directory(outputPath))
		return true;

	RemoveOrphanFiles(outputPath, foundModules, "metadata");
	return true;
}

} // namespace Dumpers::ModuleMetadata
