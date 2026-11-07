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
        self.assertFalse(FreeCADGui.getMainWindow().menuBar().isHidden())


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


class CommandToolsTest(unittest.TestCase):
    def setUp(self):
        # Cleanup is registered first so it runs even if the rest of setUp fails;
        # a document left open would make FreeCAD ask to save it on exit.
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup)
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        call("document_new", name="AgenticCommandTest")
        call("workbench_activate", name="PartWorkbench")

    def cleanup(self):
        if call("task_get")["taskPanel"] is not None:
            call("task_reject")
        call("selection_clear")
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_workbenches_and_commands(self):
        listed = call("workbench_list")
        self.assertIn("PartWorkbench", [w["name"] for w in listed["workbenches"]])
        self.assertEqual(listed["active"], "PartWorkbench")
        again = call("workbench_activate", name="PartWorkbench")  # already active
        self.assertEqual(again["workbench"], "PartWorkbench")
        commands = call("command_list", workbench="PartWorkbench")["commands"]
        names = [c["name"] for c in commands]
        self.assertIn("Part_Box", names)
        self.assertIn("Part_Cut", names)
        box = [c for c in commands if c["name"] == "Part_Box"][0]
        self.assertTrue(box["toolbars"])
        self.assertTrue(box["active"])
        # Listing every command tests each one; Std_Expressions used to crash here.
        everything = call("command_list")["commands"]
        self.assertIn("Std_Expressions", [c["name"] for c in everything])
        found = call("command_list", filter="fillet")["commands"]
        self.assertIn("Part_Fillet", [c["name"] for c in found])
        self.assertEqual(call("command_info", name="Part_Box")["menuText"], "Cube")
        with self.assertRaises(RuntimeError):
            call("command_info", name="No_Such_Command")
        with self.assertRaises(RuntimeError):
            call("workbench_activate", name="NoSuchWorkbench")

    def test_command_run_records_python_and_objects(self):
        run = call("command_run", name="Part_Box")
        self.assertIn('App.ActiveDocument.addObject("Part::Box","Box")', run["python"])
        self.assertEqual([o["object"] for o in run["objects"]["created"]], ["Box"])
        self.assertIsNone(run["taskPanel"])
        self.assertTrue(run["saved"][0]["saved"])
        change = call("changes_get")["changes"][-1]
        self.assertEqual(change["kind"], "command")
        self.assertIn("addObject", change["python"])

    def test_inactive_command_is_refused(self):
        with self.assertRaises(RuntimeError) as err:
            call("command_run", name="Part_Cut")  # needs two selected shapes
        self.assertIn("not active", str(err.exception))

    def test_selection_by_sub_element(self):
        call("object_create", type="Part::Box", name="B")
        selected = call("selection_set", items=[{"object": "B", "sub": ["Face6", "Edge1"]}])
        self.assertEqual(selected["selection"][0]["object"], "B")
        self.assertEqual(sorted(selected["selection"][0]["subElements"]), ["Edge1", "Face6"])
        call("object_create", type="Part::Cylinder", name="C")
        both = call("selection_set", items=[{"object": "C"}], add=True)
        self.assertEqual([s["object"] for s in both["selection"]], ["B", "C"])
        self.assertEqual(call("selection_get")["selection"], both["selection"])
        # Part_Cut counts selection entries: two sub-elements of B and C are three.
        self.assertFalse(call("command_info", name="Part_Cut")["active"])
        call("selection_set", items=[{"object": "B"}, {"object": "C"}])
        self.assertTrue(call("command_info", name="Part_Cut")["active"])
        cut = call("command_run", name="Part_Cut")
        self.assertIn("Cut", [o["object"] for o in cut["objects"]["created"]])
        # A boolean's shape has an element map: selection_get still reports Face1.
        result = [o["object"] for o in cut["objects"]["created"]][0]
        mapped = call("selection_set", items=[{"object": result, "sub": ["Face1"]}])["selection"][0]
        self.assertEqual(mapped["subElements"], ["Face1"])
        self.assertTrue(mapped["mappedSubElements"][0].startswith(";"))
        self.assertEqual(call("selection_clear")["selection"], [])
        with self.assertRaises(RuntimeError):
            call("selection_set", items=[{"object": "Missing"}])

    def test_task_panel_is_driven(self):
        run = call("command_run", name="Part_Primitives")
        panel = run["taskPanel"]
        self.assertIsNotNone(panel)
        self.assertIn("Ok", panel["buttons"])
        fields = {f["id"]: f for f in panel["fields"]}
        self.assertIn("PrimitiveTypeCB", fields)
        self.assertIn("Box", fields["PrimitiveTypeCB"]["options"])
        self.assertNotIn("helixPitch", fields)  # on an inactive page
        self.assertFalse([f for f in panel["fields"] if "ActionLabel" in f["id"]])

        changed = call("task_set", fields={"PrimitiveTypeCB": "Box"})
        self.assertEqual(changed["changed"], ["PrimitiveTypeCB"])
        fields = {f["id"]: f for f in changed["fields"]}
        self.assertEqual(fields["PrimitiveTypeCB"]["value"], "Box")
        self.assertIn("boxLength", fields)
        changed = call(
            "task_set",
            fields={"boxLength": "25 mm", "boxWidth": 4, "boxHeight": {"value": 3, "unit": "mm"}},
        )
        fields = {f["id"]: f for f in changed["fields"]}
        self.assertAlmostEqual(fields["boxLength"]["value"]["value"], 25.0)
        self.assertAlmostEqual(fields["boxWidth"]["value"]["value"], 4.0)
        with self.assertRaises(RuntimeError):
            call("task_set", fields={"noSuchField": 1})
        with self.assertRaises(RuntimeError):
            call("task_set", fields={"PrimitiveTypeCB": "Teapot"})

        # Part_Primitives creates on Create (the panel's button), then closes on Close.
        create = [f for f in changed["fields"] if f["kind"] == "button" and "Create" in f["label"]]
        if create:
            call("task_set", fields={create[0]["id"]: True})
        else:
            call("task_accept")
        if call("task_get")["taskPanel"] is not None:
            call("task_reject")
        boxes = [o for o in call("object_list", type="Part::Box")["objects"]]
        self.assertTrue(boxes)
        length = call("object_get", object=boxes[-1]["name"], properties=["Length"])["properties"][
            "Length"
        ]
        self.assertAlmostEqual(length["value"]["value"], 25.0)
        with self.assertRaises(RuntimeError):
            call("task_accept")

    def test_modal_dialogs_are_answered(self):
        code = (
            "from PySide6.QtWidgets import QMessageBox\n"
            "r1 = QMessageBox.question(None, 'Agentic', 'Proceed?')\n"
            "box = QMessageBox(QMessageBox.Warning, 'Agentic', 'Overwrite?', "
            "QMessageBox.Ok | QMessageBox.Cancel)\n"
            "box.setDefaultButton(QMessageBox.Ok)\n"
            "r2 = box.exec()\n"
            "(int(r1) == int(QMessageBox.No), r2 == int(QMessageBox.Ok))"
        )
        run = call("python_run", code=code)
        self.assertTrue(run["ok"], run.get("exception"))
        self.assertEqual(run["value"], [True, True])
        self.assertEqual([d["text"] for d in run["dialogs"]], ["Proceed?", "Overwrite?"])
        self.assertEqual([d["answer"] for d in run["dialogs"]], ["No", "OK"])


