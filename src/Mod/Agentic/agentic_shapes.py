# SPDX-License-Identifier: LGPL-2.1-or-later
"""Geometry facts, sub-element discovery, import and export for the Agentic tools.

These need only the App layer, so they also work in FreeCADCmd. The C++ tools
resolve the project, the document and every path inside the project sandbox
before calling in here.
"""

import json
import math
import os

import FreeCAD

App = FreeCAD


def _num(value):
    if value is None:
        return None
    value = float(value)
    if math.isnan(value) or math.isinf(value):
        return None
    return round(value, 9)


def _vec(v):
    return [_num(v.x), _num(v.y), _num(v.z)]


def _bbox(bb):
    return {
        "min": [_num(bb.XMin), _num(bb.YMin), _num(bb.ZMin)],
        "max": [_num(bb.XMax), _num(bb.YMax), _num(bb.ZMax)],
        "size": [_num(bb.XLength), _num(bb.YLength), _num(bb.ZLength)],
        "center": _vec(bb.Center),
    }


def _object(doc, name):
    obj = doc.getObject(name)
    if obj is None:
        matches = [o for o in doc.Objects if o.Label == name]
        if len(matches) > 1:
            raise ValueError(f"label is not unique: {name}; use the Name")
        obj = matches[0] if matches else None
    if obj is None:
        raise ValueError(f"no object named {name!r} in {doc.Name}")
    return obj


def _shape_of(obj):
    shape = getattr(obj, "Shape", None)
    if shape is None:
        try:
            import Part

            shape = Part.getShape(obj)
        except Exception:
            shape = None
    if shape is None or shape.isNull():
        raise ValueError(f"{obj.Name} has no shape")
    return shape


def _result_objects(doc):
    """Objects with a shape that nothing else consumes: what a person sees as the result."""
    out = []
    for obj in doc.Objects:
        if not hasattr(obj, "Shape"):
            continue
        try:
            if obj.Shape.isNull():
                continue
        except Exception:
            continue
        users = [u for u in obj.InList if hasattr(u, "Shape")]
        if users:
            continue
        out.append(obj)
    return out


def _facts(shape):
    facts = {
        "shapeType": shape.ShapeType,
        "valid": shape.isValid(),
        "boundBox": _bbox(shape.BoundBox),
        "counts": {
            "solids": len(shape.Solids),
            "shells": len(shape.Shells),
            "faces": len(shape.Faces),
            "edges": len(shape.Edges),
            "vertexes": len(shape.Vertexes),
        },
        "area": _num(shape.Area),
    }
    if shape.Solids:
        facts["volume"] = _num(shape.Volume)
        try:
            facts["centerOfMass"] = _vec(
                shape.Solids[0].CenterOfMass if len(shape.Solids) == 1 else _compound_center(shape)
            )
        except Exception:
            pass
    else:
        try:
            facts["centerOfGravity"] = _vec(shape.CenterOfGravity)
        except Exception:
            pass
    if shape.Edges and not shape.Faces:
        facts["length"] = _num(shape.Length)
    try:
        facts["closed"] = bool(shape.isClosed())
    except Exception:
        pass
    return facts


def _compound_center(shape):
    total = 0.0
    acc = FreeCAD.Vector()
    for solid in shape.Solids:
        acc = acc + solid.CenterOfMass * solid.Volume
        total += solid.Volume
    return acc * (1.0 / total) if total else FreeCAD.Vector()


def geometry_info(payload):
    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    names = args.get("objects") or []
    objects = [_object(doc, n) for n in names] if names else _result_objects(doc)
    out = []
    total_volume = 0.0
    union = None
    for obj in objects:
        shape = _shape_of(obj)
        facts = _facts(shape)
        facts.update({"object": obj.Name, "label": obj.Label, "type": obj.TypeId})
        out.append(facts)
        total_volume += facts.get("volume") or 0.0
        bb = shape.BoundBox
        union = bb if union is None else union.united(bb)
    result = {"document": doc.Name, "objects": out}
    if len(out) > 1:
        result["total"] = {"volume": _num(total_volume), "boundBox": _bbox(union)}
    return json.dumps(result)


