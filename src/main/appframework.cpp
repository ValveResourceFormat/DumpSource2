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

#define CS2_ONLY (1<<0)

struct AppSystemInfo
{
	bool gameBin;
	const char* moduleName;
	std::string interfaceVersion;
	bool connect = true;
	uint64_t flags = 0;
};

std::vector<AppSystemInfo> g_appSystems{
	{ false, "filesystem_stdio", FILESYSTEM_INTERFACE_VERSION },
	{ false, "resourcesystem", RESOURCESYSTEM_INTERFACE_VERSION },
	{ true, "client", "Source2ClientConfig001" },
	{ false, "engine2", SOURCE2ENGINETOSERVER_INTERFACE_VERSION },
	{ true, "host", "GameSystem2HostHook" },
	{ true, "matchmaking", MATCHFRAMEWORK_INTERFACE_VERSION, true, CS2_ONLY },
	{ true, "server", SOURCE2SERVERCONFIG_INTERFACE_VERSION },
	{ false, "animationsystem", ANIMATIONSYSTEM_INTERFACE_VERSION },
	{ false, "materialsystem2", TEXTLAYOUT_INTERFACE_VERSION },
	{ false, "meshsystem", MESHSYSTEM_INTERFACE_VERSION, false },
	{ false, "networksystem", NETWORKSYSTEM_INTERFACE_VERSION, false}, // can't connect on linux cuz of missing gameinfo	in IApplication
	{ false, "panorama", PANORAMAUIENGINE_INTERFACE_VERSION },
	{ false, "particles", PARTICLESYSTEMMGR_INTERFACE_VERSION, false }, // needs renderdevice interface
	{ false, "pulse_system", PULSESYSTEM_INTERFACE_VERSION },
#ifdef _WIN32
	{ false, "rendersystemdx11", RENDER_UTILS_INTERFACE_VERSION },
#else
	{ false, "rendersystemvulkan", RENDER_UTILS_INTERFACE_VERSION },
#endif
	{ false, "scenefilecache", "SceneFileCache002" },
	{ false, "scenesystem", SCENEUTILS_INTERFACE_VERSION },
	{ false, "soundsystem", SOUNDOPSYSTEMEDIT_INTERFACE_VERSION },
	{ false, "vphysics2", VPHYSICS2HANDLE_INTERFACE_VERSION },
	{ false, "worldrenderer", WORLD_RENDERER_MGR_INTERFACE_VERSION },
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

// SceneUtils_001 tries to set convars on init
// which then calls convar change handlers that try to load main scenesystem appsystem
void SetConvarValueStub(ICvar* icvar, ConVarRef ref)
{
	ConVarRefAbstract cvar(ref);
	if (!cvar.IsConVarDataValid())
		return;

	// it also crashes if r_dopixelvisibility is true
	if (!strcmp("r_dopixelvisibility", cvar.GetName()))
	{
		cvar.SetBool(false);
	}
}

void InitializeCoreModules()
{
	// Load modules (dlopen)
	Modules::schemaSystem = std::make_unique<CModule>("", "schemasystem");
	Modules::tier0 = std::make_unique<CModule>("", "tier0");

	// Find interfaces
	Interfaces::cvar = Modules::tier0->FindInterface<ICvar*>(CVAR_INTERFACE_VERSION);
	g_pCVar = Interfaces::cvar;
	Interfaces::schemaSystem = Modules::schemaSystem->FindInterface<CSchemaSystem*>(SCHEMASYSTEM_INTERFACE_VERSION);

	// Manually connect interfaces
	Interfaces::cvar->Connect(Modules::tier0->GetFactory());
	Interfaces::cvar->Init();

	Interfaces::schemaSystem->Connect(&AppSystemFactory);
	Interfaces::schemaSystem->Init();

	auto vtb = *((void***)Interfaces::cvar);

#ifdef _WIN32
	DWORD _;
	VirtualProtect(vtb + 14, 8, PAGE_EXECUTE_READWRITE, &_);
	*(vtb + 14) = &SetConvarValueStub;
#else
	void* patchBytes = (void*)&SetConvarValueStub;
	Plat_WriteMemory(vtb + 14, (uint8_t*)&patchBytes, 8);
#endif
}

void InitializeAppSystems()
{
	for (const auto& appSystem : g_appSystems)
	{
#ifndef GAME_CS2
		if ((appSystem.flags & CS2_ONLY) != 0)
			continue;
#endif // !GAME_CS2

		std::string path = appSystem.gameBin ? fmt::format("../../{}/bin/{}", GAME_PATH, PLATFORM_FOLDER) : "";

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