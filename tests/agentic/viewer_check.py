# SPDX-License-Identifier: LGPL-2.1-or-later
"""End-to-end check of the agent viewer, run inside an agent-mode FreeCAD.

    xvfb-run -a build/relWithDebInfo/bin/FreeCAD --agent --agent-port 0 tests/agentic/viewer_check.py

FreeCAD runs a file argument once the GUI is initialised, so this script sees
the viewer as a person would. It drives the server in-process through
Agentic.handleRequest, checks the window, and exits with 0 when every check
passes (1 otherwise), printing one line per check on stderr.
"""

import json
import os
import sys
import tempfile
import traceback

import Agentic
import FreeCAD
import FreeCADGui

failures = []


def log(text):
    sys.__stderr__.write(text + "\n")
    sys.__stderr__.flush()


def check(name, condition, detail=""):
    log(f"{'ok  ' if condition else 'FAIL'} {name}{(': ' + str(detail)) if detail and not condition else ''}")
    if not condition:
        failures.append(name)


def call(tool, **arguments):
    request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
               "params": {"name": tool, "arguments": arguments}}
    response = json.loads(Agentic.handleRequest(json.dumps(request)))
    result = response.get("result")
    if result is None:
        raise RuntimeError(f"{tool}: {response.get('error')}")
    if result["isError"]:
        raise RuntimeError(f"{tool}: {result['content'][0]['text']}")
    return result["structuredContent"]


def settle():
    for _ in range(5):
        FreeCADGui.updateGui()


def mdi_pages():
    from PySide6.QtWidgets import QMdiArea

    mdi = FreeCADGui.getMainWindow().findChild(QMdiArea)
    pages = []
    for sub in mdi.subWindowList():
        pages.append(sub.windowTitle())
    active = mdi.activeSubWindow()
    return mdi, pages, active.windowTitle() if active else None


def stage_v0(root):
    from PySide6.QtWidgets import QMdiArea

    window = FreeCADGui.getMainWindow()
    check("agent mode is on", Agentic.isAgentMode())
    from PySide6.QtWidgets import QLabel

    label = window.statusBar().findChild(QLabel, "Agentic_ModeLabel")
    check("status bar says agent mode", label is not None and label.text().startswith("Agent mode"),
          label.text() if label else None)
    check("menu bar hidden", not window.menuBar().isVisible())
    mdi, pages, _ = mdi_pages()
    check("3D views are tabbed", mdi.viewMode() == QMdiArea.TabbedView)
    check("no page without a document (Start page closed)", pages == [], pages)

    call("project_open", path=root, create=True)
    call("document_new", name="First")
    call("object_create", type="Part::Box", name="Box")
    call("document_new", name="Second")
    settle()
    _, pages, active = mdi_pages()
    check("both documents have a page", len(pages) == 2, pages)
    check("the agent's latest document is in front", active is not None and active.startswith("Second"), active)
    call("object_create", document="First", type="Part::Cylinder", name="Cyl")
    settle()
    _, _, active = mdi_pages()
    check("the viewer follows the document the agent works on", active is not None and active.startswith("First"), active)


def state():
    import AgenticGui

    settle()
    return json.loads(AgenticGui.viewerState())


def stage_v1(tmp):
    first = os.path.join(tmp, "project")
    second = os.path.join(tmp, "other")
    viewer = state()
    check("viewerState reports the current project", viewer["project"] == os.path.realpath(first), viewer["project"])
    tools = [row["tool"] for row in viewer["activity"]]
    check("activity lists the tool calls", tools[-3:] == ["object_create", "document_new", "object_create"], tools)
    check("activity summarises arguments", 'type="Part::Cylinder"' in viewer["activity"][-1]["arguments"],
          viewer["activity"][-1]["arguments"])
    kinds = [row["kind"] for row in viewer["changes"]]
    check("changes list the project's mutations", kinds.count("object") == 2, kinds)

    try:
        call("object_get", object="NoSuchObject")
    except RuntimeError:
        pass
    check("a failed call shows as an error", state()["activity"][-1]["ok"] is False)

    call("file_write", path="script.py", content="x = 1\n" * 50)
    check("file contents are summarised by length", "<300 chars>" in state()["activity"][-1]["arguments"],
          state()["activity"][-1]["arguments"])

    call("project_open", path=second, create=True)
    viewer = state()
    check("a new project starts with its own changes", viewer["changes"] == [], viewer["changes"])
    call("file_write", path="b.txt", content="b")
    call("project_open", path=first)
    viewer = state()
    kinds = [row["kind"] for row in viewer["changes"]]
    check("switching back restores the first project's changes", kinds.count("object") == 2 and kinds[-1] == "file",
          kinds)


