# SPDX-License-Identifier: LGPL-2.1-or-later
"""GUI-side initialization of the Agentic module.

Importing AgenticGui has a side effect: when the application was started in
agent mode it installs the agent viewer shell on the main window and starts
the configured MCP transport once the GUI is initialized.
"""

import FreeCAD
import AgenticGui  # noqa: F401

FreeCAD.__unit_test__ += ["TestAgenticGui"]
