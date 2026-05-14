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

#include "json_exporter.h"
#include "globalvariables.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Dumpers::Schemas::JsonExporter
{

json SerializeMetadataArray(const std::vector<IntermediateMetadata>& metadataVector)
{
	json arr = json::array();
	for (const auto& metadata : metadataVector)
	{
		json j;
		j["name"] = metadata.name;
		if (metadata.hasValue && metadata.stringValue.has_value())
		{
			j["value"] = *metadata.stringValue;
		}

		arr.push_back(j);
	}
	return arr;
}

json SerializeType(CSchemaType* type)
{
	if (!type)
		return nullptr;

	json j;

	switch (type->m_eTypeCategory)
	{
		case SCHEMA_TYPE_BUILTIN:
			j["category"] = "builtin";
			j["name"] = type->m_sTypeName.String();
			break;
		case SCHEMA_TYPE_POINTER:
			j["category"] = "ptr";
			j["nullable"] = true;
			j["inner"] = SerializeType(static_cast<CSchemaType_Ptr*>(type)->m_pObjectType);
			break;
		case SCHEMA_TYPE_FIXED_ARRAY:
		{
			auto* arr = static_cast<CSchemaType_FixedArray*>(type);
			j["category"] = "fixed_array";
			j["count"] = arr->m_nElementCount;
			j["inner"] = SerializeType(arr->m_pElementType);
			break;
		}
		case SCHEMA_TYPE_ATOMIC:
		{
			j["category"] = "atomic";

			// Extract outer name from m_sTypeName before '<'
			std::string typeName = type->m_sTypeName.String();
			std::string outerName;
			auto pos = typeName.find('<');
			if (pos != std::string::npos)
			{
				outerName = typeName.substr(0, pos);
				while (!outerName.empty() && outerName.back() == ' ')
					outerName.pop_back();
			}
			else
			{
				outerName = typeName;
			}
			j["name"] = outerName;

			if (outerName == "CHandle")
			{
				j["handle_kind"] = "entity";
				j["nullable"] = true;
			}
			else if (outerName == "CWeakHandle")
			{
				j["handle_kind"] = "weak";
				j["nullable"] = true;
			}
			else if (outerName == "CStrongHandle")
			{
				j["handle_kind"] = "strong";
				j["nullable"] = true;
			}

			if (type->m_eAtomicCategory == SCHEMA_ATOMIC_T ||
				type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T)
			{
				j["inner"] = SerializeType(static_cast<CSchemaType_Atomic_T*>(type)->m_pTemplateType);
			}
			else if (type->m_eAtomicCategory == SCHEMA_ATOMIC_TT)
			{
				auto* tt = static_cast<CSchemaType_Atomic_TT*>(type);
				j["inner"] = SerializeType(tt->m_pTemplateType);
				j["inner2"] = SerializeType(tt->m_pTemplateType2);
			}
			break;
		}
		case SCHEMA_TYPE_DECLARED_CLASS:
		{
			j["category"] = "declared_class";
			auto* classType = static_cast<CSchemaType_DeclaredClass*>(type);
			if (classType->m_pClassInfo && classType->m_pClassInfo->m_pszName)
			{
				j["name"] = classType->m_pClassInfo->m_pszName;
				j["module"] = classType->m_pClassInfo->m_pszProjectName;
			}
			else
				j["name"] = type->m_sTypeName.String();
			break;
		}
		case SCHEMA_TYPE_DECLARED_ENUM:
		{
			j["category"] = "declared_enum";
			auto* enumType = static_cast<CSchemaType_DeclaredEnum*>(type);
			if (enumType->m_pEnumInfo && enumType->m_pEnumInfo->m_pszName)
			{
				j["name"] = enumType->m_pEnumInfo->m_pszName;
				j["module"] = enumType->m_pEnumInfo->m_pszProjectName;
			}
			else
				j["name"] = type->m_sTypeName.String();
			break;
		}
		case SCHEMA_TYPE_BITFIELD:
			j["category"] = "bitfield";
			j["count"] = static_cast<CSchemaType_Bitfield*>(type)->m_nBitfieldCount;
			break;
		default:
			j["category"] = "builtin";
			j["name"] = type->m_sTypeName.String();
			break;
	}

	return j;
}

void DumpClasses(const std::vector<IntermediateSchemaClass>& classes, json& classesArray)
{
	for (const auto& intermediateClass : classes)
	{
		spdlog::trace("Dumping class for json: '{}'", intermediateClass.name);

		json classObj;
		classObj["name"] = intermediateClass.name;
		classObj["module"] = intermediateClass.module;
		classObj["size"] = intermediateClass.size;
		if (intermediateClass.alignment != 0)
			classObj["alignment"] = intermediateClass.alignment;
		if (intermediateClass.isAbstract)
			classObj["abstract"] = true;

		auto classMetadataArr = SerializeMetadataArray(intermediateClass.metadata);
		if (classMetadataArr.size())
			classObj["metadata"] = std::move(classMetadataArr);

		json parents = json::array();
		for (const auto& parent : intermediateClass.parents)
		{
			json parentObj;
			parentObj["name"] = parent.name;
			parentObj["module"] = parent.module;
			parentObj["offset"] = parent.offset;
			parents.push_back(std::move(parentObj));
		}

		if (parents.size())
			classObj["parents"] = std::move(parents);

		json fields = json::array();
		for (const auto& field : intermediateClass.fields)
		{
			spdlog::trace("Dumping field: '{}' for class: '{}'", field.name, intermediateClass.name);
			json fieldObj;
			fieldObj["name"] = field.name;
			fieldObj["offset"] = field.offset;
			fieldObj["type"] = SerializeType(field.type);

			auto fieldMetadataArr = SerializeMetadataArray(field.metadata);
			if (fieldMetadataArr.size())
				fieldObj["metadata"] = std::move(fieldMetadataArr);

			fields.push_back(fieldObj);
		}

		if (fields.size())
			classObj["fields"] = std::move(fields);

		classesArray.push_back(std::move(classObj));
	}
}

void DumpEnums(const std::vector<IntermediateSchemaEnum>& enums, json& enumsArray)
{
	for (const auto& intermediateEnum : enums)
	{
		json enumObj;
		enumObj["name"] = intermediateEnum.name;
		enumObj["module"] = intermediateEnum.module;
		enumObj["alignment"] = intermediateEnum.stringAlignment;
		if (intermediateEnum.stringAlignment.has_value())
		{
			const auto& align = *intermediateEnum.stringAlignment;
			if (align == "uint8_t")       enumObj["storage_size"] = 1;
			else if (align == "uint16_t") enumObj["storage_size"] = 2;
			else if (align == "uint32_t") enumObj["storage_size"] = 4;
			else if (align == "uint64_t") enumObj["storage_size"] = 8;
		}
		if (IsFlagsEnum(intermediateEnum.members))
			enumObj["flags"] = true;

		auto enumMetadataArr = SerializeMetadataArray(intermediateEnum.metadata);
		if (enumMetadataArr.size())
			enumObj["metadata"] = std::move(enumMetadataArr);

		json members = json::array();
		for (const auto& member : intermediateEnum.members)
		{
			json memberObj;
			memberObj["name"] = member.name;
			memberObj["value"] = member.value;

			auto memberMetadataArr = SerializeMetadataArray(member.metadata);
			if (memberMetadataArr.size())
				memberObj["metadata"] = std::move(memberMetadataArr);

			members.push_back(std::move(memberObj));
		}

		if (members.size())
			enumObj["members"] = std::move(members);

		enumsArray.push_back(std::move(enumObj));
	}
}

void Dump(const std::vector<IntermediateSchemaEnum>& enums, const std::vector<IntermediateSchemaClass>& classes)
{
	spdlog::info("Dumping schemas to json");

	nlohmann::ordered_json root;
	json classesArray = json::array();
	json enumsArray = json::array();

	DumpClasses(classes, classesArray);
	DumpEnums(enums, enumsArray);

	root["generator"] = "https://github.com/ValveResourceFormat/DumpSource2";

	if (!Globals::sourceRevision.empty())
		root["revision"] = std::stoi(Globals::sourceRevision);

	if (!Globals::versionDate.empty())
		root["version_date"] = Globals::versionDate;

	if (!Globals::versionTime.empty())
		root["version_time"] = Globals::versionTime;

	root["classes"] = classesArray;
	root["enums"] = enumsArray;

	std::ofstream output(Globals::outputPath / "schemas.json");
	output << root.dump(2) << "\n";
	output.close();

	spdlog::info("Wrote schemas.json ({} classes, {} enums)", classesArray.size(), enumsArray.size());
}

} // namespace Dumpers::Schemas::JsonExporter