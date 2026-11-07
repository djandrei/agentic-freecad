// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "DocTools.h"
#include "EngineSession.h"
#include "MessageLog.h"
#include "Protocol.h"
#include "PyBridge.h"
#include "Runtime.h"
#include "SessionManager.h"

using json = nlohmann::json;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
}

json schema(json properties, json required = json::array())
{
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

const char* objectModel = R"(How FreeCAD models things, for an agent driving it through this server.

Documents and objects
- A document (.FCStd) holds objects. Each object has a unique Name (set at creation, used in
  references and Python) and a Label (shown to people, can change). Tools accept either.
- An object's state is its properties: typed values (Length is a quantity with a unit, Placement a
  position and rotation, Base a link to another object). object_get lists them with their types,
  groups and documentation; object_set changes them.
- Appearance lives on the view provider (ViewObject): colour, transparency, line width, display
  mode, visibility. object_get returns those under `view`; object_set sets them under `view`.

Parametric recompute
- Objects depend on the objects they link to. Changing a property marks the object touched;
  recompute rebuilds touched objects in dependency order. A failed object is in error with a
  status string saying why; objects that depend on it fail too. Every mutating tool reports the
  objects in error after its recompute.
- Expressions bind a property to others: {"expression": "Box.Length * 2"} in object_set.
  Spreadsheet cells and named sketch constraints can be referenced the same way.

Shapes
- Part::Feature objects carry a Shape (OpenCASCADE B-rep): solids, shells, faces, edges, vertices.
  Sub-elements are named Face1, Edge3, Vertex2 per object; shape_elements lists them with their
  geometry so you can find "the top face" by direction and pass its name on.
- Primitives (Part::Box, Part::Cylinder, ...), booleans (Part::Cut, Part::Fuse, Part::Common),
  extrusions and revolutions of sketches, fillets and chamfers on named edges.

PartDesign
- A PartDesign::Body holds one solid built feature by feature: a sketch attached to a plane or a
  face, then Pad or Pocket it, then the next feature, each building on the previous one (Tip).
  Features are created by command_run (PartDesign_Pad with the sketch selected) and finished in
  their task panel (task_get, task_set, task_accept), or with python_run and the PartDesign API.

Sketches
- A Sketcher::SketchObject holds 2D geometry and constraints in its own plane (Placement or
  attachment via AttachmentSupport and MapMode). sketch_add, sketch_constrain and sketch_get work
  in sketch coordinates; a sketch is done when the solver reports zero degrees of freedom and no
  conflicting or redundant constraints, and its closed wires are what Pad or Extrude use.

Undo
- Every mutating tool call is one transaction: document_undo reverses exactly one call.
)";

}  // namespace

