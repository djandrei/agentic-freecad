#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""End-to-end scenario through every tool group, over a real transport.

    python3 tests/agentic/e2e.py stdio build/relWithDebInfo/bin/FreeCAD
    python3 tests/agentic/e2e.py stdio build/relWithDebInfo/bin/FreeCADCmd
    python3 tests/agentic/e2e.py http  build/relWithDebInfo/bin/FreeCAD
    xvfb-run -a python3 tests/agentic/e2e.py viewer build/relWithDebInfo/bin/FreeCAD

`stdio` and `http` run headless (offscreen Qt platform); `viewer` runs the
agent viewer and needs a display. The GUI tool groups are checked when the
server reports the GUI layer, and checked absent when it does not
(FreeCADCmd). Everything stays offline: the default addon registry is
turned off through preferences_set for the run and restored afterwards.
"""

import os
import sys
import tempfile
import traceback
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mcp_client import HttpClient, StdioClient, ToolError  # noqa: E402

failures = []


def check(name, condition, detail=""):
    print(f"{'ok  ' if condition else 'FAIL'} {name}{(': ' + str(detail)[:300]) if detail and not condition else ''}",
          flush=True)
    if not condition:
        failures.append(name)


def expect_error(fc, tool, **arguments):
    try:
        fc.call(tool, **arguments)
    except ToolError as err:
        return str(err)
    return None


def scenario(fc, root):
    status = fc.call("status")
    gui = status["gui"]
    tools = set(fc.tools())
    gui_tools = {"command_run", "task_get", "camera_set", "view_capture", "object_edit", "drawing_export"}
    if gui:
        check("GUI tool groups are present", gui_tools <= tools, gui_tools - tools)
    else:
        check("GUI tool groups are absent without the GUI layer", not (gui_tools & tools), gui_tools & tools)
    check("the server announces instructions", "loop" in (fc.server.get("instructions") or ""))

    # Preferences: stay offline for the addon catalog.
    prefs = "BaseApp/Preferences/Mod/Agentic"
    previous = fc.call("preferences_get", path=prefs)["values"].get("UseDefaultRegistry", {}).get("value", True)
    fc.call("preferences_set", path=prefs, key="UseDefaultRegistry", value=False)
    try:
        run_groups(fc, root, gui)
    finally:
        fc.call("preferences_set", path=prefs, key="UseDefaultRegistry", value=previous)


def run_groups(fc, root, gui):
    # Project and files.
    info = fc.call("project_open", path=root, create=True)
    check("project_open creates the project", info["root"] == os.path.realpath(root))
    written = fc.call("file_write", path="scripts/plate.py", content=(
        "import Part\n"
        "plate = doc.addObject('Part::Box', 'Plate')\n"
        "plate.Length, plate.Width, plate.Height = 60, 40, 5\n"
        "doc.recompute()\n"
        "plate.Shape.Volume\n"
    ))
    check("file_write returns a version", bool(written["version"]))
    check("file escapes are refused", expect_error(fc, "file_read", path="../../etc/passwd") is not None)

    # Documents, objects, Python.
    fc.call("document_new", name="Plate")
    run = fc.call("python_run", file="scripts/plate.py")
    check("python_run returns the trailing value", abs(run["value"] - 12000.0) < 1e-6, run.get("value"))
    check("python_run reports created objects", run["objects"]["created"] == [{"document": "Plate", "object": "Plate"}])
    fc.call("object_create", type="Part::Cylinder", name="Hole",
            properties={"Radius": 4, "Height": 20, "Placement": {"base": [30, 20, -5]}})
    cut = fc.call("object_create", type="Part::Cut", name="Drilled", properties={"Base": "Plate", "Tool": "Hole"})
    check("a boolean recomputes without error", not cut["recompute"]["hasError"], cut["recompute"])
    got = fc.call("object_get", object="Drilled", properties=["Base"])
    check("object_get resolves links", got["properties"]["Base"]["value"] == {"object": "Plate"})
    fc.call("object_set", object="Plate", properties={"Height": {"expression": "Width / 8"}})
    check("expressions drive properties", fc.call("geometry_info", objects=["Plate"])["objects"][0]["boundBox"]["size"][2] == 5.0)
    undone = fc.call("document_undo")
    check("undo reverses one call", undone["undone"] and undone["transaction"].startswith("object_set"))
    fc.call("document_redo")

    # Geometry, sub-elements, export and import.
    facts = fc.call("geometry_info")
    expected = 60 * 40 * 5 - 3.141592653589793 * 16 * 5
    check("geometry_info measures the result", abs(facts["objects"][0]["volume"] - expected) < 1e-3,
          facts["objects"][0].get("volume"))
    top = fc.call("shape_elements", object="Drilled", types=["Face"], direction=[0, 0, 1])
    check("shape_elements finds the top face", len(top["elements"]) == 1 and top["elements"][0]["normal"] == [0.0, 0.0, 1.0])
    check("geometry_check passes", fc.call("geometry_check")["ok"])
    exported = fc.call("export", path="out/plate.step")
    check("export writes STEP", exported["size"] > 0)
    fc.call("document_new", name="Imported")
    imported = fc.call("import", path="out/plate.step")
    check("import reads it back", len(imported["created"]) >= 1)
    fc.call("document_close", document="Imported")

    # Sketching.
    fc.call("object_create", document="Plate", type="Sketcher::SketchObject", name="Profile")
    fc.call("sketch_add", sketch="Profile", geometry=[{"type": "rectangle", "corner": [0, 0], "width": 10, "height": 6}])
    sketched = fc.call("sketch_constrain", sketch="Profile", constraints=[
        {"type": "Coincident", "elements": [[0, "start"], "origin"]},
        {"type": "DistanceX", "elements": [0], "value": 10},
        {"type": "DistanceY", "elements": [1], "value": 6},
    ])
    check("a sketch can be fully constrained", sketched["solver"]["fullyConstrained"], sketched["solver"])

    # Documentation.
    check("docs_api answers", fc.call("docs_api", name="Part.Shape.makeFillet")["owner"] == "Part.Shape")
    check("docs_search finds the API", any(h["source"] == "api" for h in fc.call("docs_search", query="fillet")["results"]))

    # Addons, from a local registry.
    archive = os.path.join(root, "..", "demo-addon.zip")
    with zipfile.ZipFile(archive, "w") as handle:
        handle.writestr("Demo-main/Init.py", "")
        handle.writestr("Demo-main/README.md", "# Demo\n")
    registry = os.path.join(root, "..", "registry.json")
    with open(registry, "w") as handle:
        handle.write('{"Demo": [{"repository": "https://example.invalid/d", "zip_url": "file://'
                     + os.path.realpath(archive) + '"}]}')
    fc.call("addon_registry_add", location=os.path.realpath(registry))
    check("the catalog lists the addon", [a["id"] for a in fc.call("addon_catalog")["addons"]] == ["Demo"])
    check("an addon installs", fc.call("addon_install", id="Demo")["method"] == "zip")

    # Logs and changes.
    check("log_get answers", "messages" in fc.call("log_get", limit=5))
    kinds = {c["kind"] for c in fc.call("changes_get", limit=500)["changes"]}
    check("the change log covers the scenario", {"file", "python", "object", "property", "sketch", "export", "addon"} <= kinds,
          kinds)

    if gui:
        run_gui_groups(fc, root)

    # Pause cannot be pressed from here; resources and prompts instead.
    resources = [r["uri"] for r in fc.request("resources/list")["resources"]]
    check("resources are listed", "agentic://status" in resources)
    prompt = fc.request("prompts/get", {"name": "freecad-object-model"})
    check("prompts render", "recompute" in prompt["messages"][0]["content"]["text"])


def run_gui_groups(fc, root):
    fc.call("workbench_activate", name="PartWorkbench")
    ran = fc.call("command_run", name="Part_Box")
    check("command_run records the command's Python", any("addObject" in line for line in ran["python"]))
    fc.call("selection_set", items=[{"object": "Drilled", "sub": ["Face1"]}])
    selection = fc.call("selection_get")["selection"]
    check("selection by sub-element", [s["subElements"] for s in selection if s["object"] == "Drilled"] == [["Face1"]],
          selection)
    fc.call("selection_clear")
    panel = fc.call("command_run", name="Part_Primitives")["taskPanel"]
    check("a task panel is described", panel is not None and any(f["id"] == "PrimitiveTypeCB" for f in panel["fields"]))
    fc.call("task_reject")
    fc.call("object_set", object="Drilled", view={"Transparency": 30})
    check("view properties are set", fc.call("object_get", object="Drilled", properties=["Transparency"])["view"]["Transparency"]["value"] == 30)
    fc.call("camera_set", view="isometric", fit="all")
    shot = fc.call("view_capture", width=200, height=150)
    check("view_capture returns an image", len(fc.images) == 1 and fc.images[0]["mimeType"] == "image/png")
    check("the capture is kept in the project", os.path.exists(os.path.join(root, shot["path"])))
    edited = fc.call("object_edit", object="Profile")
    check("object_edit opens the sketch editor", edited["editing"] and edited["taskPanel"] is not None)
    fc.call("object_edit_end")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("stdio", "http", "viewer"):
        raise SystemExit(__doc__)
    mode, binary = sys.argv[1], sys.argv[2]
    root = os.path.join(tempfile.mkdtemp(prefix="agentic-e2e-"), "project")
    env = {"LIBGL_ALWAYS_SOFTWARE": "1"}
    try:
        if mode == "stdio":
            client = StdioClient([binary, "--agent-stdio"], env=env)
        elif mode == "http":
            env["QT_QPA_PLATFORM"] = "offscreen"
            client = HttpClient.launch([binary, "--agent", "--agent-headless", "--agent-port", "0"], env=env)
        else:
            client = HttpClient.launch([binary, "--agent", "--agent-port", "0"], env=env)
        with client as fc:
            scenario(fc, root)
    except Exception:
        print("FAIL exception\n" + traceback.format_exc(), flush=True)
        failures.append("exception")
    print(f"e2e {mode} {os.path.basename(binary)}: {len(failures)} failure(s)", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
