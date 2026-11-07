# SPDX-License-Identifier: LGPL-2.1-or-later
"""Console-side initialization of the Agentic module.

This script runs inside App::Application::init(), before any Qt application
exists. That is the only place where the Qt platform plugin can still be
chosen, so a headless agent launch selects the offscreen platform here.
"""

import os

import FreeCAD


def _config(key):
    try:
        return FreeCAD.ConfigGet(key)
    except Exception:  # the key is absent
        return ""


if _config("AgentHeadless") == "1":
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

FreeCAD.__unit_test__ += ["TestAgenticApp"]