def find_node(nodes, name):
    for node in nodes:
        if node.get("name") == name:
            return node
        found = find_node(node["children"], name)
        if found:
            return found
    return None


def stage_v2(tmp):
    call("project_open", path=os.path.join(tmp, "tree"), create=True)
    call("document_new", name="Tree")
    call("object_create", type="Part::Box", name="Box", properties={"Length": 10})
    call("object_create", type="Part::Cylinder", name="Cyl", properties={"Radius": 2, "Height": 20})
    call("object_create", type="Part::Cut", name="Cut")
    viewer = state()
    labels = [doc["label"] for doc in viewer["model"]]
    check("the model shows the project's documents", labels == ["Tree"], labels)
    cut = find_node(viewer["model"], "Cut")
    check("an object in error is marked", cut is not None and cut["error"], cut)
    check("the last touched object is selected", viewer["selected"] == "Cut", viewer["selected"])

    call("object_set", object="Cut", properties={"Base": "Box", "Tool": "Cyl"})
    viewer = state()
    cut = find_node(viewer["model"], "Cut")
    children = sorted(child.get("name") for child in cut["children"]) if cut else None
    check("inputs are nested under the boolean, as in the classic tree", children == ["Box", "Cyl"], children)
    check("the fixed object is no longer in error", cut is not None and not cut["error"], cut)
    top = [node.get("name") for node in viewer["model"][0]["children"]]
    check("claimed objects are not repeated at the top", top == ["Cut"], top)
    names = {row["name"] for row in viewer["properties"]["rows"]}
    check("properties follow the agent's last touched object",
          viewer["properties"]["object"] == "Cut" and {"Base", "Tool"} <= names, viewer["properties"]["object"])
    check("view-provider properties are shown in their own group",
          any(row["group"].startswith("View") for row in viewer["properties"]["rows"]))

    call("object_set", object="Box", properties={"Height": {"expression": "Box.Length / 2"}})
    rows = {row["name"]: row for row in state()["properties"]["rows"] if not row["group"].startswith("View")}
    check("a value is shown with its unit", rows.get("Length", {}).get("value") == "10.00 mm", rows.get("Length"))
    check("an expression is shown next to its value", rows.get("Height", {}).get("expression", "").endswith("Length / 2"),
          rows.get("Height"))

    from PySide6.QtWidgets import QTreeWidget, QTreeWidgetItemIterator

    tree = FreeCADGui.getMainWindow().findChild(QTreeWidget, "Agentic_ModelPanel")
    iterator = QTreeWidgetItemIterator(tree)
    while iterator.value():
        item = iterator.value()
        if item.data(0, 0x0100 + 1) == "Cyl":
            tree.itemClicked.emit(item, 0)
            break
        iterator += 1
    viewer = state()
    check("clicking an object shows its properties", viewer["properties"]["object"] == "Cyl",
          viewer["properties"]["object"])
    check("inspection does not touch the session's selection", call("selection_get")["selection"] == [])


def stage_v3(tmp):
    viewer = state()
    check("the status bar shows the server URL", viewer["status"]["server"].startswith("http://127.0.0.1:"),
          viewer["status"]["server"])
    check("the status bar shows the project", viewer["status"]["project"] == "Project: tree",
          viewer["status"]["project"])
    call("object_create", type="Part::Cut", name="Broken")
    viewer = state()
    check("the status bar shows objects in error", "1 object(s) in error" in viewer["status"]["document"],
          viewer["status"]["document"])
    call("object_delete", object="Broken")
    check("the status bar clears when the error is gone", state()["status"]["document"].endswith("- ok"),
          state()["status"]["document"])

    call("python_run", code="import FreeCAD\nFreeCAD.Console.PrintWarning('look out\\n')\nFreeCAD.Console.PrintError('bad thing\\n')\n")
    viewer = state()
    levels = [(m["level"], m["text"]) for m in viewer["messages"]]
    check("messages show the last call's warnings and errors",
          ("warning", "look out") in levels and ("error", "bad thing") in levels, levels)
    check("the code pane shows the Python the agent ran", "look out" in viewer["code"]["text"],
          viewer["code"]["title"])
    call("version")
    check("messages are per call", state()["messages"] == [], state()["messages"])

    call("file_write", path="make.py", content="a = 1\nb = 2\nc = 3\n")
    call("file_write", path="make.py", edits=[{"oldText": "b = 2", "newText": "b = 20\nb2 = 21"}])
    viewer = state()
    check("the code pane shows the file the agent wrote", viewer["code"]["title"] == "make.py", viewer["code"]["title"])
    check("changed lines are highlighted", viewer["code"]["highlighted"] == [2, 3], viewer["code"]["highlighted"])

    from PySide6.QtWidgets import QToolButton

    pause = FreeCADGui.getMainWindow().findChild(QToolButton, "Agentic_Pause")
    pause.setChecked(True)
    check("pause is shown in the state", state()["paused"])
    try:
        call("object_create", type="Part::Box", name="WhilePaused")
        refused = False
    except RuntimeError as err:
        refused = "paused the agent" in str(err)
    check("while paused every tool call fails, saying why", refused)
    pause.setChecked(False)
    created = call("object_create", type="Part::Box", name="AfterPause")
    check("after resuming calls work again", created["object"] == "AfterPause", created["object"])
    check("nothing was created while paused",
          "WhilePaused" not in [o["name"] for o in call("object_list")["objects"]])


