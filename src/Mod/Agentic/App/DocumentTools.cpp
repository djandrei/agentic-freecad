// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "DocumentEngine.h"
#include "DocumentTools.h"
#include "EngineSession.h"
#include "Protocol.h"
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

json docProp()
{
    return prop("string", "Document name or label; defaults to the project's active document.");
}

std::string str(const json& args, const char* key, const std::string& fallback = {})
{
    return args.contains(key) && args[key].is_string() ? args[key].get<std::string>() : fallback;
}

std::optional<std::string> opt(const json& args, const char* key)
{
    if (args.contains(key) && args[key].is_string()) {
        return args[key].get<std::string>();
    }
    return std::nullopt;
}

const char* propertyValueDoc
    = "Values by property type: numbers and strings as is; quantities as a number in "
      "internal units (mm, deg) or a string with unit (\"10 mm\"); vectors as [x, y, z]; "
      "placements as {base: [x,y,z], rotation: {axis: [x,y,z], angle: deg}}; links as an "
      "object name or {object, sub: [\"Face1\"]}; link lists as arrays of those; "
      "enumerations as the option string; colors as [r, g, b] in 0..1. "
      "{expression: \"Box.Length * 2\"} binds an expression, {expression: null} clears it.";

}  // namespace

namespace Agentic
{

void registerDocumentTools(Protocol& protocol, Runtime& runtime)
{
    auto engine = [&runtime](const json& args) -> DocumentEngine& {
        return sessionFor(runtime, args).documents();
    };

    // --- documents ----------------------------------------------------------

    protocol.addTool({
        "document_list",
        "Documents of the project: the open ones (with undo depth, object counts and "
        "errors) and the .FCStd files in the folder.",
        schema(json::object()),
        [engine](const json& args) -> json { return engine(args).listDocuments(); },
    });

    protocol.addTool({
        "document_open",
        "Open an .FCStd file of the project folder and make it the active document.",
        schema({{"file", prop("string", "File path relative to the project root.")}}, {"file"}),
        [engine](const json& args) -> json {
            return engine(args).openDocument(args["file"].get<std::string>());
        },
    });

    protocol.addTool({
        "document_new",
        "Create a new document in the project and make it active. With autoSave it is "
        "written to <name>.FCStd (or `file`) at once and after every change.",
        schema({
            {"name", prop("string", "Document name (also the label). Default: FreeCAD chooses.")},
            {"file", prop("string", "File path relative to the project root. Default: <name>.FCStd.")},
        }),
        [engine](const json& args) -> json {
            return engine(args).newDocument(str(args, "name"), opt(args, "file"));
        },
    });

    protocol.addTool({
        "document_save",
        "Save a document to its file, or to `as` (relative to the project root).",
        schema(
            {{"document", docProp()},
             {"as", prop("string", "New file path; the document keeps using it.")}}
        ),
        [engine](const json& args) -> json {
            return engine(args).saveDocument(str(args, "document"), opt(args, "as"));
        },
    });

    protocol.addTool({
        "document_close",
        "Close a document. Unsaved changes are lost unless autoSave is on.",
        schema({{"document", docProp()}}),
        [engine](const json& args) -> json {
            return engine(args).closeDocument(str(args, "document"));
        },
    });

    protocol.addTool({
        "document_recompute",
        "Recompute a document. Returns the number of recomputed objects, every object in "
        "error with its status, and the console messages of the run.",
        schema(
            {{"document", docProp()},
             {"force", prop("boolean", "Recompute even if nothing is touched. Default false.")}}
        ),
        [engine](const json& args) -> json {
            return engine(args).recompute(str(args, "document"), args.value("force", false));
        },
    });

    protocol.addTool({
        "document_undo",
        "Undo the last transaction of a document (one mutating tool call).",
        schema({{"document", docProp()}}),
        [engine](const json& args) -> json { return engine(args).undo(str(args, "document")); },
    });

    protocol.addTool({
        "document_redo",
        "Redo the last undone transaction of a document.",
        schema({{"document", docProp()}}),
        [engine](const json& args) -> json { return engine(args).redo(str(args, "document")); },
    });

    // --- objects ------------------------------------------------------------

    protocol.addTool({
        "object_list",
        "Objects of a document with type, label, status, visibility and dependency names.",
        schema(
            {{"document", docProp()},
             {"type", prop("string", "Only objects derived from this type, e.g. Part::Feature.")}}
        ),
        [engine](const json& args) -> json {
            return engine(args).listObjects(str(args, "document"), opt(args, "type"));
        },
    });

    protocol.addTool({
        "object_get",
        "Everything about one object: type, status, every property with type, group, "
        "documentation, value, expression and enumeration options, the view-provider "
        "properties under `view` when the GUI is available, dependencies, and a shape "
        "summary for geometry.",
        schema(
            {
                {"document", docProp()},
                {"object", prop("string", "Object name or unique label.")},
                {"properties",
                 {{"type", "array"},
                  {"items", {{"type", "string"}}},
                  {"description", "Only these properties. Default: all."}}},
                {"view", prop("boolean", "Include view-provider properties. Default true.")},
            },
            {"object"}
        ),
        [engine](const json& args) -> json {
            return engine(args).getObject(
                str(args, "document"),
                args["object"].get<std::string>(),
                args.value("properties", json::array()),
                args.value("view", true)
            );
        },
    });

    protocol.addTool({
        "object_set",
        std::string(
            "Set properties of an object (and of its view provider under `view`) in one "
            "transaction; recomputes and saves according to the project settings. "
        ) + propertyValueDoc,
        schema(
            {
                {"document", docProp()},
                {"object", prop("string", "Object name or unique label.")},
                {"properties", {{"type", "object"}, {"description", "Property name to value."}}},
                {"view",
                 {{"type", "object"},
                  {"description",
                   "View-provider property name to value (ShapeAppearance, Transparency, "
                   "LineWidth, DisplayMode, Visibility, ...)."}}},
            },
            {"object"}
        ),
        [engine](const json& args) -> json {
            return engine(args).setObject(
                str(args, "document"),
                args["object"].get<std::string>(),
                args.value("properties", json::object()),
                args.value("view", json::object())
            );
        },
    });

    protocol.addTool({
        "object_create",
        std::string(
            "Create an object of a type (Part::Box, Part::Cylinder, PartDesign::Body, "
            "Sketcher::SketchObject, Part::Cut, App::Part, ...) and set initial properties "
            "in one transaction. "
        ) + propertyValueDoc,
        schema(
            {
                {"document", docProp()},
                {"type", prop("string", "Object type id, e.g. Part::Box.")},
                {"name", prop("string", "Internal name. Default: derived from the type.")},
                {"properties", {{"type", "object"}, {"description", "Initial property values."}}},
                {"view",
                 {{"type", "object"}, {"description", "Initial view-provider property values."}}},
            },
            {"type"}
        ),
        [engine](const json& args) -> json {
            return engine(args).createObject(
                str(args, "document"),
                args["type"].get<std::string>(),
                opt(args, "name"),
                args.value("properties", json::object()),
                args.value("view", json::object())
            );
        },
    });

    protocol.addTool({
        "object_delete",
        "Delete an object. Objects that depended on it are reported and left touched.",
        schema(
            {{"document", docProp()}, {"object", prop("string", "Object name or unique label.")}},
            {"object"}
        ),
        [engine](const json& args) -> json {
            return engine(args).deleteObject(str(args, "document"), args["object"].get<std::string>());
        },
    });

    protocol.addTool({
        "object_dependencies",
        "The dependency graph of a document, or of one object with everything it depends "
        "on and everything that depends on it.",
        schema(
            {{"document", docProp()},
             {"object", prop("string", "Limit to this object's neighbourhood.")}}
        ),
        [engine](const json& args) -> json {
            return engine(args).dependencies(str(args, "document"), opt(args, "object"));
        },
    });
}

}  // namespace Agentic
