# SPDX-License-Identifier: LGPL-2.1-or-later
"""Regenerate doc/agentic/tools.md with every tool group registered.

FreeCAD runs a file given on its command line as a module named after the
file, once the GUI is initialised, so the GUI tools are registered by then:

    QT_QPA_PLATFORM=offscreen build/relWithDebInfo/bin/FreeCAD tests/agentic/make_tools_reference.py

AGENTIC_TOOLS_MD overrides the output path (CI writes elsewhere and diffs).
"""

import os
import sys

import tools_reference

status = 1
try:
    status = tools_reference.main(os.environ.get("AGENTIC_TOOLS_MD", "doc/agentic/tools.md"))
finally:
    sys.stdout.flush()
    sys.stderr.flush()
    # Leave without the GUI's shutdown, which could stop on a prompt.
    os._exit(status)
