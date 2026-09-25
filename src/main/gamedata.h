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
#include <tier1/utlstring.h>
#include <set>
#include <string>
#include <unordered_map>
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
	const char* interfaceVersion;
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
// Schema class defaults (MGetKV3ClassDefaults)
//-----------------------------------------------------------------------------

// Their constructors crash, they need a running game (owning entity, game systems)
inline const std::unordered_set<std::string> g_ClassesWithBrokenDefaults = {
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
inline const std::unordered_set<std::string> g_HiddenDefaultKeys = {
	"m_id",
	"m_ID",
	"m_influenceOffsets", // CAnimAttachment
	"m_nRandomSeed",
};

// The same, but only zeroed in these classes. Classes that embed another one with the key are listed too.
inline const std::unordered_map<std::string, std::unordered_set<std::string>> g_HiddenClassDefaultKeys = {
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

//-----------------------------------------------------------------------------
// Interfaces
//-----------------------------------------------------------------------------

// CreateInterface walks the module's InterfaceReg list, this is the offset of the list head load from the export.
// On Linux it comes after the function prologue.
#ifdef _WIN32
inline constexpr size_t g_CreateInterfaceListOffset = 0;
#else
inline constexpr size_t g_CreateInterfaceListOffset = 0x10;
#endif

//-----------------------------------------------------------------------------
// Convars and commands
//-----------------------------------------------------------------------------

// Convars and commands are queued by each module's statically linked tier1 until ConVar_Register is called,
// so they can be read right after loading the module. The lists are ConVarRegList and ConCommandRegList from the SDK.

// Signatures of the list append code, starting at the list head load.
// To update, find the list allocation (sizeof(ConVarRegList)) in tier0. The code is only linked into modules that declare any.
#ifdef _WIN32
inline const byte g_ConVarQueueSignature[] = "\x4C\x8B\x0D\x2A\x2A\x2A\x2A\x4D\x85\xC9\x74\x2A\x45\x8B\x01\x41\x83\xF8\x64";
inline const byte g_ConCommandQueueSignature[] = "\x48\x8B\x0D\x2A\x2A\x2A\x2A\x48\x85\xC9\x74\x2A\x44\x8B\x01\x41\x83\xF8\x64";
#else
inline const byte g_ConVarQueueSignature[] = "\x48\x8B\x15\x2A\x2A\x2A\x2A\xC7\x00\x00\x00\x00\x00\xB9\x01\x00\x00\x00\x48\x89\x05\x2A\x2A\x2A\x2A\x48\x89\x90\x88\x3E";
inline const byte g_ConCommandQueueSignature[] = "\x48\x8B\x15\x2A\x2A\x2A\x2A\xC7\x00\x00\x00\x00\x00\xB9\x01\x00\x00\x00\x48\x89\x05\x2A\x2A\x2A\x2A\x48\x89\x90\x08\x19";
#endif

// Modules that always declare both, not finding their queues means the signatures are outdated
inline const std::unordered_set<std::string> g_RequiredQueueModules = { "tier0", "engine2", "client", "server" };

// Convars with a random default value on each start
inline const std::unordered_set<std::string> g_ConVarsWithRandomDefaults = { "cl_color" };

// Convars and commands that workshop maps can use, in the game directory as GameTracking extracts it from pak01_dir.vpk.
// Only CS2 has it. It's KV3 text with the names in a whitelist_cvars array.
inline constexpr const char* g_WorkshopWhitelistPath = "pak01_dir/scripts/workshop_cvar_whitelist.txt";

//-----------------------------------------------------------------------------
// Entities
//-----------------------------------------------------------------------------

// Each linked entity class (CEntityClass from the SDK) is added to a per module list when the module is loaded.
// Signature of the list walk that builds entity_api_designname_to_base for module metadata,
// starting at the list head load.
// To update, find the xref to the "entity_api_designname_to_base" string in server.
#ifdef _WIN32
inline const byte g_EntityClassListSignature[] = "\x48\x8B\x35\x2A\x2A\x2A\x2A\x33\xED\x4C\x8B\xFA\x48\x85\xF6";
#else
inline const byte g_EntityClassListSignature[] = "\x48\x8B\x1D\x2A\x2A\x2A\x2A\x48\x89\x75\xB0\x48\x85\xDB";
#endif

// Modules that always link entities, not finding their list means the signature is outdated
inline const std::unordered_set<std::string> g_RequiredEntityModules = { "client", "server" };

//-----------------------------------------------------------------------------
// Network
//-----------------------------------------------------------------------------

// Networked classes register into a CNetworkSerializerCodeGenDatabase (from the SDK) in these modules.
// Static initializers queue the registrations in a list, which is walked when the module connects, which the dumper doesn't do.
inline const std::set<std::string> g_NetworkModules = { "client", "server" };

// Signature of that code, the same in each module: the list walk call, then the database getter call.
// To update, find the xref to the "couldn't look up codegen info for CEntityClass" string, its function is called right after.
#ifdef _WIN32
inline const byte g_NetworkDatabaseSignature[] = "\xE8\x2A\x2A\x2A\x2A\xE8\x2A\x2A\x2A\x2A\x48\x8B\xC8\x48\x8D";
#else
inline const byte g_NetworkDatabaseSignature[] = "\xE8\x2A\x2A\x2A\x2A\xE8\x2A\x2A\x2A\x2A\x48\x8D\x35\x2A\x2A\x2A\x2A\x48\x89\xC7\xE8";
#endif

// The list walk loads the list head (mov reg, [rip+displacement]) this far in, after its prologue
inline constexpr size_t g_NetworkRegistrationListOffset = 10;

// Class registrations get the database first, the other queued registrations need systems that only connecting the
// module sets up. The call comes after the function prologue, which the compiler sizes per function (6 to 18 bytes).
// Registrations that aren't recognized are caught when classes refer to classes that are missing, except for classes nothing refers to.
inline constexpr size_t g_NetworkClassRegistrationGetterCallWithin = 32;

// A queued registration. What follows is how it gets called, which differs per platform.
struct NetworkRegistration_t
{
	NetworkRegistration_t* m_pNext;
	void (*m_pfnRegister)();
};

// NetworkRecipientsFilter_t in the SDK has the callback as a function pointer, but it is a pointer to member function,
// which is 16 bytes with the Itanium ABI (Linux) instead of 8, so the name is after it
struct NetworkMemberFunctionOwner_t
{
};

struct SendProxyRecipientsFilter_t
{
	void* m_unk001;
	void (NetworkMemberFunctionOwner_t::*m_Callback)();
	CUtlString m_Name;
};

// CNetworkSerializerFieldInfo::m_NetworkPolymorphic is a byte later than in the SDK
inline constexpr size_t g_NetworkPolymorphicOffset = 0x10A;

// What a NetworkOverride_t changes of the base class field, by its kind
inline const std::unordered_map<int, std::string> g_NetworkOverrideKinds = {
	{ 0, "serializer" },
	{ 1, "encoder" },
	{ 2, "changeCallback" },
	{ 4, "bitCount" },
	{ 5, "userGroup" },
	{ 6, "priority" },
};

} // namespace GameData
