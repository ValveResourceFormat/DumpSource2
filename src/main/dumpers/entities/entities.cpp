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
#include <entity2/entityclass.h>
#include <datamap.h>
#include <climits>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>

namespace Dumpers::Entities
{

// Other games don't have this entity system yet, and their SDKs have the older entity and datamap layouts
#ifdef GAME_CS2

using namespace GameData;

struct EntityClass_t
{
	std::string m_CPPClassName;
	std::string m_BaseDesignName;
	std::string m_Details;
	std::vector<std::string> m_ComponentOverrides;
	std::vector<std::string> m_KeyFields;
	std::vector<std::string> m_Strings;
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

// Classes override a few components at most, more means the array is not what it's expected to be
static constexpr int g_MaxComponentOverrides = 64;

// Flags, spawnability and spawn order, which only a few classes set
static std::string GetClassDetails(const CEntityClass* entityClass)
{
	std::string details;
	auto flags = entityClass->m_flags;

	for (const auto& [flag, name] : g_ClassFlagNames)
	{
		if (flags & flag)
		{
			details += fmt::format(", {}", name);
			flags &= ~flag;
		}
	}

	if (flags)
		details += fmt::format(", ECF_UNKNOWN_{:X}", flags);

	if (entityClass->m_pClassInfo->m_nFlags & ECIF_NOT_SPAWNABLE)
		details += ", ECIF_NOT_SPAWNABLE";

	if (entityClass->m_SpawnOrder)
		details += fmt::format(", spawn order {}", entityClass->m_SpawnOrder);

	return details;
}

static const char* g_FieldTypeNames[] = {
	"FIELD_VOID",
	"FIELD_FLOAT32",
	"FIELD_STRING",
	"FIELD_VECTOR",
	"FIELD_QUATERNION",
	"FIELD_INT32",
	"FIELD_BOOLEAN",
	"FIELD_INT16",
	"FIELD_CHARACTER",
	"FIELD_COLOR32",
	"FIELD_EMBEDDED",
	"FIELD_EHANDLE",
	"FIELD_POSITION_VECTOR",
	"FIELD_TIME",
	"FIELD_TICK",
	"FIELD_SOUNDNAME",
	"FIELD_VECTOR2D",
	"FIELD_INT64",
	"FIELD_VECTOR4D",
	"FIELD_UINT64",
	"FIELD_UINT32",
	"FIELD_UTLSTRINGTOKEN",
	"FIELD_QANGLE",
	"FIELD_NETWORK_ORIGIN_CELL_QUANTIZED_VECTOR",
	"FIELD_HMATERIAL",
	"FIELD_HMODEL",
	"FIELD_NETWORK_QUANTIZED_VECTOR",
	"FIELD_NETWORK_QUANTIZED_FLOAT",
	"FIELD_DIRECTION_VECTOR_WORLDSPACE",
	"FIELD_QANGLE_WORLDSPACE",
	"FIELD_QUATERNION_WORLDSPACE",
	"FIELD_UTLSTRING",
	"FIELD_HRENDERTEXTURE",
	"FIELD_HPARTICLESYSTEMDEFINITION",
	"FIELD_UINT8",
	"FIELD_UINT16",
	"FIELD_HPOSTPROCESSING",
	"FIELD_AMMO_INDEX",
	"FIELD_MODIFIER_HANDLE",
	"FIELD_HVDATA",
	"FIELD_GLOBALSYMBOL",
	"FIELD_NETWORK_QUANTIZED_VECTORWS",
	"FIELD_NETWORK_ORIGIN_CELL_QUANTIZED_VECTORWS",
};

static_assert(std::size(g_FieldTypeNames) == (size_t)SpawnKeyType_t::FIELD_TYPECOUNT, "Field type names do not match SpawnKeyType_t in the SDK");

// Procedural keyfields are handled in code and have no field
static constexpr int g_ProceduralKeyFieldOffset = INT_MAX;

static std::string GetFieldTypeName(SpawnKeyType_t type)
{
	return (size_t)type < std::size(g_FieldTypeNames) ? g_FieldTypeNames[(size_t)type] : fmt::format("FIELD_UNKNOWN_{}", (int)type);
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
		if (field.fieldType == SpawnKeyType_t::FIELD_EMBEDDED && field.td)
		{
			if (auto invalid = ValidateDataMap(field.td))
				return invalid;
		}
	}

