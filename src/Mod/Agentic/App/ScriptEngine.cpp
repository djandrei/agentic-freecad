// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <algorithm>

#include <App/Application.h>
#include <App/Document.h>

#include "DocumentEngine.h"
#include "EngineSession.h"
#include "MessageLog.h"
#include "ObjectTracker.h"
#include "Protocol.h"
#include "PyBridge.h"
#include "Runtime.h"
#include "ScriptEngine.h"
#include "SessionTools.h"

using json = nlohmann::json;

namespace Agentic
{

ScriptEngine::ScriptEngine(EngineSession& session, MessageLog& messages, Runtime& runtime)
    : session(session)
    , messages(messages)
    , runtime(runtime)
{}

json ScriptEngine::run(
    const std::optional<std::string>& code,
    const std::optional<std::string>& file,
    double timeoutSeconds,
    const std::optional<std::string>& document
)
{
    if (code.has_value() == file.has_value()) {
        throw std::invalid_argument("give exactly one of code or file");
    }
    std::string source;
    std::string filename = "<python_run>";
    if (file) {
        ReadResult read = session.readFile(*file);
        if (!read.isText) {
            throw std::invalid_argument(*file + " is not a text file");
        }
        source = read.content;
        filename = session.resolvePath(*file, true).string();
    }
    else {
        source = *code;
    }

    DocumentEngine& docs = session.documents();
    App::Document* doc = nullptr;
    if (document) {
        doc = docs.resolveDocument(*document);
    }
    else if (!docs.activeDocument().empty()) {
        doc = docs.resolveDocument("");
    }

    runtime.takeDialogs();
    const std::uint64_t messagesFrom = messages.nextSeq();
    json args = {
        {"code", source},
        {"filename", filename},
        {"timeoutSeconds", timeoutSeconds},
        {"root", session.project().root().string()},
        {"document", doc ? json(doc->getName()) : json()},
    };

    json transaction = json::object();
    json result;
    {
        ObjectTracker tracker;
        if (doc) {
            doc->openTransaction("python_run");
        }
        try {
            result = callPython("agentic_script", "run", args);
        }
        catch (...) {
            if (doc && doc->hasPendingTransaction()) {
                doc->abortTransaction();
            }
            throw;
        }
        const bool ok = result.value("ok", false);
        if (doc) {
            if (!doc->hasPendingTransaction()) {
                transaction["note"] = "the script closed the python_run transaction itself";
            }
            else if (ok) {
                doc->commitTransaction();
                transaction["committed"] = true;
            }
            else {
                doc->abortTransaction();
                transaction["rolledBack"] = true;
            }
        }
        result["objects"] = tracker.toJson();
    }
    result["transaction"] = transaction;
    result["dialogs"] = runtime.takeDialogs();
    result["saved"] = docs.saveProjectDocuments();

    json list = json::array();
    int errors = 0;
    int warnings = 0;
    for (const auto& entry : messages.since(messagesFrom)) {
        if (entry.level == "log") {
            continue;
        }
        list.push_back({{"level", entry.level}, {"text", entry.text}});
        if (entry.level == "error" || entry.level == "critical") {
            ++errors;
        }
        else if (entry.level == "warning") {
            ++warnings;
        }
    }
    result["messages"] = list;
    result["errors"] = errors;
    result["warnings"] = warnings;

    ChangeEntry entry;
    entry.kind = "python";
    entry.summary = file
        ? "ran " + *file
        : "ran " + std::to_string(std::count(source.begin(), source.end(), '\n') + 1)
            + " line(s) of Python";
    if (!result.value("ok", false)) {
        entry.summary += " (failed)";
    }
    entry.python = source;
    entry.detail = {
        {"file", file ? json(*file) : json()},
        {"ok", result.value("ok", false)},
        {"objects", result["objects"]},
    };
    session.recordChange(std::move(entry));
    if (doc) {
        session.notifyDocument(doc->getName(), "objects");
    }
    return result;
}

json ScriptEngine::reset()
{
    return callPython("agentic_script", "reset", {{"root", session.project().root().string()}});
}

void registerScriptTools(Protocol& protocol, Runtime& runtime)
{
    protocol.addTool({
        "python_run",
        "Run Python inside FreeCAD: a code string or a project file. The namespace "
        "persists per project between runs and has FreeCAD/App, FreeCADGui/Gui (when "
        "available), Part, `doc` (the active document) and `project_root` bound. Returns "
        "the value of a trailing expression, stdout, stderr, the exception with "
        "traceback, the objects created/changed/deleted, the documents saved, and the "
        "console messages. The run is one transaction on the active document and is "
        "rolled back on an exception. `timeoutSeconds` stops Python code (not a running "
        "recompute inside C++).",
        {{"type", "object"},
         {"properties",
          {{"code", {{"type", "string"}, {"description", "Python source to run."}}},
           {"file",
            {{"type", "string"},
             {"description", "Project-relative path of a .py file to run instead of code."}}},
           {"timeoutSeconds",
            {{"type", "number"},
             {"minimum", 0},
             {"description",
              "Abort Python execution after this many seconds. Default 60; 0 disables."}}},
           {"document",
            {{"type", "string"},
             {"description",
              "Document bound to `doc` and holding the transaction; default the active document."}}},
           {"project",
            {{"type", "string"},
             {"description", "Name or folder of an open project; defaults to the current project."}}}}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            std::optional<std::string> code;
            std::optional<std::string> file;
            std::optional<std::string> document;
            if (args.contains("code")) {
                code = args["code"].get<std::string>();
            }
            if (args.contains("file")) {
                file = args["file"].get<std::string>();
            }
            if (args.contains("document")) {
                document = args["document"].get<std::string>();
            }
            return session.scripts().run(code, file, args.value("timeoutSeconds", 60.0), document);
        },
    });

    protocol.addTool({
        "python_reset",
        "Forget the project's Python namespace (variables from earlier python_run calls).",
        {{"type", "object"},
         {"properties",
          {{"project",
            {{"type", "string"},
             {"description", "Name or folder of an open project; defaults to the current project."}}}}},
         {"additionalProperties", false}},
        [&runtime](const json& args) -> json { return sessionFor(runtime, args).scripts().reset(); },
    });
}

}  // namespace Agentic
