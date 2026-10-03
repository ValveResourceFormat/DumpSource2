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

#include "sections.h"
#include "output.h"
#include <algorithm>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

// Module metadata is what each module's metadata providers build: Pulse bindings and their fingerprints, resource manifests,
// tool bind targets, and the entity datamaps, schema classes and VData classes that other dumps already have.
// Each section is written as text with a line per entry, which diffs better than the KV3 does. Values that are not part
// of the format are written as "// key = value" comments, and values a key always has with its default left out.

namespace Dumpers::ModuleMetadata
{

using json = nlohmann::ordered_json;

static std::string ToText(const json& value)
{
	return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

// "// key = value" for a value that is not in the format, and "// key" for true
static std::string KeyComment(std::string_view indent, std::string_view prefix, const std::string& key, const json& value)
{
	if (value.is_boolean() && value.get<bool>())
		return fmt::format("{}// {}{}", indent, prefix, key);

	return fmt::format("{}// {}{} = {}", indent, prefix, key, ToText(value));
}

// Pulse bindings are inputs and outputs of entity APIs, library functions and cells (classes)
enum BindingKind
{
	KIND_CLASS = 1 << 0,
	KIND_INPUT = 1 << 1,
	KIND_OUTPUT = 1 << 2,
	KIND_FUNCTION = 1 << 3,
	KIND_ANY = KIND_CLASS | KIND_INPUT | KIND_OUTPUT | KIND_FUNCTION,
};

// Defaults that are a value of the binding: its own key, its library fingerprint key, and the inflow its parameters belong to
enum DerivedDefault
{
	DERIVED_NONE,
	DERIVED_BINDING_KEY,
	DERIVED_LIBRARY_KEY,
	DERIVED_INFLOW,
};

// Values that a key of a binding kind always has, and has this value in almost all bindings.
// Only keys every binding of the kind has are left out, so a missing key is not mistaken for its default.
struct Default_t
{
	int m_Kinds;
	const char* m_pszKey;
	json m_Value;
	DerivedDefault m_Derived = DERIVED_NONE;
};

static const json g_DefaultInflow = { { "name", "InDefault" }, { "friendly_name", "Default In" }, { "is_default_inflow", true } };
static const json g_DefaultOutflow = { { "name", "OutDefault" }, { "friendly_name", "Default Out" }, { "is_default_outflow", true } };

static const Default_t g_BindingDefaults[] = {
	{ KIND_ANY, "m_BaseClasses", json::array() },
	{ KIND_ANY, "m_FriendlyName", "" },
	{ KIND_ANY, "m_Description", "" },
	{ KIND_ANY, "m_HelpContextName", "" },
	{ KIND_ANY, "m_nClassType", "vdata" },
	{ KIND_ANY, "m_rgbColor", json::array({ 220, 30, 220 }) },
	{ KIND_ANY, "m_bmins", json::array({ -8.0, -8.0, -8.0 }) },
	{ KIND_ANY, "m_bmaxs", json::array({ 8.0, 8.0, 8.0 }) },
	{ KIND_ANY, "m_Tags", json::array() },
	{ KIND_ANY, "m_Variables", json::array() },
	{ KIND_ANY, "m_Inputs", json::array() },
	{ KIND_ANY, "m_Outputs", json::array() },
	{ KIND_ANY, "m_Helpers", json::array() },
};

static const Default_t g_MetaDataDefaults[] = {
	{ KIND_INPUT | KIND_OUTPUT, "target_arg_name", "_Target" },
	{ KIND_INPUT | KIND_OUTPUT | KIND_FUNCTION, "supported_domains", { { "BaseDomain", true } } },
	{ KIND_INPUT | KIND_OUTPUT | KIND_FUNCTION, "fingerprint", nullptr, DERIVED_LIBRARY_KEY },
	{ KIND_INPUT | KIND_OUTPUT, "pulse_feature_tag", 1 },
	{ KIND_INPUT | KIND_OUTPUT, "generic_node_category", 5 },
	{ KIND_FUNCTION, "generic_node_category", 4 },
	{ KIND_OUTPUT, "is_spontaneous", true },
	{ KIND_OUTPUT, "pulse_outflows", json::array({ g_DefaultOutflow }) },
};

static const Default_t g_InParamDefaults[] = {
	{ KIND_ANY, "description", "" },
	{ KIND_ANY, "advanced_view", false },
	{ KIND_ANY, "metadata", nullptr },
	{ KIND_ANY, "associated_inflow_name", nullptr, DERIVED_INFLOW },
};

static const Default_t g_OutParamDefaults[] = {
	{ KIND_ANY, "description", "" },
	{ KIND_ANY, "advanced_view", false },
	{ KIND_ANY, "metadata", nullptr },
	{ KIND_INPUT | KIND_FUNCTION, "associated_outflow_name", "OutDefault" },
	{ KIND_INPUT | KIND_FUNCTION, "direct_return_value_for", nullptr, DERIVED_BINDING_KEY },
	{ KIND_OUTPUT, "associated_inflow_name", nullptr, DERIVED_BINDING_KEY },
};

// The target parameter of inputs, the entity the input is called on, always has this description
static const std::string g_TargetDescription = "The Target object. In code, this parameter is the API This() object for this Input.";

struct Binding_t
{
	std::string m_Key;
	std::string m_LibraryKey;
	BindingKind m_Kind;

	// The default inflow if it has one, its first inflow otherwise, or the binding itself without inflows
	std::string m_Inflow;
};

template <size_t N>
static bool IsDefault(const Default_t (&defaults)[N], const Binding_t& binding, const std::string& key, const json& value)
{
	for (const auto& entry : defaults)
	{
		if (!(entry.m_Kinds & binding.m_Kind) || key != entry.m_pszKey)
			continue;

		switch (entry.m_Derived)
		{
			case DERIVED_BINDING_KEY:
				return value == binding.m_Key;
			case DERIVED_LIBRARY_KEY:
				return value == binding.m_LibraryKey;
			case DERIVED_INFLOW:
				return value == binding.m_Inflow;
			default:
				return value == entry.m_Value;
		}
	}

	return false;
}

// Flows as "default" for the default flow, with the keys it has besides those, like allow_direct_return
static std::string FormatFlows(const json& flows, const json& defaultFlow)
{
	if (!flows.is_array())
		return ToText(flows);

	std::vector<std::string> items;
	for (const auto& flow : flows)
	{
		bool isDefault = flow.is_object();
		for (auto it = defaultFlow.begin(); isDefault && it != defaultFlow.end(); ++it)
			isDefault = flow.contains(it.key()) && flow[it.key()] == it.value();

		if (!isDefault)
		{
			items.push_back(ToText(flow));
			continue;
		}

		auto rest = json::object();
		for (const auto& [key, value] : flow.items())
		{
			if (!defaultFlow.contains(key))
				rest[key] = value;
		}

		items.push_back(rest.empty() ? "default" : "default " + ToText(rest));
	}

	return fmt::format("{}", fmt::join(items, ", "));
}

// A parameter as "TYPE name \"Friendly Name\" = default" for the signature, and its other keys as comment lines.
// Defaults are written as the value when they are of the parameter's type, like "= 1.0" for ["PVAL_FLOAT", 1.0].
template <size_t N>
static bool FormatParam(const std::string& name, const json& param, const Binding_t& binding, const Default_t (&defaults)[N], bool isTarget, std::string& text, std::vector<std::string>& lines)
{
	auto type = param.find("type");
	if (!param.is_object() || type == param.end() || !type->is_string())
	{
		spdlog::critical("Pulse parameter {} of {} has no type", name, binding.m_Key);
		return false;
	}

	text = fmt::format("{} {}", type->get<std::string>(), name);

	if (auto it = param.find("friendly_name"); it != param.end() && it->is_string())
		text += " " + ToText(*it);

	if (auto it = param.find("default_value"); it != param.end() && *it != json::array({ "PVAL_VOID" }))
	{
		const bool isTypedValue = it->is_array() && it->size() == 2 && (*it)[0] == *type && !(*it)[1].is_array();
		text += " = " + ToText(isTypedValue ? (*it)[1] : *it);
	}

	for (const auto& [key, value] : param.items())
	{
		if (key == "type" || key == "default_value" || (key == "friendly_name" && value.is_string()))
			continue;
		if (IsDefault(defaults, binding, key, value) || (isTarget && key == "description" && value == g_TargetDescription))
			continue;

		lines.push_back(KeyComment("\t", name + ": ", key, value));
	}

	return true;
}

// Parameters as "(TYPE name, ...)", "(null)" for null, and nothing when the binding doesn't have the key
template <size_t N>
static bool FormatParams(const json& meta, const char* paramsKey, const Binding_t& binding, const Default_t (&defaults)[N], std::string& text, std::vector<std::string>& lines)
{
	auto params = meta.find(paramsKey);
	if (params == meta.end())
		return true;

	if (params->is_null())
	{
		text = "(null)";
		return true;
	}

	if (!params->is_object())
	{
		spdlog::critical("Pulse binding {} has {} that are not an object", binding.m_Key, paramsKey);
		return false;
	}

	const auto targetName = binding.m_Kind == KIND_INPUT ? meta.value("target_arg_name", std::string()) : std::string();

	std::vector<std::string> formatted;
	for (const auto& [name, param] : params->items())
	{
		std::string paramText;
		if (!FormatParam(name, param, binding, defaults, name == targetName, paramText, lines))
			return false;

		formatted.push_back(std::move(paramText));
	}

	text = fmt::format("({})", fmt::join(formatted, ", "));
	return true;
}

static const std::pair<const char*, BindingKind> g_BindingKindFlags[] = {
	{ "is_pulse_target_method", KIND_INPUT },
	{ "is_pulse_target_output", KIND_OUTPUT },
	{ "is_pulse_library_method", KIND_FUNCTION },
};

static const char* GetKindName(BindingKind kind)
{
	switch (kind)
	{
		case KIND_INPUT:
			return "input";
		case KIND_OUTPUT:
			return "output";
		case KIND_FUNCTION:
			return "function";
		default:
			return "class";
	}
}

// A binding as comment lines for its values and a signature line like "input Name(in params) -> (out params);".
// Bindings are named Class::Method, OUTPUT!Class::Output for outputs, and Class for cells.
static bool FormatBinding(const std::string& key, json binding, const std::string& scope, std::string& className, std::vector<std::string>& lines)
{
	if (!binding.is_object())
	{
		spdlog::critical("Pulse binding {} is not an object", key);
		return false;
	}

	std::string_view name = key;
	if (name.starts_with("OUTPUT!"))
		name.remove_prefix(7);

	const auto separator = name.find("::");
	className = std::string(name.substr(0, separator));
	const auto memberName = separator == std::string_view::npos ? className : std::string(name.substr(separator + 2));

	auto meta = json::object();
	if (auto it = binding.find("m_MetaData"); it != binding.end())
	{
		if (!it->is_object())
		{
			spdlog::critical("Pulse binding {} has m_MetaData that is not an object", key);
			return false;
		}

		meta = std::move(*it);
		binding.erase(it);
	}

	Binding_t info{ key, fmt::format("@lib:{}!{}", scope, className), KIND_CLASS, key };

	if (auto inflows = meta.find("pulse_inflows"); inflows != meta.end() && inflows->is_array() && !inflows->empty())
	{
		auto isDefault = std::any_of(inflows->begin(), inflows->end(), [](const json& flow) { return flow.is_object() && flow.value("name", std::string()) == "InDefault"; });
		const auto& first = inflows->front();
		info.m_Inflow = isDefault ? "InDefault" : (first.is_object() ? first.value("name", key) : key);
	}
	for (const auto& [flag, kind] : g_BindingKindFlags)
	{
		if (auto it = meta.find(flag); it != meta.end() && *it == true)
		{
			info.m_Kind = kind;
			meta.erase(it);
			break;
		}
	}

	// Keys are named from the class and member, but Dota 2 has outputs without the OUTPUT! prefix
	auto namedKey = info.m_Kind == KIND_CLASS && separator == std::string_view::npos ? className : fmt::format("{}{}::{}", info.m_Kind == KIND_OUTPUT ? "OUTPUT!" : "", className, memberName);
	if (namedKey != key)
		lines.push_back(fmt::format("\t// binding key = {}", ToText(key)));

	for (const auto& [field, value] : binding.items())
	{
		if (IsDefault(g_BindingDefaults, info, field, value))
			continue;

		// Variables of cells are long, one line each
		if (field == "m_Variables" && value.is_array())
		{
			for (const auto& variable : value)
				lines.push_back(fmt::format("\t// m_Variables[] = {}", ToText(variable)));

			continue;
		}

		lines.push_back(KeyComment("\t", "", field, value));
	}

	for (const auto& [field, value] : meta.items())
	{
		if (field == "pulse_inparams" || field == "pulse_outparams" || IsDefault(g_MetaDataDefaults, info, field, value))
			continue;

		if (field == "pulse_inflows" || field == "pulse_outflows")
			lines.push_back(fmt::format("\t// {} = {}", field, FormatFlows(value, field == "pulse_inflows" ? g_DefaultInflow : g_DefaultOutflow)));
		else
			lines.push_back(KeyComment("\t", "", field, value));
	}

	std::string inParams, outParams;
	if (!FormatParams(meta, "pulse_inparams", info, g_InParamDefaults, inParams, lines) || !FormatParams(meta, "pulse_outparams", info, g_OutParamDefaults, outParams, lines))
		return false;

	lines.push_back(fmt::format("\t{} {}{}{}{};", GetKindName(info.m_Kind), memberName, inParams, outParams.empty() ? "" : " -> ", outParams));
	return true;
}

// Bindings by class, with the fingerprint of the class's library, a hash of its methods that changes when hidden ones do.
// Fingerprints of libraries without bindings are listed at the end.
static bool WritePulseBindings(json& metadata, std::map<std::string, std::string>& files, json& unhandled)
{
	auto fingerprints = json::object();
	if (auto it = metadata.find("pulse_fingerprints"); it != metadata.end())
	{
		if (!it->is_object())
		{
			spdlog::critical("Module metadata pulse_fingerprints is not an object");
			return false;
		}

		fingerprints = std::move(*it);
		metadata.erase(it);
	}

	auto bindingsIt = metadata.find("pulse_bindings");
	if (bindingsIt == metadata.end() && fingerprints.empty())
		return true;

	std::string scope;
	auto classes = json::object();

	if (bindingsIt != metadata.end())
	{
		auto pulse = std::move(*bindingsIt);
		metadata.erase(bindingsIt);

		static const auto classesPointer = "/gamedata/m_Classes"_json_pointer;
		if (!pulse.is_object() || !pulse.contains("scope_name") || !pulse["scope_name"].is_string() || !pulse.contains(classesPointer) || !pulse[classesPointer].is_object())
		{
			spdlog::critical("Module metadata pulse_bindings has no scope_name or gamedata.m_Classes");
			return false;
		}

		scope = pulse["scope_name"].get<std::string>();
		classes = std::move(pulse[classesPointer]);
		pulse.erase("scope_name");
		pulse["gamedata"].erase("m_Classes");

		if (pulse["gamedata"].empty())
			pulse.erase("gamedata");
		if (!pulse.empty())
			unhandled["pulse_bindings"] = std::move(pulse);
	}

	// Classes by name, sorted case-insensitively like the bindings, which are in order within each class
	struct Group_t
	{
		std::string m_Name;
		std::string m_SortName;
		std::vector<std::vector<std::string>> m_Bindings;
	};

	std::vector<Group_t> groups;
	std::unordered_map<std::string, size_t> groupIndices;
	for (auto& [key, binding] : classes.items())
	{
		std::string className;
		std::vector<std::string> lines;
		if (!FormatBinding(key, std::move(binding), scope, className, lines))
			return false;

		auto [index, isNew] = groupIndices.try_emplace(className, groups.size());
		if (isNew)
			groups.push_back({ className, ToLowerAscii(className), {} });

		groups[index->second].m_Bindings.push_back(std::move(lines));
	}

	std::stable_sort(groups.begin(), groups.end(), [](const Group_t& a, const Group_t& b) { return a.m_SortName < b.m_SortName; });

	std::string text;
	if (!scope.empty())
		text += fmt::format("// scope_name = {}\n\n", scope);

	for (const auto& [className, sortName, bindings] : groups)
	{
		if (auto it = fingerprints.find(fmt::format("@lib:{}!{}", scope, className)); it != fingerprints.end())
		{
			text += fmt::format("// fingerprint = {}\n", ToText(*it));
			fingerprints.erase(it);
		}

		text += className + "\n{\n";

		for (size_t i = 0; i < bindings.size(); i++)
		{
			if (i)
				text += "\n";

			for (const auto& line : bindings[i])
				text += line + "\n";
		}

		text += "}\n\n";
	}

	if (!fingerprints.empty())
	{
		text += "// Fingerprints of libraries and types without bindings\n";

		for (const auto& [key, value] : fingerprints.items())
			text += fmt::format("{} = {}\n", key, ToText(value));
	}

	files["pulse_bindings.txt"] = std::move(text);
	return true;
}

static bool IsString(const json& object, const char* key)
{
	auto it = object.find(key);
	return it != object.end() && it->is_string();
}

// A key that was checked to be a string
static const std::string& GetString(const json& object, const char* key)
{
	return object.find(key)->get_ref<const std::string&>();
}

// Manifests as "Group \"Name\" // source file" with a resource per line, sorted as their registration order changes.
// The line in the source file is left out, it changes with unrelated code.
static bool WriteResourceManifests(json& metadata, std::map<std::string, std::string>& files)
{
	auto it = metadata.find("resource_manifests");
	if (it == metadata.end())
		return true;

	auto manifests = std::move(*it);
	metadata.erase(it);

	if (!manifests.is_array())
	{
		spdlog::critical("Module metadata resource_manifests is not an array");
		return false;
	}

	for (const auto& manifest : manifests)
	{
		if (!manifest.is_object() || !IsString(manifest, "group") || !IsString(manifest, "name") || !IsString(manifest, "source_file"))
		{
			spdlog::critical("Module metadata resource manifest has no group, name or source_file: {}", ToText(manifest));
			return false;
		}
	}

	std::stable_sort(manifests.begin(), manifests.end(), [](const json& a, const json& b) {
		return std::tie(GetString(a, "source_file"), GetString(a, "group"), GetString(a, "name")) < std::tie(GetString(b, "source_file"), GetString(b, "group"), GetString(b, "name"));
	});

	std::string text;
	for (const auto& manifest : manifests)
	{
		// Groups are names, but some manifests have none
		auto group = GetString(manifest, "group");
		if (group.empty() || group.find_first_of(" \t\"") != std::string::npos)
			group = ToText(manifest["group"]);

		text += fmt::format("{} {} // {}\n", group, ToText(manifest["name"]), GetString(manifest, "source_file"));

		for (const auto& [key, value] : manifest.items())
		{
			if (key == "group" || key == "name" || key == "source_file" || key == "source_line")
				continue;

			if (key == "resources" && value.is_array())
			{
				for (const auto& resource : value)
					text += fmt::format("\t{}\n", resource.is_string() ? resource.get<std::string>() : ToText(resource));

				continue;
			}

			text += KeyComment("\t", "", key, value) + "\n";
		}

		text += "\n";
	}

	files["resource_manifests.txt"] = std::move(text);
	return true;
}

// Tool bind targets as "Name : Class // source file", with a command per line and its description.
// The line in the source file is left out, it changes with unrelated code.
static bool WriteBindTargets(json& metadata, std::map<std::string, std::string>& files)
{
	auto it = metadata.find("bind_targets");
	if (it == metadata.end())
		return true;

	auto targets = std::move(*it);
	metadata.erase(it);

	if (!targets.is_object())
	{
		spdlog::critical("Module metadata bind_targets is not an object");
		return false;
	}

	std::string text;
	for (const auto& [name, target] : targets.items())
	{
		if (!target.is_object() || !IsString(target, "ClassName") || !IsString(target, "File"))
		{
			spdlog::critical("Module metadata bind target {} has no ClassName or File", name);
			return false;
		}

		text += fmt::format("{} : {} // {}\n", name, GetString(target, "ClassName"), GetString(target, "File"));

		for (const auto& [key, value] : target.items())
		{
			if (key == "ClassName" || key == "File" || key == "FileLine" || (key == "OverideHelpContextName" && value == ""))
				continue;

			if (key == "commands" && value.is_object())
			{
				for (const auto& [command, info] : value.items())
				{
					auto line = "\t" + command;

					if (!info.is_object())
					{
						line += " = " + ToText(info);
					}
					else
					{
						for (const auto& [field, fieldValue] : info.items())
						{
							// Descriptions are one line, unless they have line breaks
							if (field == "description" && fieldValue.is_string())
							{
								auto description = fieldValue.get<std::string>();
								if (!description.empty())
									line += " // " + (description.find_first_of("\r\n") == std::string::npos ? description : ToText(fieldValue));
							}
							else
							{
								line += fmt::format(" [{} = {}]", field, ToText(fieldValue));
							}
						}
					}

					text += line + "\n";
				}

				continue;
			}

			text += KeyComment("\t", "", key, value) + "\n";
		}

		text += "\n";
	}

	files["bind_targets.txt"] = std::move(text);
	return true;
}

// Sections that other dumps already have: the entity datamaps are in the entity FGDs,
// and the generated model and VData class descriptions are a copy of the schemas, which have them more accurately
static constexpr const char* g_DuplicateSections[] = {
	"entity_api",
	"entity_api_designname_to_base",
	"model_classes",
	"vdata_classes",
};

bool WriteSections(const char* moduleName, json metadata, std::map<std::string, std::string>& files, json& unhandled)
{
	if (!WritePulseBindings(metadata, files, unhandled) || !WriteResourceManifests(metadata, files) || !WriteBindTargets(metadata, files))
	{
		spdlog::critical("Module metadata of {} is not what it's expected to be, see above", moduleName);
		return false;
	}

	for (const auto* section : g_DuplicateSections)
		metadata.erase(section);

	for (auto& [key, value] : metadata.items())
		unhandled[key] = std::move(value);

	return true;
}

} // namespace Dumpers::ModuleMetadata
