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
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <unordered_set>

namespace Dumpers::ModuleMetadata
{

namespace
{

typedef void* (*ExtractModuleMetadataFn)(SimpleCUtlString& str);
typedef int (*SaveKV3Text_ToStringFn)(KV3ID_t const&, void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);

SaveKV3Text_ToStringFn GetSaveKV3TextToString()
{
#ifdef WIN32
	return Modules::tier0->GetSymbol<SaveKV3Text_ToStringFn>("?SaveKV3Text_ToString@@YA_NAEBUKV3ID_t@@PEBVKeyValues3@@PEAVCUtlString@@2I@Z");
#else
	return Modules::tier0->GetSymbol<SaveKV3Text_ToStringFn>("_Z20SaveKV3Text_ToStringRK7KV3ID_tPK10KeyValues3P10CUtlStringS6_j");
#endif
}

ExtractModuleMetadataFn TryGetExtractModuleMetadata(const CModule& module)
{
	return reinterpret_cast<ExtractModuleMetadataFn>(dlsym(module.m_hModule, "ExtractModuleMetadata"));
}

} // namespace

bool GetModuleMetadata(const CModule& module, SimpleCUtlString& err, SimpleCUtlString& buf)
{
	spdlog::info("Dumping metadata for {}", module.m_pszModule);

	auto extractModuleMetadataFn = TryGetExtractModuleMetadata(module);
	if (!extractModuleMetadataFn)
	{
		spdlog::warn("{} has no ExtractModuleMetadata export ({})", module.m_pszModule, dlerror());
		return false;
	}

	SimpleCUtlString additional_info;
	void* kv3 = nullptr;
	try
	{
		kv3 = extractModuleMetadataFn(additional_info);
	}
	catch (...)
	{
		spdlog::error("{} ExtractModuleMetadata threw an exception", module.m_pszModule);
		return false;
	}

	if (!kv3)
	{
		spdlog::warn("{} ExtractModuleMetadata returned null", module.m_pszModule);
		return false;
	}

	static auto saveKV3Text_ToStringFn = GetSaveKV3TextToString();
	if (!saveKV3Text_ToStringFn)
	{
		spdlog::error("SaveKV3Text_ToString not found in tier0");
		return false;
	}

	const auto saved = saveKV3Text_ToStringFn(g_KV3Encoding_Text, kv3, err, buf);
	if (!saved)
	{
		if (err.Get())
			spdlog::warn("{} SaveKV3Text_ToString failed: {}", module.m_pszModule, err.Get());
		else
			spdlog::warn("{} SaveKV3Text_ToString failed", module.m_pszModule);
		return false;
	}

	if (!buf.Get() || !buf.Get()[0])
	{
		spdlog::warn("{} produced empty module metadata buffer", module.m_pszModule);
		return false;
	}

	spdlog::info("{} metadata: {} bytes", module.m_pszModule, std::strlen(buf.Get()));

	if (additional_info.Get())
		spdlog::warn("{} has additional_info {}", module.m_pszModule, additional_info.Get());

	return true;
}

void Dump()
{
	spdlog::info("Dumping module metadata ({} loaded module(s))", Modules::allModules.size());
	std::unordered_set<std::string> foundModules;
	const auto outputPath = Globals::outputPath / "module_metadata";

	for (const auto& module : Modules::allModules)
	{
		SimpleCUtlString err, buf;
		if (!GetModuleMetadata(module, err, buf))
			continue;

		auto sanitizedModuleName = std::string(module.m_pszModule);
		std::replace(sanitizedModuleName.begin(), sanitizedModuleName.end(), '/', '_');
		foundModules.insert(sanitizedModuleName);

		if (!std::filesystem::is_directory(outputPath) && !std::filesystem::create_directories(outputPath))
		{
			spdlog::error("Failed to create module_metadata directory");
			return;
		}

		std::ofstream output((outputPath / sanitizedModuleName).replace_extension(".kv3"));
		output << buf.Get() << std::endl;
	}

	if (std::filesystem::exists(outputPath))
	{
		for (const auto& typeScopePath : std::filesystem::directory_iterator(outputPath))
		{
			if (foundModules.find(typeScopePath.path().stem().string()) == foundModules.end())
			{
				spdlog::info("Removing orphan metadata file {}", typeScopePath.path().generic_string());
				std::filesystem::remove(typeScopePath.path());
			}
		}
	}

	spdlog::info("Module metadata dump complete: {} kv3 file(s)", foundModules.size());
}

} // namespace Dumpers::ModuleMetadata
