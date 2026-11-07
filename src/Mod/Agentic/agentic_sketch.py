# SPDX-License-Identifier: LGPL-2.1-or-later
"""Structured sketching for the Agentic tools, over the Sketcher Python API.

Interactive sketching in the GUI is mouse driven and has no command-level
equivalent; these functions give the agent geometry, constraints and the
solver's diagnosis as data. Coordinates are in the sketch plane (mm), angles
in degrees. Every mutating function returns the equivalent Python lines for
the change log.

Element references used by constraints:
    3                 geometry 3 as a whole (an edge)
    [3, "start"]      a point of geometry 3: start, end, center (or 1, 2, 3)
    "xAxis", "yAxis"  the sketch axes; "origin" is the root point
    "external:0"      external geometry 0
"""

import json
import math

import FreeCAD

App = FreeCAD
V = FreeCAD.Vector

POSITIONS = {"none": 0, "edge": 0, "start": 1, "end": 2, "center": 3, "mid": 3}
POSITION_NAMES = {0: "edge", 1: "start", 2: "end", 3: "center"}
ANGLE_TYPES = {"Angle"}
VALUE_TYPES = {
    "Distance",
    "DistanceX",
    "DistanceY",
    "Radius",
    "Diameter",
    "Angle",
    "Weight",
    "SnellsLaw",
}


def _sketch(doc_name, name):
    doc = App.getDocument(doc_name)
    obj = doc.getObject(name) or next((o for o in doc.Objects if o.Label == name), None)
    if obj is None:
        raise ValueError(f"no object named {name!r}")
    if not obj.isDerivedFrom("Sketcher::SketchObject"):
        raise ValueError(f"{obj.Name} is not a sketch")
    return doc, obj


def _round(v):
    return round(float(v), 9)


def _pt(v):
    return [_round(v.x), _round(v.y)]


# ------------------------------------------------------------------ inspection


def _geometry_entry(sketch, index, geo):
    kind = type(geo).__name__
    entry = {"index": index, "type": kind, "construction": bool(sketch.getConstruction(index))}
    try:
        if kind == "LineSegment":
            entry["start"] = _pt(geo.StartPoint)
            entry["end"] = _pt(geo.EndPoint)
            entry["length"] = _round(geo.length())
        elif kind == "Circle":
            entry["center"] = _pt(geo.Center)
            entry["radius"] = _round(geo.Radius)
        elif kind == "ArcOfCircle":
            entry["center"] = _pt(geo.Center)
            entry["radius"] = _round(geo.Radius)
            entry["start"] = _pt(geo.StartPoint)
            entry["end"] = _pt(geo.EndPoint)
            entry["startAngle"] = _round(math.degrees(geo.FirstParameter))
            entry["endAngle"] = _round(math.degrees(geo.LastParameter))
        elif kind == "Point":
            entry["at"] = [_round(geo.X), _round(geo.Y)]
        elif kind in ("Ellipse", "ArcOfEllipse"):
            entry["center"] = _pt(geo.Center)
            entry["majorRadius"] = _round(geo.MajorRadius)
            entry["minorRadius"] = _round(geo.MinorRadius)
        elif kind == "BSplineCurve":
            entry["poles"] = [_pt(p) for p in geo.getPoles()]
            entry["degree"] = geo.Degree
    except Exception:
        pass
    return entry


def _ref_name(geo, pos):
    if geo == -1:
        name = "xAxis" if pos == 0 else "origin"
    elif geo == -2:
        name = "yAxis"
    elif geo <= -3:
        name = f"external:{-geo - 3}"
    else:
        name = geo
    if pos and not (geo == -1 and pos == 1):
        return [name, POSITION_NAMES.get(pos, pos)]
    return name


def _constraint_entry(index, c):
    elements = []
    if c.First != -2000:
        elements.append(_ref_name(c.First, c.FirstPos))
    if c.Second != -2000:
        elements.append(_ref_name(c.Second, c.SecondPos))
    if c.Third != -2000:
        elements.append(_ref_name(c.Third, c.ThirdPos))
    entry = {"index": index, "type": c.Type, "elements": elements, "driving": bool(c.Driving)}
    if c.Type in VALUE_TYPES:
        entry["value"] = _round(math.degrees(c.Value) if c.Type in ANGLE_TYPES else c.Value)
    if c.Name:
        entry["name"] = c.Name
    if not c.IsActive:
        entry["active"] = False
    return entry


