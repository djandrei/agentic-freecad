# SPDX-License-Identifier: LGPL-2.1-or-later
"""GUI tests of the Agentic module.

The capture test is the stage 0 proof that a 3D view can be rendered to an
image without a visible window. It is meant to run both under a display
(xvfb-run FreeCAD -t TestAgenticGui) and under the offscreen platform plugin
(QT_QPA_PLATFORM=offscreen FreeCAD -t TestAgenticGui).
"""

import json
import os
import tempfile
import unittest

import FreeCAD
import FreeCADGui
import Agentic


def call(tool, **arguments):
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": tool, "arguments": arguments},
    }
    response = json.loads(Agentic.handleRequest(json.dumps(request)))
    if "error" in response:
        raise AssertionError(f"{tool}: protocol error {response['error']}")
    result = response["result"]
    if result["isError"]:
        raise RuntimeError(result["content"][0]["text"])
    return result["structuredContent"]


class AgenticGuiModuleTest(unittest.TestCase):
    def test_gui_is_reported_available(self):
        self.assertTrue(Agentic.config()["gui"])

    def test_test_run_keeps_classic_window(self):
        # Without --agent the viewer shell must not touch the main window.
        self.assertFalse(Agentic.isAgentMode())
        self.assertNotEqual(FreeCADGui.getMainWindow().windowTitle(), "FreeCAD (agent mode)")


class OffscreenCaptureTest(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("AgenticCapture")
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)
        self.tmp.cleanup()

    def test_box_renders_to_png(self):
        from PySide6.QtGui import QImage

        box = self.doc.addObject("Part::Box", "Box")
        box.Length = 20
        box.Width = 20
        box.Height = 20
        self.doc.recompute()
        FreeCADGui.updateGui()

        view = FreeCADGui.getDocument(self.doc.Name).ActiveView
        self.assertIsNotNone(view, "the document has no 3D view")
        view.viewIsometric()
        view.fitAll()
        FreeCADGui.updateGui()

        path = os.path.join(self.tmp.name, "box.png")
        view.saveImage(path, 320, 240, "White")
        self.assertTrue(os.path.exists(path), "saveImage wrote no file")
        self.assertGreater(os.path.getsize(path), 0)

        image = QImage(path)
        self.assertFalse(image.isNull())
        self.assertEqual((image.width(), image.height()), (320, 240))

        background = image.pixelColor(2, 2)
        center = image.pixelColor(image.width() // 2, image.height() // 2)
        self.assertGreater(background.lightness(), 200, "background is not white")
        self.assertNotEqual(
            (center.red(), center.green(), center.blue()),
            (background.red(), background.green(), background.blue()),
            "the box is not visible at the image center",
        )


class ViewPropertyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        call("project_open", path=os.path.realpath(self.tmp.name))
        call("document_new", name="AgenticViewTest")

    def tearDown(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_view_properties_are_read_and_set(self):
        created = call("object_create", type="Part::Box", name="B", view={"Transparency": 40})
        self.assertEqual(created["changed"], ["ViewObject.Transparency"])
        info = call("object_get", object="B")
        self.assertIn("view", info)
        self.assertEqual(info["view"]["Transparency"]["value"], 40)
        self.assertEqual(info["view"]["Visibility"]["value"], True)
        self.assertIn("DisplayMode", info["view"])
        self.assertIn("Flat Lines", info["view"]["DisplayMode"]["enum"])

        changed = call(
            "object_set",
            object="B",
            view={"Visibility": False, "LineWidth": 3, "DisplayMode": "Wireframe"},
        )
        self.assertEqual(
            sorted(changed["changed"]),
            ["ViewObject.DisplayMode", "ViewObject.LineWidth", "ViewObject.Visibility"],
        )
        view = call(
            "object_get", object="B", properties=["Visibility", "LineWidth", "DisplayMode"]
        )["view"]
        self.assertFalse(view["Visibility"]["value"])
        self.assertEqual(view["LineWidth"]["value"], 3)
        self.assertEqual(view["DisplayMode"]["value"], "Wireframe")
        python = [c["python"] for c in call("changes_get")["changes"] if c["kind"] == "property"][
            -1
        ]
        self.assertIn(".ViewObject.Visibility = False", python)
        with self.assertRaises(RuntimeError):
            call("object_set", object="B", view={"DisplayMode": "Nope"})
