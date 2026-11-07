# SPDX-License-Identifier: LGPL-2.1-or-later
"""Workbench, command and selection queries for the Agentic GUI tools.

Called from the C++ Gui part with one JSON string per call; returns JSON.
"""

import json

import AgenticGui
import FreeCAD
import FreeCADGui as Gui


def _command_entry(name, workbench_toolbars=None):
    cmd = Gui.Command.get(name)
    info = {}
    if cmd is not None:
        try:
            info = cmd.getInfo() or {}
        except Exception:
            info = {}
    entry = {
        "name": name,
        "menuText": (info.get("menuText") or "").replace("&", ""),
        "toolTip": info.get("toolTip") or "",
        "statusTip": info.get("statusTip") or "",
        "whatsThis": info.get("whatsThis") or "",
        "shortcut": info.get("shortcut") or "",
        # Not cmd.isActive(): some commands crash when tested before their
        # action exists; AgenticGui creates it first.
        "active": AgenticGui.isCommandActive(name) if cmd is not None else False,
    }
    if workbench_toolbars is not None:
        entry["toolbars"] = workbench_toolbars.get(name, [])
    return entry


def workbenches(payload):
    active = None
    try:
        active = Gui.activeWorkbench().name()
    except Exception:
        pass
    out = []
    for name, wb in sorted(Gui.listWorkbenches().items()):
        out.append(
            {
                "name": name,
                "menuText": getattr(wb, "MenuText", name),
                "toolTip": getattr(wb, "ToolTip", ""),
                "active": name == active,
            }
        )
    return json.dumps({"workbenches": out, "active": active})


def activate_workbench(payload):
    args = json.loads(payload)
    name = args["name"]
    if name not in Gui.listWorkbenches():
        raise ValueError(f"no workbench named {name!r}")
    # activateWorkbench returns False when the workbench is already active,
    # so success is judged by the active workbench afterwards.
    Gui.activateWorkbench(name)
    if Gui.activeWorkbench().name() != name:
        raise RuntimeError(f"workbench {name!r} did not activate")
    wb = Gui.getWorkbench(name)
    return json.dumps(
        {
            "workbench": name,
            "menuText": getattr(wb, "MenuText", name),
            "toolbars": list(wb.listToolbars()),
            "menus": list(wb.listMenus()),
        }
    )


def commands(payload):
    args = json.loads(payload)
    workbench = args.get("workbench")
    needle = (args.get("filter") or "").lower()
    toolbars = None
    names = None
    if workbench:
        if workbench not in Gui.listWorkbenches():
            raise ValueError(f"no workbench named {workbench!r}")
        Gui.activateWorkbench(workbench)
        wb = Gui.getWorkbench(workbench)
        toolbars = {}
        for toolbar, items in (wb.getToolbarItems() or {}).items():
            for item in items:
                toolbars.setdefault(item, []).append(toolbar)
        names = sorted(toolbars)
    if names is None:
        names = sorted(Gui.listCommands())
    out = []
    for name in names:
        entry = _command_entry(name, toolbars)
        if (
            needle
            and needle not in (name + " " + entry["menuText"] + " " + entry["toolTip"]).lower()
        ):
            continue
        out.append(entry)
    return json.dumps({"workbench": workbench, "commands": out})


def command_info(payload):
    args = json.loads(payload)
    name = args["name"]
    if Gui.Command.get(name) is None:
        raise ValueError(f"no command named {name!r}")
    return json.dumps(_command_entry(name))


def _element_name(name):
    """The plain element name (Face1) of a possibly mapped one.

    With topological naming, an element of a shape that has an element map is
    reported as ';Face1;:H1350,F.Face1'; the indexed name after the last dot
    is the one shape_elements, selection_set and link properties use.
    """
    if name.startswith(";") and "." in name:
        return name.rsplit(".", 1)[1]
    return name


def selection_get(payload):
    items = []
    for sel in Gui.Selection.getSelectionEx("", 0):
        names = list(sel.SubElementNames)
        item = {
            "document": sel.Document.Name,
            "object": sel.Object.Name,
            "label": sel.Object.Label,
            "type": sel.Object.TypeId,
            "subElements": [_element_name(n) for n in names],
            "pickedPoints": [[p.x, p.y, p.z] for p in sel.PickedPoints],
        }
        if item["subElements"] != names:
            item["mappedSubElements"] = names
        items.append(item)
    return json.dumps({"selection": items})


def selection_set(payload):
    args = json.loads(payload)
    if not args.get("add", False):
        Gui.Selection.clearSelection()
    for item in args.get("items", []):
        doc_name = item.get("document") or (
            FreeCAD.ActiveDocument.Name if FreeCAD.ActiveDocument else None
        )
        if not doc_name:
            raise ValueError("no document for the selection item")
        doc = FreeCAD.getDocument(doc_name)
        obj = doc.getObject(item["object"])
        if obj is None:
            for candidate in doc.Objects:
                if candidate.Label == item["object"]:
                    obj = candidate
                    break
        if obj is None:
            raise ValueError(f"no object named {item['object']!r} in {doc_name}")
        subs = item.get("sub") or []
        if subs:
            for sub in subs:
                Gui.Selection.addSelection(doc_name, obj.Name, sub)
        else:
            Gui.Selection.addSelection(doc_name, obj.Name)
    return selection_get("{}")


def selection_clear(payload):
    Gui.Selection.clearSelection()
    return json.dumps({"selection": []})
