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


def main():
    tmp = tempfile.mkdtemp(prefix="agentic-viewer-")
    try:
        stage_v0(os.path.join(tmp, "project"))
    except Exception:
        log("FAIL exception\n" + traceback.format_exc())
        failures.append("exception")
    log(f"viewer_check: {len(failures)} failure(s)")
    os._exit(1 if failures else 0)


main()
