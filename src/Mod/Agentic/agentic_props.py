# SPDX-License-Identifier: LGPL-2.1-or-later
"""Property values to and from JSON for the Agentic document engine.

The C++ engine calls the functions at the bottom of this module with one JSON
string and gets one JSON string back. Everything FreeCAD knows about a
property (type, group, documentation, status, enumeration values, expression)
is read through the Python API, which is the same API the agent would use in a
script, so the two routes never disagree.
"""

import json
import math

import FreeCAD

App = FreeCAD

VECTOR_TYPES = {"App::PropertyVector", "App::PropertyPosition", "App::PropertyDirection"}
VECTOR_LIST_TYPES = {"App::PropertyVectorList", "App::PropertyVectorDistance"}
LINK_TYPES = {
    "App::PropertyLink",
    "App::PropertyLinkChild",
    "App::PropertyLinkGlobal",
    "App::PropertyLinkHidden",
    "App::PropertyXLink",
}
LINK_SUB_TYPES = {
    "App::PropertyLinkSub",
    "App::PropertyLinkSubChild",
    "App::PropertyLinkSubGlobal",
    "App::PropertyLinkSubHidden",
    "App::PropertyXLinkSub",
}
LINK_LIST_TYPES = {
    "App::PropertyLinkList",
    "App::PropertyLinkListChild",
    "App::PropertyLinkListGlobal",
    "App::PropertyLinkListHidden",
    "App::PropertyXLinkList",
}
LINK_SUB_LIST_TYPES = {
    "App::PropertyLinkSubList",
    "App::PropertyLinkSubListChild",
    "App::PropertyLinkSubListGlobal",
    "App::PropertyLinkSubListHidden",
    "App::PropertyXLinkSubList",
}
QUANTITY_TYPES = {
    "App::PropertyQuantity",
    "App::PropertyQuantityConstraint",
    "App::PropertyLength",
    "App::PropertyDistance",
    "App::PropertyAngle",
    "App::PropertyArea",
    "App::PropertyVolume",
    "App::PropertySpeed",
    "App::PropertyAcceleration",
    "App::PropertyForce",
    "App::PropertyPressure",
    "App::PropertyStiffness",
    "App::PropertyFrequency",
    "App::PropertyTemperature",
    "App::PropertyElectricPotential",
    "App::PropertyElectricCurrent",
    "App::PropertyVacuumPermittivity",
    "App::PropertyPrecision",
    "App::PropertyDensity",
    "App::PropertyMass",
    "App::PropertyPower",
    "App::PropertyWork",
    "App::PropertyLengthConstraint",
    "App::PropertyAngleConstraint",
}
COLOR_TYPES = {"App::PropertyColor"}


# --------------------------------------------------------------------------- to JSON


def _num(value):
    if isinstance(value, float) and (math.isnan(value) or math.isinf(value)):
        return None
    return value


def _vector(vec):
    return [_num(vec.x), _num(vec.y), _num(vec.z)]


def _rotation(rot):
    axis = rot.Axis
    return {
        "axis": _vector(axis),
        "angle": _num(math.degrees(rot.Angle)),
        "quaternion": [_num(q) for q in rot.Q],
    }


def _placement(placement):
    return {"base": _vector(placement.Base), "rotation": _rotation(placement.Rotation)}


def _quantity(quantity):
    out = {"value": _num(quantity.Value), "string": quantity.UserString}
    try:
        out["unit"] = quantity.getUserPreferred()[2]
    except Exception:  # unit systems without a preferred unit
        out["unit"] = str(quantity.Unit)
    return out


def _object_ref(obj):
    if obj is None:
        return None
    ref = {"object": obj.Name}
    return ref


def _is_document_object(value):
    return hasattr(value, "TypeId") and hasattr(value, "Name") and hasattr(value, "Document")


def to_json(value):
    """Convert a property value to a JSON-compatible structure."""
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return _num(value)
    if isinstance(value, FreeCAD.Vector):
        return _vector(value)
    if isinstance(value, FreeCAD.Placement):
        return _placement(value)
    if isinstance(value, FreeCAD.Rotation):
        return _rotation(value)
    if isinstance(value, FreeCAD.Matrix):
        return [_num(v) for v in value.A]
    if isinstance(value, FreeCAD.Units.Quantity):
        return _quantity(value)
    if _is_document_object(value):
        return _object_ref(value)
    if isinstance(value, tuple) and len(value) == 2 and _is_document_object(value[0]):
        return {"object": value[0].Name, "sub": list(value[1])}
    if isinstance(value, (list, tuple)):
        return [to_json(v) for v in value]
    if isinstance(value, dict):
        return {str(k): to_json(v) for k, v in value.items()}
    if hasattr(value, "ShapeType"):
        return _shape_summary(value)
    return str(value)


