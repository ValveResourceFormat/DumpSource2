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

#include "gamedata.h"
#include "globalvariables.h"
#include "interfaces.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "metadatalist.h"
#include <optional>
#include <fmt/format.h>
#include "metadata_stringifier.h"
#include <modules.h>
#include <vector>
#include <string_view>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "utils/common.h"

#ifndef WIN32
#include <sys/mman.h>
#include <ucontext.h>
#endif

namespace Dumpers::Schemas
{

// Their constructors crash, they need a running game (owning entity, game systems)
std::unordered_set<std::string> g_classWithBrokenDefaults = {
	"C_fogplayerparams_t",
	"fogplayerparams_t",
	"CBodyComponentBaseAnimating",
	"CBodyComponentBaseAnimGraph",
	"CBodyComponentPoint",
	"CBodyComponentSkeletonInstance",
	"CCitadelPlayerPawn_GraphController2",
	"CGameSceneNode",
	"CSkeletonInstance",
};

// Keys that constructors fill with random values or leave uninitialized, so their values are zeroed at any depth.
// These are in, or embedded in, many classes, so they are zeroed in all of them.
std::unordered_set<std::string> g_hiddenDefaultKeys = {
	"m_id",
	"m_ID",
	"m_influenceOffsets", // CAnimAttachment
	"m_nRandomSeed",
};

// The same, but only zeroed in these classes. Classes that embed another one with the key are listed too.
std::unordered_map<std::string, std::unordered_set<std::string>> g_hiddenClassDefaultKeys = {
	{ "CAnimGraphDoc_ChoiceNode", { "m_seed" } },
	{ "CAnimGraphDoc_ComponentState", { "m_stateID" } },
	{ "CAnimGraphDoc_GroupNode", { "m_nodes" } }, // Input and output nodes in random order
	{ "CAnimGraphDoc_NodeState", { "m_stateID" } },
	{ "CAnimGraphDoc_State", { "m_stateID" } },
	{ "CBlockSelectionMetricEvaluator", { "m_means", "m_standardDeviations" } },
	{ "CNmBlendSpace1D::Point_t", { "m_pinID" } },
	{ "CNmGraphDocBlend1DNode", { "m_pinID" } },
	{ "CNmGraphDocEntryOverrideNode", { "m_stateID" } },
	{ "CNmGraphDocFlowGraph::Connection_t", { "m_outputPinID" } },
	{ "CNmGraphDocGlobalTransitionNode", { "m_stateID" } },
	{ "CNmGraphDocStateMachineGraph", { "m_entryStateID" } },
	{ "CNmGraphDocStateMachineNode", { "m_stateID", "m_entryStateID", "m_cloneStateVersion" } },
	{ "CNmGraphDocStateNode", { "m_cloneStateVersion" } },
	{ "CStateUpdateData", { "m_stateID" } },
	{ "CTestPulseIO::EntityHandleIntArgs_t", { "valueB" } },
	{ "FourCovMatrices3", { "m_flXY" } },
	{ "HitReactFixedSettings_t", { "m_flWhipSpringStrength" } },
	{ "RTProxyBLAS_t", { "m_vMaxBounds" } },
	{ "VMixPointerFixupEntry_t", { "m_nIndex" } },
	{ "dynpitchvol_base_t", { "pitchfrac", "vol" } },
	{ "dynpitchvol_t", { "pitchfrac", "vol" } },
	{ "vphysics_save_ragdoll_control_t", { "m_vLinearVelocityAccumulator" } },
};

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
	auto fiber = CreateFiber(0x100000, KV3DefaultsFiber, &call);
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
	if (stack == MAP_FAILED)
		return fn();

