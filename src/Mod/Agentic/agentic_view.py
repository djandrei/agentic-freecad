# SPDX-License-Identifier: LGPL-2.1-or-later
"""Camera, view options, captures and drawing export for the Agentic GUI tools.

Captures are rendered offscreen by View3DInventor.saveImage, never grabbed
from the screen, so they work under the offscreen Qt platform and do not
depend on what a person does with the viewer window. A capture that asks for
its own view or camera restores the session camera afterwards: the camera
set with camera_set is the reference.
"""

import base64
import json
import math
import os

import FreeCAD
import FreeCADGui as Gui

# The agent's camera per document: the reference a capture without its own
# view renders from, whatever a person watching has orbited to since.
_agent_camera = {}

NAMED_VIEWS = {
    "isometric": "viewIsometric",
    "dimetric": "viewDimetric",
    "trimetric": "viewTrimetric",
    "front": "viewFront",
    "rear": "viewRear",
    "top": "viewTop",
    "bottom": "viewBottom",
    "left": "viewLeft",
    "right": "viewRight",
}


def _view(document):
    gdoc = Gui.getDocument(document)
    if gdoc is None:
        raise ValueError(f"document {document} has no GUI document")
    view = gdoc.ActiveView
    if view is None or not hasattr(view, "getCamera"):
        # The document's 3D view may not be the active MDI window.
        for candidate in gdoc.mdiViewsOfType("Gui::View3DInventor"):
            view = candidate
            break
    if view is None or not hasattr(view, "getCamera"):
        raise ValueError(f"document {document} has no 3D view")
    try:
        view.setAnimationEnabled(False)  # camera changes take effect at once
    except Exception:
        pass
    return view


def _vec(v):
    return [round(v.x, 9), round(v.y, 9), round(v.z, 9)]


def _camera_state(view):
    rotation = view.getCameraOrientation()
    return {
        "type": view.getCameraType(),
        "viewDirection": _vec(view.getViewDirection()),
        "upDirection": _vec(view.getUpDirection()),
        "orientation": {"quaternion": [round(q, 9) for q in rotation.Q]},
        "inventor": view.getCamera(),
        "size": list(view.getSize()),
        "axisCross": bool(view.hasAxisCross()),
    }


def _select_only(document, names):
    previous = [
        (s.Document.Name, s.Object.Name, list(s.SubElementNames))
        for s in Gui.Selection.getSelectionEx("", 0)
    ]
    Gui.Selection.clearSelection()
    for name in names:
        Gui.Selection.addSelection(document, name)
    return previous


def _restore_selection(previous):
    Gui.Selection.clearSelection()
    for doc, obj, subs in previous:
        if subs:
            for sub in subs:
                Gui.Selection.addSelection(doc, obj, sub)
        else:
            Gui.Selection.addSelection(doc, obj)


def _apply_camera(view, document, args):
    """Applies view, direction, orientation, inventor, type and fit from args."""
    if args.get("type"):
        view.setCameraType(args["type"])
    if args.get("inventor"):
        view.setCamera(args["inventor"])
    if args.get("view"):
        name = args["view"]
        if name not in NAMED_VIEWS:
            raise ValueError(f"unknown view {name!r}; one of {sorted(NAMED_VIEWS)}")
        getattr(view, NAMED_VIEWS[name])()
    if args.get("direction"):
        view.setViewDirection(tuple(args["direction"]))
    if args.get("orientation"):
        o = args["orientation"]
        if "quaternion" in o:
            rotation = FreeCAD.Rotation(*o["quaternion"])
        else:
            rotation = FreeCAD.Rotation(
                FreeCAD.Vector(*o.get("axis", [0, 0, 1])), o.get("angle", 0)
            )
        view.setCameraOrientation(rotation)
    fit = args.get("fit")
    if fit == "all" or fit is True:
        view.fitAll()
    elif isinstance(fit, list) and fit:
        previous = _select_only(document, fit)
        try:
            Gui.SendMsgToActiveView("ViewSelection")
        finally:
            _restore_selection(previous)
    elif fit == "selection":
        Gui.SendMsgToActiveView("ViewSelection")
    Gui.updateGui()


def camera_get(payload):
    args = json.loads(payload)
    return json.dumps(_camera_state(_view(args["document"])))


def _record_agent_camera(document, view):
    _agent_camera[document] = (view.getCameraType(), view.getCamera())


def camera_set(payload):
    args = json.loads(payload)
    view = _view(args["document"])
    _apply_camera(view, args["document"], args)
    _record_agent_camera(args["document"], view)
    return json.dumps(_camera_state(view))


