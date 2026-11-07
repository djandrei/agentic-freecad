# SPDX-License-Identifier: LGPL-2.1-or-later
"""Parity audit: how the agent reaches what each classic-GUI command does.

Run in the classic GUI (not agent mode), so every workbench builds its menus:

    xvfb-run -a build/relWithDebInfo/bin/FreeCAD tests/agentic/parity_audit.py

For every shipped workbench the script activates it, walks its menu bar and
toolbars (a command's QAction is named after the command), and assigns each
command the agent's route to the same result. The table is written to
doc/agentic/parity.md (AGENTIC_PARITY_MD overrides the path).

Routes, in the order the rules are tried:
- a specific tool when the GUI command is a window onto something a tool
  covers (camera, selection, documents, undo, preferences, macros, sketch
  geometry and constraints, measuring, import and export, edit mode);
- excluded, for commands about the classic window itself (toolbars, docks,
  the Start page, help, about, quitting), which have no effect on the model;
- otherwise command_run, with the task tools when the command opens a task
  panel and the dialog policy answering any message box.

The rules are by command name and group. They say which route exists; they
do not run the commands. Commands assigned command_run have been exercised
through representatives (Part_Box, Part_Cut, Part_Primitives, Std_Expressions
listing) in TestAgenticGui, not each one individually.
"""

import os
import re
import sys
import traceback

import FreeCAD
import FreeCADGui as Gui

# (route, reason, pattern on the command name). First match wins.
RULES = [
    ("excluded", "the classic window itself, not the model",
     r"^(Std_(About|Quit|WhatsThis|OnlineHelp|FreeCADWebsite|FreeCADUserHub|FreeCADPowerUserHub|"
     r"FreeCADForum|FreeCADFAQ|ReportBug|DlgCustomize|ToolBarMenu|DockViewMenu|ViewStatusBar|"
     r"UserEditMode|MainFullscreen|ViewFullscreen|MDI|Window|ActivateNextWindow|ActivatePrevWindow|"
     r"TileWindows|CascadeWindows|CloseAllWindows|ViewDock|ViewUndock|ViewFloat|ViewCreate|"
     r"ViewDockUndockFullscreen|RestartInSafeMode|ToggleNavigation|Tooltips|SendToPythonConsole|"
     r"TreeViewActions|TreeCollapse|TreeExpand|TreeSelectAllInstances|TreeSingleDocument|"
     r"TreeMultiDocument|TreeSyncView|TreeSyncSelection|TreeSyncPlacement|TreePreSelection|"
     r"TreeRecordSelection|TreeDrag|TreeCollapseDocument|RecentFiles|RecentMacros|Unsplit|"
     r"ViewIvStereo\w*)|Start_\w+)$"),
    ("workbench_activate", "switching workbench", r"^Std_Workbench$"),
    ("addon_catalog / addon_install / addon_remove", "addons", r"^Std_AddonMgr$"),
    ("changes_get (the change log records the Python of every mutation)", "macro recording",
     r"^Std_DlgMacroRecord$"),
    ("object_dependencies", "dependency graph", r"^Std_(DependencyGraph|ExportDependencyGraph)$"),
    ("document_new / document_open / document_save / document_close",
     "documents", r"^Std_(New|Open|Save|SaveAs|SaveCopy|SaveAll|Revert|CloseActiveWindow|"
                  r"Recompute|RefreshRecompute|Print|PrintPreview|PrintPdf|MergeProjects)$"),
    ("import / export / drawing_export", "files in other formats",
     r"^(Std_(Import|Export)|.*_(Import|Export)\w*)$"),
    ("document_undo / document_redo", "undo", r"^Std_(Undo|Redo)$"),
    ("python_run (macros are Python)", "macros", r"^Std_(Macro\w*|DlgMacroExecute\w*|DlgMacro\w*|PythonHelp)$"),
    ("preferences_get / preferences_set", "preferences", r"^Std_(DlgPreferences|PreferencePacks|DlgParameter)$"),
    ("object_set (view properties)", "appearance", r"^Std_(ToggleTransparency|SetAppearance|RandomColor)$"),
    ("python_run only (no dedicated tool)", "display aids without a tool",
     r"^Std_(ToggleClipPlane|ProjectUtil|SceneInspector|DemoMode|TextureMapping)$"),
    ("camera_set / view_options / view_capture", "the 3D view",
     r"^Std_(View\w*|Orthographic\w*|Perspective\w*|BoxZoom|BoxElementSelection|DrawStyle|AxisCross|"
     r"ToggleVisibility|ShowSelection|HideSelection|SelectVisibleObjects|ToggleSelectability|ShowObjects|"
     r"HideObjects|ToggleObjects|ToggleGroupVisibility|AlignToSelection|Zoom\w*)$"),
    ("selection_set / selection_get / selection_clear", "selection",
     r"^Std_(SelectAll|Selection\w*|BoxSelection|TreeSelection|LinkSelect\w*|SelBack|SelForward|"
     r"Copy|Paste|Cut|Delete|Duplicate\w*|Refresh)$"),
    ("object_edit / object_set (Placement)", "edit mode and placement",
     r"^(Std_(Edit|TransformManip|Placement|SetEdit\w*|ToggleEditMode)|Part_EditAttachment|"
     r"PartDesign_EditAttachment)$"),
    ("object_get / object_set", "properties and expressions",
     r"^Std_(Properties|Expressions|DlgExpressions|RenameObject|ExpressionsEditor)$"),
    ("sketch_add / sketch_constrain / sketch_remove / sketch_get", "interactive sketching",
     r"^Sketcher_(Create|Constrain|CompCreate|CompConstrain|Delete|SelectConstraints|Select\w+|"
     r"Trimming|Extend|Split|Join|Symmetry|Copy|Clone|Move|Offset|Rotate|Scale|Translate|"
     r"RemoveAxesAlignment|ToggleConstruction|ToggleDrivingConstraint|ToggleActiveConstraint|"
     r"RestoreInternalAlignmentGeometry|BSpline|CompBSpline|CompModifyKnotMultiplicity|"
     r"ArcOverlay|CarbonCopy|External|Projection|Intersection|Rectangular|Fillet|Chamfer|"
     r"Dimension|CompDimension|Snap|Grid|RenderingOrder|Show|Hide)\w*$"),
    ("object_edit / object_edit_end", "opening and leaving sketches",
     r"^Sketcher_(NewSketch|EditSketch|LeaveSketch|StopOperation|ViewSketch|ViewSection|MapSketch|"
     r"ReorientSketch|ValidateSketch|MirrorSketch|MergeSketches)$"),
    ("geometry_info / shape_elements / geometry_check", "measuring and checking",
     r"^(Part_(CheckGeometry|Measure\w*)|Measure_\w+|Std_Measure\w*|Inspection_\w+)$"),
]

