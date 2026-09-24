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
#include <optional>
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

// Metadata that schemasystem stores as class flags instead of in the class metadata.
// Bits 16 and 17 are also used, but schemasystem has no names for them, the SDK's MConstructibleClassBase is a guess.
static constexpr std::pair<uint32, const char*> g_ClassInfoTagFlags[] = {
	{ SCHEMA_CF1_INFO_TAG_MNetworkAssumeNotNetworkable, "MNetworkAssumeNotNetworkable" },
	{ SCHEMA_CF1_INFO_TAG_MNetworkNoBase, "MNetworkNoBase" },
	{ SCHEMA_CF1_INFO_TAG_MIgnoreTypeScopeMetaChecks, "MIgnoreTypeScopeMetaChecks" },
	{ SCHEMA_CF1_INFO_TAG_MDisableDataDescValidation, "MDisableDataDescValidation" },
	{ SCHEMA_CF1_INFO_TAG_MClassHasEntityLimitedDataDesc, "MClassHasEntityLimitedDataDesc" },
	{ SCHEMA_CF1_INFO_TAG_MClassHasCustomAlignedNewDelete, "MClassHasCustomAlignedNewDelete" },
	{ SCHEMA_CF1_INFO_TAG_MHasKV3TransferPolymorphicClassname, "MHasKV3TransferPolymorphicClassname" },
};

// Class flags for schemas.json
static constexpr std::pair<uint32, const char*> g_ClassFlags[] = {
	// { SCHEMA_CF1_HAS_VIRTUAL_MEMBERS, "virtual" }, // Left out to not bloat schemas.json, most classes have it
	{ SCHEMA_CF1_IS_ABSTRACT, "abstract" },
	{ SCHEMA_CF1_HAS_TRIVIAL_CONSTRUCTOR, "trivial_constructor" },
	{ SCHEMA_CF1_HAS_TRIVIAL_DESTRUCTOR, "trivial_destructor" },
	{ SCHEMA_CF1_CONSTRUCT_DISALLOWED, "construct_disallowed" },
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

		for (const auto& [flag, name] : g_ClassFlags)
		{
			if (classInfo->m_nFlags1 & flag)
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

		for (uint16_t k = 0; k < enumInfo->m_nStaticMetadataCount; k++)
		{
			const auto& metadataEntry = enumInfo->m_pStaticMetadata[k];
			schemaEnum.metadata.push_back(GetMetadata(metadataEntry, enumInfo->m_pszName));
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

std::vector<CSchemaSystemTypeScope*> GetTypeScopes()
{
	auto schemaSystem = Interfaces::schemaSystem;
	const auto& typeScopes = schemaSystem->m_TypeScopes;

	std::vector<CSchemaSystemTypeScope*> scopes;
	for (auto i = 0; i < typeScopes.GetNumStrings(); ++i)
		scopes.push_back(typeScopes[i]);

	scopes.push_back(schemaSystem->GlobalTypeScope());
	return scopes;
}

// Returns false if the schemas could not be read, which are then not written to keep the previous dump
bool Dump()
{
	std::vector<IntermediateSchemaEnum> enums;
	std::vector<IntermediateSchemaClass> classes;

	for (auto typeScope : GetTypeScopes())
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