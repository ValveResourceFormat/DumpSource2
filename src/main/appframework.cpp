/**
 * =============================================================================
 * CS2Dumper
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
#include "globalvariables.h"
#include <schemasystem/schemasystem.h>
#include <fmt/format.h>

void* AppSystemFactory(const char* pName, int* pReturnCode)
{
	if (!strcmp(pName, CVAR_INTERFACE_VERSION))
		return Interfaces::cvar;

	if (!strcmp(pName, SCHEMASYSTEM_INTERFACE_VERSION))
		return Interfaces::schemaSystem;

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

struct AppSystemInfo
{
	bool gameBin;
	const char* moduleName;
	const char* interfaceVersion;
};

std::vector<AppSystemInfo> g_appSystems{
	{true, "client", "Source2ClientConfig001"}
};

void InitializeAppSystems()
{
	for (const auto& appSystem : g_appSystems)
	{
		std::string path = appSystem.gameBin ? fmt::format("../../{}/bin/{}", Globals::modName.c_str(), PLATFORM_FOLDER) : "";

		CModule module(path.c_str(), appSystem.moduleName);

		auto interface = module.FindInterface<IAppSystem*>(appSystem.interfaceVersion);

		interface->Connect(&AppSystemFactory);
		interface->Init();
	}
}