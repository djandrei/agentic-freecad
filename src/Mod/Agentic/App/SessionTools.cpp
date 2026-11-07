// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "AddonTools.h"
#include "EngineSession.h"
#include "MessageLog.h"
#include "Protocol.h"
#include "Runtime.h"
#include "SessionManager.h"
#include "SessionTools.h"

using json = nlohmann::json;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
}

json objectSchema(json properties, json required = json::array())
{
    properties["project"] = prop(
        "string",
        "Name or folder of an open project to address; defaults to the current project."
    );
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

json entryList(const std::vector<Agentic::FileEntry>& entries)
{
    json list = json::array();
    for (const auto& e : entries) {
        list.push_back({
            {"path", e.path},
            {"name", e.name},
            {"type", e.isDirectory ? "folder" : "file"},
            {"size", e.size},
            {"modified", e.modified},
        });
    }
    return list;
}

json writeResultJson(const Agentic::WriteResult& r)
{
    return {
        {"path", r.path},
        {"version", r.version},
        {"created", r.created},
        {"hunk",
         r.startLine ? json(
                           {{"startLine", r.startLine},
                            {"linesAdded", r.linesAdded},
                            {"linesRemoved", r.linesRemoved}}
                       )
                     : json()},
    };
}

std::optional<std::string> optString(const json& args, const char* key)
{
    if (args.contains(key) && args[key].is_string()) {
        return args[key].get<std::string>();
    }
    return std::nullopt;
}

}  // namespace

