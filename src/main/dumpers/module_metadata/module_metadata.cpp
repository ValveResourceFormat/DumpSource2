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
#include <spdlog/spdlog.h>
#include "modules.h"
#include "utils/module.h"
#include "utils/common.h"
#include "globalvariables.h"
#include "keyvalues3.h"
#include <unordered_set>

namespace Dumpers::ModuleMetadata
{

void GetModuleMetadata(const CModule& module, SimpleCUtlString& err, SimpleCUtlString& buf)
{
	spdlog::trace("Dumping metadata for {}", module.m_pszModule);

	typedef void* (*ExtractModuleMetadata)(SimpleCUtlString& str);
	auto extractModuleMetadataFn = module.GetSymbol<ExtractModuleMetadata>("ExtractModuleMetadata");

	SimpleCUtlString additional_info;
	auto kv3 = extractModuleMetadataFn(additional_info);

	// Modules without metadata return null on Linux (and an empty kv3 on Windows)
	if (!kv3)
	{
		if (additional_info.Get())
			spdlog::debug("{} has no metadata: {}", module.m_pszModule, additional_info.Get());

		return;
	}

	typedef int (*SaveKV3Text_ToString)(KV3ID_t const&, void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);
#ifdef WIN32
	static auto saveKV3Text_ToStringFn = Modules::tier0->GetSymbol<SaveKV3Text_ToString>("?SaveKV3Text_ToString@@YA_NAEBUKV3ID_t@@PEBVKeyValues3@@PEAVCUtlString@@2I@Z");
#else
	static auto saveKV3Text_ToStringFn = Modules::tier0->GetSymbol<SaveKV3Text_ToString>("_Z20SaveKV3Text_ToStringRK7KV3ID_tPK10KeyValues3P10CUtlStringS6_j");
#endif

	saveKV3Text_ToStringFn(g_KV3Encoding_Text, kv3, err, buf);

	if (additional_info.Get())
		spdlog::warn("{} has additional_info {}", module.m_pszModule, additional_info.Get());
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