def _solver_state(sketch):
    code = sketch.solve()

    def indices(name):
        # The Sketcher numbers these lists from 1; every index this module
        # returns or accepts is 0-based, so they can go to sketch_remove as is.
        return [i - 1 for i in getattr(sketch, name, [])]

    state = {
        "solved": code == 0,
        "solverCode": code,
        "degreesOfFreedom": sketch.DoF if hasattr(sketch, "DoF") else None,
        "conflicting": indices("ConflictingConstraints"),
        "redundant": indices("RedundantConstraints"),
        "partiallyRedundant": indices("PartiallyRedundantConstraints"),
        "malformed": indices("MalformedConstraints"),
    }
    state["fullyConstrained"] = (
        state["solved"] and state["degreesOfFreedom"] == 0 and not state["conflicting"]
    )
    try:
        state["openVertices"] = [_pt(v) for v in sketch.OpenVertices]
    except Exception:
        pass
    return state


def _state(sketch, include=True):
    out = {"sketch": sketch.Name, "label": sketch.Label}
    if include:
        out["geometry"] = [_geometry_entry(sketch, i, g) for i, g in enumerate(sketch.Geometry)]
        out["constraints"] = [_constraint_entry(i, c) for i, c in enumerate(sketch.Constraints)]
    out["solver"] = _solver_state(sketch)
    try:
        shape = sketch.Shape
        out["closedWires"] = sum(1 for w in shape.Wires if w.isClosed())
        out["wires"] = len(shape.Wires)
    except Exception:
        pass
    try:
        out["placement"] = {
            "base": [_round(c) for c in sketch.Placement.Base],
            "normal": [_round(c) for c in sketch.Placement.Rotation.multVec(V(0, 0, 1))],
        }
    except Exception:
        pass
    return out


def sketch_get(payload):
    args = json.loads(payload)
    _, sketch = _sketch(args["document"], args["sketch"])
    return json.dumps(_state(sketch))


# ----------------------------------------------------------------- geometry


def _xy(value, what):
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise ValueError(f"{what} must be [x, y]")
    return V(float(value[0]), float(value[1]), 0)


def _make(item):
    """Returns (Part geometry, python expression, implied constraints) for one item."""
    import Part

    kind = item.get("type")
    if kind == "line":
        a, b = _xy(item["start"], "start"), _xy(item["end"], "end")
        return [
            (
                Part.LineSegment(a, b),
                f"Part.LineSegment(App.Vector({a.x}, {a.y}, 0), App.Vector({b.x}, {b.y}, 0))",
            )
        ], []
    if kind == "circle":
        c = _xy(item["center"], "center")
        r = float(item["radius"])
        return [
            (
                Part.Circle(c, V(0, 0, 1), r),
                f"Part.Circle(App.Vector({c.x}, {c.y}, 0), App.Vector(0, 0, 1), {r})",
            )
        ], []
    if kind == "arc":
        c = _xy(item["center"], "center")
        r = float(item["radius"])
        a0, a1 = math.radians(float(item["startAngle"])), math.radians(float(item["endAngle"]))
        arc = Part.ArcOfCircle(Part.Circle(c, V(0, 0, 1), r), a0, a1)
        expr = f"Part.ArcOfCircle(Part.Circle(App.Vector({c.x}, {c.y}, 0), App.Vector(0, 0, 1), {r}), {a0}, {a1})"
        return [(arc, expr)], []
    if kind == "point":
        p = _xy(item["at"], "at")
        return [(Part.Point(p), f"Part.Point(App.Vector({p.x}, {p.y}, 0))")], []
    if kind == "ellipse":
        c = _xy(item["center"], "center")
        major, minor = float(item["majorRadius"]), float(item["minorRadius"])
        angle = math.radians(float(item.get("angle", 0)))
        axis = V(math.cos(angle), math.sin(angle), 0)
        s1 = c + axis * major
        s2 = c + V(-axis.y, axis.x, 0) * minor
        expr = (
            f"Part.Ellipse(App.Vector({s1.x}, {s1.y}, 0), App.Vector({s2.x}, {s2.y}, 0), "
            f"App.Vector({c.x}, {c.y}, 0))"
        )
        return [(Part.Ellipse(s1, s2, c), expr)], []
    if kind == "bspline":
        poles = [_xy(p, "pole") for p in item["poles"]]
        curve = Part.BSplineCurve()
        curve.buildFromPoles(poles, bool(item.get("periodic", False)), int(item.get("degree", 3)))
        expr = (
            "_c = Part.BSplineCurve(); _c.buildFromPoles(["
            + ", ".join(f"App.Vector({p.x}, {p.y}, 0)" for p in poles)
            + f"], {bool(item.get('periodic', False))}, {int(item.get('degree', 3))}); _c"
        )
        return [(curve, expr)], []
    if kind in ("polyline", "rectangle"):
        if kind == "rectangle":
            x0, y0 = item["corner"]
            w, h = float(item["width"]), float(item["height"])
            pts = [[x0, y0], [x0 + w, y0], [x0 + w, y0 + h], [x0, y0 + h]]
            closed = True
        else:
            pts = item["points"]
            closed = bool(item.get("closed", False))
        if len(pts) < 2:
            raise ValueError("a polyline needs at least two points")
        segments = list(zip(pts, pts[1:])) + ([(pts[-1], pts[0])] if closed else [])
        made = []
        for a, b in segments:
            pa, pb = _xy(a, "point"), _xy(b, "point")
            made.append(
                (
                    Part.LineSegment(pa, pb),
                    f"Part.LineSegment(App.Vector({pa.x}, {pa.y}, 0), App.Vector({pb.x}, {pb.y}, 0))",
                )
            )
        implied = []
        n = len(made)
        for i in range(n - 1):
            implied.append(("Coincident", [(i, 2), (i + 1, 1)]))
        if closed:
            implied.append(("Coincident", [(n - 1, 2), (0, 1)]))
        if kind == "rectangle":
            implied += [
                ("Horizontal", [(0, 0)]),
                ("Horizontal", [(2, 0)]),
                ("Vertical", [(1, 0)]),
                ("Vertical", [(3, 0)]),
            ]
        return made, implied
    raise ValueError(
        f"unknown geometry type {kind!r}; one of line, circle, arc, point, ellipse, "
        "bspline, polyline, rectangle"
    )


