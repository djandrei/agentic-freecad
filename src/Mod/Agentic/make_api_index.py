# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the Python API index of FreeCAD from the binding stubs (.pyi).

Runs at build time with the standard library only (no FreeCAD import):

    python make_api_index.py <source root> <output.json>

The stubs are the same files the binding generator and the published typing
stubs are made from, so the index always matches the binary. Classes take
their Python name from @export(PythonName=...) when given, otherwise from
their location (src/Mod/Part/App/X.pyi -> Part.X); module stubs
(<Module>.module.pyi) give module-level functions.
"""

import ast
import json
import os
import sys


def _docstring(body):
    if (
        body
        and isinstance(body[0], ast.Expr)
        and isinstance(getattr(body[0], "value", None), ast.Constant)
    ):
        value = body[0].value.value
        if isinstance(value, str):
            return _clean(value)
    return ""


def _clean(text):
    lines = text.expandtabs().splitlines()
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    indent = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
    return "\n".join(l[indent:] for l in lines)


def _export_args(node):
    for deco in node.decorator_list:
        if (
            isinstance(deco, ast.Call)
            and getattr(deco.func, "id", getattr(deco.func, "attr", "")) == "export"
        ):
            out = {}
            for kw in deco.keywords:
                if isinstance(kw.value, ast.Constant):
                    out[kw.arg] = kw.value.value
            return out
    return {}


def _signature(fn):
    try:
        args = ast.unparse(fn.args)
    except Exception:
        args = "..."
    ret = ""
    if fn.returns is not None:
        try:
            ret = " -> " + ast.unparse(fn.returns)
        except Exception:
            pass
    return f"{fn.name}({args}){ret}"


def _deprecated(fn):
    for deco in fn.decorator_list:
        if isinstance(deco, ast.Call) and getattr(deco.func, "id", "") == "deprecated":
            return {
                kw.arg: kw.value.value for kw in deco.keywords if isinstance(kw.value, ast.Constant)
            }
    return None


def _module_from_path(relative):
    parts = relative.replace(os.sep, "/").split("/")
    # src/App/X.pyi, src/Base/X.pyi, src/Gui/X.pyi, src/Mod/<M>/App|Gui/.../X.pyi
    if parts[:2] == ["src", "App"]:
        return "FreeCAD"
    if parts[:2] == ["src", "Base"]:
        return "FreeCAD.Base"
    if parts[:2] == ["src", "Gui"]:
        return "FreeCADGui"
    if parts[:2] == ["src", "Mod"] and len(parts) > 3:
        return parts[2] + ("Gui" if parts[3] == "Gui" else "")
    return parts[-2] if len(parts) > 1 else ""


def _members(cls_node):
    members = []
    body = cls_node.body
    for i, node in enumerate(body):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name.startswith("_") and node.name != "__init__":
                continue
            is_overload = any(
                getattr(d, "id", getattr(d, "attr", "")) == "overload" for d in node.decorator_list
            )
            entry = {
                "name": node.name,
                "kind": "method",
                "signature": _signature(node),
                "doc": _docstring(node.body),
            }
            if is_overload:
                entry["overload"] = True
            dep = _deprecated(node)
            if dep:
                entry["deprecated"] = dep
            if any(getattr(d, "id", "") == "property" for d in node.decorator_list):
                entry["kind"] = "attribute"
            members.append(entry)
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            doc = ""
            if (
                i + 1 < len(body)
                and isinstance(body[i + 1], ast.Expr)
                and isinstance(getattr(body[i + 1], "value", None), ast.Constant)
            ):
                if isinstance(body[i + 1].value.value, str):
                    doc = _clean(body[i + 1].value.value)
            try:
                annotation = ast.unparse(node.annotation)
            except Exception:
                annotation = ""
            members.append(
                {
                    "name": node.target.id,
                    "kind": "attribute",
                    "type": annotation,
                    "readOnly": annotation.startswith("Final"),
                    "doc": doc,
                }
            )
    # Merge overloads: keep one entry per name, with every signature.
    merged = {}
    for m in members:
        if m["name"] in merged and m["kind"] == "method":
            target = merged[m["name"]]
            target.setdefault("signatures", [target["signature"]])
            if m["signature"] not in target["signatures"]:
                target["signatures"].append(m["signature"])
            if m["doc"] and not m.get("overload"):
                target["doc"] = m["doc"]
        else:
            merged[m["name"]] = m
    for m in merged.values():
        m.pop("overload", None)
    return list(merged.values())


def build(root):
    entries = []
    for dirpath, dirnames, filenames in os.walk(os.path.join(root, "src")):
        dirnames[:] = sorted(d for d in dirnames if d not in ("3rdParty", "Tools", "__pycache__"))
        for filename in sorted(filenames):
            if not filename.endswith(".pyi"):
                continue
            path = os.path.join(dirpath, filename)
            relative = os.path.relpath(path, root)
            try:
                with open(path, encoding="utf-8") as handle:
                    tree = ast.parse(handle.read(), filename=path)
            except (SyntaxError, UnicodeDecodeError):
                continue
            if filename.endswith(".module.pyi"):
                module = filename[: -len(".module.pyi")]
                for node in tree.body:
                    if isinstance(
                        node, (ast.FunctionDef, ast.AsyncFunctionDef)
                    ) and not node.name.startswith("_"):
                        entries.append(
                            {
                                "name": f"{module}.{node.name}",
                                "kind": "function",
                                "module": module,
                                "signature": _signature(node),
                                "doc": _docstring(node.body),
                                "source": relative,
                            }
                        )
                continue
            for node in tree.body:
                if not isinstance(node, ast.ClassDef) or node.name.startswith("_"):
                    continue
                export = _export_args(node)
                if export.get("PythonName"):
                    name = export["PythonName"]
                else:
                    short = node.name[:-2] if node.name.endswith("Py") else node.name
                    name = f"{_module_from_path(relative)}.{short}"
                bases = []
                for base in node.bases:
                    try:
                        bases.append(ast.unparse(base))
                    except Exception:
                        pass
                entry = {
                    "name": name,
                    "kind": "class",
                    "class": node.name,
                    "bases": bases,
                    "doc": _docstring(node.body),
                    "members": _members(node),
                    "source": relative,
                }
                # Base types are also reachable directly on FreeCAD (FreeCAD.Vector).
                if name.startswith("FreeCAD.Base."):
                    entry["aliases"] = ["FreeCAD." + name[len("FreeCAD.Base.") :]]
                entries.append(entry)
    # Merge duplicate function entries from overloads.
    seen = {}
    out = []
    for e in entries:
        key = (e["kind"], e["name"])
        if key in seen and e["kind"] == "function":
            prev = seen[key]
            prev.setdefault("signatures", [prev["signature"]]).append(e["signature"])
            if e["doc"]:
                prev["doc"] = e["doc"]
            continue
        seen[key] = e
        out.append(e)
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    root, output = sys.argv[1], sys.argv[2]
    entries = build(root)
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    temp = output + ".tmp"
    with open(temp, "w", encoding="utf-8") as handle:
        json.dump(
            {"generatedFrom": "pyi binding stubs", "entries": entries},
            handle,
            indent=None,
            separators=(",", ":"),
        )
    os.replace(temp, output)
    classes = sum(1 for e in entries if e["kind"] == "class")
    print(f"api index: {classes} classes, {len(entries) - classes} functions -> {output}")


if __name__ == "__main__":
    main()
