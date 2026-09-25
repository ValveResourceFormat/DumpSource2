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

#include "network.h"

#ifdef GAME_DEADLOCK

namespace Dumpers::Network
{

// Deadlock still has its networking in the schema metadata, and an older network database than the SDK types
bool Dump()
{
	return true;
}

} // namespace Dumpers::Network

#else

// inetchannel.h needs generated protobuf headers, the network serializer types only use this enum from it
#define INETCHANNEL_H
enum NetChannelBufType_t : int;
#include <bitvec.h>
#include <const.h>
#include <networksystem/inetworkserializer.h>

#include "format_float.h"
#include "gamedata.h"
#include "globalvariables.h"
#include "modules.h"
#include "output.h"
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Dumpers::Network
{

using namespace GameData;
using json = nlohmann::json;

// A value in both network/<module>.txt and schemas.json
struct Property_t
{
	const char* key;   // In schemas.json
	const char* label; // In the txt dump
	json value;
	std::string text; // In the txt dump, empty for flags
};

struct Field_t
{
	std::string name;
	std::string type;
	std::vector<Property_t> properties;
};

struct Class_t
{
	std::vector<Property_t> properties;
	std::vector<Field_t> fields;
};

using Classes_t = std::map<std::string, Class_t>;

static std::string Join(const std::vector<std::string>& texts)
{
	return fmt::format("{}", fmt::join(texts, ", "));
}

// Arrays of anything else than strings pass their own text
static std::string GetText(const json& value)
{
	if (value.is_string())
		return value.get<std::string>();

	if (value.is_boolean())
		return {};

	if (value.is_array())
		return Join(value.get<std::vector<std::string>>());

	return value.dump();
}

// Adds a property unless it is empty or the default, which is null
static void Add(std::vector<Property_t>& properties, const char* key, const char* label, json value, std::string text = {})
{
	if (value.is_null() || value == "" || value == false || (value.is_structured() && value.empty()))
		return;

	if (text.empty())
		text = GetText(value);

	properties.push_back({ key, label, std::move(value), std::move(text) });
}

static json GetStrings(const CUtlVector<CUtlString>& strings)
{
	auto array = json::array();
	for (int i = 0; i < strings.Count(); i++)
		array.push_back(strings[i].Get());

	return array;
}

static json GetFloat(float value)
{
	return json::parse(FormatFloat(value));
}

static std::string GetOverrideKind(int kind)
{
	auto name = g_NetworkOverrideKinds.find(kind);
	return name != g_NetworkOverrideKinds.end() ? name->second : fmt::format("kind{}", kind);
}

static std::vector<Property_t> GetClassProperties(const CNetworkSerializerClassInfo* info)
{
	std::vector<Property_t> properties;
	Add(properties, "includeByName", "include by name", GetStrings(info->m_NetworkFilterByName.m_IncludeList));
	Add(properties, "excludeByName", "exclude by name", GetStrings(info->m_NetworkFilterByName.m_ExcludeList));
	Add(properties, "includeByUserGroup", "include by user group", GetStrings(info->m_NetworkFilterByUserGroup.m_IncludeList));
	Add(properties, "excludeByUserGroup", "exclude by user group", GetStrings(info->m_NetworkFilterByUserGroup.m_ExcludeList));

	auto overrides = json::array();
	std::vector<std::string> overrideTexts;
	for (int i = 0; i < info->m_NetworkOverrides.Count(); i++)
	{
		const auto* networkOverride = info->m_NetworkOverrides[i];
		const auto* fieldName = networkOverride->m_FieldName ? networkOverride->m_FieldName : "";
		json item{ { "field", fieldName }, { "kind", GetOverrideKind(networkOverride->m_unk001) } };
		auto text = fieldName + std::string(" ") + item["kind"].get<std::string>();

		// No class is the nearest base that has the field
		if (networkOverride->m_ParentClass)
		{
			item["class"] = networkOverride->m_ParentClass;
			text = networkOverride->m_ParentClass + std::string("::") + text;
		}

		if (networkOverride->m_FieldPriority)
		{
			item["value"] = networkOverride->m_FieldPriority;
			text += std::string(" ") + networkOverride->m_FieldPriority;
		}

		overrides.push_back(std::move(item));
		overrideTexts.push_back(std::move(text));
	}
	Add(properties, "overrides", "overrides", std::move(overrides), Join(overrideTexts));

	auto typeOverrides = json::object();
	std::vector<std::string> typeOverrideTexts;
	for (int i = 0; i < info->m_NetworkVarTypeOverrides.Count(); i++)
	{
		const auto* typeOverride = info->m_NetworkVarTypeOverrides[i];
		typeOverrides[typeOverride->m_FieldName.Get()] = typeOverride->m_OverrideType.Get();
		typeOverrideTexts.push_back(fmt::format("{} {}", typeOverride->m_FieldName.Get(), typeOverride->m_OverrideType.Get()));
	}
	Add(properties, "varTypeOverrides", "var type overrides", std::move(typeOverrides), Join(typeOverrideTexts));

	// The proxies are always for the class itself
	auto userGroupProxies = json::array();
	for (int i = 0; i < info->m_UserGroupProxies.Count(); i++)
		userGroupProxies.push_back(info->m_UserGroupProxies[i]->m_UserGroup);
	Add(properties, "userGroupProxies", "user group proxies", std::move(userGroupProxies));

	auto replayCompatFields = json::array();
	std::vector<std::string> replayCompatTexts;
	for (int i = 0; i < info->m_NetworkReplayCompatFields.Count(); i++)
	{
		const auto* field = info->m_NetworkReplayCompatFields[i];
		replayCompatFields.push_back({ { "field", field->m_FieldPath.Get() }, { "callback", field->m_FieldType.Get() } });
		replayCompatTexts.push_back(fmt::format("{} {}", field->m_FieldPath.Get(), field->m_FieldType.Get()));
	}
	Add(properties, "replayCompatFields", "replay compat fields", std::move(replayCompatFields), Join(replayCompatTexts));

	Add(properties, "varsAtomic", "vars atomic", info->m_NetworkVarsAtomic);
	Add(properties, "structNotInNetworkUtlVectorEmbedded", "struct not in network utl vector embedded", info->m_NetworkStructNotInNetworkUtlVectorEmbedded);
	Add(properties, "outOfPVSUpdates", "out of PVS updates", info->m_NetworkOutOfPVSUpdates != 2 ? json(info->m_NetworkOutOfPVSUpdates) : json());
	return properties;
}

static std::vector<Property_t> GetFieldProperties(const CNetworkSerializerFieldInfo* field, const std::string& type)
{
	std::vector<Property_t> properties;
	Add(properties, "sentAs", "sent as", field->m_TypeOverride.Get());

	// The class of embedded, pointer and component fields, when the type doesn't already say it
	std::string className = field->m_pszCodeGenType.Get();
	std::string plainType = type;
	std::erase_if(plainType, [](char c) { return c == '*' || c == ' '; });
	Add(properties, "class", "class", className != plainType ? className : "");

	Add(properties, "alias", "alias", field->m_NetworkAlias.Get());
	Add(properties, "typeAlias", "type alias", field->m_NetworkTypeAlias.Get());
	Add(properties, "serializer", "serializer", field->m_NetworkSerializer.Get());
	Add(properties, "encoder", "encoder", field->m_NetworkEncoder.Get());

	const auto* filter = reinterpret_cast<const SendProxyRecipientsFilter_t*>(field->m_NetworkSendProxyRecipientsFilter.get());
	Add(properties, "recipientsFilter", "recipients filter", filter ? filter->m_Name.Get() : "");

	const auto& changePointerCallback = field->m_NetworkChangePointerCallback;
	Add(properties, "changePointerCallback", "change pointer callback", changePointerCallback ? changePointerCallback->m_CallbackName.Get() : "");

	Add(properties, "priority", "priority", field->m_NetworkPriority != 64 ? json(field->m_NetworkPriority) : json());
	Add(properties, "userGroups", "user groups", GetStrings(field->m_NetworkIncludeByUserGroup));
	Add(properties, "changeCallbacks", "change callbacks", GetStrings(field->m_NetworkChangeCb));
	Add(properties, "bitCount", "bit count", field->m_NetworkBitCount != 32 ? json(field->m_NetworkBitCount) : json());
	Add(properties, "encodeFlags", "encode flags", field->m_NetworkEncodeFlags ? json(field->m_NetworkEncodeFlags) : json());
	Add(properties, "min", "min", field->m_NetworkMin != -FLT_MAX ? GetFloat(field->m_NetworkMin) : json());
	Add(properties, "max", "max", field->m_NetworkMax != FLT_MAX ? GetFloat(field->m_NetworkMax) : json());
	Add(properties, "embeddedFieldOffsetDelta", "embedded field offset delta", field->m_NetworkVarEmbeddedFieldOffsetDelta ? json(field->m_NetworkVarEmbeddedFieldOffsetDelta) : json());

	Add(properties, "polymorphic", "polymorphic", *((const bool*)field + g_NetworkPolymorphicOffset));

	Add(properties, "resourceType", "resource type", std::string(field->m_ResourceTypeForInfoType, strnlen(field->m_ResourceTypeForInfoType, sizeof(field->m_ResourceTypeForInfoType))));
	return properties;
}

// Runs the queued registrations of a module like it does when it connects.
// Returns the module's database, or null if the signature or code changed.
static CNetworkSerializerCodeGenDatabase* RunRegistrations(CModule& module)
{
	int error;
	auto match = (uint8_t*)module.FindSignature(g_NetworkDatabaseSignature, sizeof(g_NetworkDatabaseSignature) - 1, error);
	if (!match)
	{
		spdlog::critical("Could not find the network database in {} ({}), update g_NetworkDatabaseSignature in gamedata.h", module.m_pszModule, error == SIG_FOUND_MULTIPLE ? "multiple matches" : "no match");
		return nullptr;
	}

	auto registerAll = (void (*)(NetworkSerializationMode_t))(match + 5 + *(int32_t*)(match + 1));
	auto getDatabase = (CNetworkSerializerCodeGenDatabase * (*)())(match + 10 + *(int32_t*)(match + 6));

	// Some registrations need INetworkMessages, which the module got when connecting (see g_FactoryInterfaces)
	registerAll(strcmp(module.m_pszModule, "client") ? NET_SERIALIZATION_MODE_SERVER : NET_SERIALIZATION_MODE_CLIENT);

	auto database = getDatabase();
	if (!database->m_ClassInfos.Count())
	{
		spdlog::critical("The network database in {} is empty after running its registrations", module.m_pszModule);
		return nullptr;
	}

	return database;
}

static bool ReadClasses(const char* module, const CNetworkSerializerCodeGenDatabase* database, Classes_t& classes)
{
	for (auto i = database->m_ClassInfos.First(); i != database->m_ClassInfos.InvalidIndex(); i = database->m_ClassInfos.Next(i))
	{
		// The SDK types can go out of date while the code that fills them stays the same, so check what is read
		const auto* info = database->m_ClassInfos.Element(i);
		const auto* name = database->m_ClassInfos.GetElementName(i);
		if (!info || info->m_pDatabase != database || strcmp(info->m_pszClassName.Get(), name) || info->m_nClassSize <= 0)
		{
			spdlog::critical("Network class {} in {} does not match, CNetworkSerializerClassInfo in the SDK needs updating", name, module);
			return false;
		}

		// The networked type, like the element type of network vectors (MNetworkVarNames before it was removed)
		std::map<std::string, std::string> varTypes;
		for (int m = 0; m < info->m_FieldTypeMappings.Count(); m++)
			varTypes[info->m_FieldTypeMappings[m]->m_FieldName.Get()] = info->m_FieldTypeMappings[m]->m_FieldType.Get();

		auto& networkClass = classes[name];
		networkClass.properties = GetClassProperties(info);

		for (int f = 0; f < info->m_Fields.Count(); f++)
		{
			const auto* field = info->m_Fields[f];
			if (!field || !field->m_pszFieldName.Get()[0] || field->m_nFieldOffset < 0 || field->m_nFieldOffset >= info->m_nClassSize)
			{
				spdlog::critical("Network field {} of {} in {} does not match, CNetworkSerializerFieldInfo in the SDK needs updating", f, name, module);
				return false;
			}

			std::string fieldName = field->m_pszFieldName.Get();
			auto varType = varTypes.find(fieldName);
			auto type = varType != varTypes.end() ? varType->second : std::string(field->m_pszTypeName.Get());
			networkClass.fields.push_back({ fieldName, type, GetFieldProperties(field, type) });
		}
	}

	return true;
}

static std::string Describe(const Property_t& property)
{
	return property.text.empty() ? property.label : fmt::format("{}: {}", property.label, property.text);
}

// Names like callbacks and user groups
static void AddStringsToIgnore(const json& value)
{
	if (value.is_string())
	{
		Globals::stringsIgnoreStream << value.get_ref<const std::string&>() << "\n";
	}
	else if (value.is_structured())
	{
		for (const auto& item : value)
			AddStringsToIgnore(item);
	}
}

static bool WriteText(const char* module, const Classes_t& classes)
{
	const auto directory = Globals::outputPath / "network";
	std::filesystem::create_directories(directory);

	const auto path = directory / (std::string(module) + ".txt");
	std::ofstream output(path);

	for (const auto& [name, networkClass] : classes)
	{
		output << name << "\n";

		for (const auto& property : networkClass.properties)
		{
			output << "\t" << Describe(property) << "\n";
			AddStringsToIgnore(property.value);
		}

		for (const auto& field : networkClass.fields)
		{
			output << "\t" << field.type << " " << field.name;

			std::vector<std::string> descriptions;
			for (const auto& property : field.properties)
			{
				descriptions.push_back(Describe(property));
				AddStringsToIgnore(property.value);
			}

			if (!descriptions.empty())
				output << " (" << fmt::format("{}", fmt::join(descriptions, "; ")) << ")";

			output << "\n";
		}

		output << "\n";
	}

	if (!CloseOutput(output, path))
		return false;

	spdlog::info("Wrote {} network classes to network/{}.txt", classes.size(), module);
	return true;
}

// Sets a "network" object, which a few base classes get from both modules and must be the same from both
static bool SetNetwork(json& target, const std::vector<Property_t>& properties, json network, const std::string& name)
{
	for (const auto& property : properties)
		network[property.key] = property.value;

	if (target.contains("network") && target["network"] != network)
	{
		spdlog::critical("Network data of {} differs between modules", name);
		return false;
	}

	target["network"] = std::move(network);
	return true;
}

// Schema classes in schemas.json by name, in every module that declares them
using SchemaClasses_t = std::unordered_multimap<std::string_view, json*>;

// Adds the network data to the schema classes and fields. The classes are in the module that networks them, except for
// a few base classes in another module, like CEntityInstance in entity2.
static bool AddToSchemas(const char* module, const Classes_t& classes, const SchemaClasses_t& schemaClasses)
{
	for (const auto& [name, networkClass] : classes)
	{
		json* target = nullptr;
		json* other = nullptr;
		int others = 0;
		auto [begin, end] = schemaClasses.equal_range(name);
		for (auto it = begin; it != end; it++)
		{
			if ((*it->second)["module"].get_ref<const std::string&>() == module)
			{
				target = it->second;
			}
			else
			{
				other = it->second;
				others++;
			}
		}

		if (!target && others == 1)
			target = other;

		if (!target)
		{
			spdlog::critical("Network class {} in {} is in {} schema scopes other than its own", name, module, others);
			return false;
		}

		if (!networkClass.properties.empty() && !SetNetwork(*target, networkClass.properties, json::object(), name))
			return false;

		std::unordered_map<std::string_view, json*> schemaFields;
		if (target->contains("fields"))
		{
			for (auto& schemaField : (*target)["fields"])
				schemaFields.emplace(schemaField["name"].get_ref<const std::string&>(), &schemaField);
		}

		for (const auto& field : networkClass.fields)
		{
			auto schemaField = schemaFields.find(field.name);
			if (schemaField == schemaFields.end())
			{
				spdlog::critical("Network field {}::{} in {} is not in the schema class", name, field.name, module);
				return false;
			}

			if (!SetNetwork(*schemaField->second, field.properties, { { "type", field.type } }, name + "::" + field.name))
				return false;
		}
	}

	return true;
}

bool Dump()
{
	// Not written without schemas
	SchemaClasses_t schemaClasses;
	if (auto classes = Globals::schemasJson.find("classes"); classes != Globals::schemasJson.end())
	{
		for (auto& schemaClass : classes->second)
			schemaClasses.emplace(schemaClass["name"].get_ref<const std::string&>(), &schemaClass);
	}

	for (const auto& name : g_NetworkModules)
	{
		auto module = std::find_if(Modules::allModules.begin(), Modules::allModules.end(), [&](const CModule& m) { return name == m.m_pszModule; });
		if (module == Modules::allModules.end())
		{
			spdlog::critical("{} did not load, not writing its network classes", name);
			return false;
		}

		auto database = RunRegistrations(*module);
		Classes_t classes;
		if (!database || !ReadClasses(name.c_str(), database, classes) || !WriteText(name.c_str(), classes) || !AddToSchemas(name.c_str(), classes, schemaClasses))
			return false;
	}

	RemoveOrphanFiles(Globals::outputPath / "network", g_NetworkModules, "network");
	return true;
}

} // namespace Dumpers::Network

#endif