def _vec(v):
    return [v.x, v.y, v.z]


class ViewToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup)
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        call("document_new", name="AgenticViewToolsTest")
        call(
            "object_create",
            type="Part::Box",
            name="Box",
            properties={"Length": 10, "Width": 20, "Height": 30},
        )
        call(
            "object_create",
            type="Part::Cylinder",
            name="Cyl",
            properties={"Radius": 2, "Height": 40},
        )

    def cleanup(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def assertDirection(self, actual, expected):
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=5)

    def test_camera_set_and_get(self):
        top = call("camera_set", view="top", fit="all")
        self.assertDirection(top["viewDirection"], [0, 0, -1])
        perspective = call("camera_set", type="Perspective")
        self.assertEqual(perspective["type"], "Perspective")
        call("camera_set", type="Orthographic", direction=[1, 0, 0])
        self.assertDirection(call("camera_get")["viewDirection"], [1, 0, 0])
        saved = call("camera_set", view="isometric", fit=["Cyl"])
        call("camera_set", view="front")
        restored = call("camera_set", inventor=saved["inventor"])
        self.assertDirection(restored["viewDirection"], saved["viewDirection"])
        with self.assertRaises(AssertionError):  # schema rejects an unknown view name
            call("camera_set", view="sideways")

    def test_capture_uses_the_agent_camera_not_the_person_view(self):
        agent = call("camera_set", view="isometric", fit="all")
        person = FreeCADGui.getDocument("AgenticViewToolsTest").ActiveView
        person.viewTop()  # a person orbits the viewer
        FreeCADGui.updateGui()
        shot = call("view_capture", width=120, height=90, inline=False)
        self.assertDirection(shot["camera"]["viewDirection"], agent["viewDirection"])
        self.assertDirection(
            _vec(person.getViewDirection()), [0, 0, -1]
        )  # left as the person had it

    def test_view_options(self):
        options = call(
            "view_options",
            axisCross=True,
            cornerCross=False,
            drawStyle="wireframe",
            background=[0.2, 0.2, 0.2],
        )
        self.assertEqual(
            sorted(options["changed"]), ["axisCross", "background", "cornerCross", "drawStyle"]
        )
        self.assertTrue(options["axisCross"])
        self.assertFalse(options["cornerCross"])
        call("view_options", axisCross=False, drawStyle="as-is")

    def test_capture_returns_an_image_and_keeps_the_camera(self):
        from PySide6.QtGui import QImage

        reference = call("camera_set", view="isometric", fit="all")
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "view_capture",
                "arguments": {"width": 320, "height": 200, "view": "front"},
            },
        }
        raw = json.loads(Agentic.handleRequest(json.dumps(request)))["result"]
        self.assertFalse(raw["isError"])
        self.assertEqual([c["type"] for c in raw["content"]], ["text", "image"])
        self.assertEqual(raw["content"][1]["mimeType"], "image/png")
        result = raw["structuredContent"]
        self.assertEqual((result["width"], result["height"]), (320, 200))
        self.assertTrue(result["path"].startswith(".agentic/captures/"))
        image = QImage(os.path.join(self.root, result["path"]))
        self.assertEqual((image.width(), image.height()), (320, 200))
        # No navigation cube: the top-right corner, where the cube sits, is
        # plain background (the model is fitted to the middle).
        corner = {
            image.pixelColor(x, y).rgb()
            for x in range(image.width() - 90, image.width(), 3)
            for y in range(0, 90, 3)
        }
        self.assertEqual(corner, {image.pixelColor(2, 2).rgb()})
        viewer = FreeCADGui.getDocument("AgenticViewToolsTest").ActiveView.getViewer()
        self.assertTrue(viewer.isEnabledNaviCube())  # the person's view keeps it
        self.assertDirection(result["camera"]["viewDirection"], [0, 1, 0])
        self.assertDirection(call("camera_get")["viewDirection"], reference["viewDirection"])

        only = call(
            "view_capture",
            objects=["Cyl"],
            path="shots/cyl.png",
            inline=False,
            width=200,
            height=200,
        )
        self.assertEqual(only["path"], "shots/cyl.png")
        self.assertTrue(os.path.exists(os.path.join(self.root, "shots", "cyl.png")))
        visible = call("object_get", object="Box", properties=["Visibility"])["view"]["Visibility"][
            "value"
        ]
        self.assertTrue(visible)  # hidden only for the capture

    def test_drawing_export(self):
        template = os.path.join(
            FreeCAD.getResourceDir(),
            "Mod",
            "TechDraw",
            "Templates",
            "Default_Template_A4_Landscape.svg",
        )
        made = call(
            "python_run",
            code=(
                "import TechDraw\n"
                "page = doc.addObject('TechDraw::DrawPage', 'Page')\n"
                "tpl = doc.addObject('TechDraw::DrawSVGTemplate', 'Template')\n"
                f"tpl.Template = {template!r}\n"
                "page.Template = tpl\n"
                "view = doc.addObject('TechDraw::DrawViewPart', 'View')\n"
                "page.addView(view)\n"
                "view.Source = [doc.Box]\n"
                "doc.recompute()\n"
            ),
        )
        self.assertTrue(made["ok"], made.get("exception"))
        for ext in ("svg", "pdf"):
            out = call("drawing_export", page="Page", path=f"drawings/page.{ext}")
            self.assertGreater(out["size"], 0)
            self.assertTrue(os.path.exists(os.path.join(self.root, "drawings", f"page.{ext}")))
        with self.assertRaises(RuntimeError):
            call("drawing_export", page="Box", path="drawings/box.svg")
        # The drawing view is now the active window; camera tools still find the 3D view.
        self.assertIn("viewDirection", call("camera_get"))


class EditModeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup)
        call("project_open", path=os.path.realpath(self.tmp.name))
        call("document_new", name="AgenticEditTest")

    def cleanup(self):
        call("object_edit_end")
        if call("task_get")["taskPanel"] is not None:
            call("task_reject")
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_sketch_edit_mode_round_trip(self):
        call("object_create", type="Sketcher::SketchObject", name="Sketch")
        call(
            "sketch_add",
            sketch="Sketch",
            geometry=[{"type": "circle", "center": [0, 0], "radius": 3}],
        )
        entered = call("object_edit", object="Sketch")
        self.assertTrue(entered["editing"])
        self.assertIsNotNone(entered["taskPanel"])
        # Sketch tools keep working while the sketch is open in the editor.
        added = call("sketch_add", sketch="Sketch", geometry=[{"type": "point", "at": [1, 1]}])
        self.assertEqual(added["added"][0]["indices"], [1])
        left = call("object_edit_end")
        self.assertEqual(left["object"], "Sketch")
        self.assertIsNone(left["taskPanel"])
        again = call("object_edit_end")
        self.assertFalse(again["editing"])
        self.assertIsNone(again["object"])

    def test_feature_edit_opens_its_task_panel(self):
        call("object_create", type="Part::Box", name="Box")
        entered = call("object_edit", object="Box", mode=1)  # transform
        self.assertTrue(entered["editing"])
        call("object_edit_end")
        with self.assertRaises(RuntimeError):
            call("object_edit", object="NoSuchObject")