def stage_v4(tmp):
    from PySide6.QtGui import QAction  # Qt 6 moved QAction to QtGui
    from PySide6.QtWidgets import QToolButton, QTreeWidget, QTreeWidgetItemIterator

    window = FreeCADGui.getMainWindow()
    viewer = state()
    names = sorted(viewer["panels"])
    check("every panel has a toggle", names == ["Activity", "Changes", "Code", "Messages", "Model", "Properties"],
          names)
    check("every panel starts open", all(p["visible"] for p in viewer["panels"].values()))

    toggle = window.findChild(QToolButton, "Agentic_Activity_Toggle")
    toggle.setChecked(False)
    settle()
    check("a toggle closes its panel", not state()["panels"]["Activity"]["visible"])
    call("version")
    panel = state()["panels"]["Activity"]
    check("a hidden panel with new content shows a dot", panel["alert"] == "new" and panel["toggle"].startswith("\u25cf"),
          panel)
    try:
        call("object_get", object="NoSuchObject")
    except RuntimeError:
        pass
    check("a failed call turns the dot red", state()["panels"]["Activity"]["alert"] == "error",
          state()["panels"]["Activity"])
    params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Agentic/Viewer")
    check("the layout is saved", params.GetBool("ActivityVisible", True) is False and params.GetString("State") != "")
    toggle.setChecked(True)
    settle()
    panel = state()["panels"]["Activity"]
    check("opening the panel clears the dot", panel["visible"] and panel["alert"] == "none", panel)
    check("nothing opens by itself", not state()["panels"]["Activity"]["alert"] == "new")

    window.findChild(QToolButton, "Agentic_Code_Toggle").setChecked(False)
    settle()
    view_only = window.findChild(QAction, "Agentic_ViewOnly")
    view_only.setChecked(True)
    settle()
    viewer = state()
    check("View only hides every panel", viewer["viewOnly"] and not any(p["visible"] for p in viewer["panels"].values()))
    view_only.setChecked(False)
    settle()
    viewer = state()
    shown = sorted(name for name, p in viewer["panels"].items() if p["visible"])
    check("leaving View only restores the same set", shown == ["Activity", "Changes", "Messages", "Model", "Properties"],
          shown)
    window.findChild(QToolButton, "Agentic_Code_Toggle").setChecked(True)

    # A distant object, so that fitting one object differs from fitting all;
    # the double-click goes to a visible object (fitting a hidden one is a no-op).
    call("object_create", type="Part::Sphere", name="Far", properties={"Placement": {"base": [300, 0, 0]}})
    agent = call("camera_set", view="isometric", fit="all")
    tree = window.findChild(QTreeWidget, "Agentic_ModelPanel")
    iterator = QTreeWidgetItemIterator(tree)
    while iterator.value():
        item = iterator.value()
        if item.data(0, 0x0100 + 1) == "AfterPause":
            tree.itemDoubleClicked.emit(item, 0)
            break
        iterator += 1
    settle()
    person = call("camera_get")
    check("a double-click fits the person's view to the object", person["inventor"] != agent["inventor"])
    shot = call("view_capture", width=64, height=48, inline=False)
    same = all(abs(a - b) < 1e-5 for a, b in zip(shot["camera"]["viewDirection"], agent["viewDirection"]))
    check("captures still render from the agent's camera", same, shot["camera"]["viewDirection"])
    check("the session's selection is left as it was", call("selection_get")["selection"] == [])
    settle()
    check("every panel is open again for the next run", all(p["visible"] for p in state()["panels"].values()))


def main():
    tmp = tempfile.mkdtemp(prefix="agentic-viewer-")
    try:
        stage_v0(os.path.join(tmp, "project"))
        stage_v1(tmp)
        stage_v2(tmp)
        stage_v3(tmp)
        stage_v4(tmp)
    except Exception:
        log("FAIL exception\n" + traceback.format_exc())
        failures.append("exception")
    log(f"viewer_check: {len(failures)} failure(s)")
    os._exit(1 if failures else 0)


main()
