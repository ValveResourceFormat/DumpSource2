/**
 * =============================================================================
 * DumpSource2
 * Copyright (C) 2026 ValveResourceFormat Contributors
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
#pragma once

// Signatures and game struct layouts that are not in the SDK, these are what change with game updates.
// Signatures use \x2A as a wildcard byte, and start at the load of the global they find (see GetGlobalFromSignatureMatch).

#include <icvar.h>
#include <interfaces/interfaces.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace GameData
{

//-----------------------------------------------------------------------------
// Modules and app systems
//-----------------------------------------------------------------------------

// Modules that link tier1 contain this string, others can't have convars or schemas and are only loaded if they are app systems
inline constexpr const char* g_Tier1ModuleMarker = "RegisterConVar: Unknown error";

struct AppSystemInfo_t
{
	bool gameBin;
	const char* moduleName;
	std::string interfaceVersion;
};

// App systems that get connected (but not initialized) to fill in module interface globals used by KV3 defaults.
// Not listed: toolframework2 hangs when connecting, rendersystemempty/vulkan are alternatives to dx11.
// This is also the module load order, which decides which convar declaration wins when declared by multiple modules.
inline const std::vector<AppSystemInfo_t> g_AppSystems{
	{ false, "filesystem_stdio", FILESYSTEM_INTERFACE_VERSION },
	{ false, "resourcesystem", RESOURCESYSTEM_INTERFACE_VERSION },
	{ true, "client", "Source2ClientConfig001" },
	{ false, "engine2", SOURCE2ENGINETOSERVER_INTERFACE_VERSION },
	{ true, "host", "GameSystem2HostHook" },
	{ true, "modtools", "Source2ModTools001" },
	{ true, "matchmaking", MATCHFRAMEWORK_INTERFACE_VERSION },
	{ true, "server", SOURCE2SERVERCONFIG_INTERFACE_VERSION },
	{ false, "animationsystem", ANIMATIONSYSTEM_INTERFACE_VERSION },
	{ false, "materialsystem2", TEXTLAYOUT_INTERFACE_VERSION },
	{ false, "meshsystem", MESHSYSTEM_INTERFACE_VERSION },
	{ false, "networksystem", NETWORKSYSTEM_INTERFACE_VERSION },
	{ false, "panorama", PANORAMAUIENGINE_INTERFACE_VERSION },
	{ false, "particles", PARTICLESYSTEMMGR_INTERFACE_VERSION },
	{ false, "pulse_system", PULSESYSTEM_INTERFACE_VERSION },
#ifdef _WIN32
	{ false, "rendersystemdx11", RENDER_UTILS_INTERFACE_VERSION },
#else
	{ false, "rendersystemvulkan", RENDER_UTILS_INTERFACE_VERSION },
#endif
	{ false, "scenefilecache", "SceneFileCache002" },
	{ false, "scenesystem", SCENEUTILS_INTERFACE_VERSION },
	{ false, "soundsystem", SOUNDOPSYSTEMEDIT_INTERFACE_VERSION },
	{ false, "steamaudio", STEAMAUDIO_INTERFACE_VERSION },
	{ false, "vphysics2", VPHYSICS2_INTERFACE_VERSION },
	{ false, "worldrenderer", WORLD_RENDERER_MGR_INTERFACE_VERSION },
	{ false, "assetsystem", ASSETSYSTEM_INTERFACE_VERSION },
	{ false, "assetpreview", ASSETPREVIEWSYSTEM_INTERFACE_VERSION },
	{ false, "assetbrowser", ASSETBROWSERSYSTEM_INTERFACE_VERSION },
	{ false, "assetrename", "AssetRenameSystem_001" },
	{ false, "exportsystem", "EXPORTSYSTEM_INTERFACE_VERSION_001" },
	{ false, "helpsystem", "HelpSystem_001" },
	{ false, "imemanager", "IMEManager001" },
	{ false, "inputsystem", "InputSystemVersion001" },
	{ false, "localize", "Localize_001" },
	{ false, "modeldoc_utils", "ModelDocUtils001" },
	{ false, "navsystem", "NavSystem001" },
	{ false, "p4lib", "VP4003" },
	{ false, "panorama_text_pango", "PanoramaTextServices001" },
	{ false, "panoramauiclient", "PanoramaUIClient001" },
	{ false, "physicsbuilder", "PhysicsBuilderMgr001" },
	{ false, "propertyeditor", "PropertyEditorSystem_001" },
	{ false, "smartprops", "SmartPropsSystem_001" },
	{ false, "toolscenenodes", "ToolSceneNodeFactory_001" },
	{ false, "v8system", "Source2V8System001" },
	{ false, "vconcomm", "VConComm001" },
	{ false, "visbuilder", "VisBuilder_001" },
	{ false, "vrad3", "Vrad3_001" },
	{ false, "vscript", "VScriptManager010" },
	{ false, "valve_avi", "VAvi001" },
	{ false, "valve_wmf", "VMediaFoundation001" },
	{ false, "resourcecompiler", RESOURCECOMPILERSYSTEM_INTERFACE_VERSION },
	{ false, "tools/hammer", "ToolSystem2_001" },
	{ false, "tools/met", "ToolSystem2_001" },
	{ false, "tools/pet", "ToolSystem2_001" },
	{ false, "tools/sfm", "ToolSystem2_001" },
	{ false, "tools/postprocessingeditor", "ToolSystem2_001" },
	{ false, "tools/subrecteditor", "ToolSystem2_001" },
	{ false, "tools/cs2_item_editor", "ToolSystem2_001" },
	{ false, "tools/cs2_workshop_manager", "ToolSystem2_001" },
	{ false, "tools/workshopmanager", "ToolSystem2_001" },
	{ false, "tools/modeldoc_editor", "ToolSystem2_ModelDoc" },
	{ false, "subtools/bugreporter_subtool", "VConsole_SubTool_001_BugReporterTool" },
	{ false, "subtools/convarhelper_subtool", "VConsole_SubTool_001_ConvarHelperTool" },
	{ false, "subtools/dashboard_subtool", "VConsole_SubTool_001_DashboardTool" },
	{ false, "subtools/netgraph_subtool", "VConsole_SubTool_001_NetGraphTool" },
	{ false, "subtools/soundviewer_subtool", "VConsole_SubTool_001_SoundViewerTool" },
	{ false, "subtools/vprof_subtool", "VConsole_SubTool_001_ShowBudgetTool" },
};

//-----------------------------------------------------------------------------
// tier0 exports
//-----------------------------------------------------------------------------

#ifdef _WIN32
inline constexpr const char* g_SaveKV3AsJSONSymbol = "?SaveKV3AsJSON@@YA_NPEBVKeyValues3@@PEAVCUtlString@@1@Z";
inline constexpr const char* g_SaveKV3TextToStringSymbol = "?SaveKV3Text_ToString@@YA_NAEBUKV3ID_t@@PEBVKeyValues3@@PEAVCUtlString@@2I@Z";
#else
inline constexpr const char* g_SaveKV3AsJSONSymbol = "_Z13SaveKV3AsJSONPK10KeyValues3P10CUtlStringS3_";
inline constexpr const char* g_SaveKV3TextToStringSymbol = "_Z20SaveKV3Text_ToStringRK7KV3ID_tPK10KeyValues3P10CUtlStringS6_j";
#endif

//-----------------------------------------------------------------------------
// Convars and commands
//-----------------------------------------------------------------------------

// Convars and commands are queued by each module's statically linked tier1 until ConVar_Register is called,
// so they can be read right after loading the module.
// These mirror ConVarRegList and ConCommandRegList from the SDK, which are only defined in tier1/convar.cpp.
struct ConVarRegList
{
	struct Entry_t
	{
		ConVarCreation_t m_Info;
		ConVarRefAbstract* m_pConVar;
		ConVarData** m_pConVarData;
	};

	uint32 m_nSize;
	Entry_t m_Entries[100];
	ConVarRegList* m_pPrev;
};

struct ConCommandRegList
{
	struct Entry_t
	{
		ConCommandCreation_t m_Info;
		ConCommandRef* m_Command;
	};

	uint32 m_nSize;
	Entry_t m_Entries[100];
	ConCommandRegList* m_pPrev;
};

static_assert(sizeof(ConVarRegList) == 0x3E90, "List size is part of g_ConVarQueueSignature");
static_assert(sizeof(ConCommandRegList) == 0x1910, "List size is part of g_ConCommandQueueSignature");

// Signatures of the list append code, starting at the list head load.
// To update, find the list allocation (sizeof(ConVarRegList)) in tier0. The code is only linked into modules that declare any.
#ifdef _WIN32
inline const byte g_ConVarQueueSignature[] = "\x4C\x8B\x0D\x2A\x2A\x2A\x2A\x4D\x85\xC9\x74\x2A\x45\x8B\x01\x41\x83\xF8\x64\x0F\x85\x2A\x2A\x2A\x2A\xB9\x90\x3E\x00\x00";
inline const byte g_ConCommandQueueSignature[] = "\x48\x8B\x0D\x2A\x2A\x2A\x2A\x48\x85\xC9\x74\x2A\x44\x8B\x01\x41\x83\xF8\x64\x75\x2A\xB9\x10\x19\x00\x00";
#else
inline const byte g_ConVarQueueSignature[] = "\x48\x8B\x15\x2A\x2A\x2A\x2A\xC7\x00\x00\x00\x00\x00\xB9\x01\x00\x00\x00\x48\x89\x05\x2A\x2A\x2A\x2A\x48\x89\x90\x88\x3E\x00\x00";
inline const byte g_ConCommandQueueSignature[] = "\x48\x8B\x15\x2A\x2A\x2A\x2A\xC7\x00\x00\x00\x00\x00\xB9\x01\x00\x00\x00\x48\x89\x05\x2A\x2A\x2A\x2A\x48\x89\x90\x08\x19\x00\x00";
#endif

// Modules that always declare both, not finding their queues means the signatures are outdated
inline const std::unordered_set<std::string> g_RequiredQueueModules = { "tier0", "engine2", "client", "server" };

//-----------------------------------------------------------------------------
// Entities
//-----------------------------------------------------------------------------

// Each linked entity class (CEntityClass from the SDK) is added to a per module list when the module is loaded.
// Signature of the list walk that builds entity_api_designname_to_base for module metadata,
// starting at the list head load.
// To update, find the xref to the "entity_api_designname_to_base" string in server.
#ifdef _WIN32
inline const byte g_EntityClassListSignature[] = "\x48\x8B\x35\x2A\x2A\x2A\x2A\x33\xED\x4C\x8B\xFA\x48\x85\xF6\x0F\x84\x2A\x2A\x2A\x2A\x48\x89\x5C\x24\x2A\x48\x89\x7C\x24\x2A\x4C\x89\x64\x24\x2A\x4C\x8D\x25";
#else
inline const byte g_EntityClassListSignature[] = "\x48\x8B\x1D\x2A\x2A\x2A\x2A\x48\x89\x75\xB0\x48\x85\xDB\x0F\x84\x2A\x2A\x2A\x2A\x4C\x8D\x45\xC0\x45\x31\xFF\x4D\x89\xC4";
#endif

// Modules that always link entities, not finding their list means the signature is outdated
inline const std::unordered_set<std::string> g_RequiredEntityModules = { "client", "server" };

} // namespace GameData
