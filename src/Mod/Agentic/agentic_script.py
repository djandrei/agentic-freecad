# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run agent Python inside FreeCAD for the Agentic script engine.

One namespace per project survives between runs, so an agent can build up
state the way it would in the Python console. Each run captures stdout and
stderr, returns the value of a trailing expression, reports an exception with
its traceback, and is bounded by a timeout enforced through a trace hook (so
a long call into C++, a recompute for instance, cannot be interrupted).
"""

import ast
import io
import json
import sys
import time
import traceback

import FreeCAD

import agentic_props

_namespaces = {}
MAX_OUTPUT = 256 * 1024  # characters of stdout or stderr kept per run


class RunTimeout(Exception):
    pass


def _namespace(root, document):
    ns = _namespaces.get(root)
    if ns is None:
        ns = {"__name__": "__agentic__", "__builtins__": __builtins__}
        ns["FreeCAD"] = FreeCAD
        ns["App"] = FreeCAD
        if FreeCAD.GuiUp:
            import FreeCADGui

            ns["FreeCADGui"] = FreeCADGui
            ns["Gui"] = FreeCADGui
        for module in ("Part",):
            try:
                ns[module] = __import__(module)
            except ImportError:
                pass
        ns["project_root"] = root
        _namespaces[root] = ns
    ns["doc"] = FreeCAD.getDocument(document) if document else None
    return ns


def _split_trailing_expression(tree):
    """Return (body statements, trailing expression node or None)."""
    if tree.body and isinstance(tree.body[-1], ast.Expr):
        return tree.body[:-1], tree.body[-1]
    return tree.body, None


def run(payload):
    args = json.loads(payload)
    code = args.get("code")
    filename = args.get("filename") or "<python_run>"
    timeout = float(args.get("timeoutSeconds") or 0)
    ns = _namespace(args["root"], args.get("document"))

    out = io.StringIO()
    err = io.StringIO()
    result = {"ok": True, "value": None, "stdout": "", "stderr": "", "timedOut": False}

    deadline = time.monotonic() + timeout if timeout > 0 else None

    def tracer(frame, event, arg):
        if deadline is not None and time.monotonic() > deadline:
            raise RunTimeout(f"python_run exceeded {timeout:g} seconds")
        return tracer

    old_stdout, old_stderr = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = out, err
    started = time.monotonic()
    try:
        tree = ast.parse(code, filename=filename, mode="exec")
        body, trailing = _split_trailing_expression(tree)
        if deadline is not None:
            sys.settrace(tracer)
        try:
            if body:
                module = ast.Module(body=body, type_ignores=[])
                exec(compile(module, filename, "exec"), ns)
            if trailing is not None:
                expression = ast.Expression(body=trailing.value)
                ast.copy_location(expression, trailing)
                value = eval(compile(expression, filename, "eval"), ns)
                ns["_"] = value
                try:
                    result["value"] = agentic_props.to_json(value)
                except Exception:
                    result["value"] = repr(value)
        finally:
            if deadline is not None:
                sys.settrace(None)
    except RunTimeout as exc:
        result["ok"] = False
        result["timedOut"] = True
        result["exception"] = {"type": "TimeoutError", "message": str(exc), "traceback": ""}
    except SystemExit as exc:
        result["ok"] = False
        result["exception"] = {"type": "SystemExit", "message": str(exc.code), "traceback": ""}
    except BaseException as exc:  # the agent's error, reported, never propagated
        result["ok"] = False
        tb = traceback.format_exception(type(exc), exc, exc.__traceback__)
        # Drop the frames of this runner; keep the agent's code.
        kept = [line for line in tb if "agentic_script.py" not in line]
        result["exception"] = {
            "type": type(exc).__name__,
            "message": str(exc),
            "traceback": "".join(kept),
        }
    finally:
        sys.stdout, sys.stderr = old_stdout, old_stderr

    for name, stream in (("stdout", out), ("stderr", err)):
        text = stream.getvalue()
        if len(text) > MAX_OUTPUT:
            # Keep the end: the last lines say how a long run ended.
            result[name] = text[-MAX_OUTPUT:]
            result[name + "Truncated"] = len(text) - MAX_OUTPUT
        else:
            result[name] = text
    result["duration"] = time.monotonic() - started
    return json.dumps(result)


def reset(payload):
    """Forget the namespace of a project."""
    args = json.loads(payload)
    _namespaces.pop(args["root"], None)
    return json.dumps({"reset": True})