namespace Agentic
{

EngineSession& sessionFor(Runtime& runtime, const json& arguments)
{
    SessionManager& sessions = runtime.sessions();
    if (arguments.contains("project") && arguments["project"].is_string()) {
        EngineSession* session = sessions.find(arguments["project"].get<std::string>());
        if (!session) {
            throw std::invalid_argument(
                "project is not open: " + arguments["project"].get<std::string>()
                + " (call project_open first)"
            );
        }
        sessions.setCurrent(session);
        return *session;
    }
    EngineSession* session = sessions.current();
    if (!session) {
        throw std::runtime_error("no project is open; call project_open with a folder");
    }
    return *session;
}

void registerSessionTools(Protocol& protocol, Runtime& runtime)
{
    // --- projects -----------------------------------------------------------

    protocol.addTool({
        "project_open",
        "Open a project folder and make it current. The folder's "
        "`.freecad-project.json` is created when missing; with `create` the folder "
        "itself is created too. Returns the project info.",
        {{"type", "object"},
         {"properties",
          {{"path", prop("string", "Absolute path of the project folder.")},
           {"create", prop("boolean", "Create the folder when it does not exist. Default false.")}}},
         {"required", {"path"}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            const bool create = args.value("create", false);
            const std::size_t before = runtime.sessions().list().size();
            EngineSession& session = runtime.sessions().open(args["path"].get<std::string>(), create);
            json info = session.info();
            if (runtime.sessions().list().size() > before) {
                info["addons"] = loadProjectAddons(session)["loaded"];
            }
            return info;
        },
    });

    protocol.addTool({
        "project_list",
        "List the open projects and which one is current.",
        {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
        [&runtime](const json&) -> json {
            json list = json::array();
            for (EngineSession* session : runtime.sessions().list()) {
                list.push_back({
                    {"name", session->project().settings().name},
                    {"root", session->project().root().string()},
                    {"current", session == runtime.sessions().current()},
                });
            }
            EngineSession* current = runtime.sessions().current();
            return {
                {"projects", list},
                {"current", current ? json(current->project().root().string()) : json()},
            };
        },
    });

    protocol.addTool({
        "project_info",
        "Settings and contents of a project: name, main document, scripts and addon "
        "folders, autoSave, autoRecompute, the .FCStd documents in the folder.",
        objectSchema(json::object()),
        [&runtime](const json& args) -> json { return sessionFor(runtime, args).info(); },
    });

    protocol.addTool({
        "project_set",
        "Change project settings and save them to `.freecad-project.json`.",
        objectSchema({
            {"name", prop("string", "Project name.")},
            {"mainDocument",
             {{"type", {"string", "null"}},
              {"description", "Relative path of the main .FCStd document, or null."}}},
            {"scriptsFolder",
             prop("string", "Folder of the agent's Python scripts, relative to the project.")},
            {"addonFolder", prop("string", "Folder for per-project addon installs.")},
            {"autoSave", prop("boolean", "Save documents after every mutating tool call.")},
            {"autoRecompute", prop("boolean", "Recompute after object_set and object_create.")},
            {"registries",
             {{"type", "array"},
              {"items", {{"type", "string"}}},
              {"description", "Addon registry files or URLs."}}},
        }),
        [&runtime](const json& args) -> json {
            json changes = args;
            changes.erase("project");
            return sessionFor(runtime, args).setSettings(changes);
        },
    });

    // --- files --------------------------------------------------------------

    protocol.addTool({
        "file_list",
        "List a folder of the project. Hidden entries (dot files) are skipped.",
        objectSchema({
            {"path", prop("string", "Folder relative to the project root. Default: the root.")},
            {"recursive", prop("boolean", "Descend into sub-folders. Default false.")},
        }),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            const std::string path = args.value("path", std::string());
            return {
                {"path", path},
                {"entries", entryList(session.listFiles(path, args.value("recursive", false)))}
            };
        },
    });

    protocol.addTool({
        "file_read",
        "Read a text file of the project. Returns the content and its version (a content "
        "hash) to pass as baseVersion to file_write. Binary files return only size and version.",
        objectSchema({{"path", prop("string", "File path relative to the project root.")}}, {"path"}),
        [&runtime](const json& args) -> json {
            ReadResult r = sessionFor(runtime, args).readFile(args["path"].get<std::string>());
            json out
                = {{"path", r.path}, {"isText", r.isText}, {"version", r.version}, {"size", r.size}};
            if (r.isText) {
                out["content"] = r.content;
            }
            return out;
        },
    });

    protocol.addTool({
        "file_write",
        "Write a text file of the project, atomically. Give either `content` (whole file) "
        "or `edits` (exact-text replacements, each oldText must occur exactly once). With "
        "`baseVersion` the write fails if the file changed since that version was read. "
        "Returns the new version and the changed hunk.",
        objectSchema(
            {
                {"path",
                 prop("string", "File path relative to the project root; folders are created.")},
                {"content", prop("string", "The whole new content.")},
                {"edits",
                 {{"type", "array"},
                  {"description", "Exact-text edits applied in order."},
                  {"items",
                   {{"type", "object"},
                    {"properties",
                     {{"oldText", prop("string", "Text to find; must occur exactly once.")},
                      {"newText", prop("string", "Replacement text.")}}},
                    {"required", {"oldText", "newText"}},
                    {"additionalProperties", false}}}}},
                {"baseVersion", prop("string", "Version from file_read or a previous write.")},
            },
            {"path"}
        ),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            const std::string path = args["path"].get<std::string>();
            const auto base = optString(args, "baseVersion");
            if (args.contains("content") == args.contains("edits")) {
                throw std::invalid_argument("give exactly one of content or edits");
            }
            if (args.contains("content")) {
                return writeResultJson(
                    session.writeFile(path, args["content"].get<std::string>(), base)
                );
            }
            std::vector<TextEdit> edits;
            for (const auto& e : args["edits"]) {
                edits.push_back({e["oldText"].get<std::string>(), e["newText"].get<std::string>()});
            }
            return writeResultJson(session.editFile(path, edits, base));
        },
    });

    protocol.addTool({
        "file_rename",
        "Rename or move a file or folder inside the project.",
        objectSchema(
            {{"from", prop("string", "Current path.")}, {"to", prop("string", "New path.")}},
            {"from", "to"}
        ),
        [&runtime](const json& args) -> json {
            sessionFor(runtime, args)
                .renameFile(args["from"].get<std::string>(), args["to"].get<std::string>());
            return {{"from", args["from"]}, {"to", args["to"]}};
        },
    });

    protocol.addTool({
        "file_delete",
        "Delete a file or an empty folder of the project.",
        objectSchema({{"path", prop("string", "Path to delete.")}}, {"path"}),
        [&runtime](const json& args) -> json {
            sessionFor(runtime, args).deleteFile(args["path"].get<std::string>());
            return {{"path", args["path"]}, {"deleted", true}};
        },
    });

    // --- logs ---------------------------------------------------------------

    protocol.addTool({
        "log_get",
        "Messages FreeCAD printed (errors, warnings, messages, log lines), newest last. "
        "Pass the `next` value of a previous call as `since` to get only new ones.",
        {{"type", "object"},
         {"properties",
          {{"since", prop("integer", "First sequence number to return. Default 1.")},
           {"limit", prop("integer", "Maximum number of entries. Default 200.")},
           {"level",
            {{"type", "string"},
             {"enum", {"all", "warning", "error"}},
             {"description",
              "Minimum severity: all, warning (warnings and errors) or error. Default all."}}}}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            const auto since = static_cast<std::uint64_t>(args.value("since", 1));
            const auto limit = static_cast<std::size_t>(args.value("limit", 200));
            const std::string level = args.value("level", std::string("all"));
            json list = json::array();
            for (const auto& entry : runtime.messages().since(since, 0)) {
                const bool isError = entry.level == "error" || entry.level == "critical";
                const bool isWarning = entry.level == "warning";
                if (level == "error" && !isError) {
                    continue;
                }
                if (level == "warning" && !isError && !isWarning) {
                    continue;
                }
                list.push_back(entry.toJson());
                if (list.size() >= limit) {
                    break;
                }
            }
            return {{"messages", list}, {"next", runtime.messages().nextSeq()}};
        },
    });

    protocol.addTool({
        "changes_get",
        "The change log of a project: every mutating operation in order, with the "
        "equivalent Python where there is one. Pass `next` back as `since` for new entries.",
        objectSchema({
            {"since", prop("integer", "First sequence number to return. Default 1.")},
            {"limit", prop("integer", "Maximum number of entries. Default 200.")},
        }),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            const auto since = static_cast<std::uint64_t>(args.value("since", 1));
            const auto limit = static_cast<std::size_t>(args.value("limit", 200));
            json list = json::array();
            for (const auto& entry : session.changes().since(since, limit)) {
                list.push_back(entry.toJson());
            }
            return {{"changes", list}, {"next", session.changes().nextSeq()}};
        },
    });
}

}  // namespace Agentic
