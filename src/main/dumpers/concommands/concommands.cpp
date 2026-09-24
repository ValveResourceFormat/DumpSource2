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

// The queued default, min and max values are private in ConVarValueInfo_t, which has no getters
#define _ALLOW_KEYWORD_MACROS 1
#define private public
#include <icvar.h>
#undef private
#undef _ALLOW_KEYWORD_MACROS

#include "concommands.h"
#include "globalvariables.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <charconv>
#include <iterator>
#include <fstream>
#include <vector>
#include <map>
#include <optional>
#include <string_view>
#include "gamedata.h"
#include "output.h"
#include "modules.h"
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace Dumpers::ConCommands
{

using namespace GameData;

static const std::vector<std::pair<uint64_t, const char*>> g_flagMap{
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
	{ FCVAR_PERFORMING_CALLBACKS, "performing_callbacks" },
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
	// Not in every SDK yet
	{ 1ull << 30, "snapshot_ignored" },
	{ FCVAR_DEFENSIVE, "defensive" },
	{ 1ull << 34, "gameinfo_cannot_override" },
};

static std::vector<std::string> GetFlagNames(uint64_t flags)
{
	std::vector<std::string> names;
	for (const auto& [value, name] : g_flagMap)
	{
		if (flags & value)
		{
			names.push_back(name);
			flags &= ~value;
		}
	}

	// Flags without a name are written by bit, so they aren't lost
	for (int bit = 0; bit < 64; bit++)
	{
		if (flags & (1ull << bit))
			names.push_back(fmt::format("flag_{}", bit));
	}

	return names;
}

// Floats are written as the shortest text that reads back as the same value, like 100.1 or 1000000 rather than 1e+06,
// with at most 6 decimals for values that aren't exact in binary, like 0.015686275
template <typename T>
static std::string FormatFloat(T value)
{
	char buffer[512];
	std::string text(buffer, std::to_chars(buffer, std::end(buffer), value, std::chars_format::fixed).ptr);

	if (auto dot = text.find('.'); dot != std::string::npos && text.size() - dot - 1 > 6)
	{
		text = fmt::format("{:.6f}", value);
		text.erase(text.find_last_not_of('0') + 1);

		if (text.back() == '.')
			text.pop_back();
	}

	return text == "-0" ? "0" : text;
}

template <typename T>
static std::string FormatNumber(T value)
{
	if constexpr (std::is_floating_point_v<T>)
		return FormatFloat(value);
	else
		return std::to_string(value);
}

template <typename T>
static std::string FormatVector(const T& vector, int count)
{
	std::string text = "[";
	for (int i = 0; i < count; i++)
		text += (i ? ", " : "") + FormatFloat(vector[i]);

	return text + "]";
}

struct ConVarValue_t
{
	const char* m_pszType; // As named in schemas.json
	std::string m_Text;    // The same in convars.txt and schemas.json
	bool m_bHasMinMax;     // Only numbers have a min and max
};

static ConVarValue_t FormatValue(EConVarType type, const CVValue_t* value)
{
	switch (type)
	{
		case EConVarType_Bool:
			return { "bool", value->m_bValue ? "true" : "false", false };
		case EConVarType_Int16:
			return { "int16", FormatNumber(value->m_i16Value), true };
		case EConVarType_UInt16:
			return { "uint16", FormatNumber(value->m_u16Value), true };
		case EConVarType_Int32:
			return { "int32", FormatNumber(value->m_i32Value), true };
		case EConVarType_UInt32:
			return { "uint32", FormatNumber(value->m_u32Value), true };
		case EConVarType_Int64:
			return { "int64", FormatNumber(value->m_i64Value), true };
		case EConVarType_UInt64:
			return { "uint64", FormatNumber(value->m_u64Value), true };
		case EConVarType_Float32:
			return { "float32", FormatNumber(value->m_fl32Value), true };
		case EConVarType_Float64:
			return { "float64", FormatNumber(value->m_fl64Value), true };
		case EConVarType_String:
			return { "string", value->m_StringValue.m_pString ? value->m_StringValue.m_pString : "", false };
		case EConVarType_Color:
			return { "color", fmt::format("[{}, {}, {}, {}]", value->m_clrValue.r(), value->m_clrValue.g(), value->m_clrValue.b(), value->m_clrValue.a()), false };
		case EConVarType_Vector2:
			return { "vector2", FormatVector(value->m_vec2Value, 2), false };
		case EConVarType_Vector3:
			return { "vector3", FormatVector(value->m_vec3Value, 3), false };
		case EConVarType_Vector4:
			return { "vector4", FormatVector(value->m_vec4Value, 4), false };
		case EConVarType_Qangle:
			return { "qangle", FormatVector(value->m_angValue, 3), false };
#ifndef GAME_DEADLOCK
		case EConVarType_VectorWS:
			return { "vector_ws", FormatVector(value->m_vecwsValue, 3), false };
#endif
		// ValidateConVar only lets known types through
		default:
			return { "unknown", {}, false };
	}
}

static void WriteValueLine(const std::string& value, const std::optional<std::string>& minValue, const std::optional<std::string>& maxValue, const std::string& flags, std::ostream& stream)
{
	stream << " " << value << " (";

	if (minValue)
		stream << "min: " << *minValue;

	if (maxValue)
		stream << (minValue ? ", " : "") << "max: " << *maxValue;

	if (minValue || maxValue)
		stream << ", ";

	stream << flags << ")";
}

static void FixNewlineTabbing(std::string& str)
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
	if (!str.empty() && str.back() == '\t')
		str.pop_back();

	if (!str.empty() && str.back() == '\n')
		str.pop_back();
}

