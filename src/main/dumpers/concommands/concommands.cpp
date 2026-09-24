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

// Yes this is shit, no we can't make it better.
#define _ALLOW_KEYWORD_MACROS 1
#define private public
#include <icvar.h>
#undef private
#undef _ALLOW_KEYWORD_MACROS

#include "concommands.h"
#include "globalvariables.h"
#include <algorithm>
#include <iterator>
#include <fstream>
#include <vector>
#include <iostream>
#include <map>
#include <optional>
#include <unordered_set>
#include "gamedata.h"
#include "modules.h"
#include <spdlog/spdlog.h>

namespace Dumpers::ConCommands
{

using namespace GameData;

#define FCVAR_MISSING1 (1ull << 30)
#define FCVAR_MISSING2 (1ull << 31)

std::vector<std::pair<uint64_t, const char*>> g_flagMap{
	{ FCVAR_LINKED_CONCOMMAND, "linked_concommand" },
	{ FCVAR_DEVELOPMENTONLY, "developmentonly" },
	{ FCVAR_GAMEDLL, "gamedll" },
	{ FCVAR_CLIENTDLL, "clientdll" },
	{ FCVAR_HIDDEN, "hidden" },
	{ FCVAR_PROTECTED, "protected" },
	{ FCVAR_SPONLY, "sponly" },
	{ FCVAR_ARCHIVE, "archive" },
	{ FCVAR_NOTIFY, "notify" },
	{ FCVAR_USERINFO, "userinfo" },
	{ FCVAR_REFERENCE, "reference" },
	{ FCVAR_UNLOGGED, "unlogged" },
	{ FCVAR_INITIAL_SETVALUE, "initial_setvalue" },
	{ FCVAR_REPLICATED, "replicated" },
	{ FCVAR_CHEAT, "cheat" },
	{ FCVAR_PER_USER, "per_user" },
	{ FCVAR_DEMO, "demo" },
	{ FCVAR_DONTRECORD, "dontrecord" },
	{ FCVAR_PERFORMING_CALLBACKS, "performing_Callbacks" },
	{ FCVAR_RELEASE, "release" },
	{ FCVAR_MENUBAR_ITEM, "menubar_item" },
	{ FCVAR_COMMANDLINE_ENFORCED, "commandline_enforced" },
	{ FCVAR_NOT_CONNECTED, "notconnected" },
	{ FCVAR_VCONSOLE_FUZZY_MATCHING, "vconsole_fuzzy_matching" },
	{ FCVAR_SERVER_CAN_EXECUTE, "server_can_execute" },
	{ FCVAR_CLIENT_CAN_EXECUTE, "client_can_execute" },
	{ FCVAR_SERVER_CANNOT_QUERY, "server_cannot_query" },
	{ FCVAR_VCONSOLE_SET_FOCUS, "vconsole_set_focus" },
	{ FCVAR_CLIENTCMD_CAN_EXECUTE, "clientcmd_can_execute" },
	{ FCVAR_EXECUTE_PER_TICK, "execute_per_tick" },
	{ FCVAR_MISSING1, "missing1" },
	{ FCVAR_MISSING2, "missing2" },
	{ FCVAR_DEFENSIVE, "defensive" }
};

void WriteFlags(uint64_t flags, std::ostream& stream)
{
	bool found = false;
	for (const auto& [value, name] : g_flagMap)
	{
		if (flags & value)
		{
			stream << (found ? " " : "") << name;
			found = true;
		}
	}
}

// Writes a numeric value with its min and max, the member selects which CVValue_t union member to print
template <typename T>
static void WriteMinMaxValue(T CVValue_t::* member, uint64_t flags, const CVValue_t* value, const CVValue_t* minValue, const CVValue_t* maxValue, std::ostream& stream)
{
	stream << " " << value->*member << " (";

	if (minValue)
		stream << "min: " << minValue->*member;

	if (maxValue)
	{
		if (minValue)
			stream << ", ";
		stream << "max: " << maxValue->*member;
	}

	if (minValue || maxValue)
		stream << ", ";

	WriteFlags(flags, stream);
	stream << ")";
}

void WriteValueLine(EConVarType type, uint64_t flags, const CVValue_t* value, const CVValue_t* minValue, const CVValue_t* maxValue, std::ostream& stream)
{
	switch (type)
	{
		case EConVarType_Bool:
		{
			stream << " " << (value->m_bValue ? "true" : "false") << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Int16:
			WriteMinMaxValue(&CVValue_t::m_i16Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_Int32:
			WriteMinMaxValue(&CVValue_t::m_i32Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_UInt32:
			WriteMinMaxValue(&CVValue_t::m_u32Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_Int64:
			WriteMinMaxValue(&CVValue_t::m_i64Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_UInt64:
			WriteMinMaxValue(&CVValue_t::m_u64Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_Float32:
			WriteMinMaxValue(&CVValue_t::m_fl32Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_Float64:
			WriteMinMaxValue(&CVValue_t::m_fl64Value, flags, value, minValue, maxValue, stream);
			break;
		case EConVarType_String:
		{
			stream << " \"" << (value->m_StringValue.m_pString ? value->m_StringValue.m_pString : "") << "\"" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Color:
		{
			stream << " [" << value->m_clrValue.r() << ", " << value->m_clrValue.g() << ", " << value->m_clrValue.b() << ", " << value->m_clrValue.a() << "]" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Vector2:
		{
			stream << " [" << value->m_vec2Value.x << ", " << value->m_vec2Value.y << "]" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Vector3:
		{
			stream << " [" << value->m_vec3Value.x << ", " << value->m_vec3Value.y << ", " << value->m_vec3Value.z << "]" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Vector4:
		{
			stream << " [" << value->m_vec4Value.x << ", " << value->m_vec4Value.y << ", " << value->m_vec4Value.z << ", " << value->m_vec4Value.w << "]" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		case EConVarType_Qangle:
		{
			stream << " [" << value->m_angValue.x << ", " << value->m_angValue.y << ", " << value->m_angValue.z << "]" << " (";
			WriteFlags(flags, stream);
			stream << ")";
			break;
		}
		default:
			stream << " UNKNOWN VALUE TYPE";
			break;
	}
}

void FixNewlineTabbing(std::string& str)
{
	auto it = str.begin();
	while ((it = std::find(it, str.end(), '\n')) != str.end())
	{
		if (it + 1 == str.end() || *(it + 1) != '\t')
			it = str.insert(it + 1, '\t') + 1;
		else
			it++;
	}

	// trim end of string
	if (str.back() == '\t')
		str.pop_back();

	if (str.back() == '\n')
		str.pop_back();
}

std::string EscapeDescription(std::string str)
{
	for (auto it = str.begin(); it != str.end(); it++)
	{
		if (*it == '\n')
		{
			*it = '\\';
			it = str.insert(it + 1, 'n');
		}
		else if (*it == '\t')
		{
			*it = '\\';
			it = str.insert(it + 1, 't');
		}
	}

	return str;
}

// Copied when read, the queue blocks are freed once the module calls ConVar_Register
struct QueuedValue_t
{
	bool m_bSet = false;
	bool m_bIsString = false;
	alignas(CVValue_t) uint8 m_Value[sizeof(CVValue_t)] = {};
	std::string m_String;

	void Set(EConVarType type, const uint8* value)
	{
		m_bSet = true;
		memcpy(m_Value, value, sizeof(m_Value));

		if (type == EConVarType_String)
		{
			m_bIsString = true;
			auto str = ((const CVValue_t*)value)->m_StringValue.m_pString;
			m_String = str ? str : "";
		}
	}

	const CVValue_t* Get()
	{
		if (!m_bSet)
			return nullptr;

		if (m_bIsString)
			((CVValue_t*)m_Value)->m_StringValue.m_pString = m_String.data();
		return (const CVValue_t*)m_Value;
	}
};

struct QueuedEntry_t
{
	const char* m_pszModule;
	std::string m_Name;
	std::string m_Help;
	uint64 m_nFlags;
	EConVarType m_eType = EConVarType_Invalid;
	QueuedValue_t m_Default;
	QueuedValue_t m_Min;
	QueuedValue_t m_Max;
};

struct Queue_t
{
	const char* m_pszKind;
	const char* m_pszFileName;
	std::vector<QueuedEntry_t> m_Entries;
	bool m_bFailed = false;
};

static Queue_t g_ConVarQueue{ "convar", "convars.txt" };
static Queue_t g_ConCommandQueue{ "concommand", "commands.txt" };

static QueuedEntry_t CopyQueued(const char* module, const ConVarRegList::Entry_t& queued)
{
	const auto& creation = queued.m_Info;
	const auto& info = creation.m_valueInfo;
	QueuedEntry_t entry{ module, creation.m_pszName, creation.m_pszHelpString ? creation.m_pszHelpString : "", creation.m_nFlags, info.m_eVarType };

	if (info.m_bHasDefault)
		entry.m_Default.Set(info.m_eVarType, info.m_defaultValue);
	if (info.m_bHasMin)
		entry.m_Min.Set(info.m_eVarType, info.m_minValue);
	if (info.m_bHasMax)
		entry.m_Max.Set(info.m_eVarType, info.m_maxValue);

	return entry;
}

static QueuedEntry_t CopyQueued(const char* module, const ConCommandRegList::Entry_t& queued)
{
	const auto& creation = queued.m_Info;
	return { module, creation.m_pszName, creation.m_pszHelpString ? creation.m_pszHelpString : "", creation.m_nFlags };
}

// A changed queue layout can still match the signature, so check what is read from the entries before using them.
// Returns what is invalid, or null.
static const char* ValidateQueued(const ConVarRegList::Entry_t& queued)
{
	const auto& creation = queued.m_Info;
	const auto& info = creation.m_valueInfo;

	if (!Modules::IsValidName(creation.m_pszName))
		return "name";
	if (creation.m_pszHelpString && !Modules::FindModuleContaining(creation.m_pszHelpString))
		return "help string";
	if (info.m_eVarType <= EConVarType_Invalid || info.m_eVarType >= EConVarType_MAX)
		return "type";

	for (auto flag : { &info.m_bHasDefault, &info.m_bHasMin, &info.m_bHasMax })
	{
		if (*(const uint8*)flag > 1)
			return "has value flags";
	}

	return nullptr;
}

static const char* ValidateQueued(const ConCommandRegList::Entry_t& queued)
{
	const auto& creation = queued.m_Info;

	if (!Modules::IsValidName(creation.m_pszName))
		return "name";
	if (creation.m_pszHelpString && !Modules::FindModuleContaining(creation.m_pszHelpString))
		return "help string";

	return nullptr;
}

template <typename RegList, size_t N>
static int CollectQueue(CModule& module, const byte (&signature)[N], Queue_t& queue)
{
	int error;
	auto match = (uint8_t*)module.FindSignature(signature, N - 1, error);

	if (error == SIG_FOUND_MULTIPLE)
	{
		spdlog::critical("Found multiple {} queue signature matches in {}, make the signature in gamedata.h more specific", queue.m_pszKind, module.m_pszModule);
		queue.m_bFailed = true;
		return 0;
	}

	if (!match)
	{
		if (g_RequiredQueueModules.contains(module.m_pszModule))
		{
			spdlog::critical("Could not find {} queue in {}, update the signature in gamedata.h", queue.m_pszKind, module.m_pszModule);
			queue.m_bFailed = true;
		}

		return 0;
	}

	auto head = Modules::GetGlobalFromSignatureMatch<RegList*>(match);
	int count = 0;

	for (auto list = *head; list; list = list->m_pPrev)
	{
		if (list->m_nSize > std::size(list->m_Entries))
		{
			spdlog::critical("{} list in {} has {} entries, the layout in gamedata.h needs updating", queue.m_pszKind, module.m_pszModule, list->m_nSize);
			queue.m_bFailed = true;
			return count;
		}

		for (uint32 i = 0; i < list->m_nSize; i++, count++)
		{
			if (auto invalid = ValidateQueued(list->m_Entries[i]))
			{
				spdlog::critical("{} list entry {} in {} has an invalid {}, the layout in gamedata.h needs updating", queue.m_pszKind, count, module.m_pszModule, invalid);
				queue.m_bFailed = true;
				return count;
			}

			queue.m_Entries.push_back(CopyQueued(module.m_pszModule, list->m_Entries[i]));
		}
	}

	return count;
}

void CollectQueues(CModule& module)
{
	auto cvars = CollectQueue<ConVarRegList>(module, g_ConVarQueueSignature, g_ConVarQueue);
	auto cmds = CollectQueue<ConCommandRegList>(module, g_ConCommandQueueSignature, g_ConCommandQueue);

	spdlog::debug("Queued in {}: {} convars, {} commands", module.m_pszModule, cvars, cmds);
}

static void WriteHelp(const char* name, const char* help, std::ofstream& output)
{
	std::string helpString = "<no description>";
	if (help && help[0])
	{
		helpString = help;
		Globals::stringsIgnoreStream << EscapeDescription(helpString) << "\n";
		FixNewlineTabbing(helpString);
	}

	output << "\n\t" << helpString;
	output << "\n"
		   << std::endl;

	Globals::stringsIgnoreStream << name << "\n";
}

// Flags passed to ConVar_Register
static uint64 GetModuleRegisterFlags(const char* module)
{
	if (!strcmp(module, "client"))
		return FCVAR_CLIENTDLL;

	if (!strcmp(module, "server"))
		return FCVAR_GAMEDLL;

	return 0;
}

// Merges declarations of the same name like ICvar does in registration order: references are ignored, flags are OR'd
// except for a few where the later declaration wins, and default/min/max/help come from the first one that has them
static QueuedEntry_t MergeQueued(const std::vector<QueuedEntry_t*>& entries)
{
	constexpr uint64 laterWins = FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_DONTRECORD | FCVAR_ARCHIVE | FCVAR_PER_USER;
	constexpr uint64 stripped = FCVAR_INITIAL_SETVALUE | FCVAR_PERFORMING_CALLBACKS;

	std::optional<QueuedEntry_t> merged;

	for (auto entry : entries)
	{
		if (entry->m_nFlags & FCVAR_REFERENCE)
			continue;

		auto flags = (entry->m_nFlags | GetModuleRegisterFlags(entry->m_pszModule)) & ~stripped;

		if (!merged)
		{
			merged = *entry;
			merged->m_nFlags = flags;
			continue;
		}

		merged->m_nFlags = flags | (merged->m_nFlags & ~laterWins);

		if (!merged->m_Default.m_bSet)
			merged->m_Default = entry->m_Default;
		if (!merged->m_Min.m_bSet)
			merged->m_Min = entry->m_Min;
		if (!merged->m_Max.m_bSet)
			merged->m_Max = entry->m_Max;
		if (merged->m_Help.empty())
			merged->m_Help = entry->m_Help;
	}

	// Only referenced, never declared
	if (!merged)
		return { entries[0]->m_pszModule, entries[0]->m_Name, "", FCVAR_REFERENCE, entries[0]->m_eType };

	return *merged;
}

static void WriteQueued(Queue_t& queue, bool isConVar)
{
	std::map<std::string, std::vector<QueuedEntry_t*>> byName;
	for (auto& entry : queue.m_Entries)
		byName[entry.m_Name].push_back(&entry);

	spdlog::info("Wrote {} {}s to {}", byName.size(), queue.m_pszKind, queue.m_pszFileName);

	std::ofstream output(Globals::outputPath / queue.m_pszFileName);

	for (const auto& [name, entries] : byName)
	{
		auto entry = MergeQueued(entries);
		output << name;

		if (isConVar)
		{
			// cl_color has a random default value on each start.
			alignas(CVValue_t) static const uint8 empty[sizeof(CVValue_t)] = {};
			auto value = entry.m_Default.m_bSet && name != "cl_color" ? entry.m_Default.Get() : (const CVValue_t*)empty;
			WriteValueLine(entry.m_eType, entry.m_nFlags, value, entry.m_Min.Get(), entry.m_Max.Get(), output);
		}
		else
		{
			output << " (";
			WriteFlags(entry.m_nFlags, output);
			output << ")";
		}

		WriteHelp(name.c_str(), entry.m_Help.c_str(), output);
	}
}

// Returns false if either could not be dumped, which is then not written to keep the previous dump
bool Dump()
{
	for (auto queue : { &g_ConVarQueue, &g_ConCommandQueue })
	{
		if (queue->m_bFailed)
			spdlog::critical("Not writing {} because the {} queue signature failed, see above", queue->m_pszFileName, queue->m_pszKind);
		else
			WriteQueued(*queue, queue == &g_ConVarQueue);
	}

	return !g_ConVarQueue.m_bFailed && !g_ConCommandQueue.m_bFailed;
}

} // namespace Dumpers::ConCommands