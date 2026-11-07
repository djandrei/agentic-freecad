# SPDX-License-Identifier: LGPL-2.1-or-later
"""GUI tests of the Agentic module.

The capture test is the stage 0 proof that a 3D view can be rendered to an
image without a visible window. It is meant to run both under a display
(xvfb-run FreeCAD -t TestAgenticGui) and under the offscreen platform plugin
(QT_QPA_PLATFORM=offscreen FreeCAD -t TestAgenticGui).
"""

import os
import tempfile
import unittest

import FreeCAD
import FreeCADGui
import Agentic


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
