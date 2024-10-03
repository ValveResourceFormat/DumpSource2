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

static DumperApplication g_Application;

struct AppSystemInfo
{
	bool gameBin;
	const char* moduleName;
	std::string interfaceVersion;
	bool connect = true;
};

std::vector<AppSystemInfo> g_appSystems{
	{false, "filesystem_stdio", FILESYSTEM_INTERFACE_VERSION},
	{false, "resourcesystem", RESOURCESYSTEM_INTERFACE_VERSION, true},
	{true, "client", "Source2ClientConfig001"},
	{false, "engine2", SOURCE2ENGINETOSERVER_INTERFACE_VERSION},
	{true, "host", "Source2Host001" },
	{true, "matchmaking", "MATCHFRAMEWORK_001"},
	{true, "server", "Source2ServerConfig001"},
	{false, "animationsystem", "AnimationSystem_001"},
	{false, "materialsystem2", "TextLayout_001"},
	{false, "meshsystem", "MeshSystem001", false},
};

std::map<std::string, IAppSystem*> g_factoryMap;

void* AppSystemFactory(const char* pName, int* pReturnCode)
{
	if (!strcmp(pName, CVAR_INTERFACE_VERSION))
		return Interfaces::cvar;

	if (!strcmp(pName, SCHEMASYSTEM_INTERFACE_VERSION))
		return Interfaces::schemaSystem;
	
	if (!strcmp(pName, APPLICATION_INTERFACE_VERSION))
		return &g_Application;

	if (g_factoryMap.find(pName) != g_factoryMap.end())
	{
		printf("Connected %s\n", pName);
		return g_factoryMap.at(pName);
	}

	return nullptr;
}

void InitializeCoreModules()
{
	// Load modules (dlopen)
	Modules::schemaSystem = std::make_unique<CModule>("", "schemasystem");
	Modules::tier0 = std::make_unique<CModule>("", "tier0");

	// Find interfaces
	Interfaces::cvar = Modules::tier0->FindInterface<ICvar*>(CVAR_INTERFACE_VERSION);
	Interfaces::schemaSystem = Modules::schemaSystem->FindInterface<CSchemaSystem*>(SCHEMASYSTEM_INTERFACE_VERSION);

	// Manually connect interfaces
	Interfaces::cvar->Connect(Modules::tier0->GetFactory());
	Interfaces::cvar->Init();

	Interfaces::schemaSystem->Connect(&AppSystemFactory);
	Interfaces::schemaSystem->Init();
}

void InitializeAppSystems()
{
	for (const auto& appSystem : g_appSystems)
	{
		std::string path = appSystem.gameBin ? fmt::format("../../{}/bin/{}", Globals::modName.c_str(), PLATFORM_FOLDER) : "";

		CModule module(path.c_str(), appSystem.moduleName);

		auto interface = module.FindInterface<IAppSystem*>(appSystem.interfaceVersion.c_str());

		g_factoryMap[appSystem.interfaceVersion] = interface;

		if (appSystem.connect)
		{

			interface->Connect(&AppSystemFactory);
			interface->Init();
		}
		else
		{
			// We can't connect this interface, let's at least dump schemas
			typedef void* (*InstallSchemaBindings)(const char* interfaceName, void* pSchemaSystem);
			InstallSchemaBindings fn = (InstallSchemaBindings)dlsym(module.m_hModule, "InstallSchemaBindings");

			fn(SCHEMASYSTEM_INTERFACE_VERSION, Interfaces::schemaSystem);
		}
	}
}