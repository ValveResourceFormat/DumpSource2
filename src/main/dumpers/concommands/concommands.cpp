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

#include "concommands.h"
#include "interfaces.h"
#include "globalvariables.h"
#include <icvar.h>
#include <algorithm>
#include <fstream>
#include <vector>

namespace Dumpers::ConCommands
{

#define MINMAXVALUEPRINT(typeName) \
	stream << " " << value.typeName; 													\
																														\
	bool hasMinValue = pCvar->m_cvvMinValue;									\
	bool hasMaxValue = pCvar->m_cvvMaxValue;									\
																														\
	stream << " (";																						\
																														\
	if (hasMinValue)																					\
		stream << "min: " << pCvar->m_cvvMinValue->typeName;		\
																														\
	if (hasMaxValue)																					\
	{																													\
		if (hasMinValue)																				\
			stream << ", ";																				\
		stream << "max: " << pCvar->m_cvvMaxValue->typeName;		\
	}																													\
																														\
	if (hasMinValue || hasMaxValue)														\
		stream << ", ";																					\
																														\
	WriteFlags(pCvar, stream);																\
																														\
	stream << ")";

#define FCVAR_MISSING5	((uint64_t)1<<(uint64_t)30)
#define FCVAR_MISSING6	((uint64_t)1<<(uint64_t)31)
#define FCVAR_DEFENSIVE	((uint64_t)1<<(uint64_t)32)

std::vector<std::pair<uint64_t, const char*>> g_flagMap{
	{FCVAR_LINKED_CONCOMMAND, "linked_concommand"},
	{FCVAR_DEVELOPMENTONLY, "developmentonly"},
	{FCVAR_GAMEDLL, "gamedll"},
	{FCVAR_CLIENTDLL, "clientdll"},
	{FCVAR_HIDDEN, "hidden"},
	{FCVAR_PROTECTED, "protected"},
	{FCVAR_SPONLY, "sponly"},
	{FCVAR_ARCHIVE, "archive"},
	{FCVAR_NOTIFY, "notify"},
	{FCVAR_USERINFO, "userinfo"},
	{FCVAR_MISSING0, "hide"},
	{FCVAR_UNLOGGED, "unlogged"},
	{FCVAR_MISSING1, "missing1"},
	{FCVAR_REPLICATED, "replicated"},
	{FCVAR_CHEAT, "cheat"},
	{FCVAR_PER_USER, "per_user"},
	{FCVAR_DEMO, "demo"},
	{FCVAR_DONTRECORD, "dontrecord"},
	{FCVAR_MISSING2, "missing2"},
	{FCVAR_RELEASE, "release"},
	{FCVAR_MENUBAR_ITEM, "menubar_item"},
	{FCVAR_MISSING3, "missing3"},
	{FCVAR_NOT_CONNECTED, "notconnected"},
	{FCVAR_VCONSOLE_FUZZY_MATCHING, "vconsole_fuzzy_matching"},
	{FCVAR_SERVER_CAN_EXECUTE, "server_can_execute"},
	{FCVAR_CLIENT_CAN_EXECUTE, "client_can_execute"},
	{FCVAR_SERVER_CANNOT_QUERY, "server_cannot_query"},
	{FCVAR_VCONSOLE_SET_FOCUS, "vconsole_set_focus"},
	{FCVAR_CLIENTCMD_CAN_EXECUTE, "clientcmd_can_execute"},
	{FCVAR_EXECUTE_PER_TICK, "execute_per_tick"},
	{FCVAR_MISSING5, "missing5"},
	{FCVAR_MISSING6, "missing6"},
	{FCVAR_DEFENSIVE, "defensive"}
};

void WriteFlags(ConVar* pCvar, std::ofstream& stream)
{
	bool found = false;
	for (const auto& [value, name] : g_flagMap)
	{
		if (pCvar->flags & value)
		{
			stream << (found ? " " : "") << name;
			found = true;
		}
	}
}

void WriteValueLine(ConVar* pCvar, std::ofstream& stream)
{
	auto& value = *pCvar->m_cvvDefaultValue;

	switch (pCvar->m_eVarType)
	{
	case EConVarType_Bool:
	{
		stream << " " << (value.m_bValue ? "true" : "false") << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Int16:
	{
		MINMAXVALUEPRINT(m_i16Value);
		break;
	}
	case EConVarType_Int32:
	{
		MINMAXVALUEPRINT(m_i32Value);
		break;
	}
	case EConVarType_UInt32:
	{
		MINMAXVALUEPRINT(m_u32Value);
		break;
	}
	case EConVarType_Int64:
	{
		MINMAXVALUEPRINT(m_i64Value);
		break;
	}
	case EConVarType_UInt64:
	{
		MINMAXVALUEPRINT(m_u64Value);
		break;
	}
	case EConVarType_Float32:
	{
		MINMAXVALUEPRINT(m_flValue);
		break;
	}
	case EConVarType_Float64:
	{
		MINMAXVALUEPRINT(m_dbValue);
		break;
	}
	case EConVarType_String:
	{
		stream << " \"" << (value.m_szValue ? value.m_szValue : "") << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Color:
	{
		stream << " \"" << value.m_clrValue.r() << ", " << value.m_clrValue.g() << ", " << value.m_clrValue.b() << ", " << value.m_clrValue.a() << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Vector2:
	{
		stream << " \"" << value.m_vec2Value.x << ", " << value.m_vec2Value.y << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Vector3:
	{
		stream << " \"" << value.m_vec3Value.x << ", " << value.m_vec3Value.y << ", " << value.m_vec3Value.x << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Vector4:
	{
		stream << " \"" << value.m_vec4Value.x << ", " << value.m_vec4Value.y << ", " << value.m_vec4Value.x << ", " << value.m_vec4Value.w << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	case EConVarType_Qangle:
	{
		stream << " \"" << value.m_angValue.x << ", " << value.m_angValue.y << ", " << value.m_angValue.x << "\"" << " (";
		WriteFlags(pCvar, stream);
		stream << ")";
		break;
	}
	default:
		stream << " UNKNOWN VALUE TYPE";
		break;
	}
}

void Dump()
{
	ConVarHandle hCvarHandle;
	hCvarHandle.Set(0);
	ConVar* pCvar = nullptr;

	std::vector<ConVar*> convars;
	// there's always gonna be a lot of convars, let's save some reallocations
	convars.reserve(1000);

	do
	{
		pCvar = Interfaces::cvar->GetConVar(hCvarHandle);

		hCvarHandle.Set(hCvarHandle.Get() + 1);

		if (!pCvar)
			continue;

		convars.push_back(pCvar);
	} while (pCvar);

	std::sort(convars.begin(), convars.end(), [](const ConVar* a, const ConVar* b) {
		return strcmp(a->m_pszName, b->m_pszName) < 0;
	});

	std::ofstream output(Globals::outputPath / "convars.txt");

	for (const auto cvar : convars)
	{
		output << cvar->m_pszName;
		WriteValueLine(cvar, output);
		output << "\n\t" << (cvar->m_pszHelpString[0] ? cvar->m_pszHelpString : "<no description>");

		output << "\n" << std::endl;
	}
}

} // namespace Dumpers::ConCommands