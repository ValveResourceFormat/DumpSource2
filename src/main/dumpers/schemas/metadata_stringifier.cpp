/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2024 ValveResourceFormat Contributors
 *
 * source2gen
 * Copyright 2024 neverlosecc
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

#include "metadata_stringifier.h"
#include "metadatalist.h"
#include "dumpers/module_metadata/module_metadata.h"
#include "gamedata.h"
#include "globalvariables.h"
#include "modules.h"
#include <algorithm>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifndef WIN32
#include <sys/mman.h>
#include <ucontext.h>
#endif

namespace Dumpers::Schemas
{

bool g_bInvalidKV3Defaults = false;

typedef void* (*GetKV3DefaultsFn)();

// GetKV3Defaults serializes a default constructed object on the stack, so fields the constructor does not initialize
// contain whatever was on the stack. Each call runs on a newly allocated stack, which is always zeroed.
#ifdef WIN32
struct KV3DefaultsCall
{
	GetKV3DefaultsFn fn;
	void* result;
	void* returnFiber;
};

static void CALLBACK KV3DefaultsFiber(void* param)
{
	auto call = static_cast<KV3DefaultsCall*>(param);
	call->result = call->fn();
	SwitchToFiber(call->returnFiber);
}

static void* CallKV3Defaults(GetKV3DefaultsFn fn)
{
	static void* mainFiber = ConvertThreadToFiber(nullptr);

	KV3DefaultsCall call{ fn, nullptr, mainFiber };
	auto fiber = mainFiber ? CreateFiber(0x100000, KV3DefaultsFiber, &call) : nullptr;
	if (!fiber)
	{
		spdlog::critical("Failed to create a fiber for KV3 defaults, error {}", GetLastError());
		g_bInvalidKV3Defaults = true;
		return nullptr;
	}

	SwitchToFiber(fiber);
	DeleteFiber(fiber);

	return call.result;
}
#else
static GetKV3DefaultsFn g_KV3DefaultsFn;
static void* g_KV3DefaultsResult;

static void KV3DefaultsContext()
{
	g_KV3DefaultsResult = g_KV3DefaultsFn();
}

static void* CallKV3Defaults(GetKV3DefaultsFn fn)
{
	constexpr size_t stackSize = 0x100000;
	auto stack = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	ucontext_t mainContext, context;

	if (stack == MAP_FAILED || getcontext(&context) != 0)
	{
		spdlog::critical("Failed to create a stack for KV3 defaults: {}", strerror(errno));
		g_bInvalidKV3Defaults = true;
		return nullptr;
	}

	context.uc_stack.ss_sp = stack;
	context.uc_stack.ss_size = stackSize;
	context.uc_link = &mainContext;
	makecontext(&context, KV3DefaultsContext, 0);

	g_KV3DefaultsFn = fn;
	swapcontext(&mainContext, &context);
	munmap(stack, stackSize);

	return g_KV3DefaultsResult;
}
#endif

// Resets hidden values to zero, keeping their type and shape
static void ZeroHiddenDefaults(nlohmann::ordered_json& value, const std::unordered_set<std::string>& classKeys, bool hidden = false)
{
	if (value.is_object())
	{
		for (auto it = value.begin(); it != value.end(); ++it)
			ZeroHiddenDefaults(it.value(), classKeys, hidden || GameData::g_HiddenDefaultKeys.contains(it.key()) || classKeys.contains(it.key()));
	}
	else if (value.is_array())
	{
		for (auto& item : value)
			ZeroHiddenDefaults(item, classKeys, hidden);
	}
	else if (hidden)
	{
		if (value.is_string())
			value = "";
		else if (value.is_boolean())
			value = false;
		else if (value.is_number_float())
			value = 0.0;
		else if (value.is_number())
			value = 0;
	}
}

// Writes the defaults like SaveKV3AsJSON does: a value per line, containers on their own line and floats with 6 decimals
static void WriteDefaults(const nlohmann::ordered_json& value, const std::string& indent, std::string& out)
{
	if (!value.is_structured())
	{
		out += value.is_number_float() ? fmt::format("{:.6f}", value.get<double>()) : value.dump();
		return;
	}

	const auto inner = indent + "\t";
	out += value.is_object() ? "{\n" : "[\n";

	for (auto it = value.begin(); it != value.end(); ++it)
	{
		out += inner;

		if (value.is_object())
			out += nlohmann::json(it.key()).dump() + ":" + (it->is_structured() ? "\n" + inner : " ");

		WriteDefaults(*it, inner, out);
		out += std::next(it) != value.end() ? ",\n" : "\n";
	}

	out += indent + (value.is_object() ? "}" : "]");
}

static std::optional<std::string> GetKV3Defaults(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName, std::optional<nlohmann::json>& jsonValue)
{
	if (!(*(void**)entry.m_pData) || GameData::g_ClassesWithBrokenDefaults.contains(metadataTargetName))
		return "Could not parse KV3 Defaults";

	// Abstract classes have no defaults
	auto value = CallKV3Defaults(reinterpret_cast<GetKV3DefaultsFn>(*(void**)entry.m_pData));
	if (!value)
		return "Could not parse KV3 Defaults";

	auto defaults = ModuleMetadata::KV3ToJSON(*(void**)value);
	if (defaults.is_discarded())
	{
		spdlog::critical("KV3 defaults of {} could not be converted to JSON", metadataTargetName);
		g_bInvalidKV3Defaults = true;
		return {};
	}

	static const std::unordered_set<std::string> noClassKeys;
	auto classKeys = GameData::g_HiddenClassDefaultKeys.find(metadataTargetName);
	const auto& hiddenClassKeys = classKeys != GameData::g_HiddenClassDefaultKeys.end() ? classKeys->second : noClassKeys;

	ZeroHiddenDefaults(defaults, hiddenClassKeys);
	jsonValue = defaults;

	std::string text;
	WriteDefaults(defaults, "", text);
	return text;
}

std::map<std::string, std::string> g_unknownMetadataSamples;

// Describes a value of metadata missing from metadatalist.h, to help pick its type when adding it
static std::string DescribeUnknownMetadata(const void* data)
{
	auto bytes = static_cast<const uint8_t*>(data);
	auto pointer = *static_cast<const uint8_t* const*>(data);

	std::string description = "bytes";
	for (int i = 0; i < 8; i++)
		description += fmt::format(" {:02x}", bytes[i]);
	description += fmt::format(", as int {}, as float {}", *static_cast<const int32_t*>(data), *static_cast<const float*>(data));

	for (const auto& module : Modules::allModules)
	{
		for (const auto& section : module.m_sections)
		{
			auto base = static_cast<const uint8_t*>(section.m_pBase);
			if (pointer < base || pointer >= base + section.m_iSize)
				continue;

			if (section.m_szName == ".text")
				return description + fmt::format(", points to code in {}", module.m_pszModule);

			auto length = strnlen(reinterpret_cast<const char*>(pointer), std::min<size_t>(base + section.m_iSize - pointer, 60));
			return description + fmt::format(", points to {} in {}: \"{}\"", section.m_szName, module.m_pszModule, std::string(reinterpret_cast<const char*>(pointer), length));
		}
	}

	return description;
}

// Joins the names that are set, some are -1 instead of null since the Deadlock 14/09/24 update
static std::string JoinNames(const char* first, std::string_view separator, const char* second)
{
	auto isSet = [](const char* name) { return name != nullptr && name != reinterpret_cast<const char*>(-1); };

	if (isSet(first) && isSet(second))
		return fmt::format("{}{}{}", first, separator, second);

	return isSet(first) ? first : isSet(second) ? second
	                                            : "";
}

static bool HasMetadataValue(const SchemaMetadataEntryData_t& entry)
{
	if (!entry.m_pData)
		return false;

	auto it = g_mapMetadataNameToValue.find(entry.m_pszName);
	return it == g_mapMetadataNameToValue.end() || it->second != MetadataValueType::FUNCTION;
}

// Determine how and if to output metadata entry value based on it's type.
static std::optional<std::string> GetMetadataValue(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName, std::optional<nlohmann::json>& jsonValue)
{
	if (!entry.m_pData)
		return {};

	auto valueType = g_mapMetadataNameToValue.find(entry.m_pszName);
	if (valueType == g_mapMetadataNameToValue.end())
	{
		if (!g_unknownMetadataSamples.contains(entry.m_pszName))
			g_unknownMetadataSamples[entry.m_pszName] = fmt::format("on {}: {}", metadataTargetName, DescribeUnknownMetadata(entry.m_pData));

		return {};
	}

	switch (valueType->second)
	{
		case MetadataValueType::STRING:
		{
			auto value = *static_cast<const char**>(entry.m_pData);
			if (value)
			{
				Globals::stringsIgnoreStream << value << "\n";
			}
			return fmt::format("\"{}\"", value ? value : "(NULL)");
		}
		case MetadataValueType::INTEGER:
			return std::to_string(*static_cast<int*>(entry.m_pData));
		case MetadataValueType::FLOAT:
			return std::to_string(*static_cast<float*>(entry.m_pData));
		case MetadataValueType::BOOL:
			return *static_cast<bool*>(entry.m_pData) ? "true" : "false";
		case MetadataValueType::COLOR:
		{
			auto color = static_cast<const uint8_t*>(entry.m_pData);
			return fmt::format("[{}, {}, {}, {}]", color[0], color[1], color[2], color[3]);
		}
		case MetadataValueType::INLINE_STRING:
		{
			// max 8 characters. Also check for null term.
			char* result = static_cast<char*>(entry.m_pData);
			for (uint8_t i = 0; i < 8; ++i)
			{
				if (result[i] == '\0')
				{
					return fmt::format("\"{}\"", std::string(result, i));
				}
			}
			return fmt::format("\"{}\"", std::string(result, 8));
		}
		case MetadataValueType::SEND_PROXY_RECIPIENTS_FILTER:
		{
			auto& value = *static_cast<CSchemaSendProxyRecipientsFilter*>(entry.m_pData);
			return fmt::format("\"{}\"", value.m_pszName ? value.m_pszName : "(NULL)");
		}
		case MetadataValueType::VARNAME:
		{
			// Written as "type name"
			auto value = static_cast<CSchemaVarName*>(entry.m_pData);
			return fmt::format("\"{}\"", JoinNames(value->m_pszType, " ", value->m_pszName));
		}
		case MetadataValueType::NETWORK_OVERRIDE:
		{
			// Written as "Class::field", or "field" when it's in the class itself
			auto value = static_cast<CSchemaNetworkOverride*>(entry.m_pData);
			return fmt::format("\"{}\"", JoinNames(value->m_pszClassName, "::", value->m_pszFieldName));
		}
		case MetadataValueType::KV3DEFAULTS:
			return GetKV3Defaults(entry, metadataTargetName, jsonValue);
	}

	return {};
}

IntermediateMetadata GetMetadata(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName)
{
	IntermediateMetadata metadata{ .name = entry.m_pszName, .hasValue = HasMetadataValue(entry) };
	metadata.stringValue = GetMetadataValue(entry, metadataTargetName, metadata.jsonValue);
	return metadata;
}

} // namespace Dumpers::Schemas