def view_options(payload):
    args = json.loads(payload)
    view = _view(args["document"])
    changed = []
    if "axisCross" in args:
        view.setAxisCross(bool(args["axisCross"]))
        changed.append("axisCross")
    if "cornerCross" in args:
        view.setCornerCrossVisible(bool(args["cornerCross"]))
        changed.append("cornerCross")
    if "cameraType" in args:
        view.setCameraType(args["cameraType"])
        _record_agent_camera(args["document"], view)
        changed.append("cameraType")
    if "background" in args:
        r, g, b = args["background"]
        view.getViewer().setBackgroundColor(float(r), float(g), float(b))
        changed.append("background")
    if "drawStyle" in args:
        styles = {
            "as-is": 0,
            "points": 1,
            "wireframe": 2,
            "hidden-line": 3,
            "no-shading": 4,
            "shaded": 5,
            "flat-lines": 6,
        }
        style = args["drawStyle"]
        if style not in styles:
            raise ValueError(f"unknown draw style {style!r}; one of {sorted(styles)}")
        Gui.runCommand("Std_DrawStyle", styles[style])
        changed.append("drawStyle")
    Gui.updateGui()
    state = _camera_state(view)
    state["cornerCross"] = bool(view.isCornerCrossVisible())
    state["changed"] = changed
    return json.dumps(state)


def capture(payload):
    args = json.loads(payload)
    document = args["document"]
    view = _view(document)
    width = int(args.get("width") or 800)
    height = int(args.get("height") or 600)
    background = args.get("background") or "White"
    path = args["path"]

    saved_camera = None
    camera_args = {
        k: args[k]
        for k in ("view", "direction", "orientation", "inventor", "type", "fit")
        if k in args
    }
    if camera_args:
        saved_camera = (view.getCameraType(), view.getCamera())
        if "fit" not in camera_args:
            camera_args["fit"] = "all"
    elif document in _agent_camera:
        # Render from the agent's camera, not from wherever a person orbited.
        saved_camera = (view.getCameraType(), view.getCamera())
        camera_args = {"type": _agent_camera[document][0], "inventor": _agent_camera[document][1]}

    only = args.get("objects") or []
    hidden = []
    doc = FreeCAD.getDocument(document)
    if only:
        wanted = set(only)
        for obj in doc.Objects:
            vobj = getattr(obj, "ViewObject", None)
            if vobj is None or not hasattr(vobj, "Visibility"):
                continue
            if obj.Name not in wanted and obj.Label not in wanted and vobj.Visibility:
                vobj.Visibility = False
                hidden.append(obj)
        for name in only:
            obj = doc.getObject(name) or next((o for o in doc.Objects if o.Label == name), None)
            if obj is None:
                raise ValueError(f"no object named {name!r}")
            if not obj.ViewObject.Visibility:
                obj.ViewObject.Visibility = True
                hidden.append(("shown", obj))
    try:
        if camera_args:
            _apply_camera(view, document, camera_args)
        Gui.updateGui()
        view.saveImage(path, width, height, background)
        state = _camera_state(view)
    finally:
        for item in hidden:
            if isinstance(item, tuple):
                item[1].ViewObject.Visibility = False
            else:
                item.ViewObject.Visibility = True
        if saved_camera is not None:
            view.setCameraType(saved_camera[0])
            view.setCamera(saved_camera[1])
        Gui.updateGui()

    if not os.path.exists(path) or os.path.getsize(path) == 0:
        raise RuntimeError("the renderer wrote no image (no OpenGL context?)")
    out = {
        "width": width,
        "height": height,
        "background": background,
        "camera": state,
        "size": os.path.getsize(path),
    }
    if args.get("inline", True):
        with open(path, "rb") as handle:
            data = base64.b64encode(handle.read()).decode("ascii")
        mime = "image/jpeg" if path.lower().endswith((".jpg", ".jpeg")) else "image/png"
        out["_images"] = [{"data": data, "mimeType": mime}]
    return json.dumps(out)


def drawing_export(payload):
    args = json.loads(payload)
    doc = FreeCAD.getDocument(args["document"])
    page = doc.getObject(args["page"]) or next(
        (o for o in doc.Objects if o.Label == args["page"]), None
    )
    if page is None or not page.isDerivedFrom("TechDraw::DrawPage"):
        raise ValueError(f"{args['page']!r} is not a TechDraw page")
    import TechDrawGui

    path = args["path"]
    doc.recompute()
    # The printers render the page's graphics scene, which exists only once the
    # page is shown in its drawing view (as it always is when a person exports);
    # PagePrinter::printPdf dereferences it unchecked. The drawing view becomes
    # the active window; the camera tools find the 3D view regardless (_view).
    vobj = page.ViewObject
    vobj.Visibility = True
    vobj.show()
    Gui.updateGui()
    if path.lower().endswith(".pdf"):
        TechDrawGui.exportPageAsPdf(page, path)
    elif path.lower().endswith(".svg"):
        TechDrawGui.exportPageAsSvg(page, path)
    else:
        raise ValueError("drawing_export writes .svg or .pdf")
    Gui.updateGui()
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        raise RuntimeError("TechDraw wrote no file")
    return json.dumps(
        {
            "page": page.Name,
            "size": os.path.getsize(path),
            "python": f"import TechDrawGui\nTechDrawGui.exportPage{'AsPdf' if path.lower().endswith('.pdf') else 'AsSvg'}(doc.getObject({page.Name!r}), {path!r})",
        }
    )