ROUTE_COMMAND = "command_run (+ task_get / task_set / task_accept when it opens a panel)"


def classify(name):
    for route, reason, pattern in RULES:
        if re.match(pattern, name):
            return route, reason
    return ROUTE_COMMAND, "runs as a command"


def menu_commands():
    from PySide6.QtWidgets import QMenu

    names = []

    def walk(menu):
        for action in menu.actions():
            sub = action.menu()
            if sub is not None:
                walk(sub)
            elif action.objectName() and not action.isSeparator():
                names.append(action.objectName())

    for action in Gui.getMainWindow().menuBar().actions():
        if action.menu() is not None:
            walk(action.menu())
    return names


def _tool(tool, **arguments):
    import json

    import Agentic

    request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": tool, "arguments": arguments}}
    result = json.loads(Agentic.handleRequest(json.dumps(request)))["result"]
    if result["isError"]:
        raise RuntimeError(result["content"][0]["text"].splitlines()[0])
    return result["structuredContent"]


dialogs = []


def audit():
    # BIM shows a welcome dialog on its first activation, delayed until after
    # the activating call returns. This audit runs in the classic GUI, where
    # the dialog policy only acts during a tool call, so the dialog is
    # declined up front the way answering it would record.
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/BIM").SetBool("FirstTime", False)
    rows = {}
    workbenches = sorted(Gui.listWorkbenches())
    skipped = []
    for wb_name in workbenches:
        if wb_name in ("NoneWorkbench", "TestWorkbench"):
            continue
        try:
            # Through the tool, as an agent would: the dialog policy answers
            # what a workbench asks on first activation (BIM's welcome dialog).
            activated = _tool("workbench_activate", name=wb_name)
            dialogs.extend(
                f"{wb_name}: {d.get('title')!r} answered {d.get('answer')!r}" for d in activated.get("dialogs", [])
            )
            Gui.updateGui()
            wb = Gui.getWorkbench(wb_name)
            names = set(menu_commands())
            for items in (wb.getToolbarItems() or {}).values():
                names.update(i for i in items if i and i != "Separator")
        except Exception as err:
            skipped.append(f"{wb_name}: {err}")
            continue
        known = set(Gui.listCommands())
        for name in names:
            if name not in known:
                continue  # placeholders and Qt's own actions
            rows.setdefault(name, set()).add(wb_name)
    return rows, skipped


