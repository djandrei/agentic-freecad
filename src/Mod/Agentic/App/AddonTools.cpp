// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>

#include "AddonTools.h"
#include "EngineSession.h"
#include "Protocol.h"
#include "PyBridge.h"
#include "Runtime.h"
#include "SessionTools.h"

using json = nlohmann::json;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
}

json schema(json properties, json required = json::array())
{
    properties["project"]
        = prop("string", "Name or folder of an open project; defaults to the current project.");
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

json projectArgs(const Agentic::EngineSession& session, const json& args)
{
    json out = args;
    out.erase("project");
    const auto& settings = session.project().settings();
    out["root"] = session.project().root().string();
    out["addonFolder"] = settings.addonFolder;
    out["registries"] = settings.registries;
    return out;
}

}  // namespace

namespace Agentic
{

json loadProjectAddons(EngineSession& session)
{
    try {
        json out = callPython("agentic_addons", "load_project", projectArgs(session, json::object()));
        for (const auto& report : out["loaded"]) {
            for (const char* key : {"init", "initGui"}) {
                if (report[key].is_string() && report[key].get<std::string>() != "ok") {
                    Base::Console().warning(
                        "Agentic: addon {} {}: {}\n",
                        report["path"].get<std::string>(),
                        key,
                        report[key].get<std::string>()
                    );
                }
            }
        }
        return out;
    }
    catch (const std::exception& e) {
        Base::Console().warning("Agentic: could not load the project's addons: {}\n", e.what());
        return {{"loaded", json::array()}, {"error", e.what()}};
    }
}

void registerAddonTools(Protocol& protocol, Runtime& runtime)
{
    auto run = [&runtime](const json& args, const char* function) -> std::pair<EngineSession*, json> {
        EngineSession& session = sessionFor(runtime, args);
        return {&session, callPython("agentic_addons", function, projectArgs(session, args))};
    };

    protocol.addTool({
        "addon_catalog",
        "Addons available from the registries (the FreeCAD addon index, the user's and the "
        "project's registries): id, repository, branch, archive URL, FreeCAD version range "
        "and whether each entry is compatible and already installed. Remote registries are "
        "cached for a day; `refresh` fetches them again.",
        schema({
            {"filter", prop("string", "Case-insensitive substring of the id, repository or note.")},
            {"refresh", prop("boolean", "Fetch remote registries again. Default false.")},
            {"limit", prop("integer", "Maximum number of addons returned. Default 200.")},
        }),
        [run](const json& args) -> json { return run(args, "catalog").second; },
    });

    protocol.addTool({
        "addon_installed",
        "Addons installed in the project's addon folder, with their source, package.xml facts "
        "(version, dependencies), README file and load report.",
        schema(json::object()),
        [run](const json& args) -> json { return run(args, "installed").second; },
    });

    protocol.addTool({
        "addon_install",
        "Install an addon from the catalog into the project's addon folder (its zip archive "
        "over TLS, or `git clone` without a shell) and load it into this session: its "
        "Init.py runs, and its InitGui.py when the GUI is loaded, so its workbenches appear "
        "in workbench_list. Installing runs the addon's code. Python dependencies listed "
        "in package.xml are reported, not installed.",
        schema(
            {
                {"id", prop("string", "Addon id as addon_catalog lists it.")},
                {"ref",
                 prop("string", "Branch or tag of a catalog entry; default the first compatible entry.")},
                {"method",
                 {{"type", "string"},
                  {"enum", {"auto", "zip", "git"}},
                  {"description", "Download method. Default auto (zip, then git)."}}},
                {"force", prop("boolean", "Replace an existing install. Default false.")},
                {"load", prop("boolean", "Load it into this session. Default true.")},
            },
            {"id"}
        ),
        [run](const json& args) -> json {
            auto [session, out] = run(args, "install");
            ChangeEntry entry;
            entry.kind = "addon";
            entry.summary = "installed addon " + args["id"].get<std::string>() + " ("
                + out.value("method", std::string()) + ")";
            entry.detail = {{"id", args["id"]}, {"path", out["path"]}, {"method", out["method"]}};
            session->recordChange(std::move(entry));
            session->notifyFileWritten(out["path"].get<std::string>());
            return out;
        },
    });

    protocol.addTool({
        "addon_remove",
        "Remove an addon from the project's addon folder. Code it already loaded stays "
        "registered until FreeCAD restarts.",
        schema({{"id", prop("string", "Installed addon id.")}}, {"id"}),
        [run](const json& args) -> json {
            auto [session, out] = run(args, "remove");
            ChangeEntry entry;
            entry.kind = "addon";
            entry.summary = "removed addon " + args["id"].get<std::string>();
            entry.detail = {{"id", args["id"]}};
            session->recordChange(std::move(entry));
            return out;
        },
    });

    protocol.addTool({
        "addon_files",
        "List the files of an installed addon, or read one (text up to 2 MiB): its README, "
        "its macros, its parts library.",
        schema(
            {
                {"id", prop("string", "Installed addon id.")},
                {"path", prop("string", "File or folder inside the addon. Default the addon root.")},
                {"recursive", prop("boolean", "List sub-folders too. Default true.")},
                {"limit", prop("integer", "Maximum number of files listed. Default 500.")},
            },
            {"id"}
        ),
        [run](const json& args) -> json { return run(args, "files").second; },
    });

    protocol.addTool({
        "addon_registry_list",
        "The registries the catalog is built from: the default FreeCAD addon index, the "
        "user's and the project's, with whether remote ones are cached.",
        schema(json::object()),
        [run](const json& args) -> json { return run(args, "registries").second; },
    });

    protocol.addTool({
        "addon_registry_add",
        "Add a registry (a JSON file in the Addon Manager's catalog format, by URL or by "
        "path) to the project's settings or to the user's preferences.",
        schema(
            {
                {"location",
                 prop(
                     "string",
                     "https URL, file URL, or a path (relative paths are relative to the project)."
                 )},
                {"scope",
                 {{"type", "string"},
                  {"enum", {"project", "user"}},
                  {"description", "Where to record it. Default project."}}},
            },
            {"location"}
        ),
        [&runtime, run](const json& args) -> json {
            const std::string location = args["location"].get<std::string>();
            if (args.value("scope", std::string("project")) == "user") {
                return run(args, "add_user_registry").second;
            }
            EngineSession& session = sessionFor(runtime, args);
            json registries = session.project().settings().registries;
            if (std::find(registries.begin(), registries.end(), location) == registries.end()) {
                registries.push_back(location);
            }
            session.setSettings({{"registries", registries}});
            return run(args, "registries").second;
        },
    });
}

}  // namespace Agentic
