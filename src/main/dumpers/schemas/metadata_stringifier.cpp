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

#include "globalvariables.h"
#include "interfaces.h"
#include <filesystem>
#include <map>
#include <unordered_set>
#include "metadatalist.h"
#include <optional>
#include <fmt/format.h>
#include "metadata_stringifier.h"
#include <modules.h>
#include <vector>
#include <regex>
#include "utils/common.h"

#ifndef WIN32
#include <sys/mman.h>
#include <ucontext.h>
#endif

namespace Dumpers::Schemas
{

std::unordered_set<std::string> g_classWithBrokenDefaults = {
	"CastSphereSATParams_t",
	"Dop26_t",
	"FourCovMatrices3",
	"VMixVocoderDesc_t",
	"CCitadelPlayerPawn_GraphController2",
	"RTProxyBLAS_t",
	"vphysics_save_ragdoll_control_t",
	"CAnimAttachment",
	"CBlockSelectionMetricEvaluator",
	"HitReactFixedSettings_t",
	"RnSoftbodySpring_t",
	"CAnimGraphDoc_GroupNode",
	"C_fogplayerparams_t",
	"fogplayerparams_t",
	"CBodyComponentBaseAnimating",
	"CBodyComponentPoint",
	"CSkeletonInstance",
	"CGameSceneNode",
	"CBodyComponentBaseAnimGraph",
	"CBodyComponentSkeletonInstance"
};

std::vector<std::regex> g_regexFilters = {
	std::regex(R"#(("m_id":) .*)#"),
	std::regex(R"#(("m_ID":) .*,)#"),
	std::regex(R"#(("m_nControlPointCount":) .*)#"),
	std::regex(R"#(("m_nControlPointStart":) .*)#"),
	std::regex(R"#(("m_nRandomSeed":) .*,)#"),
	std::regex(R"#(("m_seed":) .*,)#"),
	std::regex(R"#(("m_outputPinID":) .*,)#"),
	std::regex(R"#(("m_stateID":) .*)#"),
	std::regex(R"#(("m_pinID":) .*)#"),
	std::regex(R"#(("m_entryStateID":) .*)#"),
	std::regex(R"#(("pitchfrac":) .*)#"),
	std::regex(R"#(("vol":) .*)#"),
};

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

// Determine how and if to output metadata entry value based on it's type.
std::optional<std::string> GetMetadataValue(const SchemaMetadataEntryData_t& entry, const char* metadataTargetName)
{
	if (entry.m_pData && g_mapMetadataNameToValue.find(entry.m_pszName) != g_mapMetadataNameToValue.end())
	{
		auto valueType = g_mapMetadataNameToValue.at(entry.m_pszName);
		switch (valueType)
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
			{
				typedef int (*SaveKV3AsJsonFn)(void* kv3, SimpleCUtlString& err, SimpleCUtlString& str);

				if (!entry.m_pData || !(*(void**)entry.m_pData) || g_classWithBrokenDefaults.contains(metadataTargetName))
					return "Could not parse KV3 Defaults";

				auto value = CallKV3Defaults(reinterpret_cast<GetKV3DefaultsFn>(*(void**)entry.m_pData));

				if (!value)
					return "Could not parse KV3 Defaults";

#ifdef WIN32
				static auto SaveKV3AsJson = Modules::tier0->GetSymbol<SaveKV3AsJsonFn>("?SaveKV3AsJSON@@YA_NPEBVKeyValues3@@PEAVCUtlString@@1@Z");
#else
				static auto SaveKV3AsJson = Modules::tier0->GetSymbol<SaveKV3AsJsonFn>("_Z13SaveKV3AsJSONPK10KeyValues3P10CUtlStringS3_");
#endif
				if (!SaveKV3AsJson)
				{
					spdlog::critical("SaveKV3AsJson not found");
					return {};
				}

				SimpleCUtlString err;
				SimpleCUtlString buf;
				int res = SaveKV3AsJson(*(void**)value, err, buf);

				if (res)
				{
					std::string out = buf.Get();

					for (const auto& regex : g_regexFilters)
					{
						out = std::regex_replace(out, regex, "$1 <HIDDEN FOR DIFF>,");
					}

					return out;
				}

				return "Could not parse KV3 Defaults";
			}
			case MetadataValueType::DEBUGGER_BREAKPOINT:
			{
#ifdef WIN32
				__debugbreak();
#endif
				return "DEBUGGING";
			}
		}
	}

	return {};
}

} // namespace Dumpers::Schemas