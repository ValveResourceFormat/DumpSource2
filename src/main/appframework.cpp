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

#include "appframework.h"
#include "utils/module.h"
#include "modules.h"
#include "interfaces.h"
#include "application.h"
#include "globalvariables.h"
#include <schemasystem/schemasystem.h>
#include <fmt/format.h>
#include <map>
#include <set>
#include <fstream>
#include <spdlog/spdlog.h>
#include "dumpers/concommands/concommands.h"
#include "gamedata.h"

using namespace GameData;

// Some modules crash in Connect without VApplication001
static DumperApplication g_Application;

std::map<std::string, IAppSystem*> g_factoryMap;

void* AppSystemFactory(const char* pName, int* pReturnCode)
{
	if (!strcmp(pName, CVAR_INTERFACE_VERSION))
		return Interfaces::cvar;

	if (!strcmp(pName, SCHEMASYSTEM_INTERFACE_VERSION))
		return Interfaces::schemaSystem;

	if (!strcmp(pName, APPLICATION_INTERFACE_VERSION))
		return &g_Application;

	if (auto it = g_factoryMap.find(pName); it != g_factoryMap.end())
	{
		spdlog::trace("Connected {} interface", pName);
		return it->second;
	}

	spdlog::trace("Missing {} interface", pName);
	return nullptr;
}

static bool FileContains(const std::filesystem::path& path, std::string_view needle)
{
	std::string contents(std::filesystem::file_size(path), '\0');
	std::ifstream(path, std::ios::binary).read(contents.data(), contents.size());
	return contents.find(needle) != std::string::npos;
}

// Returns nullptr instead of exiting like CModule::FindInterface
static void* TryFindInterface(const CModule& module, const char* name)
{
	auto factory = (CreateInterfaceFn)dlsym(module.m_hModule, "CreateInterface");
	return factory ? factory(name, nullptr) : nullptr;
}

// Loads every module that links tier1 and reads its convar queues, without touching schemasystem or app systems
void InitializeModules()
{
	Modules::tier0 = std::make_unique<CModule>("", "tier0");
	Modules::schemaSystem = std::make_unique<CModule>("", "schemasystem");

	Dumpers::ConCommands::CollectQueues(*Modules::tier0);
	Dumpers::ConCommands::CollectQueues(*Modules::schemaSystem);

	struct ModuleFile
	{
		std::string path;
		std::string name;
		std::string file;
	};

	// CModule keeps pointers to the path and name
	static std::vector<ModuleFile> modules;
	std::set<std::string> seen{ "tier0", "schemasystem" };
	const std::string gameBin = fmt::format("../../{}/bin/{}", GAME_PATH, PLATFORM_FOLDER);

	// App systems are always loaded for their interfaces, other modules only if they link tier1 and can have convars or schemas
	auto addModule = [&](const std::string& path, const std::string& name, bool isAppSystem) {
		if (!seen.insert(name).second)
			return;

		auto file = (std::filesystem::current_path() / path / (MODULE_PREFIX + name + MODULE_EXT)).lexically_normal();
		if (std::filesystem::exists(file) && (isAppSystem || FileContains(file, g_Tier1ModuleMarker)))
			modules.push_back({ path, name, file.generic_string() });
	};

	for (const auto& appSystem : g_AppSystems)
		addModule(appSystem.gameBin ? gameBin : "", appSystem.moduleName, true);

	for (const auto& [path, prefix] : std::vector<std::pair<std::string, std::string>>{ { "", "" }, { "", "tools/" }, { "", "subtools/" }, { gameBin, "" } })
	{
		auto directory = std::filesystem::current_path() / path / prefix;
		if (!std::filesystem::is_directory(directory))
			continue;

		std::vector<std::string> found;
		for (const auto& entry : std::filesystem::directory_iterator(directory))
		{
			auto stem = entry.path().stem().string();
			if (entry.path().extension() == MODULE_EXT && stem.starts_with(MODULE_PREFIX))
				found.push_back(prefix + stem.substr(strlen(MODULE_PREFIX)));
		}

		std::sort(found.begin(), found.end());
		for (const auto& name : found)
			addModule(path, name, false);
	}

	// Some modules import dlls that the game does not ship (e.g. vfx_dx11 needs slang)
	std::string failed;
	for (const auto& module : modules)
	{
		spdlog::debug("Loading {}", module.file);

		if (!dlmount(module.file.c_str()))
		{
#ifdef _WIN32
			failed += fmt::format("{}{} (error {})", failed.empty() ? "" : ", ", module.name, GetLastError());
#else
			failed += fmt::format("{}{} ({})", failed.empty() ? "" : ", ", module.name, dlerror());
#endif
			continue;
		}

		// Added before reading the queues, which check that names point into a loaded module
		Modules::allModules.emplace_back(module.path.c_str(), module.name.c_str());
		Dumpers::ConCommands::CollectQueues(Modules::allModules.back());
	}

	spdlog::info("Loaded {} modules{}", Modules::allModules.size(), failed.empty() ? "" : fmt::format(", failed to load {}", failed));
}