def _shape_summary(shape):
    try:
        if shape.isNull():
            return {"shape": None}
        bb = shape.BoundBox
        out = {
            "shape": shape.ShapeType,
            "valid": shape.isValid(),
            "boundBox": [bb.XMin, bb.YMin, bb.ZMin, bb.XMax, bb.YMax, bb.ZMax],
            "solids": len(shape.Solids),
            "faces": len(shape.Faces),
            "edges": len(shape.Edges),
            "vertexes": len(shape.Vertexes),
        }
        if shape.Solids:
            out["volume"] = _num(shape.Volume)
        out["area"] = _num(shape.Area)
        return out
    except Exception as err:  # a broken shape
        return {"shape": "invalid", "error": str(err)}


# ------------------------------------------------------------------------- from JSON


def _vector_from(value):
    if isinstance(value, dict):
        return FreeCAD.Vector(value.get("x", 0), value.get("y", 0), value.get("z", 0))
    return FreeCAD.Vector(*value)


def _rotation_from(value):
    if isinstance(value, dict):
        if "quaternion" in value:
            return FreeCAD.Rotation(*value["quaternion"])
        if "yawPitchRoll" in value:
            return FreeCAD.Rotation(*value["yawPitchRoll"])
        return FreeCAD.Rotation(_vector_from(value.get("axis", [0, 0, 1])), value.get("angle", 0))
    if isinstance(value, list) and len(value) == 4:
        return FreeCAD.Rotation(*value)
    raise ValueError("a rotation is {axis, angle}, {quaternion}, {yawPitchRoll} or 4 numbers")


def _placement_from(value):
    if isinstance(value, dict) and "matrix" in value:
        return FreeCAD.Placement(FreeCAD.Matrix(*value["matrix"]))
    if isinstance(value, list) and len(value) == 16:
        return FreeCAD.Placement(FreeCAD.Matrix(*value))
    base = _vector_from(value.get("base", [0, 0, 0]))
    rotation = _rotation_from(value.get("rotation", {"axis": [0, 0, 1], "angle": 0}))
    return FreeCAD.Placement(base, rotation)


def _object_from(doc, value):
    if value is None:
        return None
    name = value["object"] if isinstance(value, dict) else value
    target_doc = doc
    if isinstance(value, dict) and value.get("document"):
        target_doc = App.getDocument(value["document"])
    obj = target_doc.getObject(name)
    if obj is None:
        for candidate in target_doc.Objects:
            if candidate.Label == name:
                obj = candidate
                break
    if obj is None:
        raise ValueError(f"no object named {name!r} in document {target_doc.Name}")
    return obj


def _sub_from(doc, value):
    if value is None:
        return None
    if isinstance(value, dict):
        return (_object_from(doc, value), list(value.get("sub", [])))
    if isinstance(value, (list, tuple)) and len(value) == 2:
        return (_object_from(doc, value[0]), list(value[1]))
    return (_object_from(doc, value), [])


def _quantity_from(value):
    if isinstance(value, str):
        return FreeCAD.Units.Quantity(value)
    if isinstance(value, dict):
        if "string" in value and "value" not in value:
            return FreeCAD.Units.Quantity(value["string"])
        unit = value.get("unit", "")
        return FreeCAD.Units.Quantity(f"{value['value']} {unit}".strip())
    return float(value)


def from_json(obj, name, value):
    """Convert a JSON value to what the property `name` of `obj` accepts."""
    ptype = obj.getTypeIdOfProperty(name)
    doc = obj.Document
    if ptype in VECTOR_TYPES:
        return _vector_from(value)
    if ptype in VECTOR_LIST_TYPES:
        return [_vector_from(v) for v in value]
    if ptype == "App::PropertyPlacement" or ptype == "App::PropertyPlacementList":
        if ptype.endswith("List"):
            return [_placement_from(v) for v in value]
        return _placement_from(value)
    if ptype == "App::PropertyRotation":
        return _rotation_from(value)
    if ptype == "App::PropertyMatrix":
        return FreeCAD.Matrix(*value)
    if ptype in LINK_TYPES:
        return _object_from(doc, value)
    if ptype in LINK_SUB_TYPES:
        return _sub_from(doc, value)
    if ptype in LINK_LIST_TYPES:
        return [_object_from(doc, v) for v in (value or [])]
    if ptype in LINK_SUB_LIST_TYPES:
        return [_sub_from(doc, v) for v in (value or [])]
    if ptype in QUANTITY_TYPES:
        return _quantity_from(value)
    if ptype in COLOR_TYPES:
        return tuple(float(c) for c in value)
    if ptype == "App::PropertyColorList":
        return [tuple(float(c) for c in color) for color in value]
    return value