def sketch_add(payload):
    import Part  # noqa: F401  (the expressions below use it)
    import Sketcher

    args = json.loads(payload)
    _, sketch = _sketch(args["document"], args["sketch"])
    ref = f'App.getDocument("{sketch.Document.Name}").getObject("{sketch.Name}")'
    lines = []
    added = []
    for item in args["geometry"]:
        made, implied = _make(item)
        construction = bool(item.get("construction", False))
        indices = []
        for geo, expr in made:
            index = sketch.addGeometry(geo, construction)
            indices.append(index)
            lines.append(f"{ref}.addGeometry({expr}, {construction})")
        for ctype, refs in implied:
            flat = []
            for local, pos in refs:
                flat.append(indices[local])
                if pos:
                    flat.append(pos)
            sketch.addConstraint(Sketcher.Constraint(ctype, *flat))
            lines.append(
                f"{ref}.addConstraint(Sketcher.Constraint({ctype!r}, {', '.join(map(str, flat))}))"
            )
        added.append({"type": item.get("type"), "indices": indices})
    sketch.Document.recompute()
    out = _state(sketch, include=args.get("details", True))
    out["added"] = added
    out["python"] = lines
    return json.dumps(out)


# -------------------------------------------------------------- constraints


def _element(value):
    """A reference to (geometry index, position)."""
    if isinstance(value, bool):
        raise ValueError("a reference cannot be a boolean")
    if isinstance(value, int):
        return value, None
    if isinstance(value, str):
        if value == "xAxis":
            return -1, None
        if value == "yAxis":
            return -2, None
        if value == "origin":
            return -1, 1
        if value.startswith("external:"):
            return -3 - int(value.split(":", 1)[1]), None
        raise ValueError(f"unknown reference {value!r}")
    if isinstance(value, (list, tuple)) and len(value) == 2:
        geo, _ = _element(value[0])
        pos = value[1]
        if isinstance(pos, str):
            if pos not in POSITIONS:
                raise ValueError(f"unknown point {pos!r}; one of start, end, center")
            pos = POSITIONS[pos]
        return geo, int(pos)
    raise ValueError(f"bad reference {value!r}")


def _value(ctype, value):
    if value is None:
        return None
    if isinstance(value, str):
        return FreeCAD.Units.Quantity(value)
    if ctype in ANGLE_TYPES:
        return FreeCAD.Units.Quantity(f"{float(value)} deg")
    return float(value)


