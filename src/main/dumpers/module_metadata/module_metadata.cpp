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
#include <spdlog/spdlog.h>
#include "modules.h"
#include "utils/module.h"
#include "utils/common.h"
#include "globalvariables.h"
#include "keyvalues3.h"
#include <unordered_set>

namespace Dumpers::ModuleMetadata
{

// Returns the module's metadata KV3, or null if it has none
static void* ExtractModuleMetadata(const CModule& module)
{
	typedef void* (*ExtractModuleMetadataFn)(SimpleCUtlString& str);
	auto extractModuleMetadataFn = module.GetSymbol<ExtractModuleMetadataFn>("ExtractModuleMetadata");

	SimpleCUtlString additional_info;
	auto kv3 = extractModuleMetadataFn(additional_info);

	// Modules without metadata return null on Linux (and an empty kv3 on Windows)
	if (!kv3)
	{
		if (additional_info.Get())
			spdlog::debug("{} has no metadata: {}", module.m_pszModule, additional_info.Get());

		return nullptr;
	}

	if (additional_info.Get())
		spdlog::warn("{} has additional_info {}", module.m_pszModule, additional_info.Get());

	return kv3;
}

void GetModuleMetadata(const CModule& module, SimpleCUtlString& err, SimpleCUtlString& buf)
{
	spdlog::trace("Dumping metadata for {}", module.m_pszModule);

	auto kv3 = ExtractModuleMetadata(module);
	if (!kv3)
		return;

	typedef int (*SaveKV3Text_ToString)(KV3ID_t const&, void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);
	static auto saveKV3Text_ToStringFn = Modules::tier0->GetSymbol<SaveKV3Text_ToString>(GameData::g_SaveKV3TextToStringSymbol);

	saveKV3Text_ToStringFn(g_KV3Encoding_Text, kv3, err, buf);
}

nlohmann::ordered_json GetJSON(const CModule& module)
{
	auto kv3 = ExtractModuleMetadata(module);
	if (!kv3)
		return nullptr;

	typedef int (*SaveKV3AsJSON)(void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);
	static auto saveKV3AsJSONFn = Modules::tier0->GetSymbol<SaveKV3AsJSON>(GameData::g_SaveKV3AsJSONSymbol);

	SimpleCUtlString err, buf;
	if (!saveKV3AsJSONFn(kv3, err, buf) || !buf.Get())
	{
		spdlog::error("Failed to convert {} metadata to JSON: {}", module.m_pszModule, err.Get() ? err.Get() : "");
		return nlohmann::ordered_json::value_t::discarded;
	}

	return nlohmann::ordered_json::parse(buf.Get(), nullptr, false);
}

void Dump()
{
	std::unordered_set<std::string> foundModules;
	const auto outputPath = Globals::outputPath / "module_metadata";

	for (const auto& module : Modules::allModules)
	{
		SimpleCUtlString err, buf;
		GetModuleMetadata(module, err, buf);

		if (buf.Get())
		{
			auto sanitizedModuleName = std::string(module.m_pszModule);
			std::replace(sanitizedModuleName.begin(), sanitizedModuleName.end(), '/', '_');
			foundModules.insert(sanitizedModuleName);

			if (!std::filesystem::is_directory(outputPath) && !std::filesystem::create_directory(outputPath))
			{
				spdlog::error("Failed to create {}", outputPath.generic_string());
				return;
			}

			std::ofstream output((outputPath / sanitizedModuleName).replace_extension(".kv3"));
			output << buf.Get() << std::endl;
		}
	}

	spdlog::info("Wrote module metadata for {} modules", foundModules.size());

	if (!std::filesystem::is_directory(outputPath))
		return;

	for (const auto& typeScopePath : std::filesystem::directory_iterator(outputPath))
	{
		if (foundModules.find(typeScopePath.path().stem().string()) == foundModules.end())
		{
			spdlog::info("Removing orphan metadata file {}", typeScopePath.path().generic_string());
			std::filesystem::remove(typeScopePath.path());
		}
	}
}

} // namespace Dumpers::ModuleMetadata