namespace Agentic
{

void registerDocTools(Protocol& protocol, Runtime& runtime)
{
    auto projectArgs = [&runtime](json args) {
        if (EngineSession* session = runtime.sessions().current()) {
            args["root"] = session->project().root().string();
            args["addonFolder"] = session->project().settings().addonFolder;
        }
        return args;
    };

    protocol.addTool({
        "docs_search",
        "Search FreeCAD's documentation: the Python API (generated from the binding stubs of "
        "this build), the GUI commands, cached wiki pages and the READMEs of installed "
        "addons. Every word of the query must occur. Returns ranked hits with snippets; read "
        "a hit with docs_api, docs_command or docs_page.",
        schema(
            {
                {"query",
                 prop("string", "Words to look for, e.g. \"fillet edges\" or \"Placement rotation\".")},
                {"sources",
                 {{"type", "array"},
                  {"items", {{"type", "string"}, {"enum", {"api", "command", "wiki", "addon"}}}},
                  {"description", "Where to search. Default all."}}},
                {"limit", prop("integer", "Maximum hits. Default 20.")},
            },
            {"query"}
        ),
        [projectArgs](const json& args) -> json {
            return callPython("agentic_docs", "search", projectArgs(args));
        },
    });

    protocol.addTool({
        "docs_api",
        "The Python API of a class, module function or member: Part.Shape, Part.Shape.makeFillet, "
        "FreeCAD.Vector, FreeCAD.newDocument, Sketcher.Constraint. Classes come with every "
        "member's signature (all overloads) and documentation.",
        schema(
            {{"name", prop("string", "Dotted name, a bare class name, or the C++ class name.")}},
            {"name"}
        ),
        [](const json& args) -> json { return callPython("agentic_docs", "api", args); },
    });

    protocol.addTool({
        "docs_command",
        "A GUI command's documentation: menu text, tooltip, status tip, what's this, shortcut, "
        "and its wiki page when cached (docs_page fetches it).",
        schema({{"name", prop("string", "Command name, e.g. PartDesign_Pad.")}}, {"name"}),
        [](const json& args) -> json { return callPython("agentic_docs", "command", args); },
    });

    protocol.addTool({
        "docs_page",
        "A FreeCAD wiki page as Markdown (from the FreeCAD-documentation repository, the source "
        "the Help module reads), fetched over HTTPS once and cached. Page names use "
        "underscores: Part_Box, PartDesign_Pad, Sketcher_Workbench, Expressions.",
        schema(
            {
                {"page", prop("string", "Wiki page name.")},
                {"refresh", prop("boolean", "Fetch again even if cached. Default false.")},
            },
            {"page"}
        ),
        [](const json& args) -> json { return callPython("agentic_docs", "page", args); },
    });

    protocol.addTool({
        "docs_status",
        "What documentation is available: API classes and functions, cached wiki pages and the "
        "cache folder, GUI commands.",
        schema(json::object()),
        [](const json& args) -> json { return callPython("agentic_docs", "status", args); },
    });

    protocol.addTool({
        "docs_update",
        "Fetch wiki pages into the cache: the listed `pages`, or with `archive: true` every "
        "English page from the documentation repository's archive (a large download).",
        schema({
            {"pages",
             {{"type", "array"},
              {"items", {{"type", "string"}}},
              {"description", "Wiki page names to fetch."}}},
            {"archive", prop("boolean", "Download the whole wiki archive. Default false.")},
        }),
        [](const json& args) -> json { return callPython("agentic_docs", "update", args); },
    });

    protocol.addResourceTemplate({
        "agentic://docs/api/{name}",
        "API reference entry",
        "A class, function or member of FreeCAD's Python API as JSON (see docs_api).",
        "application/json",
        [](const std::string& uri) {
            const std::string name = uri.substr(std::string("agentic://docs/api/").size());
            return callPython("agentic_docs", "api", {{"name", name}}).dump(2);
        },
    });

    protocol.addResourceTemplate({
        "agentic://wiki/{page}",
        "Wiki page",
        "A FreeCAD wiki page as Markdown (fetched and cached on first read).",
        "text/markdown",
        [](const std::string& uri) {
            const std::string page = uri.substr(std::string("agentic://wiki/").size());
            return callPython("agentic_docs", "page", {{"page", page}})["content"].get<std::string>();
        },
    });

    protocol.addResource({
        "agentic://project/changes",
        "Change log",
        "The current project's change log as JSON: every mutation with its Python.",
        "application/json",
        [&runtime]() {
            EngineSession* session = runtime.sessions().current();
            if (!session) {
                throw std::runtime_error("no project is open");
            }
            json list = json::array();
            for (const auto& entry : session->changes().since(1)) {
                list.push_back(entry.toJson());
            }
            return list.dump(2);
        },
    });

    protocol.addResource({
        "agentic://project/log",
        "Message log",
        "The last 500 console messages FreeCAD printed, as JSON.",
        "application/json",
        [&runtime]() {
            const std::uint64_t next = runtime.messages().nextSeq();
            json list = json::array();
            for (const auto& entry : runtime.messages().since(next > 500 ? next - 500 : 1)) {
                list.push_back(entry.toJson());
            }
            return list.dump(2);
        },
    });

    protocol.addPrompt({
        "freecad-object-model",
        "How FreeCAD models things: documents, objects, properties, view providers, recompute, "
        "expressions, shapes and sub-elements, PartDesign bodies, sketches, undo.",
        {},
        [](const json&) { return std::string(objectModel); },
    });
}

}  // namespace Agentic