// Installs schema bindings and connects app systems. Connecting frees the convar queues, so they must be read before this.
bool InitializeSchemas()
{
	Interfaces::cvar = (ICvar*)TryFindInterface(*Modules::tier0, CVAR_INTERFACE_VERSION);
	if (!Interfaces::cvar)
	{
		spdlog::critical("Could not find {} in tier0, the interface version in the SDK needs updating", CVAR_INTERFACE_VERSION);
		return false;
	}

	// schemasystem needs ICvar when connecting
	g_pCVar = Interfaces::cvar;
	Interfaces::cvar->Connect(Modules::tier0->GetFactory());

	Interfaces::schemaSystem = (CSchemaSystem*)TryFindInterface(*Modules::schemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	if (!Interfaces::schemaSystem)
	{
		spdlog::critical("Could not find {} in schemasystem, the interface version in the SDK needs updating", SCHEMASYSTEM_INTERFACE_VERSION);
		return false;
	}

	Interfaces::schemaSystem->Connect(&AppSystemFactory);
	Interfaces::schemaSystem->Init();

	typedef void* (*InstallSchemaBindings)(const char* interfaceName, void* pSchemaSystem);
	for (const auto& module : Modules::allModules)
	{
		if (auto fn = (InstallSchemaBindings)dlsym(module.m_hModule, "InstallSchemaBindings"))
			fn(SCHEMASYSTEM_INTERFACE_VERSION, Interfaces::schemaSystem);
	}

	std::vector<std::pair<const char*, IAppSystem*>> connectable;
	for (const auto& appSystem : g_AppSystems)
	{
		auto module = std::find_if(Modules::allModules.begin(), Modules::allModules.end(), [&](const CModule& m) { return !strcmp(m.m_pszModule, appSystem.moduleName); });
		if (module == Modules::allModules.end())
			continue;

		auto interface = (IAppSystem*)TryFindInterface(*module, appSystem.interfaceVersion.c_str());
		if (!interface)
		{
			spdlog::warn("{} does not expose {}, update its interface version in g_AppSystems in gamedata.h", appSystem.moduleName, appSystem.interfaceVersion);
			continue;
		}

		g_factoryMap[appSystem.interfaceVersion] = interface;
		connectable.emplace_back(appSystem.moduleName, interface);
	}

	std::string failed;
	for (const auto& [name, interface] : connectable)
	{
		spdlog::debug("Connecting {}", name);
		if (!interface->Connect(&AppSystemFactory))
			failed += fmt::format("{}{}", failed.empty() ? "" : ", ", name);
	}

	spdlog::info("Connected {} app systems", connectable.size());

	if (!failed.empty())
		spdlog::warn("Connect returned false for {}, add the app systems they need to g_AppSystems in gamedata.h (LOGLEVEL=trace lists missing interfaces)", failed);

	return true;
}