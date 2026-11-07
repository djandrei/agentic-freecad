// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "Schema.h"

using json = nlohmann::json;

namespace
{

const char* typeName(const json& value)
{
    switch (value.type()) {
        case json::value_t::null:
            return "null";
        case json::value_t::boolean:
            return "boolean";
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
            return "integer";
        case json::value_t::number_float:
            return "number";
        case json::value_t::string:
            return "string";
        case json::value_t::array:
            return "array";
        case json::value_t::object:
            return "object";
        default:
            return "unknown";
    }
}

bool matchesType(const std::string& type, const json& value)
{
    if (type == "string") {
        return value.is_string();
    }
    if (type == "integer") {
        return value.is_number_integer();
    }
    if (type == "number") {
        return value.is_number();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "object") {
        return value.is_object();
    }
    if (type == "array") {
        return value.is_array();
    }
    if (type == "null") {
        return value.is_null();
    }
    return false;
}

std::string joinTypes(const json& types)
{
    std::string out;
    for (const auto& t : types) {
        if (!out.empty()) {
            out += " or ";
        }
        out += t.get<std::string>();
    }
    return out;
}

}  // namespace

namespace Agentic
{

std::optional<std::string> validateJson(const json& schema, const json& value, const std::string& path)
{
    if (!schema.is_object()) {
        return std::nullopt;
    }

    if (schema.contains("type")) {
        const json& type = schema["type"];
        bool ok = false;
        if (type.is_string()) {
            ok = matchesType(type.get<std::string>(), value);
        }
        else if (type.is_array()) {
            for (const auto& t : type) {
                if (t.is_string() && matchesType(t.get<std::string>(), value)) {
                    ok = true;
                    break;
                }
            }
        }
        if (!ok) {
            std::string expected = type.is_string() ? type.get<std::string>() : joinTypes(type);
            return path + ": expected " + expected + ", got " + typeName(value);
        }
    }

    if (schema.contains("const") && schema["const"] != value) {
        return path + ": must equal " + schema["const"].dump();
    }

    if (schema.contains("enum")) {
        bool found = false;
        for (const auto& candidate : schema["enum"]) {
            if (candidate == value) {
                found = true;
                break;
            }
        }
        if (!found) {
            return path + ": must be one of " + schema["enum"].dump();
        }
    }

    if (value.is_string()) {
        const auto length = value.get<std::string>().size();
        if (schema.contains("minLength") && length < schema["minLength"].get<size_t>()) {
            return path + ": shorter than " + schema["minLength"].dump() + " characters";
        }
        if (schema.contains("maxLength") && length > schema["maxLength"].get<size_t>()) {
            return path + ": longer than " + schema["maxLength"].dump() + " characters";
        }
    }

    if (value.is_number()) {
        const double number = value.get<double>();
        if (schema.contains("minimum") && number < schema["minimum"].get<double>()) {
            return path + ": below minimum " + schema["minimum"].dump();
        }
        if (schema.contains("maximum") && number > schema["maximum"].get<double>()) {
            return path + ": above maximum " + schema["maximum"].dump();
        }
    }

    if (value.is_array()) {
        if (schema.contains("minItems") && value.size() < schema["minItems"].get<size_t>()) {
            return path + ": fewer than " + schema["minItems"].dump() + " items";
        }
        if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<size_t>()) {
            return path + ": more than " + schema["maxItems"].dump() + " items";
        }
        if (schema.contains("items")) {
            size_t index = 0;
            for (const auto& item : value) {
                auto err
                    = validateJson(schema["items"], item, path + "[" + std::to_string(index) + "]");
                if (err) {
                    return err;
                }
                ++index;
            }
        }
    }

    if (value.is_object()) {
        const json properties = schema.contains("properties") ? schema["properties"] : json::object();
        if (schema.contains("required")) {
            for (const auto& name : schema["required"]) {
                if (!value.contains(name.get<std::string>())) {
                    return path + ": missing required property '" + name.get<std::string>() + "'";
                }
            }
        }
        for (const auto& [name, member] : value.items()) {
            if (properties.contains(name)) {
                auto err = validateJson(properties[name], member, path + "." + name);
                if (err) {
                    return err;
                }
            }
            else if (
                schema.contains("additionalProperties") && schema["additionalProperties"].is_boolean()
                && !schema["additionalProperties"].get<bool>()
            ) {
                return path + ": unknown property '" + name + "'";
            }
        }
    }

    return std::nullopt;
}

}  // namespace Agentic
