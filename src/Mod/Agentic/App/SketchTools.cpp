// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Document.h>

#include "DocumentEngine.h"
#include "EngineSession.h"
#include "Protocol.h"
#include "PyBridge.h"
#include "Runtime.h"
#include "SessionTools.h"
#include "SketchTools.h"

using json = nlohmann::json;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
}

json schema(json properties, json required)
{
    properties["project"]
        = prop("string", "Name or folder of an open project; defaults to the current project.");
    properties["document"]
        = prop("string", "Document name or label; defaults to the project's active document.");
    properties["sketch"] = prop("string", "Sketch object name or label.");
    properties["details"] = prop(
        "boolean",
        "Return the full geometry and constraint lists. Default true (false for sketch_solve)."
    );
    required.push_back("sketch");
    return {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

const char* referenceDoc
    = "Element references: a geometry index (an edge), [index, \"start\"|\"end\"|\"center\"] "
      "(a point), \"xAxis\", \"yAxis\", \"origin\", \"external:N\".";

}  // namespace

namespace Agentic
{

void registerSketchTools(Protocol& protocol, Runtime& runtime)
{
    // A mutating sketch call: one transaction, logged, saved per project settings.
    auto mutate = [&runtime](const json& args, const char* function, const char* verb) -> json {
        EngineSession& session = sessionFor(runtime, args);
        DocumentEngine& docs = session.documents();
        App::Document* doc = docs.resolveDocument(args.value("document", std::string()));
        json forward = args;
        forward.erase("project");
        forward["document"] = doc->getName();
        doc->openTransaction(std::string(function) + " " + args["sketch"].get<std::string>());
        json out;
        try {
            out = callPython("agentic_sketch", function, forward);
            doc->commitTransaction();
        }
        catch (...) {
            doc->abortTransaction();
            throw;
        }
        std::string python;
        for (const auto& line : out.value("python", json::array())) {
            python += line.get<std::string>() + "\n";
        }
        ChangeEntry entry;
        entry.kind = "sketch";
        entry.summary = std::string(verb) + " in sketch " + out.value("sketch", std::string());
        entry.python = python;
        entry.detail
            = {{"document", doc->getName()}, {"sketch", out["sketch"]}, {"solver", out["solver"]}};
        session.recordChange(std::move(entry));
        session.notifyDocument(doc->getName(), "objects");
        out.erase("python");
        out["saved"] = docs.saveProjectDocuments();
        return out;
    };

    protocol.addTool({
        "sketch_get",
        "A sketch as data: geometry with indices (type, construction flag, points, radius, "
        "angles), constraints with indices (type, element references, value, name, "
        "driving), and the solver's diagnosis: solved, degrees of freedom, fully "
        "constrained, conflicting, redundant and malformed constraints (0-based indices, "
        "like everything else here), open vertices, closed wires, placement.",
        schema(json::object(), json::array()),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            App::Document* doc = session.documents().resolveDocument(
                args.value("document", std::string())
            );
            json forward = args;
            forward.erase("project");
            forward["document"] = doc->getName();
            return callPython("agentic_sketch", "sketch_get", forward);
        },
    });

    protocol.addTool({
        "sketch_add",
        "Add geometry to a sketch in sketch-plane coordinates (mm, angles in degrees): "
        "line {start, end}, circle {center, radius}, arc {center, radius, startAngle, "
        "endAngle}, point {at}, ellipse {center, majorRadius, minorRadius, angle}, bspline "
        "{poles, degree, periodic}, polyline {points, closed} (with coincident joints), "
        "rectangle {corner, width, height} (with coincident, horizontal and vertical "
        "constraints). `construction: true` makes construction geometry. Returns the new "
        "indices and the solver state.",
        schema(
            {{"geometry",
              {{"type", "array"},
               {"minItems", 1},
               {"items", {{"type", "object"}, {"required", {"type"}}}},
               {"description", "Geometry items, each with a `type`."}}}},
            {"geometry"}
        ),
        [mutate](const json& args) -> json { return mutate(args, "sketch_add", "added geometry"); },
    });

    protocol.addTool({
        "sketch_constrain",
        std::string(
            "Add constraints: Coincident, PointOnObject, Horizontal, Vertical, Parallel, "
            "Perpendicular, Tangent, Equal, Symmetric, Block, Lock, Distance, DistanceX, "
            "DistanceY, Radius, Diameter, Angle. Each is {type, elements, value?, name?, "
            "driving?}; values are numbers (mm, degrees for Angle) or strings with units. "
        ) + referenceDoc
            + " Returns the new constraint indices and the solver state, with a warning when "
              "constraints conflict.",
        schema(
            {{"constraints",
              {{"type", "array"},
               {"minItems", 1},
               {"items",
                {{"type", "object"},
                 {"properties",
                  {{"type", prop("string", "Constraint type.")},
                   {"elements", {{"type", "array"}, {"description", "Element references."}}},
                   {"value", {{"type", {"number", "string"}}, {"description", "Dimension value."}}},
                   {"name",
                    prop("string", "Constraint name, usable in expressions as Sketch.Constraints.<name>.")},
                   {"driving", prop("boolean", "false makes a reference (measuring) dimension.")}}},
                 {"required", {"type"}},
                 {"additionalProperties", false}}},
               {"description", "Constraints to add."}}}},
            {"constraints"}
        ),
        [mutate](const json& args) -> json {
            return mutate(args, "sketch_constrain", "added constraints");
        },
    });

    protocol.addTool({
        "sketch_remove",
        "Remove constraints and geometry by index (0-based, as sketch_get lists them). "
        "Removing geometry also removes its constraints and renumbers the rest.",
        schema(
            {{"constraints",
              {{"type", "array"},
               {"items", {{"type", "integer"}}},
               {"description", "Constraint indices."}}},
             {"geometry",
              {{"type", "array"},
               {"items", {{"type", "integer"}}},
               {"description", "Geometry indices."}}}},
            json::array()
        ),
        [mutate](const json& args) -> json {
            return mutate(args, "sketch_remove", "removed elements");
        },
    });

    protocol.addTool({
        "sketch_solve",
        "Recompute a sketch and report the solver's diagnosis and the sketch's status.",
        schema(json::object(), json::array()),
        [&runtime](const json& args) -> json {
            EngineSession& session = sessionFor(runtime, args);
            App::Document* doc = session.documents().resolveDocument(
                args.value("document", std::string())
            );
            json forward = args;
            forward.erase("project");
            forward["document"] = doc->getName();
            return callPython("agentic_sketch", "sketch_solve", forward);
        },
    });
}

}  // namespace Agentic