def sketch_constrain(payload):
    import Sketcher

    args = json.loads(payload)
    _, sketch = _sketch(args["document"], args["sketch"])
    ref = f'App.getDocument("{sketch.Document.Name}").getObject("{sketch.Name}")'
    lines = []
    created = []
    for spec in args["constraints"]:
        ctype = spec["type"]
        flat = []
        for element in spec.get("elements", []):
            geo, pos = _element(element)
            flat.append(geo)
            if pos is not None:
                flat.append(pos)
        value = _value(ctype, spec.get("value"))
        cargs = flat + ([value] if value is not None else [])
        if ctype == "Lock":
            # The GUI's lock is two driving distances from the origin.
            geo, pos = _element(spec["elements"][0])
            point = sketch.getPoint(geo, pos if pos is not None else 1)
            pairs = [("DistanceX", point.x), ("DistanceY", point.y)]
            for kind, coordinate in pairs:
                c = Sketcher.Constraint(kind, -1, 1, geo, pos if pos is not None else 1, coordinate)
                index = sketch.addConstraint(c)
                created.append(index)
                lines.append(
                    f"{ref}.addConstraint(Sketcher.Constraint({kind!r}, -1, 1, {geo}, {pos or 1}, {coordinate!r}))"
                )
            continue
        try:
            constraint = Sketcher.Constraint(ctype, *cargs)
        except Exception as err:
            raise ValueError(f"{ctype} with elements {spec.get('elements')}: {err}")
        index = sketch.addConstraint(constraint)
        if spec.get("name"):
            sketch.renameConstraint(index, spec["name"])
        if spec.get("driving") is False:
            sketch.setDriving(index, False)
        created.append(index)
        shown = ", ".join(
            [repr(ctype)]
            + [
                str(a) if not hasattr(a, "UserString") else f"App.Units.Quantity({a.UserString!r})"
                for a in cargs
            ]
        )
        lines.append(f"{ref}.addConstraint(Sketcher.Constraint({shown}))")
    sketch.Document.recompute()
    out = _state(sketch, include=args.get("details", True))
    out["created"] = created
    out["python"] = lines
    if out["solver"]["conflicting"] or out["solver"]["redundant"]:
        out["warning"] = (
            "the solver reports conflicting or redundant constraints; remove some "
            "of the listed constraint indices with sketch_remove"
        )
    return json.dumps(out)


def sketch_remove(payload):
    args = json.loads(payload)
    _, sketch = _sketch(args["document"], args["sketch"])
    ref = f'App.getDocument("{sketch.Document.Name}").getObject("{sketch.Name}")'
    lines = []
    constraints = sorted(set(args.get("constraints") or []), reverse=True)
    geometry = sorted(set(args.get("geometry") or []), reverse=True)
    for index in constraints:
        if index < 0 or index >= len(sketch.Constraints):
            raise ValueError(f"no constraint {index}")
    for index in geometry:
        if index < 0 or index >= len(sketch.Geometry):
            raise ValueError(f"no geometry {index}")
    if constraints:
        sketch.delConstraints(constraints)
        lines.append(f"{ref}.delConstraints({constraints})")
    if geometry:
        # Deleting geometry also deletes the constraints on it and renumbers.
        sketch.delGeometries(geometry)
        lines.append(f"{ref}.delGeometries({geometry})")
    sketch.Document.recompute()
    out = _state(sketch, include=args.get("details", True))
    out["python"] = lines
    return json.dumps(out)


def sketch_solve(payload):
    args = json.loads(payload)
    doc, sketch = _sketch(args["document"], args["sketch"])
    doc.recompute()
    out = _state(sketch, include=args.get("details", False))
    out["error"] = bool("Invalid" in sketch.State or "Error" in sketch.State)
    out["status"] = sketch.getStatusString()
    return json.dumps(out)


# -------------------------------------------------------------------- edit mode


def object_edit(payload):
    import FreeCADGui as Gui

    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    obj = doc.getObject(args["object"]) or next(
        (o for o in doc.Objects if o.Label == args["object"]), None
    )
    if obj is None:
        raise ValueError(f"no object named {args['object']!r}")
    gdoc = Gui.getDocument(doc.Name)
    if gdoc.getInEdit() is not None:
        gdoc.resetEdit()
    ok = gdoc.setEdit(obj.Name, int(args.get("mode", 0)))
    Gui.updateGui()
    editing = gdoc.getInEdit()
    if not ok or editing is None:
        raise RuntimeError(f"{obj.Name} has no edit mode")
    return json.dumps(
        {
            "object": obj.Name,
            "editing": True,
            "python": f'Gui.getDocument("{doc.Name}").setEdit("{obj.Name}", {int(args.get("mode", 0))})',
        }
    )


def object_edit_end(payload):
    import FreeCADGui as Gui

    args = json.loads(payload)
    gdoc = Gui.getDocument(args["document"])
    editing = gdoc.getInEdit()
    if editing is None:
        return json.dumps({"editing": False, "object": None})
    name = editing.Object.Name if hasattr(editing, "Object") else None
    gdoc.resetEdit()
    Gui.updateGui()
    App.getDocument(args["document"]).recompute()
    return json.dumps(
        {
            "editing": False,
            "object": name,
            "python": f'Gui.getDocument("{args["document"]}").resetEdit()',
        }
    )
