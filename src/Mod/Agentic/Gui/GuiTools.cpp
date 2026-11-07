// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Mod/Agentic/App/DocumentEngine.h>
#include <Mod/Agentic/App/EngineSession.h>
#include <Mod/Agentic/App/Protocol.h>
#include <Mod/Agentic/App/PyBridge.h>
#include <Mod/Agentic/App/Runtime.h>
#include <Mod/Agentic/App/SessionManager.h>
#include <Mod/Agentic/App/SessionTools.h>

#include <App/Document.h>

#include "CommandEngine.h"
#include "GuiTools.h"
#include "TaskPanelEngine.h"

using json = nlohmann::json;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
}

json schema(json properties = json::object(), json required = json::array())
{
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

json py(const char* function, const json& args = json::object())
{
    return Agentic::callPython("agentic_gui", function, args);
}

void recordSelection(Agentic::Runtime& runtime, const std::string& summary, const json& detail)
{
    if (Agentic::EngineSession* session = runtime.sessions().current()) {
        Agentic::ChangeEntry entry;
        entry.kind = "selection";
        entry.summary = summary;
        entry.detail = detail;
        session->recordChange(std::move(entry));
    }
}

}  // namespace

namespace Agentic
{

void registerGuiTools(Protocol& protocol, Runtime& runtime)
{
    static CommandEngine commands(runtime);

    protocol.addTool({
        "workbench_list",
        "The installed workbenches with their menu text, and which one is active.",
        schema(),
        [](const json&) -> json { return py("workbenches"); },
    });

    protocol.addTool({
        "workbench_activate",
        "Activate a workbench (loads its module and commands). Returns its toolbars and menus.",
        schema(
            {{"name",
              prop("string", "Workbench name, e.g. PartWorkbench, PartDesignWorkbench, SketcherWorkbench.")}},
            {"name"}
        ),
        [&runtime](const json& args) -> json {
            runtime.takeDialogs();
            json out = py("activate_workbench", args);
            // Some workbenches ask something on first activation (BIM's welcome).
            out["dialogs"] = runtime.takeDialogs();
            if (EngineSession* session = runtime.sessions().current()) {
                ChangeEntry entry;
                entry.kind = "workbench";
                entry.summary = "activated workbench " + args["name"].get<std::string>();
                entry.python = "Gui.activateWorkbench(" + json(args["name"]).dump() + ")";
                session->recordChange(std::move(entry));
            }
            return out;
        },
    });

    protocol.addTool({
        "command_list",
        "GUI commands with menu text, tooltip, shortcut and whether each is active for the "
        "current document and selection. With `workbench` the workbench is activated and "
        "only its toolbar commands are listed, each with its toolbar names.",
        schema({
            {"workbench", prop("string", "Limit to the toolbars of this workbench (activates it).")},
            {"filter", prop("string", "Case-insensitive substring of the name, menu text or tooltip.")},
        }),
        [](const json& args) -> json { return py("commands", args); },
    });

    protocol.addTool({
        "command_info",
        "Details of one GUI command: menu text, tooltip, status tip, what's this, shortcut, "
        "active.",
        schema({{"name", prop("string", "Command name, e.g. Part_Box or PartDesign_Pad.")}}, {"name"}),
        [](const json& args) -> json { return py("command_info", args); },
    });

    protocol.addTool({
        "command_run",
        "Run a GUI command as its toolbar button would, against the current selection and "
        "active document. Returns the Python the command recorded, the objects it created, "
        "changed or deleted, the task panel it opened (drive it with task_get, task_set, "
        "task_accept, task_reject), modal dialogs that were auto-answered, and the console "
        "messages. Fails when the command is not active.",
        schema({{"name", prop("string", "Command name, e.g. Part_Box.")}}, {"name"}),
        [](const json& args) -> json { return commands.run(args["name"].get<std::string>()); },
    });

    protocol.addTool({
        "task_get",
        "The open task panel: its boxes, buttons and input fields (id, kind, label, value, "
        "options). Null when no panel is open.",
        schema(),
        [](const json&) -> json { return {{"taskPanel", TaskPanelEngine::describe()}}; },
    });

    protocol.addTool({
        "task_set",
        "Fill fields of the open task panel by id (from task_get). Quantities take a number "
        "in the field's unit or a string with unit; choices take an option text or index; "
        "booleans true/false; buttons any value to click them; lists an array of item texts.",
        schema({{"fields", {{"type", "object"}, {"description", "Field id to value."}}}}, {"fields"}),
        [](const json& args) -> json { return TaskPanelEngine::set(args["fields"]); },
    });

    protocol.addTool({
        "task_accept",
        "Press OK on the open task panel; the feature is created or updated and saved "
        "according to the project settings.",
        schema(),
        [&runtime](const json&) -> json {
            runtime.takeDialogs();
            json out = TaskPanelEngine::accept();
            out["dialogs"] = runtime.takeDialogs();
            if (EngineSession* session = runtime.sessions().current()) {
                ChangeEntry entry;
                entry.kind = "command";
                entry.summary = "accepted task panel";
                session->recordChange(std::move(entry));
                out["saved"] = session->documents().saveProjectDocuments();
            }
            return out;
        },
    });

    protocol.addTool({
        "task_reject",
        "Press Cancel on the open task panel.",
        schema(),
        [&runtime](const json&) -> json {
            runtime.takeDialogs();
            json out = TaskPanelEngine::reject();
            out["dialogs"] = runtime.takeDialogs();
            return out;
        },
    });

    protocol.addTool({
        "object_edit",
        "Enter an object's edit mode, as a double-click in the tree does: a sketch opens for "
        "editing, a feature opens its task panel, a placement its dialog. Returns the task "
        "panel that opened (drive it with task_get, task_set, task_accept). `mode` selects "
        "an alternative edit mode where the object has one (0 default, 1 transform, ...).",
        {{"type", "object"},
         {"properties",
          {{"object", prop("string", "Object name or label.")},
           {"mode", prop("integer", "Edit mode. Default 0.")},
           {"document", prop("string", "Document name; default the project's active document.")},
           {"project",
            prop("string", "Name or folder of an open project; defaults to the current project.")}}},
         {"required", {"object"}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            App::Document* doc = session.documents().resolveDocument(
                args.value("document", std::string())
            );
            json forward = args;
            forward.erase("project");
            forward["document"] = doc->getName();
            runtime.takeDialogs();
            json out = callPython("agentic_sketch", "object_edit", forward);
            ChangeEntry entry;
            entry.kind = "edit";
            entry.summary = "edit mode on " + out["object"].get<std::string>();
            entry.python = out.value("python", std::string());
            session.recordChange(std::move(entry));
            out.erase("python");
            out["taskPanel"] = TaskPanelEngine::describe();
            out["dialogs"] = runtime.takeDialogs();
            return out;
        },
    });

    protocol.addTool({
        "object_edit_end",
        "Leave edit mode (closing the edit task panel as Close would) and recompute.",
        {{"type", "object"},
         {"properties",
          {{"document", prop("string", "Document name; default the project's active document.")},
           {"project",
            prop("string", "Name or folder of an open project; defaults to the current project.")}}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            App::Document* doc = session.documents().resolveDocument(
                args.value("document", std::string())
            );
            json out = callPython("agentic_sketch", "object_edit_end", {{"document", doc->getName()}});
            if (out.contains("python")) {
                ChangeEntry entry;
                entry.kind = "edit";
                entry.summary = "left edit mode";
                entry.python = out["python"].get<std::string>();
                session.recordChange(std::move(entry));
                out.erase("python");
            }
            out["taskPanel"] = TaskPanelEngine::describe();
            out["saved"] = session.documents().saveProjectDocuments();
            return out;
        },
    });

    protocol.addTool({
        "selection_get",
        "The current selection: objects with their selected sub-elements (Face1, Edge3, ...).",
        schema(),
        [](const json&) -> json { return py("selection_get"); },
    });

    protocol.addTool({
        "selection_set",
        "Select objects and sub-elements (replacing the selection unless `add`). This is "
        "how the agent picks faces and edges for commands; find element names with "
        "object_get or shape_elements.",
        schema(
            {
                {"items",
                 {{"type", "array"},
                  {"description", "What to select."},
                  {"items",
                   {{"type", "object"},
                    {"properties",
                     {{"document", prop("string", "Document name; default the active document.")},
                      {"object", prop("string", "Object name or unique label.")},
                      {"sub",
                       {{"type", "array"},
                        {"items", {{"type", "string"}}},
                        {"description", "Sub-element names such as Face1, Edge2, Vertex3."}}}}},
                    {"required", {"object"}},
                    {"additionalProperties", false}}}}},
                {"add", prop("boolean", "Keep the existing selection. Default false.")},
            },
            {"items"}
        ),
        [&runtime](const json& args) -> json {
            json out = py("selection_set", args);
            recordSelection(runtime, "selection set: " + args["items"].dump(), args);
            return out;
        },
    });

    protocol.addTool({
        "selection_clear",
        "Clear the selection.",
        schema(),
        [&runtime](const json&) -> json {
            json out = py("selection_clear");
            recordSelection(runtime, "selection cleared", json::object());
            return out;
        },
    });
}

}  // namespace Agentic
