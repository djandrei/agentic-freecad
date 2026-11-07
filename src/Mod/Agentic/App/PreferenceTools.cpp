// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Application.h>
#include <Base/Parameter.h>

#include "EngineSession.h"
#include "PreferenceTools.h"
#include "Protocol.h"
#include "Runtime.h"
#include "SessionManager.h"

using json = nlohmann::json;

namespace
{

std::string fullPath(const std::string& path)
{
    if (path.find(':') != std::string::npos) {
        return path;
    }
    return "User parameter:" + path;
}

Base::Reference<ParameterGrp> group(const std::string& path)
{
    return App::GetApplication().GetParameterGroupByPath(fullPath(path).c_str());
}

json groupContents(const Base::Reference<ParameterGrp>& grp)
{
    json groups = json::array();
    for (const auto& sub : grp->GetGroups()) {
        groups.push_back(sub->GetGroupName());
    }
    json values = json::object();
    for (const auto& [name, value] : grp->GetBoolMap()) {
        values[name] = {{"type", "bool"}, {"value", value}};
    }
    for (const auto& [name, value] : grp->GetIntMap()) {
        values[name] = {{"type", "int"}, {"value", value}};
    }
    for (const auto& [name, value] : grp->GetUnsignedMap()) {
        values[name] = {{"type", "unsigned"}, {"value", value}};
    }
    for (const auto& [name, value] : grp->GetFloatMap()) {
        values[name] = {{"type", "float"}, {"value", value}};
    }
    for (const auto& [name, value] : grp->GetASCIIMap()) {
        values[name] = {{"type", "string"}, {"value", value}};
    }
    return {{"groups", groups}, {"values", values}};
}

}  // namespace

namespace Agentic
{

void registerPreferenceTools(Protocol& protocol, Runtime& runtime)
{
    protocol.addTool({
        "preferences_get",
        "Read a group of the user preferences (the parameter tree behind the Preferences "
        "dialog): its sub-groups and its values with types. Paths are relative to "
        "\"User parameter:\", e.g. BaseApp/Preferences/Units.",
        {{"type", "object"},
         {"properties",
          {{"path",
            {{"type", "string"}, {"description", "Group path, e.g. BaseApp/Preferences/Units."}}}}},
         {"required", {"path"}},
         {"additionalProperties", false}},
        [](const json& args) -> json {
            const std::string path = args["path"].get<std::string>();
            json out = groupContents(group(path));
            out["path"] = fullPath(path);
            return out;
        },
    });

    protocol.addTool({
        "preferences_set",
        "Set one preference value. The type follows the JSON value (bool, int, float, "
        "string) unless `type` says otherwise; `unsigned` is for colour values. Pass "
        "`value: null` to remove the key.",
        {{"type", "object"},
         {"properties",
          {{"path",
            {{"type", "string"}, {"description", "Group path, e.g. BaseApp/Preferences/Units."}}},
           {"key", {{"type", "string"}, {"description", "Value name inside the group."}}},
           {"value",
            {{"type", {"boolean", "integer", "number", "string", "null"}},
             {"description", "The new value."}}},
           {"type",
            {{"type", "string"},
             {"enum", {"bool", "int", "unsigned", "float", "string"}},
             {"description", "Stored type; default from the value."}}}}},
         {"required", {"path", "key", "value"}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            const std::string path = args["path"].get<std::string>();
            const std::string key = args["key"].get<std::string>();
            const json& value = args["value"];
            auto grp = group(path);
            std::string type = args.value("type", std::string());
            std::string python;
            const std::string handle = "App.ParamGet(\"" + fullPath(path) + "\")";
            if (value.is_null()) {
                grp->RemoveBool(key.c_str());
                grp->RemoveInt(key.c_str());
                grp->RemoveUnsigned(key.c_str());
                grp->RemoveFloat(key.c_str());
                grp->RemoveASCII(key.c_str());
                type = "removed";
                python = handle + ".RemBool(\"" + key
                    + "\")  # and RemInt/RemUnsigned/RemFloat/RemString";
            }
            else {
                if (type.empty()) {
                    type = value.is_boolean()       ? "bool"
                        : value.is_number_integer() ? "int"
                        : value.is_number()         ? "float"
                                                    : "string";
                }
                if (type == "bool") {
                    grp->SetBool(key.c_str(), value.get<bool>());
                    python = handle + ".SetBool(\"" + key + "\", "
                        + (value.get<bool>() ? "True" : "False") + ")";
                }
                else if (type == "int") {
                    grp->SetInt(key.c_str(), value.get<long>());
                    python = handle + ".SetInt(\"" + key + "\", "
                        + std::to_string(value.get<long>()) + ")";
                }
                else if (type == "unsigned") {
                    grp->SetUnsigned(key.c_str(), value.get<unsigned long>());
                    python = handle + ".SetUnsigned(\"" + key + "\", "
                        + std::to_string(value.get<unsigned long>()) + ")";
                }
                else if (type == "float") {
                    grp->SetFloat(key.c_str(), value.get<double>());
                    python = handle + ".SetFloat(\"" + key + "\", "
                        + std::to_string(value.get<double>()) + ")";
                }
                else {
                    const std::string text = value.is_string() ? value.get<std::string>()
                                                               : value.dump();
                    grp->SetASCII(key.c_str(), text.c_str());
                    python = handle + ".SetString(\"" + key + "\", " + json(text).dump() + ")";
                }
            }
            if (EngineSession* session = runtime.sessions().current()) {
                ChangeEntry entry;
                entry.kind = "preference";
                entry.summary = "preference " + fullPath(path) + "/" + key + " = " + value.dump();
                entry.python = python;
                entry.detail
                    = {{"path", fullPath(path)}, {"key", key}, {"value", value}, {"type", type}};
                session->recordChange(std::move(entry));
            }
            json out = groupContents(grp);
            out["path"] = fullPath(path);
            out["key"] = key;
            out["type"] = type;
            return out;
        },
    });
}

}  // namespace Agentic