class AddonWorkbenchTest(unittest.TestCase):
    PREFS = "User parameter:BaseApp/Preferences/Mod/Agentic"

    def setUp(self):
        from TestAgenticApp import make_addon_registry

        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        prefs = FreeCAD.ParamGet(self.PREFS)
        self.addCleanup(
            prefs.SetBool, "UseDefaultRegistry", prefs.GetBool("UseDefaultRegistry", True)
        )
        prefs.SetBool("UseDefaultRegistry", False)
        self.root = os.path.realpath(os.path.join(self.tmp.name, "project"))
        call("project_open", path=self.root, create=True)
        call("addon_registry_add", location=make_addon_registry(os.path.realpath(self.tmp.name)))

    def test_installed_workbench_appears(self):
        installed = call("addon_install", id="Demo", force=True)
        self.assertIn(installed["loaded"]["initGui"], ("ok", None))
        names = [w["name"] for w in call("workbench_list")["workbenches"]]
        self.assertIn("DemoWorkbench", names)
        self.assertEqual(
            call("workbench_activate", name="DemoWorkbench")["workbench"], "DemoWorkbench"
        )
        call("workbench_activate", name="PartWorkbench")


class DocCommandTest(unittest.TestCase):
    def test_command_docs_and_search(self):
        info = call("docs_command", name="PartDesign_Pad")
        self.assertEqual(info["wikiPage"], "PartDesign_Pad")
        self.assertTrue(info["menuText"])
        hits = call("docs_search", query="pad", sources=["command"])["results"]
        self.assertIn("PartDesign_Pad", [h["id"] for h in hits])
        self.assertIsNotNone(call("docs_status")["commands"])