// The engine looks up convars and commands case-insensitively
static std::string ToLower(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
	return str;
}

static std::string EscapeDescription(std::string str)
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

struct QueuedEntry_t
{
	const char* m_pszModule;
	std::string m_Name;
	std::string m_Help;
	uint64 m_nFlags;
	EConVarType m_eType = EConVarType_Invalid;
	// Formatted when read, the queue blocks are freed once the module calls ConVar_Register
	std::optional<std::string> m_Default;
	std::optional<std::string> m_Min;
	std::optional<std::string> m_Max;
};

struct Queue_t
{
	const char* m_pszKind;
	const char* m_pszListType;
	const char* m_pszFileName;
	std::vector<QueuedEntry_t> m_Entries;
	bool m_bFailed = false;
};

static Queue_t g_ConVarQueue{ "convar", "ConVarRegList", "convars.txt" };
static Queue_t g_ConCommandQueue{ "concommand", "ConCommandRegList", "commands.txt" };
static std::set<std::string> g_CollectedModules;

static QueuedEntry_t CopyQueued(const char* module, const ConVarRegList::Entry_t& queued)
{
	const auto& creation = queued.m_Info;
	const auto& info = creation.m_valueInfo;
	QueuedEntry_t entry{ module, creation.m_pszName, creation.m_pszHelpString ? creation.m_pszHelpString : "", creation.m_nFlags, info.m_eVarType };

	if (info.m_bHasDefault)
		entry.m_Default = FormatValue(info.m_eVarType, (const CVValue_t*)info.m_defaultValue).m_Text;
	if (info.m_bHasMin)
		entry.m_Min = FormatValue(info.m_eVarType, (const CVValue_t*)info.m_minValue).m_Text;
	if (info.m_bHasMax)
		entry.m_Max = FormatValue(info.m_eVarType, (const CVValue_t*)info.m_maxValue).m_Text;

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
			spdlog::critical("{} list in {} has {} entries, {} in the SDK needs updating", queue.m_pszKind, module.m_pszModule, list->m_nSize, queue.m_pszListType);
			queue.m_bFailed = true;
			return count;
		}

		for (uint32 i = 0; i < list->m_nSize; i++, count++)
		{
			if (auto invalid = ValidateQueued(list->m_Entries[i]))
			{
				spdlog::critical("{} list entry {} in {} has an invalid {}, {} in the SDK needs updating", queue.m_pszKind, count, module.m_pszModule, invalid, queue.m_pszListType);
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
	g_CollectedModules.insert(module.m_pszModule);
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

	output << "\n\t" << helpString << "\n\n";

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
// modules gets the modules that declare it
static QueuedEntry_t MergeQueued(const std::vector<QueuedEntry_t*>& entries, std::set<std::string>& modules)
{
	constexpr uint64 laterWins = FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_DONTRECORD | FCVAR_ARCHIVE | FCVAR_PER_USER;
	constexpr uint64 stripped = FCVAR_INITIAL_SETVALUE | FCVAR_PERFORMING_CALLBACKS;

	std::optional<QueuedEntry_t> merged;

	for (auto entry : entries)
	{
		if (entry->m_nFlags & FCVAR_REFERENCE)
			continue;

		modules.insert(entry->m_pszModule);
		auto flags = (entry->m_nFlags | GetModuleRegisterFlags(entry->m_pszModule)) & ~stripped;

		if (!merged)
		{
			merged = *entry;
			merged->m_nFlags = flags;
			continue;
		}

		merged->m_nFlags = flags | (merged->m_nFlags & ~laterWins);

		if (!merged->m_Default)
			merged->m_Default = entry->m_Default;
		if (!merged->m_Min)
			merged->m_Min = entry->m_Min;
		if (!merged->m_Max)
			merged->m_Max = entry->m_Max;
		if (merged->m_Help.empty())
			merged->m_Help = entry->m_Help;
	}

	// Only referenced, never declared
	if (!merged)
		return { entries[0]->m_pszModule, entries[0]->m_Name, "", FCVAR_REFERENCE, entries[0]->m_eType };

	return *merged;
}

// Names in the workshop whitelist, lowercase since the engine looks them up case-insensitively.
// Returns false if the file exists but has no names, which means its format changed.
static bool LoadWorkshopWhitelist(std::set<std::string>& names)
{
	const auto path = std::filesystem::current_path() / "../.." / GAME_PATH / g_WorkshopWhitelistPath;
	std::ifstream file(path);
	if (!file)
	{
		spdlog::info("No workshop whitelist at {}, not flagging whitelisted convars and commands", path.lexically_normal().generic_string());
		return true;
	}

	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const auto start = text.find('[', text.find("whitelist_cvars"));
	const auto end = text.find(']', start);

	for (auto quote = text.find('"', start); quote < end; quote = text.find('"', quote + 1))
	{
		const auto close = text.find('"', quote + 1);
		if (close == std::string::npos)
			break;

		names.insert(ToLower(text.substr(quote + 1, close - quote - 1)));
		quote = close;
	}

	if (names.empty())
	{
		spdlog::critical("Workshop whitelist at {} has no whitelist_cvars names, its format changed", path.lexically_normal().generic_string());
		return false;
	}

	return true;
}

static bool WriteQueued(Queue_t& queue, bool isConVar, std::set<std::string>& whitelist)
{
	std::map<std::string, std::vector<QueuedEntry_t*>> byName;
	for (auto& entry : queue.m_Entries)
		byName[entry.m_Name].push_back(&entry);

	const auto path = Globals::outputPath / queue.m_pszFileName;
	std::ofstream output(path);
	auto items = nlohmann::json::array();

	for (const auto& [name, entries] : byName)
	{
		std::set<std::string> modules;
		auto entry = MergeQueued(entries, modules);
		output << name;

		nlohmann::json item;
		item["name"] = name;

		auto flagNames = GetFlagNames(entry.m_nFlags);

		// Found names are removed, so the ones left over can be reported
		if (whitelist.erase(ToLower(name)))
			flagNames.push_back("workshop_whitelisted");

		auto flags = fmt::format("{}", fmt::join(flagNames, " "));

		if (isConVar)
		{
			const bool hasDefault = entry.m_Default && !g_ConVarsWithRandomDefaults.contains(name);

			// convars.txt writes the type's empty value when there is no default
			alignas(CVValue_t) static const uint8 empty[sizeof(CVValue_t)] = {};
			auto value = FormatValue(entry.m_eType, (const CVValue_t*)empty);
			if (hasDefault)
				value.m_Text = *entry.m_Default;

			std::optional<std::string> minValue, maxValue;
			if (value.m_bHasMinMax)
			{
				minValue = entry.m_Min;
				maxValue = entry.m_Max;
			}

			WriteValueLine(entry.m_eType == EConVarType_String ? "\"" + value.m_Text + "\"" : value.m_Text, minValue, maxValue, flags, output);

			item["type"] = value.m_pszType;
			if (hasDefault)
				item["default"] = value.m_Text;
			if (minValue)
				item["min"] = *minValue;
			if (maxValue)
				item["max"] = *maxValue;
		}
		else
		{
			output << " (" << flags << ")";
		}

		WriteHelp(name.c_str(), entry.m_Help.c_str(), output);

		// Flags that the declaring modules imply are left out, a few other modules set them too
		std::erase_if(flagNames, [&](const std::string& flag) { return (flag == "gamedll" && modules.contains("server")) || (flag == "clientdll" && modules.contains("client")); });

		// The text dumps keep them in bit order
		std::sort(flagNames.begin(), flagNames.end());
		item["flags"] = std::move(flagNames);
		item["modules"] = modules;

		// Some help texts end in a newline or space
		const auto helpStart = entry.m_Help.find_first_not_of(" \t\r\n");
		if (helpStart != std::string::npos)
			item["help"] = entry.m_Help.substr(helpStart, entry.m_Help.find_last_not_of(" \t\r\n") - helpStart + 1);

		items.push_back(std::move(item));
	}

	Globals::schemasJson[isConVar ? "convars" : "commands"] = std::move(items);

	if (!CloseOutput(output, path))
		return false;

	spdlog::info("Wrote {} {}s to {}", byName.size(), queue.m_pszKind, queue.m_pszFileName);
	return true;
}

// Returns false if either could not be dumped, which is then not written to keep the previous dump
bool Dump()
{
	std::set<std::string> whitelist;
	bool success = LoadWorkshopWhitelist(whitelist);

	// Their convars and commands would silently be missing
	for (const auto& name : g_RequiredQueueModules)
	{
		if (!g_CollectedModules.contains(name))
		{
			spdlog::critical("Required module {} did not load", name);
			g_ConVarQueue.m_bFailed = g_ConCommandQueue.m_bFailed = true;
		}
	}

	for (auto queue : { &g_ConVarQueue, &g_ConCommandQueue })
	{
		if (queue->m_bFailed)
		{
			spdlog::critical("Not writing {} because reading the {} queues failed, see above", queue->m_pszFileName, queue->m_pszKind);
			success = false;
		}
		else if (!WriteQueued(*queue, queue == &g_ConVarQueue, whitelist))
		{
			success = false;
		}
	}

	// Like button commands, which are registered at runtime, or names the game no longer has
	if (!whitelist.empty())
		spdlog::info("{} workshop whitelisted names are not convars or commands: {}", whitelist.size(), fmt::join(whitelist, ", "));

	return success;
}

} // namespace Dumpers::ConCommands