def geometry_check(payload):
    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    names = args.get("objects") or []
    objects = [_object(doc, n) for n in names] if names else _result_objects(doc)
    out = []
    for obj in objects:
        shape = _shape_of(obj)
        entry = {"object": obj.Name, "label": obj.Label, "valid": shape.isValid(), "problems": []}
        try:
            shape.check(bool(args.get("booleanCheck", True)))
        except Exception as err:  # check() raises with the list of problems
            text = str(err).strip()
            entry["problems"] = [line.strip() for line in text.splitlines() if line.strip()]
        if shape.Solids:
            for index, solid in enumerate(shape.Solids, 1):
                if solid.Volume < 0:
                    entry["problems"].append(f"Solid{index} has negative volume (inverted)")
        try:
            if shape.ShapeType in ("Solid", "Shell") and not shape.isClosed():
                entry["problems"].append(f"{shape.ShapeType} is not closed")
        except Exception:
            pass
        entry["ok"] = entry["valid"] and not entry["problems"]
        out.append(entry)
    return json.dumps({"document": doc.Name, "objects": out, "ok": all(e["ok"] for e in out)})


# ----------------------------------------------------------------- sub-elements


def _surface_info(face):
    info = {}
    surface = face.Surface
    kind = type(surface).__name__
    info["surface"] = kind
    try:
        u0, u1, v0, v1 = face.ParameterRange
        normal = face.normalAt((u0 + u1) / 2, (v0 + v1) / 2)
        info["normal"] = _vec(normal)
    except Exception:
        pass
    if kind in ("Cylinder", "Cone", "Sphere", "Toroid"):
        try:
            info["center"] = _vec(surface.Center)
        except Exception:
            pass
        for attr in ("Axis",):
            if hasattr(surface, attr):
                info["axis"] = _vec(getattr(surface, attr))
        for attr in ("Radius", "MajorRadius", "MinorRadius", "SemiAngle"):
            if hasattr(surface, attr):
                info[attr[0].lower() + attr[1:]] = _num(getattr(surface, attr))
    return info


def _curve_info(edge):
    info = {}
    try:
        curve = edge.Curve
        kind = type(curve).__name__
        info["curve"] = kind
        if kind == "Line":
            info["direction"] = _vec(curve.Direction)
        if kind in ("Circle", "ArcOfCircle"):
            info["center"] = _vec(curve.Center)
            info["axis"] = _vec(curve.Axis)
            info["radius"] = _num(curve.Radius)
    except Exception:
        info["curve"] = "unknown"
    info["length"] = _num(edge.Length)
    info["start"] = _vec(edge.Vertexes[0].Point) if edge.Vertexes else None
    info["end"] = _vec(edge.Vertexes[-1].Point) if edge.Vertexes else None
    return info


def shape_elements(payload):
    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    obj = _object(doc, args["object"])
    shape = _shape_of(obj)
    kinds = args.get("types") or ["Face", "Edge", "Vertex"]
    want_surface = args.get("surface")
    want_curve = args.get("curve")
    direction = args.get("direction")
    direction_mode = args.get("directionMode", "extreme")
    near = args.get("nearPoint")
    limit = int(args.get("limit") or 100)

    elements = []
    if "Face" in kinds:
        for i, face in enumerate(shape.Faces, 1):
            entry = {
                "name": f"Face{i}",
                "type": "Face",
                "area": _num(face.Area),
                "centerOfMass": _vec(face.CenterOfMass),
                "boundBox": _bbox(face.BoundBox),
            }
            entry.update(_surface_info(face))
            elements.append(entry)
    if "Edge" in kinds:
        for i, edge in enumerate(shape.Edges, 1):
            entry = {
                "name": f"Edge{i}",
                "type": "Edge",
                "centerOfMass": _vec(edge.CenterOfMass),
                "boundBox": _bbox(edge.BoundBox),
            }
            entry.update(_curve_info(edge))
            elements.append(entry)
    if "Vertex" in kinds:
        for i, vertex in enumerate(shape.Vertexes, 1):
            elements.append(
                {
                    "name": f"Vertex{i}",
                    "type": "Vertex",
                    "point": _vec(vertex.Point),
                    "centerOfMass": _vec(vertex.Point),
                }
            )

    if want_surface:
        elements = [e for e in elements if e.get("surface") == want_surface]
    if want_curve:
        elements = [e for e in elements if e.get("curve") == want_curve]

    if direction is not None:
        d = FreeCAD.Vector(*direction)
        if d.Length == 0:
            raise ValueError("direction must not be zero")
        d.normalize()
        if direction_mode == "facing":
            # Planar faces whose outward normal points along the direction.
            kept = []
            for e in elements:
                n = e.get("normal")
                if n is not None and e.get("surface") == "Plane":
                    if FreeCAD.Vector(*n).dot(d) > 0.999:
                        kept.append(e)
            elements = kept
        for e in elements:
            e["along"] = _num(FreeCAD.Vector(*e["centerOfMass"]).dot(d))
        elements.sort(key=lambda e: e["along"], reverse=True)
        if direction_mode == "extreme" and elements:
            top = elements[0]["along"]
            tolerance = float(args.get("tolerance", 1e-6))
            elements = [e for e in elements if abs(e["along"] - top) <= tolerance]

    if near is not None:
        p = FreeCAD.Vector(*near)
        for e in elements:
            e["distance"] = _num((FreeCAD.Vector(*e["centerOfMass"]) - p).Length)
        elements.sort(key=lambda e: e["distance"])

    total = len(elements)
    return json.dumps(
        {
            "document": doc.Name,
            "object": obj.Name,
            "counts": {
                "faces": len(shape.Faces),
                "edges": len(shape.Edges),
                "vertexes": len(shape.Vertexes),
            },
            "matched": total,
            "elements": elements[:limit],
        }
    )


