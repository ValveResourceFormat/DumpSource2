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

#include "entities.h"
#include "gamedata.h"
#include "globalvariables.h"
#include "modules.h"
#include "interfaces.h"
#include "output.h"
#include "dumpers/module_metadata/module_metadata.h"
#include "dumpers/schemas/schemas.h"
#include <entity2/entityclass.h>
#include <datamap.h>
#include <schemasystem/schemasystem.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fmt/printf.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace Dumpers::Entities
{

// Other games don't have this entity system yet, and their SDKs have the older entity and datamap layouts
// TODO: Enable for a game when it and its SDK have this entity system
#if !defined(GAME_DOTA) && !defined(GAME_HLVR) && !defined(GAME_STEAMVR)

using namespace GameData;

// An input or output as an FGD line and for schemas.json
struct EntityIO_t
{
	std::string m_Line;
	nlohmann::json m_Json;
};

struct EntityClass_t
{
	const CEntityClass* m_pClass;
	std::string m_BaseName;
	std::vector<std::string> m_Lines;
	std::vector<EntityIO_t> m_Inputs;
	std::vector<EntityIO_t> m_Outputs;
	std::vector<std::string> m_Strings;

	// The same keys and components for schemas.json
	nlohmann::json m_Keys = nlohmann::json::array();
	nlohmann::json m_Components = nlohmann::json::array();

	// Datamaps whose keys the class has from itself or its bases, so they are written once: those of its datamap chain,
	// and those of each component by the base component's name, like the scene node of CBodyComponent and its override CBodyComponentPoint
	std::unordered_set<const datamap_t*> m_DataMaps;
	std::map<std::string, std::unordered_set<const datamap_t*>> m_ComponentDataMaps;
	bool m_bKeysAdded = false;
};

static const std::pair<int, const char*> g_ClassFlagNames[] = {
	{ ECF_NOT_NETWORKED, "ECF_NOT_NETWORKED" },
	{ ECF_ALIAS, "ECF_ALIAS" },
	{ ECF_SPAWN_GROUP_HANDLE_INVALID, "ECF_SPAWN_GROUP_HANDLE_INVALID" },
	{ ECF_HAS_REQUIRED_ENTITY_HANDLE, "ECF_HAS_REQUIRED_ENTITY_HANDLE" },
	{ ECF_ALWAYS_SPAWN_ON_CLIENT, "ECF_ALWAYS_SPAWN_ON_CLIENT" },
	{ ECF_BECOME_SUSPENDED_INSTEAD_OF_DORMANT, "ECF_BECOME_SUSPENDED_INSTEAD_OF_DORMANT" },
	{ ECF_ANONYMOUS_ENTITY, "ECF_ANONYMOUS_ENTITY" },
	{ ECF_PRECACHE_NETWORKED_ENTITY_ON_CLIENT, "ECF_PRECACHE_NETWORKED_ENTITY_ON_CLIENT" },
	{ ECF_UNK001, "ECF_UNK001" },
	{ ECF_UNK002, "ECF_UNK002" },
	{ ECF_FORCE_WORLDGROUPID, "ECF_FORCE_WORLDGROUPID" },
};

// Classes add and override a few components at most, more means the list is not what it's expected to be
static constexpr int g_MaxComponents = 64;

// The names of the set flags, and the bits that are neither named nor otherwise known as one UNKNOWN_ name
template <size_t N>
static std::vector<std::string> GetFlagNames(uint32_t flags, const std::pair<int, const char*> (&names)[N], uint32_t otherKnownFlags, std::string_view unknownPrefix)
{
	std::vector<std::string> result;
	uint32_t known = otherKnownFlags;

	for (const auto& [flag, name] : names)
	{
		known |= flag;
		if (flags & flag)
			result.push_back(name);
	}

	if (auto unknown = flags & ~known)
		result.push_back(fmt::format("{}UNKNOWN_{:X}", unknownPrefix, unknown));

	return result;
}

// Flags as ", NAME, NAME" for FGD comments
static std::string FormatFlags(const std::vector<std::string>& flags)
{
	std::string text;
	for (const auto& flag : flags)
		text += ", " + flag;

	return text;
}

// Flags which only a few classes set
static std::vector<std::string> GetClassFlags(const CEntityClass* entityClass)
{
	return GetFlagNames(entityClass->m_flags, g_ClassFlagNames, 0, "ECF_");
}

// C++ class, flags and spawn order, for the comment above FGD classes
static std::string GetClassComment(const CEntityClass* entityClass)
{
	auto comment = std::string(entityClass->m_pClassInfo->m_pszCPPClassname) + FormatFlags(GetClassFlags(entityClass));

	if (entityClass->m_SpawnOrder)
		comment += fmt::format(", spawn order {}", entityClass->m_SpawnOrder);

	return comment;
}

// Datadesc field types, and the FGD key type Hammer knows for them
static const std::pair<const char*, const char*> g_FieldTypeNames[] = {
	{ "FIELD_VOID", "string" },
	{ "FIELD_FLOAT32", "float" },
	{ "FIELD_STRING", "string" },
	{ "FIELD_VECTOR", "vector" },
	{ "FIELD_QUATERNION", "string" },
	{ "FIELD_INT32", "integer" },
	{ "FIELD_BOOLEAN", "boolean" },
	{ "FIELD_INT16", "integer" },
	{ "FIELD_CHARACTER", "string" },
	{ "FIELD_COLOR32", "color255" },
	{ "FIELD_EMBEDDED", "void" },
	{ "FIELD_EHANDLE", "target_destination" },
	{ "FIELD_POSITION_VECTOR", "vector" },
	{ "FIELD_TIME", "float" },
	{ "FIELD_TICK", "integer" },
	{ "FIELD_SOUNDNAME", "sound" },
	{ "FIELD_VECTOR2D", "vector2d" },
	{ "FIELD_INT64", "integer" },
	{ "FIELD_VECTOR4D", "string" },
	{ "FIELD_UINT64", "integer" },
	{ "FIELD_UINT32", "integer" },
	{ "FIELD_UTLSTRINGTOKEN", "string" },
	{ "FIELD_QANGLE", "angle" },
	{ "FIELD_NETWORK_ORIGIN_CELL_QUANTIZED_VECTOR", "vector" },
	{ "FIELD_HMATERIAL", "material" },
	{ "FIELD_HMODEL", "studio" },
	{ "FIELD_NETWORK_QUANTIZED_VECTOR", "vector" },
	{ "FIELD_NETWORK_QUANTIZED_FLOAT", "float" },
	{ "FIELD_DIRECTION_VECTOR_WORLDSPACE", "vector" },
	{ "FIELD_QANGLE_WORLDSPACE", "angle" },
	{ "FIELD_QUATERNION_WORLDSPACE", "string" },
	{ "FIELD_UTLSTRING", "string" },
	{ "FIELD_HRENDERTEXTURE", "resource:texture" },
	{ "FIELD_HPARTICLESYSTEMDEFINITION", "particlesystem" },
	{ "FIELD_UINT8", "integer" },
	{ "FIELD_UINT16", "integer" },
	{ "FIELD_HPOSTPROCESSING", "resource:postprocessing" },
	{ "FIELD_AMMO_INDEX", "integer" },
	{ "FIELD_MODIFIER_HANDLE", "string" },
	{ "FIELD_HVDATA", "string" },
	{ "FIELD_GLOBALSYMBOL", "string" },
	{ "FIELD_NETWORK_QUANTIZED_VECTORWS", "vector" },
	{ "FIELD_NETWORK_ORIGIN_CELL_QUANTIZED_VECTORWS", "vector" },
};

static_assert(std::size(g_FieldTypeNames) == (size_t)SpawnKeyType_t::FIELD_TYPECOUNT, "Field type names do not match SpawnKeyType_t in the SDK");

static std::pair<std::string, std::string> GetFieldTypeNames(SpawnKeyType_t type)
{
	if ((size_t)type < std::size(g_FieldTypeNames))
		return g_FieldTypeNames[(size_t)type];

	return { fmt::format("FIELD_UNKNOWN_{}", (int)type), "string" };
}

// Datamaps are static objects in the module, but some build their fields on load, so only the names can be checked.
// Returns what is invalid, or null.
static const char* ValidateDataMap(const datamap_t* map)
{
	if (!Modules::FindModuleContaining(map) || !Modules::IsValidName(map->dataClassName))
		return "datamap";
	if (map->dataNumFields < 0 || (map->dataNumFields > 0 && !map->dataDesc))
		return "datamap fields";

	// Names can be null or empty, like in the empty field some datamaps have
	auto isValidOptionalName = [](const char* name) { return !name || (Modules::FindModuleContaining(name) && (!name[0] || Modules::IsValidName(name))); };

	for (int i = 0; i < map->dataNumFields; i++)
	{
		const auto& field = map->dataDesc[i];

		if (!isValidOptionalName(field.fieldName) || !isValidOptionalName(field.externalName))
			return "datamap field name";
	}

	return nullptr;
}

// Schema enums, which have the values of enum keys, and schema classes, which have the modules and component datamaps, by name
struct ModuleSchemas_t
{
	std::unordered_map<std::string, const SchemaEnumInfoData_t*> m_Enums;
	std::unordered_map<std::string, const SchemaClassInfoData_t*> m_Classes;

	// Datamaps of the schema classes in every scope, with the module of their class
	std::vector<std::pair<const datamap_t*, std::string_view>> m_DataMaps;

	// Datamaps whose keys are in the FGD, filled while adding them
	std::unordered_set<const datamap_t*> m_WrittenDataMaps;

	// Datamaps of the module's entity classes
	std::unordered_set<const datamap_t*> m_EntityDataMaps;
};

// The class keys are added to, and the component they are on with the datamaps it already has
struct KeyTarget_t
{
	ModuleSchemas_t& m_Schemas;
	EntityClass_t& m_Entity;
	const char* m_pszComponent = nullptr;
	std::unordered_set<const datamap_t*>* m_pComponentDataMaps = nullptr;
};

// Flags that only a few keys have, the others are shown by the key's type and name
static const std::pair<int, const char*> g_RareFieldFlagNames[] = {
	{ FTYPEDESC_ADDED_KEYFIELD, "ADDED_KEYFIELD" },
	{ FTYPEDESC_ADDITIONAL_FIELDS, "ADDITIONAL_FIELDS" },
	{ FTYPEDESC_EXPLICIT_BASE, "EXPLICIT_BASE" },
};

static constexpr int g_ShownFieldFlags = FTYPEDESC_KEY | FTYPEDESC_PTR | FTYPEDESC_GEN_ARRAY_KEYNAMES_0 | FTYPEDESC_GEN_ARRAY_KEYNAMES_1 | FTYPEDESC_PROCEDURAL_KEYFIELD | FTYPEDESC_ENUM |
                                         FTYPEDESC_REMOVED_KEYFIELD;

static std::vector<std::string> GetRareFieldFlags(int flags)
{
	return GetFlagNames(flags, g_RareFieldFlagNames, g_ShownFieldFlags, "");
}

// Enums and classes shared by client and server are only in one of their scopes, and others are in library scopes,
// so all scopes are used. The module's own scope comes first, then the others by name.
// Returns false if an enum is invalid.
static bool GetModuleSchemas(const CModule& module, ModuleSchemas_t& schemas)
{
	if (!Interfaces::schemaSystem)
	{
		spdlog::critical("Schema system is not initialized, can't read the values of entity enum keys");
		return false;
	}

	const auto moduleScopeName = fmt::format("{}{}{}", MODULE_PREFIX, module.m_pszModule, MODULE_EXT);
	auto scopes = Schemas::GetTypeScopes();

	std::sort(scopes.begin(), scopes.end(), [&](auto a, auto b) {
		return std::make_tuple(moduleScopeName != a->m_szScopeName, std::string_view(a->m_szScopeName)) < std::make_tuple(moduleScopeName != b->m_szScopeName, std::string_view(b->m_szScopeName));
	});

	std::unordered_set<const datamap_t*> dataMaps;

	for (auto typeScope : scopes)
	{
		FOR_EACH_MAP(typeScope->m_DeclaredEnums.m_Map, iter)
		{
			const auto enumInfo = typeScope->m_DeclaredEnums.m_Map.Element(iter)->m_pEnumInfo;

			// Declared by name only
			if (!enumInfo)
				continue;

			if (auto invalid = Schemas::ValidateEnum(enumInfo))
			{
				spdlog::critical("Schema enum in {} has an invalid {}, SchemaEnumInfoData_t in the SDK needs updating", typeScope->m_szScopeName, invalid);
				return false;
			}

			schemas.m_Enums.try_emplace(enumInfo->m_pszName, enumInfo);
		}

		// Validated by the schemas dumper
		FOR_EACH_MAP(typeScope->m_DeclaredClasses.m_Map, iter)
		{
			const auto classInfo = typeScope->m_DeclaredClasses.m_Map.Element(iter)->m_pClassInfo;
			if (!classInfo)
				continue;

			schemas.m_Classes.try_emplace(classInfo->m_pszName, classInfo);

			if (classInfo->m_pDataDescMap && dataMaps.insert(classInfo->m_pDataDescMap).second)
				schemas.m_DataMaps.emplace_back(classInfo->m_pDataDescMap, classInfo->m_pszProjectName);
		}
	}

	return true;
}

// Keys of arrays are named from a pattern like "cpoint%d" or "Filter%02d", one key per element.
// Returns nothing if the pattern is not understood. Returns false if the array flags don't match the pattern.
static bool GetArrayKeyNames(const typedescription_t& field, const char* key, std::vector<std::string>& names)
{
	const bool isArray = field.flags & (FTYPEDESC_GEN_ARRAY_KEYNAMES_0 | FTYPEDESC_GEN_ARRAY_KEYNAMES_1);

	// Only %d with an optional zero padded width is used, like the game formats them
	auto spec = strchr(key, '%');
	if (spec)
	{
		for (spec++; std::isdigit((unsigned char)*spec); spec++)
			;
	}

	const bool isPattern = spec && *spec == 'd' && !strchr(spec, '%');

	// Keys without a pattern having these flags means the FTYPEDESC flags in the SDK are outdated
	if (isArray != isPattern)
		return !isArray;

	if (isArray)
	{
		const int start = (field.flags & FTYPEDESC_GEN_ARRAY_KEYNAMES_1) ? 1 : 0;
		for (int i = 0; i < field.fieldSize; i++)
			names.push_back(fmt::sprintf(key, start + i));
	}

	return true;
}

// Datamaps chain to their base class datamap, deeper means the chain is not what it's expected to be
static constexpr int g_MaxDataMapDepth = 64;

static bool AddDataMapKeys(const datamap_t* map, const char* ownerName, KeyTarget_t& target, std::unordered_set<const datamap_t*>& dataMaps, const std::string& indent, const std::string& path,
	const std::vector<std::string>& pathFlags, std::vector<std::string>& lines, bool isEntityClass = false);

// Keys that can be set on the entity as FGD keys, with the datadesc type and C++ field in a comment.
// Enum keys list their values as choices. Keys from embedded datamaps (like CCollisionProperty) are indented under a comment with their field.
// The same keys go into the class's schemas.json keys, with the path of the embedded datamap fields they are under,
// and the rare flags of those fields, like EXPLICIT_BASE, with their own.
// Returns false if the datamap is not what it's expected to be.
static bool AddKeyFields(const datamap_t* map, KeyTarget_t& target, const std::string& indent, const std::string& path, const std::vector<std::string>& pathFlags, std::vector<std::string>& lines)
{
	const auto& schemas = target.m_Schemas;

	for (int i = 0; i < map->dataNumFields; i++)
	{
		const auto& field = map->dataDesc[i];
		if (!field.fieldName || !field.fieldName[0])
			continue;

		const auto flags = GetRareFieldFlags(field.flags);
		const auto flagsComment = FormatFlags(flags);

		if (field.fieldType == SpawnKeyType_t::FIELD_EMBEDDED)
		{
			if (!field.td)
				continue;

			// Outside components, the same struct can be embedded in several fields, like fogparams_t in the player pawns
			std::unordered_set<const datamap_t*> embeddedDataMaps;
			auto& dataMaps = target.m_pComponentDataMaps ? *target.m_pComponentDataMaps : embeddedDataMaps;

			auto embeddedFlags = pathFlags;
			for (const auto& flag : flags)
			{
				if (std::ranges::find(embeddedFlags, flag) == embeddedFlags.end())
					embeddedFlags.push_back(flag);
			}

			std::vector<std::string> embedded;
			if (!AddDataMapKeys(field.td, field.td->dataClassName, target, dataMaps, indent + "\t", path.empty() ? field.fieldName : path + "." + field.fieldName, embeddedFlags, embedded))
				return false;

			if (!embedded.empty())
			{
				lines.push_back(fmt::format("{}// {} ({}{})", indent, field.fieldName, field.td->dataClassName, flagsComment));
				lines.insert(lines.end(), embedded.begin(), embedded.end());
			}

			continue;
		}

		// Procedural keys are handled in code, and removed keys remove a key of a base class.
		// Neither has a C++ field, their key is in the field name.
		const bool isProcedural = field.flags & FTYPEDESC_PROCEDURAL_KEYFIELD;
		const bool isRemoved = field.flags & FTYPEDESC_REMOVED_KEYFIELD;
		const bool hasCppField = !isProcedural && !isRemoved;
		const char* key = hasCppField ? field.externalName : field.fieldName;
		if (!key || !key[0])
			continue;

		auto [typeName, fgdType] = GetFieldTypeNames(field.fieldType);

		// FGDs remove keys a base class has with this type
		if (isRemoved)
			fgdType = "remove_key";

		auto cppField = hasCppField ? fmt::format(" {}", field.fieldName) : std::string();

		// The union holds an enum name for enum fields
		const SchemaEnumInfoData_t* enumInfo = nullptr;
		std::string enumComment;
		nlohmann::json keyJson;
		if (Modules::IsValidName(field.enumName))
		{
			if (auto it = schemas.m_Enums.find(field.enumName); it != schemas.m_Enums.end())
				enumInfo = it->second;
			else
				spdlog::warn("Enum {} of key {} in {} is not in the schema system", field.enumName, key, map->dataClassName);

			enumComment = fmt::format(" ({})", field.enumName);
			keyJson["enum"] = field.enumName;

			if (enumInfo)
				keyJson["enumModule"] = enumInfo->m_pszProjectName;
		}

		target.m_Entity.m_Strings.push_back(key);

		std::vector<std::string> arrayKeyNames;
		if (!GetArrayKeyNames(field, key, arrayKeyNames))
		{
			spdlog::critical("Key {} of {} has array flags {:X} without a key name pattern, the FTYPEDESC flags in the SDK need updating", key, map->dataClassName, field.flags);
			return false;
		}

		keyJson["name"] = key;
		keyJson["type"] = typeName;
		keyJson["declaredIn"] = map->dataClassName;

		// Embedded structs can be from another module, like hudtextparms_t in server's CGameText is only in client
		if (auto it = schemas.m_Classes.find(map->dataClassName); it != schemas.m_Classes.end())
			keyJson["declaredInModule"] = it->second->m_pszProjectName;

		if (target.m_pszComponent)
			keyJson["component"] = target.m_pszComponent;
		if (hasCppField)
			keyJson["field"] = field.fieldName;
		if (!path.empty())
			keyJson["path"] = path;
		if (isProcedural)
			keyJson["procedural"] = true;
		if (isRemoved)
			keyJson["removed"] = true;
		auto keyFlags = flags;
		for (const auto& flag : pathFlags)
		{
			if (std::ranges::find(keyFlags, flag) == keyFlags.end())
				keyFlags.push_back(flag);
		}

		if (!keyFlags.empty())
			keyJson["flags"] = std::move(keyFlags);

		// Array keys are one key with the name pattern
		if (!arrayKeyNames.empty())
		{
			keyJson["arrayStart"] = (field.flags & FTYPEDESC_GEN_ARRAY_KEYNAMES_1) ? 1 : 0;
			keyJson["arrayCount"] = arrayKeyNames.size();
		}

		target.m_Entity.m_Keys.push_back(std::move(keyJson));

		// Procedural keys like weapon%d are named in code and have no array size
		if (arrayKeyNames.empty() && strchr(key, '%'))
		{
			lines.push_back(fmt::format("{}// {}({}) // {}{}{}{}", indent, key, fgdType, typeName, cppField, enumComment, flagsComment));
			continue;
		}

		if (arrayKeyNames.empty())
			arrayKeyNames.push_back(key);

		for (size_t k = 0; k < arrayKeyNames.size(); k++)
		{
			const auto& keyName = arrayKeyNames[k];
			auto comment = fmt::format("{}{}{}{}{}", typeName, cppField, keyName != key ? fmt::format("[{}]", k) : "", enumComment, flagsComment);

			if (!enumInfo || isRemoved)
			{
				lines.push_back(fmt::format("{}{}({}) // {}", indent, keyName, fgdType, comment));
				continue;
			}

			lines.push_back(fmt::format("{}{}(intchoices) : \"{}\" : : \"\" = // {}", indent, keyName, keyName, comment));
			lines.push_back(indent + "[");

			for (uint16_t e = 0; e < enumInfo->m_nEnumeratorCount; e++)
				lines.push_back(fmt::format("{}\t{} : \"{}\"", indent, enumInfo->m_pEnumerators[e].m_nValue, enumInfo->m_pEnumerators[e].m_pszName));

			lines.push_back(indent + "]");
		}
	}

	return true;
}

// Keys of a datamap and its bases, down to one in dataMaps, which the class already has the keys of.
// Bases with keys are under a comment with their name, unless it's the owner's, like the class the datamap is of.
// For an entity class's own datamap, the walk stops at a base datamap that isn't an entity class's: the engine builds each
// entity class's key table from its datamap only, linked to its base entity class's table, and follows the base datamaps
// only in embedded fields (FIELD_EMBEDDED, and FTYPEDESC_EXPLICIT_BASE ones that bring in a base that isn't an entity class).
// Returns false if a datamap is invalid.
static bool AddDataMapKeys(const datamap_t* map, const char* ownerName, KeyTarget_t& target, std::unordered_set<const datamap_t*>& dataMaps, const std::string& indent, const std::string& path,
	const std::vector<std::string>& pathFlags, std::vector<std::string>& lines, bool isEntityClass)
{
	int depth = 0;

	for (; map && !dataMaps.contains(map); map = map->baseMap)
	{
		auto invalid = ValidateDataMap(map);
		if (!invalid && ++depth > g_MaxDataMapDepth)
			invalid = "datamap chain";

		if (invalid)
		{
			spdlog::critical("Datamap of {} has an invalid {}, datamap_t in the SDK needs updating", ownerName, invalid);
			return false;
		}

		if (isEntityClass && depth > 1 && !target.m_Schemas.m_EntityDataMaps.contains(map))
		{
			if (map->dataNumFields > 0)
				spdlog::info("Not listing the keys of datamap {} under {}, the engine doesn't apply a base datamap that isn't an entity class's", map->dataClassName, ownerName);

			break;
		}

		dataMaps.insert(map);
		target.m_Schemas.m_WrittenDataMaps.insert(map);

		std::vector<std::string> keys;
		if (!AddKeyFields(map, target, indent, path, pathFlags, keys))
			return false;

		if (!keys.empty() && strcmp(ownerName, map->dataClassName))
			lines.push_back(fmt::format("{}// {}", indent, map->dataClassName));

		lines.insert(lines.end(), keys.begin(), keys.end());
	}

	return true;
}

// Whether a Pulse type is the base type or one of its subtypes, like PVAL_EHANDLE:trigger or PVAL_VEC3_WORLDSPACE
static bool IsPulseType(const std::string& type, std::string_view baseType)
{
	return type.starts_with(baseType) && (type.size() == baseType.size() || type[baseType.size()] == ':' || type[baseType.size()] == '_');
}

// FGD type of an input or output with one parameter, like Valve's FGDs use them
static const char* GetPulseFGDType(const std::string& type)
{
	static const std::pair<const char*, const char*> pulseTypes[] = {
		{ "PVAL_FLOAT", "float" },
		{ "PVAL_INT", "integer" },
		{ "PVAL_BOOL", "boolean" },
		{ "PVAL_STRING", "string" },
		{ "PVAL_COLOR_RGB", "color255" },
		{ "PVAL_VEC3", "vector" },
		{ "PVAL_EHANDLE", "target_destination" },
	};

	for (const auto& [pulseType, fgdType] : pulseTypes)
	{
		if (IsPulseType(type, pulseType))
			return fgdType;
	}

	return "string";
}

// Pulse parameters without the target parameter, as [{ name, type }] like in schemas.json.
// Schema enum parameters, typed like PVAL_SCHEMA_ENUM:Name or PVAL_ARRAY:PVAL_SCHEMA_ENUM:Name, also get the enum's module.
static nlohmann::json GetPulseParams(const nlohmann::ordered_json& meta, const char* paramsKey, const std::string& targetArg, const ModuleSchemas_t& schemas)
{
	auto params = nlohmann::json::array();

	if (auto it = meta.find(paramsKey); it != meta.end() && it->is_object())
	{
		for (const auto& [name, param] : it->items())
		{
			if (name == targetArg)
				continue;

			const auto type = param.value("type", std::string());
			nlohmann::json paramJson{ { "name", name }, { "type", type } };

			constexpr std::string_view enumPrefix = "PVAL_SCHEMA_ENUM:";
			if (auto prefix = type.find(enumPrefix); prefix != std::string::npos)
			{
				if (auto it = schemas.m_Enums.find(type.substr(prefix + enumPrefix.size())); it != schemas.m_Enums.end())
					paramJson["enumModule"] = it->second->m_pszProjectName;
			}

			params.push_back(std::move(paramJson));
		}
	}

	return params;
}

// Parameters as "type name, type name" for FGD comments
static std::string FormatPulseParams(const nlohmann::json& params)
{
	std::string text;
	for (const auto& param : params)
		text += fmt::format("{}{} {}", text.empty() ? "" : ", ", param["type"].get<std::string>(), param["name"].get<std::string>());

	return text;
}

// Entity inputs and outputs are Pulse bindings in the module metadata, on the API class of the entity (like CBaseTrigger_API).
// Their target parameter is a handle to the entity's design name, or to any entity for the base entity API.
// Returns false if the metadata is not what it's expected to be.
static bool AddInputsAndOutputs(const CModule& module, const std::string& rootName, const ModuleSchemas_t& schemas, std::map<std::string, EntityClass_t>& classes)
{
	const auto& metadata = ModuleMetadata::GetJSON(module);

	// Already logged
	if (metadata.is_discarded())
		return false;

#ifndef _WIN32
	// Modules have no metadata on Linux
	if (metadata.is_null())
	{
		spdlog::info("{} has no module metadata, not writing entity inputs and outputs", module.m_pszModule);
		return true;
	}
#endif

	static const auto bindingsPointer = "/pulse_bindings/gamedata/m_Classes"_json_pointer;
	if (!metadata.is_object() || !metadata.contains(bindingsPointer) || !metadata[bindingsPointer].is_object())
	{
		spdlog::critical("Module metadata of {} has no pulse_bindings.gamedata.m_Classes for entity inputs and outputs", module.m_pszModule);
		return false;
	}

	int inputs = 0;
	int outputs = 0;

	for (const auto& [key, binding] : metadata[bindingsPointer].items())
	{
		auto metaIt = binding.find("m_MetaData");
		if (metaIt == binding.end() || !metaIt->is_object())
			continue;

		const auto& meta = *metaIt;
		const bool isInput = meta.value("is_pulse_target_method", false);
		if (!isInput && !meta.value("is_pulse_target_output", false))
			continue;

		// Inputs take in parameters and can return out parameters, outputs pass out parameters
		const auto targetArg = meta.value("target_arg_name", std::string());
		const auto paramsKey = isInput ? "pulse_inparams" : "pulse_outparams";
		const auto targetType = meta.value(nlohmann::ordered_json::json_pointer(fmt::format("/{}/{}/type", paramsKey, targetArg)), std::string());

		// Other APIs, like CTakeDamageResultAPI, target an opaque handle and are not on entities
		if (!IsPulseType(targetType, "PVAL_EHANDLE"))
			continue;

		auto subtype = targetType.find(':');
		auto designName = subtype == std::string::npos ? rootName : targetType.substr(subtype + 1);

		auto entity = classes.find(designName);
		if (entity == classes.end())
		{
			spdlog::warn("Entity {} of {} {} is not in the entity class list of {}", designName, isInput ? "input" : "output", key, module.m_pszModule);
			continue;
		}

		auto name = key.substr(key.rfind(':') + 1);
		auto params = GetPulseParams(meta, paramsKey, targetArg, schemas);

		// Inputs with several parameters can only be called from Pulse
		const bool isApi = params.size() > 1;
		const char* fgdType = params.empty() ? "void" : isApi ? "api"
		                                                      : GetPulseFGDType(params[0]["type"].get<std::string>());
		auto line = fmt::format("\t{} {}({})", isInput ? "input" : "output", name, fgdType);

		// Empty lists and a false pulseNode are left out
		nlohmann::json json;
		json["name"] = name;
		if (!params.empty())
			json["params"] = params;

		std::vector<std::string> comments;
		if (!params.empty())
			comments.push_back(FormatPulseParams(params));

		// Some inputs are Pulse functions that return values
		if (isInput)
		{
			auto returned = GetPulseParams(meta, "pulse_outparams", targetArg, schemas);
			if (!returned.empty())
				comments.push_back("returns " + FormatPulseParams(returned));

			// Most inputs are hidden in the Pulse editor, the rest are also Pulse nodes
			const bool isPulseNode = !meta.value("hidden_in_tool", false);
			if (isPulseNode)
				comments.push_back("Pulse node");

			if (!returned.empty())
				json["returns"] = std::move(returned);
			if (isPulseNode)
				json["pulseNode"] = true;
		}

		auto comment = fmt::format("{}", fmt::join(comments, ", "));

		auto description = binding.value("m_Description", std::string());
		if (!description.empty())
			json["description"] = description;

		// FGD strings are one line without quotes, and api inputs and outputs can't have a description
		std::replace(description.begin(), description.end(), '"', '\'');
		std::replace(description.begin(), description.end(), '\n', ' ');
		if (!description.empty())
		{
			if (isApi)
				comment += ": " + description;
			else
				line += fmt::format(" : \"{}\"", description);
		}

		if (!comment.empty())
			line += " // " + comment;

		(isInput ? entity->second.m_Inputs : entity->second.m_Outputs).push_back({ std::move(line), std::move(json) });
		entity->second.m_Strings.push_back(name);
		(isInput ? inputs : outputs)++;
	}

	// Fields in the metadata being renamed would silently lose them
	if (!inputs || !outputs)
	{
		spdlog::critical("Found {} inputs and {} outputs in the module metadata of {}, the Pulse binding format changed", inputs, outputs, module.m_pszModule);
		return false;
	}

	spdlog::debug("Found {} entity inputs and {} outputs in {}", inputs, outputs, module.m_pszModule);
	return true;
}

// The SDK layout can be outdated while the signature still matches, so check the list before reading it.
// Classes and their infos are static objects in the module. Returns what is invalid, or null.
static const char* ValidateEntityClass(const CModule& module, const CEntityClass* entityClass)
{
	if (!Modules::IsInModule(module, entityClass))
		return "class pointer";

	auto classInfo = entityClass->m_pClassInfo;
	if (!Modules::IsInModule(module, classInfo) || classInfo->m_pClass != entityClass)
		return "class info";

	if (!Modules::IsValidName(classInfo->m_pszCPPClassname))
		return "class name";

	// Classes without a design name can't be created by name
	auto designName = classInfo->m_pszClassname;
	if (designName && (!Modules::FindModuleContaining(designName) || (designName[0] && !Modules::IsValidName(designName))))
		return "design name";

	for (auto base = classInfo->m_pBaseClassInfo; base; base = base->m_pBaseClassInfo)
	{
		if (!Modules::FindModuleContaining(base) || !Modules::IsValidName(base->m_pszCPPClassname) || (base->m_pszClassname && !Modules::FindModuleContaining(base->m_pszClassname)))
			return "base class info";
	}

	// The schema class, which can be in another module like CEntityInstance in entity2
	if (auto binding = classInfo->m_pSchemaBinding; binding && (!Modules::FindModuleContaining(binding) || !Modules::IsValidName(binding->m_pszProjectName)))
		return "schema binding";

	if (entityClass->m_pfnEnumerateComponents && !Modules::IsInModule(module, (const void*)entityClass->m_pfnEnumerateComponents))
		return "components function";

	// A static array that ends with an empty entry
	if (auto overrides = entityClass->m_pComponentOverrides)
	{
		if (!Modules::IsInModule(module, overrides))
			return "component overrides";

		for (int i = 0; overrides[i].pszBaseComponent; i++)
		{
			if (i >= g_MaxComponents || !Modules::IsValidName(overrides[i].pszBaseComponent) || !Modules::IsValidName(overrides[i].pszOverrideComponent))
				return "component overrides";
		}
	}

	return nullptr;
}

// Classes without a design name can't be created by name, like CBaseEntity
static bool HasDesignName(const CEntityClassInfo* classInfo)
{
	return classInfo->m_pszClassname && classInfo->m_pszClassname[0];
}

static bool IsSpawnable(const CEntityClassInfo* classInfo)
{
	return HasDesignName(classInfo) && !(classInfo->m_nFlags & ECIF_NOT_SPAWNABLE);
}

// FGD class name, the C++ class name for classes without a design name
static std::string GetFGDName(const CEntityClassInfo* classInfo)
{
	return HasDesignName(classInfo) ? classInfo->m_pszClassname : classInfo->m_pszCPPClassname;
}

// Datamap of a component, from its schema class. Components without keys of their own have none, and use their base's.
static const datamap_t* GetComponentDataMap(const ModuleSchemas_t& schemas, const char* name)
{
	auto it = schemas.m_Classes.find(name);
	if (it == schemas.m_Classes.end())
	{
		spdlog::warn("Entity component {} is not in the schema system, not writing its keys", name);
		return nullptr;
	}

	auto classInfo = it->second;
	for (int depth = 0; classInfo && depth < g_MaxDataMapDepth; depth++)
	{
		if (classInfo->m_pDataDescMap)
			return classInfo->m_pDataDescMap;

		classInfo = classInfo->m_nBaseClassCount > 0 ? classInfo->m_pBaseClasses[0].m_pClass : nullptr;
	}

	return nullptr;
}

// Components the class adds and the ones it overrides, under a comment with the keys they add.
// The classes add their own components, and overrides replace a base's component with a subclass of it, which adds keys to the base's.
// Returns false if the components or a datamap are invalid.
static bool AddComponentKeys(const CEntityClass* entityClass, ModuleSchemas_t& schemas, EntityClass_t& entity)
{
	// The base component is the one it overrides, or itself, which overrides share the written datamaps of
	auto addComponent = [&](const std::string& comment, const char* name, const char* baseName) {
		entity.m_Lines.push_back(fmt::format("\t// component {}", comment));
		entity.m_Strings.push_back(name);

		auto& dataMaps = entity.m_ComponentDataMaps[baseName];
		KeyTarget_t target{ schemas, entity, name, &dataMaps };
		auto map = GetComponentDataMap(schemas, name);
		return !map || AddDataMapKeys(map, name, target, dataMaps, "\t\t", "", {}, entity.m_Lines);
	};

	if (entityClass->m_pfnEnumerateComponents)
	{
		// Allocated by the game, and freed with memoverride.cpp
		CUtlVector<EntComponentNameEntry_t> added;
		entityClass->m_pfnEnumerateComponents(&added);

		if (added.Count() > g_MaxComponents)
		{
			spdlog::critical("Entity class {} has {} components, EntComponentNameEntry_t in the SDK needs updating", entityClass->m_pClassInfo->m_pszCPPClassname, added.Count());
			return false;
		}

		for (int i = 0; i < added.Count(); i++)
		{
			const auto& component = added[i];

			if (!Modules::IsValidName(component.pszComponentClassName))
			{
				spdlog::critical("Entity class {} has an invalid component name, EntComponentNameEntry_t in the SDK needs updating", entityClass->m_pClassInfo->m_pszCPPClassname);
				return false;
			}

			entity.m_Components.push_back({ { "name", component.pszComponentClassName } });
			if (!addComponent(component.pszComponentClassName, component.pszComponentClassName, component.pszComponentClassName))
				return false;
		}
	}

	for (auto overrides = entityClass->m_pComponentOverrides; overrides && overrides->pszBaseComponent; overrides++)
	{
		entity.m_Components.push_back({ { "base", overrides->pszBaseComponent }, { "override", overrides->pszOverrideComponent } });
		if (!addComponent(fmt::format("{}: {}", overrides->pszBaseComponent, overrides->pszOverrideComponent), overrides->pszOverrideComponent, overrides->pszBaseComponent))
			return false;
	}

	return true;
}

// Keys the class adds to its nearest base in the FGD: its components, and its datamap down to the base's datamap.
// Returns false if a datamap is invalid.
static bool AddClassKeys(const CEntityClass* entityClass, ModuleSchemas_t& schemas, EntityClass_t& entity)
{
	if (!AddComponentKeys(entityClass, schemas, entity))
		return false;

	KeyTarget_t target{ schemas, entity };
	return AddDataMapKeys(entityClass->m_pClassInfo->m_pDataDescMap, entityClass->m_pClassInfo->m_pszCPPClassname, target, entity.m_DataMaps, "\t", "", {}, entity.m_Lines, true);
}

// Datamaps in the module that no entity class has, like modifiers and structs only code fills, as FGD base classes nothing uses.
// A base datamap that has its own block is the FGD base, like for the entity classes. Returns false if a datamap is invalid.
static bool GetUnusedDataMapLines(const CModule& module, ModuleSchemas_t& schemas, std::vector<std::string>& lines)
{
	// By the module of their class, as pointers can't be checked to be in a module on Linux
	std::vector<const datamap_t*> unused;
	for (const auto& [map, project] : schemas.m_DataMaps)
	{
		if (project == module.m_pszModule && !schemas.m_WrittenDataMaps.contains(map))
			unused.push_back(map);
	}

	// Only those that have keys of their own get a block
	std::unordered_set<const datamap_t*> withKeys;
	for (auto map : unused)
	{
		if (auto invalid = ValidateDataMap(map))
		{
			spdlog::critical("Schema class datamap in {} has an invalid {}, datamap_t in the SDK needs updating", module.m_pszModule, invalid);
			return false;
		}

		EntityClass_t entity;
		KeyTarget_t target{ schemas, entity };
		std::vector<std::string> keys;
		if (!AddKeyFields(map, target, "\t", "", {}, keys))
			return false;

		if (!keys.empty())
			withKeys.insert(map);
	}

	std::sort(unused.begin(), unused.end(), [](auto a, auto b) { return strcmp(a->dataClassName, b->dataClassName) < 0; });

	// FGD base classes have to be defined before the classes that use them
	std::unordered_set<const datamap_t*> written;
	std::function<bool(const datamap_t*)> writeBlock = [&](const datamap_t* map) {
		if (!written.insert(map).second)
			return true;

		auto base = map->baseMap;
		for (int depth = 0; base && Modules::FindModuleContaining(base) && !withKeys.contains(base) && depth < g_MaxDataMapDepth; depth++)
			base = base->baseMap;

		const auto baseBlock = base && withKeys.contains(base) ? base : nullptr;
		if (baseBlock && !writeBlock(baseBlock))
			return false;

		// Keys down to the base's block
		EntityClass_t entity;
		if (baseBlock)
			entity.m_DataMaps.insert(baseBlock);

		KeyTarget_t target{ schemas, entity };
		std::vector<std::string> keys;
		if (!AddDataMapKeys(map, map->dataClassName, target, entity.m_DataMaps, "\t", "", {}, keys))
			return false;

		lines.push_back(fmt::format("@BaseClass{} = {}", baseBlock ? fmt::format(" base({})", baseBlock->dataClassName) : "", map->dataClassName));
		lines.push_back("[");
		lines.insert(lines.end(), keys.begin(), keys.end());
		lines.push_back("]");
		lines.push_back("");

		Globals::stringsIgnoreStream << map->dataClassName << "\n";
		for (const auto& str : entity.m_Strings)
			Globals::stringsIgnoreStream << str << "\n";

		return true;
	};

	for (auto map : unused)
	{
		if (withKeys.contains(map) && !writeBlock(map))
			return false;
	}

	return true;
}

// Inputs or outputs for schemas.json, moved out of the list
static nlohmann::json GetIOJson(std::vector<EntityIO_t>& list)
{
	auto array = nlohmann::json::array();
	for (auto& io : list)
		array.push_back(std::move(io.m_Json));

	return array;
}

// Entities for schemas.json, each with its C++ class which is also its schema class. Moves the JSON out of the classes.
static nlohmann::json GetEntitiesJson(std::map<std::string, std::map<std::string, EntityClass_t>>& entities)
{
	auto array = nlohmann::json::array();

	for (auto& [module, classes] : entities)
	{
		for (auto& [name, entity] : classes)
		{
			auto classInfo = entity.m_pClass->m_pClassInfo;

			// Values that are the same as the entity's own are left out: classModule when it's the module,
			// and the declaring class and module of keys when they're the entity's class
			const std::string classModule = classInfo->m_pSchemaBinding ? classInfo->m_pSchemaBinding->m_pszProjectName : module;

			for (auto& key : entity.m_Keys)
			{
				if (key["declaredIn"] == classInfo->m_pszCPPClassname)
					key.erase("declaredIn");
				if (key.value("declaredInModule", "") == classModule)
					key.erase("declaredInModule");
			}

			nlohmann::json json;
			json["class"] = classInfo->m_pszCPPClassname;
			json["module"] = module;
			if (classModule != module)
				json["classModule"] = classModule;
			json["spawnable"] = IsSpawnable(classInfo);

			if (HasDesignName(classInfo))
				json["designName"] = classInfo->m_pszClassname;
			if (!entity.m_BaseName.empty())
				json["baseClass"] = classes.at(entity.m_BaseName).m_pClass->m_pClassInfo->m_pszCPPClassname;
			// The flags and spawn order as the class declares them, read before CEntitySystem::RegisterEntityClass runs.
			// Registration copies ECF_NOT_NETWORKED, ECF_ALWAYS_SPAWN_ON_CLIENT, ECF_BECOME_SUSPENDED_INSTEAD_OF_DORMANT,
			// ECF_ANONYMOUS_ENTITY and ECF_SPAWN_GROUP_HANDLE_INVALID and ECF_FORCE_WORLDGROUPID from the nearest base,
			// and its spawn order when the class's is zero, so classes have those of their baseClass chain too.
			if (auto flags = GetClassFlags(entity.m_pClass); !flags.empty())
				json["flags"] = std::move(flags);
			if (entity.m_pClass->m_SpawnOrder)
				json["spawnOrder"] = entity.m_pClass->m_SpawnOrder;
			if (!entity.m_Components.empty())
				json["components"] = std::move(entity.m_Components);
			if (!entity.m_Keys.empty())
				json["keys"] = std::move(entity.m_Keys);
			if (!entity.m_Inputs.empty())
				json["inputs"] = GetIOJson(entity.m_Inputs);
			if (!entity.m_Outputs.empty())
				json["outputs"] = GetIOJson(entity.m_Outputs);

			array.push_back(std::move(json));
		}
	}

	return array;
}

// Returns false if the entity class list could not be found or read in a module that has one
bool Dump()
{
	// module -> design name -> class
	std::map<std::string, std::map<std::string, EntityClass_t>> entities;
	std::map<std::string, std::vector<std::string>> unusedDataMaps;
	bool failed = false;

	for (auto& module : Modules::allModules)
	{
		int error;
		auto match = (uint8_t*)module.FindSignature(g_EntityClassListSignature, sizeof(g_EntityClassListSignature) - 1, error);

		if (error == SIG_FOUND_MULTIPLE)
		{
			spdlog::critical("Found multiple entity class list signature matches in {}, make the signature in gamedata.h more specific", module.m_pszModule);
			failed = true;
			continue;
		}

		if (!match)
		{
			if (g_RequiredEntityModules.contains(module.m_pszModule))
			{
				spdlog::critical("Could not find entity class list in {}, update the signature in gamedata.h", module.m_pszModule);
				failed = true;
			}

			continue;
		}

		auto head = *Modules::GetGlobalFromSignatureMatch<CEntityClass*>(match);
		spdlog::debug("Found entity class list in {}, {}", module.m_pszModule, head ? "not empty" : "empty");
		auto& classes = entities[module.m_pszModule];
		bool moduleFailed = false;

		for (auto entityClass = head; entityClass; entityClass = entityClass->m_pNext)
		{
			if (auto invalid = ValidateEntityClass(module, entityClass))
			{
				spdlog::critical("Entity class list in {} has an invalid {}, CEntityClass in the SDK needs updating", module.m_pszModule, invalid);
				moduleFailed = true;
				break;
			}

			EntityClass_t entity;
			entity.m_pClass = entityClass;

			auto fgdName = GetFGDName(entityClass->m_pClassInfo);
			if (!classes.try_emplace(fgdName, std::move(entity)).second)
			{
				spdlog::critical("Entity class list in {} has {} twice", module.m_pszModule, fgdName);
				moduleFailed = true;
				break;
			}
		}

		// Some modules, like engine2, link the entity system without any entity classes
		if (moduleFailed || classes.empty())
		{
			failed |= moduleFailed;
			entities.erase(module.m_pszModule);
			continue;
		}

		// Enums of keys are in the module's schema scope
		ModuleSchemas_t schemas;
		if (!GetModuleSchemas(module, schemas))
		{
			failed = true;
			continue;
		}

		for (const auto& [name, entity] : classes)
		{
			if (entity.m_pClass->m_pClassInfo->m_pDataDescMap)
				schemas.m_EntityDataMaps.insert(entity.m_pClass->m_pClassInfo->m_pDataDescMap);
		}

		// The nearest base in the list, and the keys added since it. The hierarchy root gets the inputs and outputs of any entity.
		std::string rootName;
		for (auto& [name, entity] : classes)
		{
			auto base = entity.m_pClass->m_pClassInfo->m_pBaseClassInfo;
			while (base && !classes.contains(GetFGDName(base)))
				base = base->m_pBaseClassInfo;

			if (base)
			{
				entity.m_BaseName = GetFGDName(base);
			}
			else if (!rootName.empty())
			{
				spdlog::critical("Entity classes in {} have more than one root, {} and {}", module.m_pszModule, rootName, name);
				moduleFailed = true;
				break;
			}
			else
			{
				rootName = name;
			}
		}

		// Bases first, so keys a class reaches again, like through a component override, are left out
		std::function<bool(EntityClass_t&)> addKeys = [&](EntityClass_t& entity) {
			if (entity.m_bKeysAdded)
				return true;

			entity.m_bKeysAdded = true;

			if (!entity.m_BaseName.empty())
			{
				auto& base = classes.at(entity.m_BaseName);
				if (!addKeys(base))
					return false;

				entity.m_DataMaps = base.m_DataMaps;
				entity.m_ComponentDataMaps = base.m_ComponentDataMaps;
			}

			return AddClassKeys(entity.m_pClass, schemas, entity);
		};

		for (auto it = classes.begin(); !moduleFailed && it != classes.end(); ++it)
			moduleFailed = !addKeys(it->second);

		if (moduleFailed || !AddInputsAndOutputs(module, rootName, schemas, classes) || !GetUnusedDataMapLines(module, schemas, unusedDataMaps[module.m_pszModule]))
		{
			failed = true;
			continue;
		}

		// Lines start with the name, so this sorts by name
		auto byName = [](const EntityIO_t& a, const EntityIO_t& b) { return a.m_Line < b.m_Line; };
		for (auto& [name, entity] : classes)
		{
			std::sort(entity.m_Inputs.begin(), entity.m_Inputs.end(), byName);
			std::sort(entity.m_Outputs.begin(), entity.m_Outputs.end(), byName);
		}
	}

	if (failed)
	{
		spdlog::critical("Not writing entities, see above");
		return false;
	}

	if (entities.empty())
	{
		spdlog::info("No entity class lists found, not writing entities");
		return true;
	}

	const auto outputPath = Globals::outputPath / "entities";
	std::filesystem::create_directories(outputPath);

	size_t count = 0;
	std::unordered_set<std::string> fileNames;

	// One FGD per module, the client and server classes of an entity differ
	for (auto& [module, classes] : entities)
	{
		const auto fileName = GetModuleFileName(module);
		fileNames.insert(fileName);

		const auto path = outputPath / (fileName + ".fgd");
		std::ofstream output(path);
		output << "// Dumped by https://github.com/ValveResourceFormat/DumpSource2\n\n";

		std::unordered_set<std::string> written;

		// FGD base classes have to be defined before the classes that use them
		std::function<void(const std::string&)> writeClass = [&](const std::string& designName) {
			if (!written.insert(designName).second)
				return;

			auto& entityClass = classes.at(designName);
			if (!entityClass.m_BaseName.empty())
				writeClass(entityClass.m_BaseName);

			auto classInfo = entityClass.m_pClass->m_pClassInfo;
			output << "// " << GetClassComment(entityClass.m_pClass) << "\n";
			output << (IsSpawnable(classInfo) ? "@PointClass" : "@BaseClass");

			if (!entityClass.m_BaseName.empty())
				output << " base(" << entityClass.m_BaseName << ")";

			output << " = " << designName << "\n[\n";

			for (const auto& line : entityClass.m_Lines)
				output << line << "\n";

			for (const auto* list : { &entityClass.m_Inputs, &entityClass.m_Outputs })
			{
				for (const auto& io : *list)
					output << io.m_Line << "\n";
			}

			output << "]\n\n";

			Globals::stringsIgnoreStream << designName << "\n"
										 << classInfo->m_pszCPPClassname << "\n";

			for (const auto& str : entityClass.m_Strings)
				Globals::stringsIgnoreStream << str << "\n";
		};

		for (const auto& [designName, entityClass] : classes)
			writeClass(designName);

		if (const auto& unused = unusedDataMaps[module]; !unused.empty())
		{
			output << "// Datamaps that no entity class has, like modifiers and structs that only code fills\n\n";

			for (const auto& line : unused)
				output << line << "\n";
		}

		if (!CloseOutput(output, path))
			return false;

		count += classes.size();
	}

	RemoveOrphanFiles(outputPath, fileNames, "entities");

	spdlog::info("Wrote {} entity classes from {} modules to entities", count, entities.size());

	Globals::schemasJson["entities"] = GetEntitiesJson(entities);
	return true;
}

#else

bool Dump()
{
	spdlog::info("Entities are not supported in this game yet");
	return true;
}

#endif

} // namespace Dumpers::Entities
