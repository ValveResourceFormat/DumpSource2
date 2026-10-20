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

#include "schemas.h"
#include "interfaces.h"
#include "modules.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include "metadata_stringifier.h"
#include <spdlog/spdlog.h>
#include "schemasystem/schemasystem.h"
#include "filesystem_exporter.h"
#include "json_exporter.h"

namespace Dumpers::Schemas
{

// Schemas are read through the SDK structs, which can be outdated while everything still loads.
// Class and enum infos are static data in the module that declares them, so check the pointers before reading.
// These return what is invalid, or null.
static const char* ValidateMetadata(const SchemaMetadataEntryData_t* metadata, int count)
{
	if (count > 0 && !Modules::FindModuleContaining(metadata))
		return "metadata pointer";

	for (int i = 0; i < count; i++)
	{
		if (!Modules::IsValidName(metadata[i].m_pszName))
			return "metadata name";
	}

	return nullptr;
}

static const char* ValidateClass(const SchemaClassInfoData_t* classInfo)
{
	if (!Modules::FindModuleContaining(classInfo))
		return "class info pointer";
	if (!Modules::IsValidName(classInfo->m_pszName) || !Modules::IsValidName(classInfo->m_pszProjectName))
		return "class name";
	if (auto invalid = ValidateMetadata(classInfo->m_pStaticMetadata, classInfo->m_nStaticMetadataCount))
		return invalid;
	if (classInfo->m_nBaseClassCount > 0 && !Modules::FindModuleContaining(classInfo->m_pBaseClasses))
		return "base classes pointer";
	if (classInfo->m_nFieldCount > 0 && !Modules::FindModuleContaining(classInfo->m_pFields))
		return "fields pointer";

	for (uint16_t i = 0; i < classInfo->m_nBaseClassCount; i++)
	{
		const auto baseClass = classInfo->m_pBaseClasses[i].m_pClass;

		if (baseClass && (!Modules::FindModuleContaining(baseClass) || !Modules::IsValidName(baseClass->m_pszName) || !Modules::IsValidName(baseClass->m_pszProjectName)))
			return "base class";
	}

	for (uint16_t i = 0; i < classInfo->m_nFieldCount; i++)
	{
		const auto& field = classInfo->m_pFields[i];

		if (!Modules::IsValidName(field.m_pszName))
			return "field name";
		if (!field.m_pType)
			return "field type";
		if (field.m_nSingleInheritanceOffset < 0 || field.m_nSingleInheritanceOffset > classInfo->m_nSize)
			return "field offset";
		if (auto invalid = ValidateMetadata(field.m_pStaticMetadata, field.m_nStaticMetadataCount))
			return invalid;
	}

	return nullptr;
}

const char* ValidateEnum(const SchemaEnumInfoData_t* enumInfo)
{
	if (!Modules::FindModuleContaining(enumInfo))
		return "enum info pointer";
	if (!Modules::IsValidName(enumInfo->m_pszName) || !Modules::IsValidName(enumInfo->m_pszProjectName))
		return "enum name";
	if (auto invalid = ValidateMetadata(enumInfo->m_pStaticMetadata, enumInfo->m_nStaticMetadataCount))
		return invalid;
	if (enumInfo->m_nEnumeratorCount > 0 && !Modules::FindModuleContaining(enumInfo->m_pEnumerators))
		return "enumerators pointer";

	for (uint16_t i = 0; i < enumInfo->m_nEnumeratorCount; i++)
	{
		const auto& enumerator = enumInfo->m_pEnumerators[i];

		if (!Modules::IsValidName(enumerator.m_pszName))
			return "enumerator name";
		if (auto invalid = ValidateMetadata(enumerator.m_pStaticMetadata, enumerator.m_nStaticMetadataCount))
			return invalid;
	}

	return nullptr;
}

// Metadata that schemasystem stores as class flags instead of in the class metadata. MConstructibleClassBase and
// MGlobalTypeScope are named in schemasystem's registration errors. Classes in the global type scope only have the latter
// when they ask for it, the others get there by default. Bit 16 is also used, which schemasystem treats like an abstract class
// when checking that classes are constructible like their bases, but it has no name for it.
// TODO: Write bit 16 when its name is known, the SDK's MNonConstructibleClassBase is a guess
static constexpr std::pair<uint32, const char*> g_ClassInfoTagFlags[] = {
	{ SCHEMA_CF1_GLOBAL_TYPE_SCOPE, "MGlobalTypeScope" },
	{ SCHEMA_CF1_INFO_TAG_MNetworkAssumeNotNetworkable, "MNetworkAssumeNotNetworkable" },
	{ SCHEMA_CF1_INFO_TAG_MNetworkNoBase, "MNetworkNoBase" },
	{ SCHEMA_CF1_INFO_TAG_MIgnoreTypeScopeMetaChecks, "MIgnoreTypeScopeMetaChecks" },
	{ SCHEMA_CF1_INFO_TAG_MDisableDataDescValidation, "MDisableDataDescValidation" },
	{ SCHEMA_CF1_INFO_TAG_MClassHasEntityLimitedDataDesc, "MClassHasEntityLimitedDataDesc" },
	{ SCHEMA_CF1_INFO_TAG_MClassHasCustomAlignedNewDelete, "MClassHasCustomAlignedNewDelete" },
	{ SCHEMA_CF1_INFO_TAG_MConstructibleClassBase, "MConstructibleClassBase" },
	{ SCHEMA_CF1_INFO_TAG_MHasKV3TransferPolymorphicClassname, "MHasKV3TransferPolymorphicClassname" },
};

// Class flags for schemas.json
static constexpr std::pair<uint32, const char*> g_ClassFlags[] = {
	// { SCHEMA_CF1_HAS_VIRTUAL_MEMBERS, "virtual" }, // Left out to not bloat schemas.json, most classes have it
	{ SCHEMA_CF1_IS_ABSTRACT, "abstract" },
	{ SCHEMA_CF1_HAS_TRIVIAL_CONSTRUCTOR, "trivial_constructor" },
	{ SCHEMA_CF1_HAS_TRIVIAL_DESTRUCTOR, "trivial_destructor" },
	{ SCHEMA_CF1_CONSTRUCT_ALLOWED, "construct_allowed" },
	{ SCHEMA_CF1_CONSTRUCT_DISALLOWED, "construct_disallowed" },
};

// Schemasystem treats a class as constructible when only the allowed bit of these is set
static constexpr uint32 g_ClassConstructMask = SCHEMA_CF1_IS_ABSTRACT | SCHEMA_CF1_CONSTRUCT_ALLOWED | SCHEMA_CF1_CONSTRUCT_DISALLOWED;

// Same as the class tag, enums in the global type scope only have it when they ask for it
static constexpr std::pair<uint16, const char*> g_EnumInfoTagFlags[] = {
	{ SCHEMA_EF_GLOBAL_TYPE_SCOPE, "MGlobalTypeScope" },
};

// Enum flags for schemas.json. Bits 4 and 5 have no known meaning: bit 4 is on most enums of bit flags, bit 5 on about a third
// of the enums.
// TODO: Use the SDK's name when it has one for bit 3, and write bits 4 and 5 when their names are known
static constexpr std::pair<uint16, const char*> g_EnumFlags[] = {
	// Unsigned storage type: on every enum with values above the signed limit, and on others declared with an unsigned type
	{ SCHEMA_EF_UNK003, "unsigned" },
};

static bool DumpClasses(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaClass>& classes)
{
	FOR_EACH_MAP(typeScope->m_DeclaredClasses.m_Map, iter)
	{
		const auto classInfo = typeScope->m_DeclaredClasses.m_Map.Element(iter)->m_pClassInfo;

		// Declared by name only
		if (!classInfo)
			continue;

		if (auto invalid = ValidateClass(classInfo))
		{
			spdlog::critical("Schema class in {} has an invalid {}, SchemaClassInfoData_t in the SDK needs updating", typeScope->m_szScopeName, invalid);
			return false;
		}

		IntermediateSchemaClass schemaClass{
			.name = std::string(classInfo->m_pszName),
			.module = std::string(classInfo->m_pszProjectName),
			.size = classInfo->m_nSize,
			.alignment = classInfo->m_nAlignment,
		};

		// Constructible classes have an alignment, so the flag is only written for the others
		const bool hasAlignment = classInfo->m_nAlignment != 255;
		if (hasAlignment && (classInfo->m_nFlags1 & g_ClassConstructMask) != SCHEMA_CF1_CONSTRUCT_ALLOWED)
		{
			spdlog::critical("Schema class '{}' has an alignment but is not constructible, the construct_allowed handling in schemas.cpp needs updating", classInfo->m_pszName);
			return false;
		}

		uint32 flags = classInfo->m_nFlags1;
		if (hasAlignment)
			flags &= ~SCHEMA_CF1_CONSTRUCT_ALLOWED;

		for (const auto& [flag, name] : g_ClassFlags)
		{
			if (flags & flag)
				schemaClass.flags.push_back(name);
		}

		spdlog::trace("Dumping class: '{}'", classInfo->m_pszName);
#ifdef GAME_DOTA
		if (!classInfo->m_pszCPPName)
			spdlog::warn("Class '{}' has no cppname", classInfo->m_pszName);
		else if (strcmp(classInfo->m_pszName, classInfo->m_pszCPPName) != 0)
			spdlog::warn("Class '{}' has different cppname '{}'", classInfo->m_pszName, classInfo->m_pszCPPName);
#endif

		for (uint16_t k = 0; k < classInfo->m_nStaticMetadataCount; k++)
		{
			const auto& metadataEntry = classInfo->m_pStaticMetadata[k];
			schemaClass.metadata.push_back(GetMetadata(metadataEntry, classInfo->m_pszName, classInfo));
		}

		for (const auto& [flag, name] : g_ClassInfoTagFlags)
		{
			if (classInfo->m_nFlags1 & flag)
				schemaClass.metadata.push_back({ .name = name, .hasValue = false });
		}

		for (uint16_t baseIndex = 0; baseIndex < classInfo->m_nBaseClassCount; ++baseIndex)
		{
			const auto& base = classInfo->m_pBaseClasses[baseIndex];
			if (!base.m_pClass)
				continue;

			schemaClass.parents.push_back({ base.m_pClass->m_pszName, base.m_pClass->m_pszProjectName, base.m_nOffset });
		}

		for (uint16_t k = 0; k < classInfo->m_nFieldCount; k++)
		{
			const auto& field = classInfo->m_pFields[k];
			spdlog::trace("Dumping field: '{}' for class: '{}'", field.m_pszName, classInfo->m_pszName);

			IntermediateSchemaClassField intermediateField{
				.name = std::string(field.m_pszName),
				.offset = field.m_nSingleInheritanceOffset,
				.type = field.m_pType,
			};

			for (uint16_t l = 0; l < field.m_nStaticMetadataCount; l++)
			{
				const auto& metadataEntry = field.m_pStaticMetadata[l];
				intermediateField.metadata.push_back(GetMetadata(metadataEntry, classInfo->m_pszName, classInfo));
			}

			schemaClass.fields.push_back(std::move(intermediateField));
		}

		classes.push_back(std::move(schemaClass));
	}

	return true;
}

static bool DumpEnums(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaEnum>& enums)
{
	FOR_EACH_MAP(typeScope->m_DeclaredEnums.m_Map, iter)
	{
		const auto enumInfo = typeScope->m_DeclaredEnums.m_Map.Element(iter)->m_pEnumInfo;

		// Declared by name only
		if (!enumInfo)
			continue;

		if (auto invalid = ValidateEnum(enumInfo))
		{
			spdlog::critical("Schema enum in {} has an invalid {}, SchemaEnumInfoData_t in the SDK needs updating", typeScope->m_szScopeName, invalid);
			return false;
		}

		std::optional<std::string> alignment;

		switch (enumInfo->m_nAlignment)
		{
			case 1:
				alignment = "uint8_t";
				break;
			case 2:
				alignment = "uint16_t";
				break;
			case 4:
				alignment = "uint32_t";
				break;
			case 8:
				alignment = "uint64_t";
				break;
		}

		spdlog::trace("Dumping enum: '{}'", enumInfo->m_pszName);

		IntermediateSchemaEnum schemaEnum{
			.name = std::string(enumInfo->m_pszName),
			.module = std::string(enumInfo->m_pszProjectName),
			.stringAlignment = std::move(alignment),
		};

		for (const auto& [flag, name] : g_EnumFlags)
		{
			if (enumInfo->m_nFlags & flag)
				schemaEnum.flags.push_back(name);
		}

		for (uint16_t k = 0; k < enumInfo->m_nStaticMetadataCount; k++)
		{
			const auto& metadataEntry = enumInfo->m_pStaticMetadata[k];
			schemaEnum.metadata.push_back(GetMetadata(metadataEntry, enumInfo->m_pszName));
		}

		for (const auto& [flag, name] : g_EnumInfoTagFlags)
		{
			if (enumInfo->m_nFlags & flag)
				schemaEnum.metadata.push_back({ .name = name, .hasValue = false });
		}

		for (uint16_t k = 0; k < enumInfo->m_nEnumeratorCount; k++)
		{
			const auto& field = enumInfo->m_pEnumerators[k];
			spdlog::trace("Dumping enumerator: '{}' for enum: '{}'", field.m_pszName, enumInfo->m_pszName);
			IntermediateSchemaEnumMember member{
				.name = std::string(field.m_pszName),
				.value = field.m_nValue,
			};

			for (uint16_t l = 0; l < field.m_nStaticMetadataCount; l++)
			{
				const auto& metadataEntry = field.m_pStaticMetadata[l];
				member.metadata.push_back(GetMetadata(metadataEntry, enumInfo->m_pszName));
			}

			schemaEnum.members.push_back(std::move(member));
		}

		enums.push_back(std::move(schemaEnum));
	}

	return true;
}

static bool DumpTypeScope(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaEnum>& enums, std::vector<IntermediateSchemaClass>& classes)
{
	return DumpClasses(typeScope, classes) && DumpEnums(typeScope, enums);
}

// KV3 defaults in schemas.json leave out what a class inherits, which readers merge back in from the parents:
// keys with the same value in the full defaults of the first parent, and _class when it's the class itself.
// Some classes repeat thousands of inherited values, like the VData classes of Deadlock.
static void RemoveInheritedDefaults(std::vector<IntermediateSchemaClass>& classes)
{
	auto findDefaults = [](IntermediateSchemaClass& schemaClass) -> nlohmann::json* {
		for (auto& metadata : schemaClass.metadata)
		{
			if (metadata.name == "MGetKV3ClassDefaults" && metadata.jsonValue && metadata.jsonValue->is_object())
				return &*metadata.jsonValue;
		}

		return nullptr;
	};

	std::unordered_map<std::string, nlohmann::json> fullDefaults;
	for (auto& schemaClass : classes)
	{
		if (auto defaults = findDefaults(schemaClass))
			fullDefaults.emplace(schemaClass.module + "/" + schemaClass.name, *defaults);
	}

	for (auto& schemaClass : classes)
	{
		auto defaults = findDefaults(schemaClass);
		if (!defaults)
			continue;

		if (auto it = defaults->find("_class"); it != defaults->end() && *it == schemaClass.name)
			defaults->erase(it);

		if (schemaClass.parents.empty())
			continue;

		auto parent = fullDefaults.find(schemaClass.parents[0].module + "/" + schemaClass.parents[0].name);
		if (parent == fullDefaults.end())
			continue;

		for (auto it = defaults->begin(); it != defaults->end();)
		{
			auto inherited = parent->second.find(it.key());
			if (inherited != parent->second.end() && *inherited == *it)
				it = defaults->erase(it);
			else
				++it;
		}
	}
}

// Walking a map read through an outdated struct crashes, so check that its tree is consistent first
template <typename T>
static bool IsValidSchemaMap(T& map)
{
	auto tree = map.m_Map.AccessTree();
	if (tree->Count() == 0)
		return tree->Root() == tree->InvalidIndex();

	return tree->Count() <= tree->MaxElement() && tree->Root() < tree->MaxElement();
}

static std::vector<CSchemaSystemTypeScope*> ReadTypeScopes()
{
	auto schemaSystem = Interfaces::schemaSystem;
	const auto& typeScopes = schemaSystem->m_TypeScopes;

	// Type scopes are named after the module they belong to, so there can't be more than the loaded modules
	std::set<std::string> moduleScopeNames;
	for (const auto& module : Modules::allModules)
		moduleScopeNames.insert(fmt::format("{}{}{}", MODULE_PREFIX, std::filesystem::path(module.m_pszModule).filename().string(), MODULE_EXT));
	for (const auto& module : { Modules::tier0.get(), Modules::schemaSystem.get() })
		moduleScopeNames.insert(fmt::format("{}{}{}", MODULE_PREFIX, module->m_pszModule, MODULE_EXT));

	const auto numScopes = typeScopes.GetNumStrings();
	if (*(const uint8*)&schemaSystem->m_bDefaultTypeScopeIsLocal > 1 || *(const uint8*)&schemaSystem->m_bSchemaSystemIsReady > 1 || numScopes <= 0 || static_cast<size_t>(numScopes) > moduleScopeNames.size())
	{
		spdlog::critical("Schema system has {} type scopes, CSchemaSystem in the SDK needs updating", numScopes);
		return {};
	}

	std::vector<CSchemaSystemTypeScope*> scopes;
	for (auto i = 0; i < numScopes; ++i)
	{
		auto scope = typeScopes[i];
		if (!scope || !memchr(scope->m_szScopeName, '\0', sizeof(scope->m_szScopeName)))
		{
			spdlog::critical("Schema type scope {} is invalid, CSchemaSystem in the SDK needs updating", i);
			return {};
		}

		if (!moduleScopeNames.contains(scope->m_szScopeName))
		{
			spdlog::critical("Schema type scope {} is not a loaded module, CSchemaSystem in the SDK needs updating", scope->m_szScopeName);
			return {};
		}

		scopes.push_back(scope);
	}

	scopes.push_back(schemaSystem->GlobalTypeScope());

	for (auto scope : scopes)
	{
		if (!IsValidSchemaMap(scope->m_DeclaredClasses) || !IsValidSchemaMap(scope->m_DeclaredEnums))
		{
			spdlog::critical("Schema type scope {} has invalid declared classes or enums, CSchemaSystemTypeScope in the SDK needs updating", scope->m_szScopeName);
			return {};
		}
	}

	return scopes;
}

std::vector<CSchemaSystemTypeScope*> GetTypeScopes()
{
	// Schema bindings are installed before any dumper runs, so the scopes are read and checked once
	static const auto scopes = ReadTypeScopes();
	return scopes;
}

// Returns false if the schemas could not be read, which are then not written to keep the previous dump
bool Dump()
{
	std::vector<IntermediateSchemaEnum> enums;
	std::vector<IntermediateSchemaClass> classes;

	auto typeScopes = GetTypeScopes();
	if (typeScopes.empty())
		return false;

	for (auto typeScope : typeScopes)
	{
		if (!DumpTypeScope(typeScope, enums, classes))
			return false;
	}

	if (g_bInvalidKV3Defaults)
		return false;

	// Schema system order depends on registration order, sort so the output is stable between runs
	auto byModuleAndName = [](const auto& a, const auto& b) { return std::tie(a.module, a.name) < std::tie(b.module, b.name); };
	std::stable_sort(enums.begin(), enums.end(), byModuleAndName);
	std::stable_sort(classes.begin(), classes.end(), byModuleAndName);

	// Static libraries like pulse_runtime_lib declare the same schemas in every module that links them
	auto sameModuleAndName = [](const auto& a, const auto& b) { return std::tie(a.module, a.name) == std::tie(b.module, b.name); };
	enums.erase(std::unique(enums.begin(), enums.end(), sameModuleAndName), enums.end());
	classes.erase(std::unique(classes.begin(), classes.end(), sameModuleAndName), classes.end());

	if (!FilesystemExporter::Dump(enums, classes))
		return false;

	RemoveInheritedDefaults(classes);
	JsonExporter::Dump(enums, classes);
	return true;
}

} // namespace Dumpers::Schemas