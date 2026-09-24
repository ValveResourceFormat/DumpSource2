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
#include <optional>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Dumpers::Schemas::JsonExporter
{

static json SerializeMetadataArray(const std::vector<IntermediateMetadata>& metadataVector)
{
	json arr = json::array();
	for (const auto& metadata : metadataVector)
	{
		json j;
		j["name"] = metadata.name;
		// KV3 defaults of classes without any are null, which are left out like metadata without a value
		if (metadata.hasValue && metadata.stringValue.has_value() && !(metadata.jsonValue && metadata.jsonValue->is_null()))
		{
			j["value"] = metadata.jsonValue.value_or(*metadata.stringValue);
		}

		arr.push_back(std::move(j));
	}
	return arr;
}

static json SerializeType(CSchemaType* type)
{
	if (!type)
		return nullptr;

	json j;

#ifdef GAME_HLVR
	// Half-Life: Alyx's types return their categories from virtuals
	const auto typeCategory = type->GetTypeCategory();
	const auto atomicCategory = type->GetAtomicCategory();
#else
	const auto typeCategory = type->m_eTypeCategory;
	const auto atomicCategory = type->m_eAtomicCategory;
#endif

	switch (typeCategory)
	{
		case SCHEMA_TYPE_BUILTIN:
			j["category"] = "builtin";
			j["name"] = type->m_sTypeName.String();
			break;
		case SCHEMA_TYPE_POINTER:
			j["category"] = "ptr";
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
			auto pos = typeName.find('<');
			if (pos != std::string::npos)
			{
				auto name = typeName.substr(0, pos);
				// Trim trailing whitespace
				while (!name.empty() && name.back() == ' ')
					name.pop_back();
				j["name"] = name;
			}
			else
			{
				j["name"] = typeName;
			}

			if (atomicCategory == SCHEMA_ATOMIC_T ||
				atomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T)
			{
				j["inner"] = SerializeType(static_cast<CSchemaType_Atomic_T*>(type)->m_pTemplateType);
			}
			else if (atomicCategory == SCHEMA_ATOMIC_TT)
			{
				auto* tt = static_cast<CSchemaType_Atomic_TT*>(type);
				j["inner"] = SerializeType(tt->m_pTemplateType);
				j["inner2"] = SerializeType(tt->m_pTemplateType2);
			}
			else if (atomicCategory == SCHEMA_ATOMIC_I)
			{
				// Like the size of CBitVec<N>
				j["count"] = static_cast<CSchemaType_Atomic_I*>(type)->m_nInteger;
			}

#ifndef GAME_HLVR
			// Like the inline element count of CUtlVectorFixedGrowable<T, N>
			if (atomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T)
			{
				if (auto count = static_cast<CSchemaType_Atomic_CollectionOfT*>(type)->m_nFixedBufferCount)
					j["count"] = count;
			}
#endif
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
			spdlog::warn("Type '{}' has unknown category {}, written as builtin", type->m_sTypeName.String(), (int)typeCategory);
			j["category"] = "builtin";
			j["name"] = type->m_sTypeName.String();
			break;
	}

	return j;
}

static void DumpClasses(const std::vector<IntermediateSchemaClass>& classes, json& classesArray)
{
	for (const auto& intermediateClass : classes)
	{
		json classObj;
		classObj["name"] = intermediateClass.name;
		classObj["module"] = intermediateClass.module;
#ifndef GAME_HLVR // Half-Life: Alyx is dumped once, the layout would go out of date when the game updates
		classObj["size"] = intermediateClass.size;

		if (intermediateClass.alignment != 255)
			classObj["alignment"] = intermediateClass.alignment;
#endif

		if (!intermediateClass.flags.empty())
			classObj["flags"] = intermediateClass.flags;

		auto classMetadataArr = SerializeMetadataArray(intermediateClass.metadata);
		if (classMetadataArr.size())
			classObj["metadata"] = std::move(classMetadataArr);

		json parents = json::array();
		for (const auto& parent : intermediateClass.parents)
		{
			json parentObj;
			parentObj["name"] = parent.name;
			parentObj["module"] = parent.module;

#ifndef GAME_HLVR
			if (parent.offset)
				parentObj["offset"] = parent.offset;
#endif
			parents.push_back(std::move(parentObj));
		}

		if (parents.size())
			classObj["parents"] = std::move(parents);

		json fields = json::array();
		for (const auto& field : intermediateClass.fields)
		{
			json fieldObj;
			fieldObj["name"] = field.name;
#ifndef GAME_HLVR
			fieldObj["offset"] = field.offset;
#endif
			fieldObj["type"] = SerializeType(field.type);

			auto fieldMetadataArr = SerializeMetadataArray(field.metadata);
			if (fieldMetadataArr.size())
				fieldObj["metadata"] = std::move(fieldMetadataArr);

			fields.push_back(std::move(fieldObj));
		}

		// Static fields are listed after the fields, tagged with a metadata entry that is ours. They have no offset.
		for (const auto& field : intermediateClass.staticFields)
		{
			json fieldObj;
			fieldObj["name"] = field.name;
			fieldObj["type"] = SerializeType(field.type);

			auto fieldMetadataArr = SerializeMetadataArray(field.metadata);
			fieldMetadataArr.push_back({ { "name", "static" } });
			fieldObj["metadata"] = std::move(fieldMetadataArr);

			fields.push_back(std::move(fieldObj));
		}

		if (fields.size())
			classObj["fields"] = std::move(fields);

		classesArray.push_back(std::move(classObj));
	}
}

static void DumpEnums(const std::vector<IntermediateSchemaEnum>& enums, json& enumsArray)
{
	for (const auto& intermediateEnum : enums)
	{
		json enumObj;
		enumObj["name"] = intermediateEnum.name;
		enumObj["module"] = intermediateEnum.module;
		enumObj["alignment"] = intermediateEnum.stringAlignment;

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
	json classesArray = json::array();
	json enumsArray = json::array();

	DumpClasses(classes, classesArray);
	DumpEnums(enums, enumsArray);

	Globals::schemasJson["classes"] = std::move(classesArray);
	Globals::schemasJson["enums"] = std::move(enumsArray);
}

} // namespace Dumpers::Schemas::JsonExporter