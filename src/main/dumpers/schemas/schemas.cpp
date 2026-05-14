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
#include <optional>
#include "metadata_stringifier.h"
#include <spdlog/spdlog.h>
#define private public
#include "schemasystem/schemasystem.h"
#undef private
#include "filesystem_exporter.h"
#include "json_exporter.h"

namespace Dumpers::Schemas
{

void DumpClasses(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaClass>& classes)
{
	FOR_EACH_MAP(typeScope->m_DeclaredClasses.m_Map, iter)
	{
		const auto classInfo = typeScope->m_DeclaredClasses.m_Map.Element(iter)->m_pClassInfo;

		if (!classInfo)
			continue;

		IntermediateSchemaClass schemaClass{
			.name = std::string(classInfo->m_pszName),
			.module = std::string(classInfo->m_pszProjectName),
			.size = classInfo->m_nSize,
			.alignment = classInfo->m_nAlignment,
			.isAbstract = (classInfo->m_nFlags1 & SCHEMA_CF1_IS_ABSTRACT) != 0,
		};

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
			schemaClass.metadata.emplace_back(std::string(metadataEntry.m_pszName), GetMetadataValue(metadataEntry, classInfo->m_pszName), metadataEntry.m_pData);
		}

		if (classInfo->m_nBaseClassCount > 0)
		{
			for (uint16_t baseIndex = 0; baseIndex < classInfo->m_nBaseClassCount; ++baseIndex)
			{
				const auto* baseClass = classInfo->m_pBaseClasses[baseIndex].m_pClass;
				if (!baseClass)
					continue;

				schemaClass.parents.emplace_back(std::string(baseClass->m_pszName), std::string(baseClass->m_pszProjectName), (int32_t)classInfo->m_pBaseClasses[baseIndex].m_nOffset);
			}
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
				intermediateField.metadata.emplace_back(std::string(metadataEntry.m_pszName), GetMetadataValue(metadataEntry, classInfo->m_pszName), metadataEntry.m_pData);
			}

			schemaClass.fields.push_back(std::move(intermediateField));
		}

		classes.push_back(std::move(schemaClass));
	}
}

void DumpEnums(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaEnum>& enums)
{
	FOR_EACH_MAP(typeScope->m_DeclaredEnums.m_Map, iter)
	{
		const auto enumInfo = typeScope->m_DeclaredEnums.m_Map.Element(iter)->m_pEnumInfo;

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
			schemaEnum.metadata.emplace_back(std::string(metadataEntry.m_pszName), GetMetadataValue(metadataEntry, enumInfo->m_pszName), metadataEntry.m_pData);
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
				member.metadata.emplace_back(std::string(metadataEntry.m_pszName), GetMetadataValue(metadataEntry, enumInfo->m_pszName), metadataEntry.m_pData);
			}

			schemaEnum.members.push_back(std::move(member));
		}

		enums.push_back(std::move(schemaEnum));
	}
}

void DumpTypeScope(CSchemaSystemTypeScope* typeScope, std::vector<IntermediateSchemaEnum>& enums, std::vector<IntermediateSchemaClass>& classes)
{
	DumpClasses(typeScope, classes);
	DumpEnums(typeScope, enums);
}

void Dump()
{
	spdlog::info("Dumping schemasystem");
	auto schemaSystem = Interfaces::schemaSystem;

	const auto& typeScopes = schemaSystem->m_TypeScopes;
	std::vector<IntermediateSchemaEnum> enums;
	std::vector<IntermediateSchemaClass> classes;

	for (auto i = 0; i < typeScopes.m_Vector.Count(); ++i)
		DumpTypeScope(typeScopes[i], enums, classes);

	DumpTypeScope(schemaSystem->GlobalTypeScope(), enums, classes);

	FilesystemExporter::Dump(enums, classes);
	JsonExporter::Dump(enums, classes);
}

} // namespace Dumpers::Schemas