# --------------------------------------------------------------- import/export


def _modules(kind, extension):
    getter = FreeCAD.getImportType if kind == "import" else FreeCAD.getExportType
    try:
        modules = list(getter(extension) or [])
    except Exception:
        modules = []
    if not modules:
        # Extensions are registered in both cases (stp/STP); try the other.
        try:
            modules = list(getter(extension.upper()) or [])
        except Exception:
            modules = []
    if not FreeCAD.GuiUp:
        modules = [m for m in modules if not m.endswith("Gui")]
    # The GUI variants keep colours; prefer them when present.
    modules.sort(key=lambda m: 0 if m.endswith("Gui") else 1)
    return modules


def import_file(payload):
    args = json.loads(payload)
    path = args["path"]
    extension = os.path.splitext(path)[1].lstrip(".").lower()
    modules = _modules("import", extension)
    if not modules:
        raise ValueError(f"no importer is registered for .{extension}")
    doc = App.getDocument(args["document"])
    before = {o.Name for o in doc.Objects}
    errors = []
    for module_name in modules:
        try:
            module = __import__(module_name)
            if hasattr(module, "insert"):
                module.insert(path, doc.Name)
            elif hasattr(module, "open"):
                raise ValueError(f"{module_name} can only open into a new document")
            else:
                raise ValueError(f"{module_name} has no insert()")
            doc.recompute()
            created = [o.Name for o in doc.Objects if o.Name not in before]
            return json.dumps(
                {
                    "document": doc.Name,
                    "module": module_name,
                    "created": created,
                    "python": f"import {module_name}\n{module_name}.insert({path!r}, {doc.Name!r})",
                }
            )
        except Exception as err:
            errors.append(f"{module_name}: {err}")
    raise RuntimeError("import failed: " + "; ".join(errors))


def export_file(payload):
    args = json.loads(payload)
    path = args["path"]
    doc = App.getDocument(args["document"])
    names = args.get("objects") or []
    objects = [_object(doc, n) for n in names] if names else _result_objects(doc)
    if not objects:
        raise ValueError("nothing to export: the document has no shapes")
    extension = os.path.splitext(path)[1].lstrip(".").lower()
    modules = _modules("export", extension)
    if not modules:
        raise ValueError(f"no exporter is registered for .{extension}")
    errors = []
    for module_name in modules:
        try:
            module = __import__(module_name)
            if not hasattr(module, "export"):
                raise ValueError("no export()")
            module.export(objects, path)
            if not os.path.exists(path):
                raise RuntimeError("the exporter wrote no file")
            obj_list = ", ".join(f"doc.getObject({o.Name!r})" for o in objects)
            return json.dumps(
                {
                    "document": doc.Name,
                    "module": module_name,
                    "objects": [o.Name for o in objects],
                    "size": os.path.getsize(path),
                    "python": f"import {module_name}\n{module_name}.export([{obj_list}], {path!r})",
                }
            )
        except Exception as err:
            errors.append(f"{module_name}: {err}")
    raise RuntimeError("export failed: " + "; ".join(errors))


def formats(payload):
    return json.dumps(
        {
            "import": sorted({ext.lower() for ext in FreeCAD.getImportType()}),
            "export": sorted({ext.lower() for ext in FreeCAD.getExportType()}),
        }
    )
