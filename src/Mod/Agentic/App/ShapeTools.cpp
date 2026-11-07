// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Document.h>

#include "DocumentEngine.h"
#include "EngineSession.h"
#include "Protocol.h"
#include "PyBridge.h"
#include "Runtime.h"
#include "SessionTools.h"
#include "ShapeTools.h"

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
    properties["document"]
        = prop("string", "Document name or label; defaults to the project's active document.");
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

json objectList(const char* description)
{
    return {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", description}};
}

json vector3(const char* description)
{
    return {
        {"type", "array"},
        {"items", {{"type", "number"}}},
        {"minItems", 3},
        {"maxItems", 3},
        {"description", description}
    };
}

}  // namespace

namespace Agentic
{

void registerShapeTools(Protocol& protocol, Runtime& runtime)
{
    // Resolves the document a call addresses and forwards the arguments to Python.
    auto withDocument = [&runtime](const json& args, const char* function) -> json {
        EngineSession& session = sessionFor(runtime, args);
        App::Document* doc = session.documents().resolveDocument(args.value("document", std::string()));
        json forward = args;
        forward.erase("project");
        forward["document"] = doc->getName();
        return callPython("agentic_shapes", function, forward);
    };

    protocol.addTool({
        "geometry_info",
        "Measured facts about shapes: volume, area, bounding box (min, max, size, center), "
        "center of mass, counts of solids, shells, faces, edges and vertices, validity and "
        "closedness. Default objects: the results of the document (shapes no other shape "
        "consumes); with several, a total volume and combined bounding box.",
        schema(
            {{"objects", objectList("Object names or labels; default the document's result shapes.")}}
        ),
        [withDocument](const json& args) -> json { return withDocument(args, "geometry_info"); },
    });

    protocol.addTool({
        "geometry_check",
        "Check shapes for defects with OpenCASCADE's checker (BRepCheck and, by default, the "
        "boolean-operation check): invalid sub-shapes, self-intersections, open shells, "
        "inverted solids. Returns per object whether it is ok and the problems found.",
        schema({
            {"objects", objectList("Object names or labels; default the document's result shapes.")},
            {"booleanCheck",
             prop("boolean", "Also run the boolean-operation check (slower). Default true.")},
        }),
        [withDocument](const json& args) -> json { return withDocument(args, "geometry_check"); },
    });

    protocol.addTool({
        "shape_elements",
        "The faces, edges and vertices of an object with their names (Face6, Edge3, Vertex1) "
        "and geometry: surface or curve type, area or length, center of mass, bounding box, "
        "normal of planar faces, axis, center and radius of cylinders, cones and circles. "
        "This replaces clicking in the 3D view: to find the top face, pass "
        "direction [0,0,1]; directionMode `extreme` keeps the elements furthest along it, "
        "`facing` keeps planar faces whose normal points that way, `sort` only orders. "
        "Pass the names to selection_set or to a feature's link property.",
        schema(
            {
                {"object", prop("string", "Object name or label.")},
                {"types",
                 {{"type", "array"},
                  {"items", {{"type", "string"}, {"enum", {"Face", "Edge", "Vertex"}}}},
                  {"description", "Element kinds; default all."}}},
                {"surface",
                 prop("string", "Only faces of this surface type: Plane, Cylinder, Cone, Sphere, Toroid, BSplineSurface, ...")},
                {"curve",
                 prop("string", "Only edges of this curve type: Line, Circle, Ellipse, BSplineCurve, ...")},
                {"direction",
                 vector3("Order (and filter, see directionMode) by position along this direction.")},
                {"directionMode",
                 {{"type", "string"},
                  {"enum", {"extreme", "facing", "sort"}},
                  {"description", "How direction filters. Default extreme."}}},
                {"tolerance",
                 prop("number", "Distance tolerance for directionMode extreme. Default 1e-6.")},
                {"nearPoint", vector3("Order by distance of the center of mass from this point.")},
                {"limit", prop("integer", "Maximum number of elements returned. Default 100.")},
            },
            {"object"}
        ),
        [withDocument](const json& args) -> json { return withDocument(args, "shape_elements"); },
    });

    protocol.addTool({
        "file_formats",
        "The file extensions this FreeCAD can import and export (depends on the modules loaded).",
        {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
        [](const json&) -> json { return callPython("agentic_shapes", "formats", json::object()); },
    });

    protocol.addTool({
        "import",
        "Import a file of the project folder (STEP, IGES, BREP, STL, OBJ, DXF, SVG, IFC, glTF, "
        "...) into a document through FreeCAD's registered importer for its extension.",
        schema({{"path", prop("string", "File path relative to the project root.")}}, {"path"}),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            DocumentEngine& docs = session.documents();
            App::Document* doc = docs.resolveDocument(args.value("document", std::string()));
            const std::string relative = args["path"].get<std::string>();
            json forward = {
                {"document", doc->getName()},
                {"path", session.resolvePath(relative, true).string()},
            };
            json out = callPython("agentic_shapes", "import_file", forward);
            ChangeEntry entry;
            entry.kind = "document";
            entry.summary = "imported " + relative + " into " + doc->getName();
            entry.python = out.value("python", std::string());
            entry.detail = {{"path", relative}, {"created", out["created"]}};
            session.recordChange(std::move(entry));
            session.notifyDocument(doc->getName(), "objects");
            out.erase("python");
            out["path"] = relative;
            out["saved"] = docs.saveProjectDocuments();
            return out;
        },
    });

    protocol.addTool({
        "export",
        "Export objects to a file in the project folder; the format follows the extension "
        "(step, iges, brep, stl, obj, 3mf, ply, amf, dxf, svg, dae, ifc, gltf, csg, ...). "
        "Default objects: the document's result shapes.",
        schema(
            {
                {"path",
                 prop("string", "Output path relative to the project root; folders are created.")},
                {"objects",
                 objectList("Object names or labels; default the document's result shapes.")},
            },
            {"path"}
        ),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            App::Document* doc = session.documents().resolveDocument(
                args.value("document", std::string())
            );
            const std::string relative = args["path"].get<std::string>();
            const std::filesystem::path target = session.resolvePath(relative, false);
            std::filesystem::create_directories(target.parent_path());
            json forward = {
                {"document", doc->getName()},
                {"path", target.string()},
                {"objects", args.value("objects", json::array())},
            };
            json out = callPython("agentic_shapes", "export_file", forward);
            ChangeEntry entry;
            entry.kind = "export";
            entry.summary = "exported " + relative;
            entry.python = out.value("python", std::string());
            entry.detail = {{"path", relative}, {"objects", out["objects"]}, {"size", out["size"]}};
            session.recordChange(std::move(entry));
            session.notifyFileWritten(relative);
            out.erase("python");
            out["path"] = relative;
            return out;
        },
    });
}

}  // namespace Agentic