	return nullptr;
}

// Keys that can be set on the entity, with embedded datamaps (like CCollisionProperty) indented under their field
static void AddKeyFields(const datamap_t* map, const std::string& indent, std::vector<std::string>& lines, std::vector<std::string>& strings)
{
	for (int i = 0; i < map->dataNumFields; i++)
	{
		const auto& field = map->dataDesc[i];
		if (!field.fieldName || !field.fieldName[0])
			continue;

		auto type = GetFieldTypeName(field.fieldType);

		if (field.fieldType == SpawnKeyType_t::FIELD_EMBEDDED)
		{
			if (!field.td)
				continue;

			std::vector<std::string> embedded;
			AddKeyFields(field.td, indent + "\t", embedded, strings);

			if (!embedded.empty())
			{
				lines.push_back(fmt::format("{}{}: {} ({})", indent, field.fieldName, type, field.td->dataClassName));
				lines.insert(lines.end(), embedded.begin(), embedded.end());
			}

			continue;
		}

		// The union holds an enum name for enum fields
		auto enumName = Modules::IsValidName(field.enumName) ? fmt::format(" ({})", field.enumName) : "";

		if (field.fieldOffset == g_ProceduralKeyFieldOffset)
		{
			lines.push_back(fmt::format("{}{}: {}{}", indent, field.fieldName, type, enumName));
			strings.push_back(field.fieldName);
		}
		else if (field.externalName && field.externalName[0])
		{
			lines.push_back(fmt::format("{}{}: {} {}{}", indent, field.externalName, type, field.fieldName, enumName));
			strings.push_back(field.externalName);
		}
	}
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

	// Classes without a design name can't be created by name, and are skipped
	auto designName = classInfo->m_pszClassname;
	if (designName && (!Modules::FindModuleContaining(designName) || (designName[0] && !Modules::IsValidName(designName))))
		return "design name";

	for (auto base = classInfo->m_pBaseClassInfo; base; base = base->m_pBaseClassInfo)
	{
		if (!Modules::FindModuleContaining(base) || (base->m_pszClassname && !Modules::FindModuleContaining(base->m_pszClassname)))
			return "base class info";
	}

	// A static array that ends with an empty entry
	if (auto overrides = entityClass->m_pComponentOverrides)
	{
		if (!Modules::IsInModule(module, overrides))
			return "component overrides";

		for (int i = 0; overrides[i].pszBaseComponent; i++)
		{
			if (i >= g_MaxComponentOverrides || !Modules::IsValidName(overrides[i].pszBaseComponent) || !Modules::IsValidName(overrides[i].pszOverrideComponent))
				return "component overrides";
		}
	}

	if (classInfo->m_pDataDescMap)
		return ValidateDataMap(classInfo->m_pDataDescMap);

	return nullptr;
}

// Nearest base class that has a design name, like module metadata does
static std::string GetBaseDesignName(const CEntityClassInfo* classInfo)
{
	for (auto base = classInfo->m_pBaseClassInfo; base; base = base->m_pBaseClassInfo)
	{
		if (base->m_pszClassname && base->m_pszClassname[0])
			return base->m_pszClassname;
	}

	return {};
}

// Returns false if the entity class list could not be found or read in a module that has one
bool Dump()
{
	// design name -> module -> class
	std::map<std::string, std::map<std::string, EntityClass_t>> entities;
	bool failed = false;
	int modules = 0;

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
		modules++;

		for (auto entityClass = head; entityClass; entityClass = entityClass->m_pNext)
		{
			if (auto invalid = ValidateEntityClass(module, entityClass))
			{
				spdlog::critical("Entity class list in {} has an invalid {}, CEntityClass in the SDK needs updating", module.m_pszModule, invalid);
				failed = true;
				break;
			}

			auto classInfo = entityClass->m_pClassInfo;
			if (!classInfo->m_pszClassname || !classInfo->m_pszClassname[0])
				continue;

			EntityClass_t entity{ classInfo->m_pszCPPClassname, GetBaseDesignName(classInfo), GetClassDetails(entityClass) };

			for (auto overrides = entityClass->m_pComponentOverrides; overrides && overrides->pszBaseComponent; overrides++)
			{
				entity.m_ComponentOverrides.push_back(fmt::format("\t\tcomponent {}: {}", overrides->pszBaseComponent, overrides->pszOverrideComponent));
				entity.m_Strings.push_back(overrides->pszOverrideComponent);
			}

			// Classes without their own datadesc point to their base class datamap
			auto map = classInfo->m_pDataDescMap;
			if (map && entity.m_CPPClassName == map->dataClassName)
				AddKeyFields(map, "\t\t", entity.m_KeyFields, entity.m_Strings);

			entities[classInfo->m_pszClassname][module.m_pszModule] = std::move(entity);
		}
	}

	if (failed)
	{
		spdlog::critical("Not writing entities.txt because the entity class list could not be read, see above");
		return false;
	}

	if (entities.empty())
	{
		spdlog::info("No entity class lists found, not writing entities.txt");
		return true;
	}

	std::ofstream output(Globals::outputPath / "entities.txt");

	for (const auto& [designName, classes] : entities)
	{
		output << designName << "\n";
		Globals::stringsIgnoreStream << designName << "\n";

		for (const auto& [module, entityClass] : classes)
		{
			output << "\t" << module << ": " << entityClass.m_CPPClassName;

			if (!entityClass.m_BaseDesignName.empty())
				output << ", base " << entityClass.m_BaseDesignName;

			output << entityClass.m_Details << "\n";
			Globals::stringsIgnoreStream << entityClass.m_CPPClassName << "\n";

			for (const auto& line : entityClass.m_ComponentOverrides)
				output << line << "\n";

			for (const auto& line : entityClass.m_KeyFields)
				output << line << "\n";

			for (const auto& str : entityClass.m_Strings)
				Globals::stringsIgnoreStream << str << "\n";
		}

		output << "\n";
	}

	spdlog::info("Wrote {} entities from {} modules to entities.txt", entities.size(), modules);
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
