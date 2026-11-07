// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <filesystem>

#include <App/Document.h>

#include <Mod/Agentic/App/DocumentEngine.h>
#include <Mod/Agentic/App/EngineSession.h>
#include <Mod/Agentic/App/Protocol.h>
#include <Mod/Agentic/App/PyBridge.h>
#include <Mod/Agentic/App/Runtime.h>
#include <Mod/Agentic/App/SessionTools.h>

#include "ViewTools.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{

json prop(const char* type, const std::string& description)
{
    return {{"type", type}, {"description", description}};
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

json cameraProperties()
{
    return {
        {"view",
         {{"type", "string"},
          {"enum",
           {"isometric", "dimetric", "trimetric", "front", "rear", "top", "bottom", "left", "right"}},
          {"description", "A named orientation."}}},
        {"direction", vector3("View direction (from the camera into the scene).")},
        {"orientation",
         {{"type", "object"},
          {"description", "{quaternion: [x,y,z,w]} or {axis: [x,y,z], angle: degrees}."}}},
        {"inventor", prop("string", "A camera as returned by camera_get.inventor.")},
        {"type",
         {{"type", "string"},
          {"enum", {"Orthographic", "Perspective"}},
          {"description", "Camera type."}}},
        {"fit",
         {{"type", {"string", "array", "boolean"}},
          {"description", "\"all\" (or true), \"selection\", or a list of object names to fit."}}},
    };
}

}  // namespace

namespace Agentic
{

void registerViewTools(Protocol& protocol, Runtime& runtime)
{
    auto forward = [&runtime](const json& args) {
        EngineSession& session = sessionFor(runtime, args);
        App::Document* doc = session.documents().resolveDocument(args.value("document", std::string()));
        json out = args;
        out.erase("project");
        out["document"] = doc->getName();
        return std::make_pair(&session, out);
    };

    protocol.addTool({
        "camera_get",
        "The 3D view's camera: type, view and up directions, orientation quaternion, the "
        "Inventor camera string (to restore it with camera_set), view size, axis cross.",
        schema(json::object()),
        [forward](const json& args) -> json {
            return callPython("agentic_view", "camera_get", forward(args).second);
        },
    });

    protocol.addTool({
        "camera_set",
        "Set the 3D view's camera: a named orientation, a view direction, an orientation, an "
        "Inventor camera string, the camera type, and fit to all, the selection or listed "
        "objects. This camera is the reference: captures without their own view use it.",
        schema(cameraProperties()),
        [forward](const json& args) -> json {
            return callPython("agentic_view", "camera_set", forward(args).second);
        },
    });

    protocol.addTool({
        "view_options",
        "Options of the 3D view: axis cross, corner cross, camera type, background colour, and "
        "the draw style (as-is, points, wireframe, hidden-line, no-shading, shaded, flat-lines).",
        schema({
            {"axisCross", prop("boolean", "Show the axis cross at the origin.")},
            {"cornerCross", prop("boolean", "Show the orientation cross in the corner.")},
            {"cameraType",
             {{"type", "string"},
              {"enum", {"Orthographic", "Perspective"}},
              {"description", "Camera type."}}},
            {"background",
             {{"type", "array"},
              {"items", {{"type", "number"}}},
              {"minItems", 3},
              {"maxItems", 3},
              {"description", "Background colour [r, g, b] in 0..1."}}},
            {"drawStyle",
             {{"type", "string"},
              {"enum",
               {"as-is", "points", "wireframe", "hidden-line", "no-shading", "shaded", "flat-lines"}},
              {"description", "Draw style of the view."}}},
        }),
        [forward](const json& args) -> json {
            return callPython("agentic_view", "view_options", forward(args).second);
        },
    });

    protocol.addTool({
        "view_capture",
        "Render the 3D view offscreen to a PNG and return it as an image. Uses the session "
        "camera unless the capture gives its own view, direction, orientation or camera, "
        "which is then restored afterwards (a capture with its own view fits all unless "
        "`fit` says otherwise). `objects` shows only those objects in the picture. The "
        "image is also written to `path` in the project (default .agentic/captures/).",
        [] {
            json props = cameraProperties();
            props["width"] = {
                {"type", "integer"},
                {"minimum", 16},
                {"maximum", 8192},
                {"description", "Width in pixels. Default 800."}
            };
            props["height"] = {
                {"type", "integer"},
                {"minimum", 16},
                {"maximum", 8192},
                {"description", "Height in pixels. Default 600."}
            };
            props["background"] = prop(
                "string",
                "White, Black, Transparent, Current, or a colour name. Default White."
            );
            props["objects"] = {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Show only these objects."}
            };
            props["path"] = prop("string", "Where to keep the PNG, relative to the project root.");
            props["inline"] = prop("boolean", "Return the image in the response. Default true.");
            return schema(props);
        }(),
        [forward](const json& args) -> json {
            auto [session, out] = forward(args);
            static int counter = 0;
            const std::string relative = args.value(
                "path",
                ".agentic/captures/capture-" + std::to_string(++counter) + ".png"
            );
            const fs::path target = session->resolvePath(relative, false);
            fs::create_directories(target.parent_path());
            out["path"] = target.string();
            json result = callPython("agentic_view", "capture", out);
            result["path"] = relative;
            if (!relative.starts_with(".")) {
                session->notifyFileWritten(relative);
            }
            return result;
        },
    });

    protocol.addTool({
        "drawing_export",
        "Export a TechDraw page to SVG or PDF in the project folder.",
        schema(
            {
                {"page", prop("string", "Page object name or label.")},
                {"path",
                 prop("string", "Output path ending in .svg or .pdf, relative to the project root.")},
            },
            {"page", "path"}
        ),
        [forward](const json& args) -> json {
            auto [session, out] = forward(args);
            const std::string relative = args["path"].get<std::string>();
            const fs::path target = session->resolvePath(relative, false);
            fs::create_directories(target.parent_path());
            out["path"] = target.string();
            json result = callPython("agentic_view", "drawing_export", out);
            ChangeEntry entry;
            entry.kind = "export";
            entry.summary = "exported drawing " + args["page"].get<std::string>() + " to " + relative;
            entry.python = result.value("python", std::string());
            entry.detail = {{"path", relative}, {"page", result["page"]}};
            session->recordChange(std::move(entry));
            session->notifyFileWritten(relative);
            result.erase("python");
            result["path"] = relative;
            return result;
        },
    });
}

}  // namespace Agentic