def python_repr(value):
    """A Python expression that reproduces `value` in a FreeCAD script."""
    if isinstance(value, FreeCAD.Vector):
        return f"App.Vector({value.x!r}, {value.y!r}, {value.z!r})"
    if isinstance(value, FreeCAD.Rotation):
        return f"App.Rotation({python_repr(value.Axis)}, {math.degrees(value.Angle)!r})"
    if isinstance(value, FreeCAD.Placement):
        return f"App.Placement({python_repr(value.Base)}, {python_repr(value.Rotation)})"
    if isinstance(value, FreeCAD.Matrix):
        return "App.Matrix(" + ", ".join(repr(v) for v in value.A) + ")"
    if isinstance(value, FreeCAD.Units.Quantity):
        return repr(value.UserString)
    if _is_document_object(value):
        return f'doc.getObject("{value.Name}")'
    if isinstance(value, tuple) and len(value) == 2 and _is_document_object(value[0]):
        return f"({python_repr(value[0])}, {list(value[1])!r})"
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(python_repr(v) for v in value) + "]"
    return repr(value)


# ------------------------------------------------------------------ object inspection


def _status(obj):
    state = list(getattr(obj, "State", []))
    return {
        "touched": "Touched" in state,
        "error": "Invalid" in state or "Error" in state,
        "valid": obj.isValid() if hasattr(obj, "isValid") else True,
        "state": state,
    }


def _property_info(container, name):
    info = {
        "name": name,
        "type": container.getTypeIdOfProperty(name),
        "group": container.getGroupOfProperty(name),
        "doc": container.getDocumentationOfProperty(name),
    }
    status = list(container.getPropertyStatus(name))
    info["status"] = status
    info["readOnly"] = "ReadOnly" in status or "Immutable" in status
    info["hidden"] = "Hidden" in status
    try:
        info["value"] = to_json(getattr(container, name))
    except Exception as err:
        info["value"] = None
        info["error"] = str(err)
    if info["type"] == "App::PropertyEnumeration":
        try:
            info["enum"] = list(container.getEnumerationsOfProperty(name) or [])
        except Exception:
            pass
    return info


def _expressions(obj):
    try:
        return {path: expr for path, expr in obj.ExpressionEngine}
    except Exception:
        return {}


def object_info(payload):
    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    obj = doc.getObject(args["object"])
    if obj is None:
        raise ValueError(f"no object named {args['object']!r}")
    wanted = args.get("properties")
    include_view = args.get("view", True)

    expressions = _expressions(obj)
    properties = {}
    for name in obj.PropertiesList:
        if wanted and name not in wanted:
            continue
        info = _property_info(obj, name)
        if name in expressions:
            info["expression"] = expressions[name]
        properties[name] = info

    out = {
        "name": obj.Name,
        "label": obj.Label,
        "type": obj.TypeId,
        "document": doc.Name,
        "status": _status(obj),
        "statusString": obj.getStatusString(),
        "properties": properties,
        "inList": [o.Name for o in obj.InList],
        "outList": [o.Name for o in obj.OutList],
    }
    if hasattr(obj, "Shape"):
        out["shape"] = _shape_summary(obj.Shape)
    if include_view and FreeCAD.GuiUp:
        vobj = getattr(obj, "ViewObject", None)
        if vobj is not None:
            view = {}
            for name in vobj.PropertiesList:
                if wanted and name not in wanted:
                    continue
                view[name] = _property_info(vobj, name)
            out["view"] = view
    return json.dumps(out)


def set_properties(payload):
    args = json.loads(payload)
    doc = App.getDocument(args["document"])
    obj = doc.getObject(args["object"])
    if obj is None:
        raise ValueError(f"no object named {args['object']!r}")
    objref = f'App.getDocument("{doc.Name}").getObject("{obj.Name}")'
    changed = []
    lines = []

    for name, value in (args.get("properties") or {}).items():
        if name not in obj.PropertiesList:
            raise ValueError(f"{obj.Name} has no property {name!r}")
        if isinstance(value, dict) and "expression" in value and len(value) == 1:
            expression = value["expression"]
            if expression is None:
                obj.clearExpression(name)
                lines.append(f'{objref}.clearExpression("{name}")')
            else:
                obj.setExpression(name, expression)
                lines.append(f'{objref}.setExpression("{name}", {expression!r})')
            changed.append(name)
            continue
        converted = from_json(obj, name, value)
        setattr(obj, name, converted)
        lines.append(f"{objref}.{name} = {python_repr(converted)}")
        changed.append(name)

    view_values = args.get("view") or {}
    if view_values:
        if not FreeCAD.GuiUp or getattr(obj, "ViewObject", None) is None:
            raise ValueError("view properties need the GUI layer (this server reports gui: false)")
        vobj = obj.ViewObject
        for name, value in view_values.items():
            if name not in vobj.PropertiesList:
                raise ValueError(f"{obj.Name}.ViewObject has no property {name!r}")
            converted = from_json(vobj, name, value)
            setattr(vobj, name, converted)
            lines.append(f"{objref}.ViewObject.{name} = {python_repr(converted)}")
            changed.append("ViewObject." + name)

    return json.dumps({"changed": changed, "python": lines})
