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

#include "dllmain.h"
#include "utils/common.h"
#include <vector>
#include <string>
#include <modules.h>
#include <memory>
#include <icvar.h>
#include <eiface.h>
#include <schemasystem/schemasystem.h>

void* AppSystemFactory(const char* pName, int* pReturnCode)
{

	if (!strcmp(pName, CVAR_INTERFACE_VERSION))
		return Modules::tier0->FindInterface<void*>(CVAR_INTERFACE_VERSION);

	if (!strcmp(pName, "SchemaSystem_001"))
		return Modules::schemaSystem->FindInterface<void*>("SchemaSystem_001");

	return nullptr;
}

int main()
{
	printf("Running\n");

	Modules::schemaSystem = std::make_unique<CModule>("", "schemasystem");
	Modules::tier0 = std::make_unique<CModule>("", "tier0");
	Modules::engine = std::make_unique<CModule>("", "engine2");
	Modules::client = std::make_unique<CModule>("../../" GAMEBIN, "client");

	auto cvar = Modules::tier0->FindInterface<ICvar*>(CVAR_INTERFACE_VERSION);

	cvar->Connect(Modules::tier0->GetFactory());
	cvar->Init();


	Modules::schemaSystem->FindInterface<IAppSystem*>("SchemaSystem_001")->Connect(&AppSystemFactory);
	Modules::schemaSystem->FindInterface<IAppSystem*>("SchemaSystem_001")->Init();

	Modules::engine->FindInterface<IAppSystem*>(SOURCE2ENGINETOCLIENT_INTERFACE_VERSION)->Connect(&AppSystemFactory);
	Modules::engine->FindInterface<IAppSystem*>(SOURCE2ENGINETOCLIENT_INTERFACE_VERSION)->Init();

	Modules::client->FindInterface<IAppSystem*>("Source2ClientConfig001")->Connect(&AppSystemFactory);
	Modules::client->FindInterface<IAppSystem*>("Source2ClientConfig001")->Init();

	ConVarHandle hCvarHandle;
	hCvarHandle.Set(0);
	ConVar* pCvar = nullptr;

	do
	{
		pCvar = cvar->GetConVar(hCvarHandle);

		hCvarHandle.Set(hCvarHandle.Get() + 1);

		if (!pCvar)
			continue;

		printf("name: %s\n", pCvar->m_pszName);

	} while (pCvar);


	auto schemaSystem = Modules::schemaSystem->FindInterface<CSchemaSystem*>("SchemaSystem_001");

	const auto& typeScopes = schemaSystem->m_TypeScopes;
	//Msg("test: %i\n", typeScopes.GetNumStrings());
	for (auto i = 0; i < typeScopes.GetNumStrings(); ++i)
	{
		const auto& typeScope = typeScopes[i];
		printf("Type scope: %s\n", typeScope->m_szScopeName);

		const auto& classes = typeScope->m_ClassBindings;

		UtlTSHashHandle_t* handles = new UtlTSHashHandle_t[classes.Count()];
		classes.GetElements(0, classes.Count(), handles);

		for (int j = 0; j < classes.Count(); ++j) {
			auto class_info = classes[handles[j]];

			printf("\t - %s\n", class_info->m_pszName);
		}
	}
}