def write(rows, skipped, path):
    by_route = {}
    lines = []
    for name in sorted(rows, key=str.lower):
        route, reason = classify(name)
        by_route.setdefault(route, 0)
        by_route[route] += 1
        cmd = Gui.Command.get(name)
        menu = ""
        try:
            menu = (cmd.getInfo() or {}).get("menuText", "").replace("&", "").replace("|", "/")
        except Exception:
            pass
        wbs = ", ".join(sorted(w.replace("Workbench", "") for w in rows[name]))
        lines.append(f"| `{name}` | {menu} | {wbs} | {route} |")

    out = [
        "# Parity audit",
        "",
        "Generated by `tests/agentic/parity_audit.py` from the menus and toolbars of every",
        "workbench of this build; do not edit by hand. The rule is that every control a person",
        "has in the classic window is reachable by the agent through a tool, with `python_run`",
        "as the escape hatch rather than the route.",
        "",
        "The route says which tool reaches the same result. It is assigned by command name and",
        "group, not by running each command. Commands routed to `command_run` run as a toolbar",
        "button does, through `command_run`; one that opens a task panel is finished with",
        "`task_get`, `task_set` and `task_accept`, and a message box it raises is answered by",
        "the dialog policy. Representatives of that route are exercised in the test suites.",
        "",
        f"{len(rows)} commands across {len({w for ws in rows.values() for w in ws})} workbenches.",
        "",
        "| Route | Commands |",
        "|---|---|",
    ]
    for route in sorted(by_route, key=lambda r: -by_route[r]):
        out.append(f"| {route} | {by_route[route]} |")
    gaps = sorted(n for n in rows if classify(n)[0].startswith("python_run only"))
    out += [
        "",
        "Gaps: commands with no dedicated tool, reachable only through `python_run`: "
        + (", ".join(f"`{g}`" for g in gaps) if gaps else "none") + ". They act on the",
        "display (a clipping plane, a texture, the scene inspector, demo rotation) or on project",
        "files (the document utility), not on the model; the Python API covers each.",
        "",
        "Excluded commands act on the classic window itself (toolbars, docks, window",
        "arrangement, the Start page, help pages, about, quitting) and change nothing in a",
        "document; the agent viewer has no use for them.",
        "",
    ]
    if dialogs:
        out += ["Dialogs workbenches raised on activation, answered by the dialog policy:", ""]
        out += [f"- {d}" for d in dialogs]
        out.append("")
    if skipped:
        out += ["Workbenches that could not be activated here:", ""]
        out += [f"- {s}" for s in skipped]
        out.append("")
    out += ["## Commands", "", "| Command | Menu text | Workbenches | Route |", "|---|---|---|---|"]
    out += lines
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(out) + "\n")
    return by_route


status = 1
try:
    rows, skipped = audit()
    path = os.environ.get("AGENTIC_PARITY_MD", "doc/agentic/parity.md")
    by_route = write(rows, skipped, path)
    sys.__stderr__.write(f"parity audit: {len(rows)} commands -> {path}\n")
    for route, count in sorted(by_route.items(), key=lambda item: -item[1]):
        sys.__stderr__.write(f"  {count:4d}  {route}\n")
    for s in skipped:
        sys.__stderr__.write(f"  skipped {s}\n")
    status = 0
except Exception:
    sys.__stderr__.write(traceback.format_exc())
finally:
    sys.__stderr__.flush()
    os._exit(status)