	ucontext_t mainContext, context;
	getcontext(&context);
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

// SaveKV3AsJSON writes NaN and infinity as bare words, which JSON has no value for, so they are quoted into strings
static std::string QuoteNonFiniteFloats(std::string_view text)
{
	// In this order so -nan is not matched as nan
	constexpr std::string_view nonFiniteFloats[] = { "-nan", "nan", "-inf", "inf" };

	std::string result;
	bool inString = false;

	for (size_t i = 0; i < text.size(); i++)
	{
		if (inString)
		{
			if (text[i] == '\\')
				result += text[i++];
			else if (text[i] == '"')
				inString = false;
		}
		else if (text[i] == '"')
		{
			inString = true;
		}
		else
		{
			// Outside strings, only numbers and true/false/null are bare, none of which contain these
			auto nonFinite = std::find_if(std::begin(nonFiniteFloats), std::end(nonFiniteFloats), [&](std::string_view word) { return text.substr(i, word.size()) == word; });
			if (nonFinite != std::end(nonFiniteFloats))
			{
				result += fmt::format("\"{}\"", *nonFinite);
				i += nonFinite->size() - 1;
				continue;
			}
		}

		result += text[i];
	}

	return result;
}

// Resets hidden values to zero, keeping their type and shape
static void ZeroHiddenDefaults(nlohmann::ordered_json& value, const std::unordered_set<std::string>& classKeys, bool hidden = false)
{
	if (value.is_object())
	{
		for (auto it = value.begin(); it != value.end(); ++it)
			ZeroHiddenDefaults(it.value(), classKeys, hidden || g_hiddenDefaultKeys.contains(it.key()) || classKeys.contains(it.key()));
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
	typedef int (*SaveKV3AsJsonFn)(void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);

	if (!(*(void**)entry.m_pData) || g_classWithBrokenDefaults.contains(metadataTargetName))
		return "Could not parse KV3 Defaults";

	static auto SaveKV3AsJson = Modules::tier0->GetSymbol<SaveKV3AsJsonFn>(GameData::g_SaveKV3AsJSONSymbol);

	auto value = CallKV3Defaults(reinterpret_cast<GetKV3DefaultsFn>(*(void**)entry.m_pData));
	SimpleCUtlString err;
	SimpleCUtlString buf;

	if (!value || !SaveKV3AsJson(*(void**)value, err, buf))
		return "Could not parse KV3 Defaults";

	auto defaults = nlohmann::ordered_json::parse(QuoteNonFiniteFloats(buf.Get()), nullptr, false);

	if (defaults.is_discarded())
	{
		spdlog::critical("KV3 defaults of {} are not valid JSON, the SaveKV3AsJSON output format changed", metadataTargetName);
		g_bInvalidKV3Defaults = true;
		return {};
	}

	static const std::unordered_set<std::string> noClassKeys;
	auto classKeys = g_hiddenClassDefaultKeys.find(metadataTargetName);
	const auto& hiddenClassKeys = classKeys != g_hiddenClassDefaultKeys.end() ? classKeys->second : noClassKeys;

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
			auto value = static_cast<CSchemaVarName*>(entry.m_pData);

			const auto check_ptr = [](const char* ptr) -> bool {
				// Authored: source2gen
				// @note: hotfix for the deadlock 14/09/24 update,
				// where they filled some ptrs with -1 instead of nullptr
				return ptr != nullptr && ptr != reinterpret_cast<const char*>(-1);
			};

			std::stringstream stringStream;
			auto hasType = check_ptr(value->m_pszType);
			auto hasName = check_ptr(value->m_pszName);

			stringStream << "\"";

			if (hasType)
				stringStream << value->m_pszType;

			if (hasName)
			{
				if (hasType)
					stringStream << " ";
				stringStream << value->m_pszName;
			}

			stringStream << "\"";

			return stringStream.str();
		}
		case MetadataValueType::KV3DEFAULTS:
			return GetKV3Defaults(entry, metadataTargetName, jsonValue);
		case MetadataValueType::DEBUGGER_BREAKPOINT:
		{
#ifdef WIN32
			__debugbreak();
#endif
			return "DEBUGGING";
		}
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