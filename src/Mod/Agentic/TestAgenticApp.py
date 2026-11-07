# SPDX-License-Identifier: LGPL-2.1-or-later
"""Console tests of the Agentic module: the MCP protocol answered in-process."""

import json
import os
import tempfile
import unittest

import FreeCAD
import Agentic


def rpc(method, params=None, request_id=1):
    request = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        request["params"] = params
    response = Agentic.handleRequest(json.dumps(request))
    return json.loads(response)


def call(tool, **arguments):
    """Call a tool; return structuredContent, or raise with the tool error text."""
    response = rpc("tools/call", {"name": tool, "arguments": arguments})
    if "error" in response:
        raise AssertionError(f"{tool}: protocol error {response['error']}")
    result = response["result"]
    if result["isError"]:
        raise RuntimeError(result["content"][0]["text"])
    return result["structuredContent"]


class AgenticModuleTest(unittest.TestCase):
    def test_version_matches_freecad(self):
        version = FreeCAD.Version()
        self.assertTrue(Agentic.version().startswith(f"{version[0]}.{version[1]}"))

    def test_test_run_is_not_agent_mode(self):
        self.assertFalse(Agentic.isAgentMode())
        cfg = Agentic.config()
        self.assertFalse(cfg["enabled"])
        self.assertEqual(cfg["project"], "")


class AgenticProtocolTest(unittest.TestCase):
    def test_initialize(self):
        response = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
        self.assertEqual(response["result"]["serverInfo"]["name"], "agentic-freecad")
        self.assertIn("tools", response["result"]["capabilities"])

    def test_tools_list(self):
        response = rpc("tools/list")
        names = [tool["name"] for tool in response["result"]["tools"]]
        self.assertIn("version", names)
        self.assertIn("status", names)

    def test_version_tool(self):
        response = rpc("tools/call", {"name": "version", "arguments": {}})
        result = response["result"]
        self.assertFalse(result["isError"])
        structured = result["structuredContent"]
        self.assertEqual(structured["build"]["major"], FreeCAD.Version()[0])
        self.assertEqual(json.loads(result["content"][0]["text"]), structured)

    def test_status_tool(self):
        response = rpc("tools/call", {"name": "status"})
        status = response["result"]["structuredContent"]
        self.assertFalse(status["agentMode"])
        self.assertEqual(status["transport"], "none")

    def test_unknown_method(self):
        response = rpc("completion/complete")
        self.assertEqual(response["error"]["code"], -32601)

    def test_notification_has_no_response(self):
        line = json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"})
        self.assertEqual(Agentic.handleRequest(line), "")

    def test_parse_error(self):
        response = json.loads(Agentic.handleRequest("{"))
        self.assertEqual(response["error"]["code"], -32700)
        self.assertIsNone(response["id"])

    def test_invalid_arguments_rejected(self):
        response = rpc("tools/call", {"name": "version", "arguments": {"extra": 1}})
        self.assertEqual(response["error"]["code"], -32602)
        self.assertIn("unknown property 'extra'", response["error"]["message"])

    def test_resources(self):
        listed = rpc("resources/list")["result"]["resources"]
        self.assertIn("agentic://status", [r["uri"] for r in listed])
        read = rpc("resources/read", {"uri": "agentic://status"})["result"]["contents"][0]
        self.assertEqual(read["mimeType"], "application/json")
        self.assertFalse(json.loads(read["text"])["agentMode"])
        missing = rpc("resources/read", {"uri": "agentic://missing"})
        self.assertEqual(missing["error"]["code"], -32002)

    def test_prompts(self):
        listed = rpc("prompts/list")["result"]["prompts"]
        self.assertIn("agentic-loop", [p["name"] for p in listed])
        got = rpc("prompts/get", {"name": "agentic-loop"})["result"]
        self.assertEqual(got["messages"][0]["role"], "user")
        self.assertIn("FreeCAD", got["messages"][0]["content"]["text"])

    def test_tool_reference_renders(self):
        import tools_reference

        text = tools_reference.render()
        self.assertIn("### `version`", text)
        self.assertIn("agentic://status", text)
        self.assertIn("### `agentic-loop`", text)


class ProjectToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.realpath(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_project_open_creates_settings_and_is_current(self):
        info = call("project_open", path=self.root)
        self.assertEqual(info["root"], self.root)
        self.assertTrue(os.path.exists(os.path.join(self.root, ".freecad-project.json")))
        listed = call("project_list")
        self.assertEqual(listed["current"], self.root)
        self.assertIn(self.root, [p["root"] for p in listed["projects"]])
        created = os.path.join(self.root, "sub", "p")
        info = call("project_open", path=created, create=True)
        self.assertEqual(info["name"], "p")
        with self.assertRaises(RuntimeError):
            call("project_open", path=os.path.join(self.root, "missing"))

    def test_project_set_persists(self):
        call("project_open", path=self.root)
        info = call("project_set", mainDocument="main.FCStd", autoRecompute=False)
        self.assertEqual(info["mainDocument"], "main.FCStd")
        self.assertFalse(info["autoRecompute"])
        with open(os.path.join(self.root, ".freecad-project.json")) as handle:
            self.assertEqual(json.load(handle)["mainDocument"], "main.FCStd")
        info = call("project_set", mainDocument=None)
        self.assertIsNone(info["mainDocument"])
        changes = call("changes_get")["changes"]
        self.assertEqual([c["kind"] for c in changes], ["project", "project"])

    def test_file_tools_round_trip(self):
        call("project_open", path=self.root)
        written = call("file_write", path="scripts/box.py", content="import Part\n")
        self.assertTrue(written["created"])
        self.assertEqual(written["hunk"]["linesAdded"], 1)
        read = call("file_read", path="scripts/box.py")
        self.assertEqual(read["content"], "import Part\n")
        self.assertEqual(read["version"], written["version"])

        edited = call(
            "file_write",
            path="scripts/box.py",
            edits=[{"oldText": "import Part", "newText": "import Part, FreeCAD"}],
            baseVersion=read["version"],
        )
        self.assertFalse(edited["created"])
        self.assertEqual(
            call("file_read", path="scripts/box.py")["content"], "import Part, FreeCAD\n"
        )
        with self.assertRaises(RuntimeError) as conflict:
            call("file_write", path="scripts/box.py", content="x", baseVersion=read["version"])
        self.assertIn("changed since", str(conflict.exception))
        with self.assertRaises(RuntimeError):
            call("file_write", path="scripts/box.py")

        listing = call("file_list", recursive=True)["entries"]
        self.assertIn("scripts/box.py", [e["path"] for e in listing])
        self.assertNotIn(".freecad-project.json", [e["path"] for e in listing])
        call("file_rename", **{"from": "scripts/box.py", "to": "box.py"})
        call("file_delete", path="box.py")
        call("file_delete", path="scripts")
        self.assertEqual(call("file_list")["entries"], [])
        with self.assertRaises(RuntimeError):
            call("file_read", path="../etc/passwd")

        kinds = [c["kind"] for c in call("changes_get")["changes"]]
        self.assertEqual(kinds, ["file"] * 5)
        summaries = [c["summary"] for c in call("changes_get")["changes"]]
        self.assertTrue(summaries[0].startswith("created scripts/box.py"))

    def test_log_get_sees_console_messages(self):
        before = call("log_get", limit=1)["next"]
        FreeCAD.Console.PrintWarning("agentic test warning\n")
        FreeCAD.Console.PrintError("agentic test error\n")
        FreeCAD.Console.PrintMessage("agentic test message\n")
        logged = call("log_get", since=before)["messages"]
        texts = [(m["level"], m["text"]) for m in logged]
        self.assertIn(("warning", "agentic test warning"), texts)
        self.assertIn(("error", "agentic test error"), texts)
        self.assertIn(("message", "agentic test message"), texts)
        errors = call("log_get", since=before, level="error")["messages"]
        self.assertEqual([m["level"] for m in errors], ["error"])

    def test_no_project_is_an_error(self):
        # Projects opened by other tests stay open; address a name that is not.
        with self.assertRaises(RuntimeError):
            call("file_list", project="no-such-project")


class DocumentToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        self.doc = call("document_new", name="AgenticDocTest")["name"]

    def tearDown(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_new_document_is_saved_and_listed(self):
        self.assertTrue(os.path.exists(os.path.join(self.root, self.doc + ".FCStd")))
        listed = call("document_list")
        self.assertEqual(listed["active"], self.doc)
        self.assertEqual([d["name"] for d in listed["documents"]], [self.doc])
        self.assertTrue(listed["files"][0]["open"])

    def test_create_set_get_box(self):
        created = call(
            "object_create",
            type="Part::Box",
            name="MyBox",
            properties={"Length": "20 mm", "Width": 10, "Height": 5},
        )
        self.assertEqual(created["object"], "MyBox")
        self.assertFalse(created["recompute"]["hasError"])
        self.assertTrue(created["saved"])

        info = call("object_get", object="MyBox")
        self.assertEqual(info["type"], "Part::Box")
        length = info["properties"]["Length"]
        self.assertEqual(length["type"], "App::PropertyLength")
        self.assertAlmostEqual(length["value"]["value"], 20.0)
        self.assertEqual(info["shape"]["shape"], "Solid")
        self.assertAlmostEqual(info["shape"]["volume"], 1000.0)
        self.assertNotIn("view", info)  # the console binary has no GUI layer

        changed = call(
            "object_set",
            object="MyBox",
            properties={
                "Height": {"expression": "MyBox.Width * 2"},
                "Placement": {"base": [1, 2, 3], "rotation": {"axis": [0, 0, 1], "angle": 90}},
                "Label": "The box",
            },
        )
        self.assertEqual(sorted(changed["changed"]), ["Height", "Label", "Placement"])
        info = call("object_get", object="MyBox", properties=["Height", "Placement"])
        # FreeCAD stores a self reference without the object prefix.
        self.assertTrue(info["properties"]["Height"]["expression"].endswith("Width * 2"))
        self.assertAlmostEqual(info["properties"]["Height"]["value"]["value"], 20.0)
        self.assertEqual(info["properties"]["Placement"]["value"]["base"], [1.0, 2.0, 3.0])
        self.assertAlmostEqual(info["properties"]["Placement"]["value"]["rotation"]["angle"], 90.0)
        self.assertEqual(call("object_get", object="The box")["name"], "MyBox")

        with self.assertRaises(RuntimeError):
            call("object_set", object="MyBox", properties={"NoSuchProperty": 1})
        with self.assertRaises(RuntimeError):
            call("object_set", object="MyBox", view={"Transparency": 50})

        kinds = [c["kind"] for c in call("changes_get")["changes"]]
        self.assertIn("object", kinds)
        self.assertIn("property", kinds)
        python = [c["python"] for c in call("changes_get")["changes"] if c["kind"] == "property"][0]
        self.assertIn('setExpression("Height"', python)
        self.assertIn("App.Placement(", python)

    def test_links_errors_undo_redo_dependencies(self):
        call("object_create", type="Part::Box", name="A")
        call(
            "object_create", type="Part::Cylinder", name="B", properties={"Radius": 2, "Height": 20}
        )
        cut = call("object_create", type="Part::Cut", name="Cut")
        self.assertTrue(cut["recompute"]["hasError"])
        self.assertEqual([e["name"] for e in cut["recompute"]["objectsInError"]], ["Cut"])

        fixed = call("object_set", object="Cut", properties={"Base": "A", "Tool": {"object": "B"}})
        self.assertFalse(fixed["recompute"]["hasError"])
        info = call("object_get", object="Cut", properties=["Base", "Tool"])
        self.assertEqual(info["properties"]["Base"]["value"], {"object": "A"})
        self.assertEqual(info["outList"], ["A", "B"])
        self.assertLess(info["shape"]["volume"], 1000.0)

        deps = call("object_dependencies", object="Cut")
        self.assertIn({"from": "Cut", "to": "A"}, deps["dependsOn"])
        self.assertEqual({n["name"] for n in deps["objects"]}, {"A", "B", "Cut"})

        undone = call("document_undo")
        self.assertTrue(undone["undone"])
        self.assertEqual(undone["transaction"], "object_set Cut")
        self.assertIsNone(
            call("object_get", object="Cut", properties=["Base"])["properties"]["Base"]["value"]
        )
        redone = call("document_redo")
        self.assertTrue(redone["redone"])
        self.assertEqual(
            call("object_get", object="Cut", properties=["Base"])["properties"]["Base"]["value"],
            {"object": "A"},
        )

        deleted = call("object_delete", object="B")
        self.assertEqual(deleted["dependentsTouched"], ["Cut"])
        self.assertTrue(deleted["recompute"]["hasError"])
        self.assertEqual(len(call("object_list")["objects"]), 2)
        boxes = call("object_list", type="Part::Box")["objects"]
        self.assertEqual([o["name"] for o in boxes], ["A"])

    def test_close_and_reopen_from_file(self):
        call("object_create", type="Part::Box", name="Kept")
        file = call("document_list")["documents"][0]["file"]
        closed = call("document_close")
        self.assertTrue(closed["closed"])
        self.assertIsNone(closed["active"])
        reopened = call("document_open", file=file)
        self.assertEqual(reopened["objects"], 1)
        self.assertEqual(call("object_list")["objects"][0]["name"], "Kept")
        with self.assertRaises(RuntimeError):
            call("document_open", file="../outside.FCStd")
        saved = call("document_save", **{"as": "copies/kept-copy.FCStd"})
        self.assertEqual(saved["file"], "copies/kept-copy.FCStd")
        self.assertTrue(os.path.exists(os.path.join(self.root, "copies", "kept-copy.FCStd")))

    def test_unknown_type_is_an_error(self):
        with self.assertRaises(RuntimeError) as err:
            call("object_create", type="Nope::Thing")
        self.assertIn("unknown object type", str(err.exception))


class PreferenceToolsTest(unittest.TestCase):
    PATH = "BaseApp/Preferences/Mod/AgenticTest"

    def tearDown(self):
        grp = FreeCAD.ParamGet("User parameter:" + self.PATH)
        grp.Clear()

    def test_set_and_get_round_trip(self):
        call("preferences_set", path=self.PATH, key="Flag", value=True)
        call("preferences_set", path=self.PATH, key="Count", value=3)
        call("preferences_set", path=self.PATH, key="Ratio", value=1.5)
        call("preferences_set", path=self.PATH, key="Name", value="box")
        call("preferences_set", path=self.PATH, key="Color", value=4294902015, type="unsigned")
        got = call("preferences_get", path=self.PATH)
        self.assertEqual(got["path"], "User parameter:" + self.PATH)
        values = got["values"]
        self.assertEqual(values["Flag"], {"type": "bool", "value": True})
        self.assertEqual(values["Count"], {"type": "int", "value": 3})
        self.assertEqual(values["Ratio"]["type"], "float")
        self.assertEqual(values["Name"], {"type": "string", "value": "box"})
        self.assertEqual(values["Color"], {"type": "unsigned", "value": 4294902015})
        self.assertTrue(FreeCAD.ParamGet("User parameter:" + self.PATH).GetBool("Flag"))
        removed = call("preferences_set", path=self.PATH, key="Flag", value=None)
        self.assertNotIn("Flag", removed["values"])
        units = call("preferences_get", path="BaseApp/Preferences/Units")
        self.assertIn("values", units)


class PythonRunTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        call("python_reset")
        self.doc = call("document_new", name="AgenticRunTest")["name"]

    def tearDown(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_value_output_and_namespace(self):
        run = call("python_run", code="x = 20\nprint('hello')\nx * 2")
        self.assertTrue(run["ok"])
        self.assertEqual(run["value"], 40)
        self.assertEqual(run["stdout"], "hello\n")
        again = call("python_run", code="x + 1")
        self.assertEqual(again["value"], 21)
        self.assertEqual(call("python_run", code="doc.Name")["value"], self.doc)
        self.assertEqual(call("python_run", code="project_root")["value"], self.root)
        vec = call("python_run", code="App.Vector(1, 2, 3)")["value"]
        self.assertEqual(vec, [1.0, 2.0, 3.0])
        call("python_reset")
        failed = call("python_run", code="x")
        self.assertFalse(failed["ok"])
        self.assertEqual(failed["exception"]["type"], "NameError")

    def test_exception_has_traceback_and_rolls_back(self):
        call("object_create", type="Part::Box", name="B", properties={"Length": 10})
        run = call("python_run", code="doc.B.Length = 99\nraise ValueError('boom')")
        self.assertFalse(run["ok"])
        self.assertEqual(run["exception"]["type"], "ValueError")
        self.assertEqual(run["exception"]["message"], "boom")
        self.assertIn("line 2", run["exception"]["traceback"])
        self.assertTrue(run["transaction"]["rolledBack"])
        length = call("object_get", object="B", properties=["Length"])["properties"]["Length"]
        self.assertAlmostEqual(length["value"]["value"], 10.0)

    def test_objects_are_tracked_and_saved(self):
        run = call(
            "python_run",
            code="b = doc.addObject('Part::Box', 'Made')\nb.Length = 5\ndoc.recompute()\nb.Shape.Volume",
        )
        self.assertTrue(run["ok"])
        self.assertAlmostEqual(run["value"], 500.0)
        self.assertEqual(run["objects"]["created"], [{"document": self.doc, "object": "Made"}])
        self.assertTrue(run["transaction"]["committed"])
        self.assertEqual([s["document"] for s in run["saved"]], [self.doc])
        self.assertTrue(os.path.exists(os.path.join(self.root, self.doc + ".FCStd")))

        run = call("python_run", code="doc.Made.Width = 7\ndoc.removeObject('Made')")
        self.assertEqual(run["objects"]["deleted"], [{"document": self.doc, "object": "Made"}])
        self.assertEqual(run["objects"]["changed"], [])
        undone = call("document_undo")
        self.assertEqual(undone["transaction"], "python_run")
        self.assertEqual([o["name"] for o in call("object_list")["objects"]], ["Made"])

    def test_file_mode_and_messages(self):
        call(
            "file_write",
            path="scripts/make.py",
            content="import FreeCAD\nFreeCAD.Console.PrintWarning('careful\\n')\nresult = 7\n",
        )
        run = call("python_run", file="scripts/make.py")
        self.assertTrue(run["ok"])
        self.assertEqual(
            [m["text"] for m in run["messages"] if m["level"] == "warning"], ["careful"]
        )
        self.assertEqual(run["warnings"], 1)
        self.assertEqual(call("python_run", code="result")["value"], 7)
        with self.assertRaises(RuntimeError):
            call("python_run", code="1", file="scripts/make.py")
        with self.assertRaises(RuntimeError):
            call("python_run", file="scripts/missing.py")
        changes = call("changes_get")["changes"]
        self.assertEqual(changes[-1]["kind"], "python")
        self.assertIn("scripts/make.py", changes[-2]["summary"])

    def test_timeout_stops_python(self):
        run = call("python_run", code="while True:\n    pass", timeoutSeconds=0.5)
        self.assertFalse(run["ok"])
        self.assertTrue(run["timedOut"])
        self.assertEqual(run["exception"]["type"], "TimeoutError")
        self.assertLess(run["duration"], 5.0)

    def test_script_created_document_is_adopted(self):
        run = call(
            "python_run",
            code="d = App.newDocument('FromScript')\nd.addObject('Part::Box', 'X')\nd.recompute()\nd.Name",
        )
        self.assertTrue(run["ok"])
        self.assertIn("FromScript", [s["document"] for s in run["saved"]])
        self.assertIn("FromScript", [d["name"] for d in call("document_list")["documents"]])
        self.assertTrue(os.path.exists(os.path.join(self.root, "FromScript.FCStd")))


class ShapeToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup)
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        call("document_new", name="AgenticShapeTest")
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
        call(
            "object_create", type="Part::Cut", name="Cut", properties={"Base": "Box", "Tool": "Cyl"}
        )

    def cleanup(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def test_geometry_info_of_results_and_listed_objects(self):
        info = call("geometry_info")
        self.assertEqual([o["object"] for o in info["objects"]], ["Cut"])
        cut = info["objects"][0]
        # The cylinder stands on the box's corner: a quarter of it is cut away.
        expected = 10 * 20 * 30 - 3.141592653589793 * 2 * 2 * 30 / 4
        self.assertAlmostEqual(cut["volume"], expected, places=3)
        self.assertEqual(cut["boundBox"]["size"], [10.0, 20.0, 30.0])
        self.assertEqual(cut["counts"]["solids"], 1)
        self.assertTrue(cut["valid"])
        both = call("geometry_info", objects=["Box", "Cyl"])
        self.assertEqual(len(both["objects"]), 2)
        self.assertAlmostEqual(both["total"]["volume"], 6000 + 3.141592653589793 * 4 * 40, places=3)
        self.assertEqual(both["total"]["boundBox"]["max"][2], 40.0)
        with self.assertRaises(RuntimeError):
            call("geometry_info", objects=["Missing"])

    def test_geometry_check(self):
        checked = call("geometry_check")
        self.assertTrue(checked["ok"])
        self.assertEqual(checked["objects"][0]["problems"], [])

    def test_shape_elements_find_the_top_face_and_the_hole(self):
        top = call("shape_elements", object="Cut", types=["Face"], direction=[0, 0, 1])
        self.assertEqual(len(top["elements"]), 1)
        face = top["elements"][0]
        self.assertEqual(face["surface"], "Plane")
        self.assertEqual(face["normal"], [0.0, 0.0, 1.0])
        self.assertAlmostEqual(face["centerOfMass"][2], 30.0)
        facing = call(
            "shape_elements",
            object="Cut",
            types=["Face"],
            direction=[0, 0, -1],
            directionMode="facing",
        )
        self.assertEqual([e["normal"] for e in facing["elements"]], [[0.0, 0.0, -1.0]])
        hole = call("shape_elements", object="Cut", surface="Cylinder")
        self.assertEqual(len(hole["elements"]), 1)
        self.assertAlmostEqual(hole["elements"][0]["radius"], 2.0)
        circles = call("shape_elements", object="Cut", types=["Edge"], curve="Circle")
        self.assertEqual(len(circles["elements"]), 2)
        near = call(
            "shape_elements", object="Box", types=["Vertex"], nearPoint=[10, 20, 30], limit=1
        )
        self.assertEqual(near["elements"][0]["point"], [10.0, 20.0, 30.0])
        self.assertEqual(near["matched"], 8)
        self.assertEqual(
            call("shape_elements", object="Box")["counts"], {"faces": 6, "edges": 12, "vertexes": 8}
        )

    def test_export_and_import_round_trip(self):
        formats = call("file_formats")
        self.assertIn("step", formats["export"])
        self.assertIn("stl", formats["export"])
        for ext in ("step", "brep", "stl"):
            out = call("export", path=f"out/part.{ext}")
            self.assertEqual(out["objects"], ["Cut"])
            self.assertGreater(out["size"], 0)
            self.assertTrue(os.path.exists(os.path.join(self.root, "out", f"part.{ext}")))
        self.assertEqual(call("changes_get")["changes"][-1]["kind"], "export")
        with self.assertRaises(RuntimeError):
            call("export", path="out/part.nothing")
        with self.assertRaises(RuntimeError):
            call("export", path="../escape.step")

        call("document_new", name="AgenticImportTest")
        imported = call("import", path="out/part.brep")
        self.assertEqual(len(imported["created"]), 1)
        info = call("geometry_info", objects=imported["created"])
        self.assertAlmostEqual(
            info["objects"][0]["volume"], 10 * 20 * 30 - 3.141592653589793 * 4 * 30 / 4, places=3
        )
        with self.assertRaises(RuntimeError):
            call("import", path="out/missing.step")


class SketchToolsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.cleanup)
        self.root = os.path.realpath(self.tmp.name)
        call("project_open", path=self.root)
        call("document_new", name="AgenticSketchTest")
        call("object_create", type="Sketcher::SketchObject", name="Sketch")

    def cleanup(self):
        for info in call("document_list")["documents"]:
            call("document_close", document=info["name"])
        self.tmp.cleanup()

    def constrain_plate(self):
        call(
            "sketch_add",
            sketch="Sketch",
            geometry=[
                {"type": "rectangle", "corner": [0, 0], "width": 40, "height": 20},
                {"type": "circle", "center": [20, 10], "radius": 4},
            ],
        )
        return call(
            "sketch_constrain",
            sketch="Sketch",
            constraints=[
                {"type": "Coincident", "elements": [[0, "start"], "origin"]},
                {"type": "DistanceX", "elements": [0], "value": 40, "name": "Length"},
                {"type": "DistanceY", "elements": [1], "value": "20 mm"},
                {"type": "Radius", "elements": [4], "value": 4},
                {"type": "DistanceX", "elements": ["origin", [4, "center"]], "value": 20},
                {"type": "DistanceY", "elements": ["origin", [4, "center"]], "value": 10},
            ],
        )

    def test_add_geometry_reports_indices_and_freedom(self):
        added = call(
            "sketch_add",
            sketch="Sketch",
            geometry=[
                {"type": "rectangle", "corner": [0, 0], "width": 40, "height": 20},
                {"type": "circle", "center": [20, 10], "radius": 4},
                {"type": "line", "start": [0, 0], "end": [40, 20], "construction": True},
            ],
        )
        self.assertEqual([a["indices"] for a in added["added"]], [[0, 1, 2, 3], [4], [5]])
        self.assertEqual(added["solver"]["degreesOfFreedom"], 11)
        self.assertTrue(added["geometry"][5]["construction"])
        self.assertEqual(added["geometry"][4]["radius"], 4.0)
        self.assertEqual(
            [c["type"] for c in added["constraints"]],
            ["Coincident"] * 4 + ["Horizontal", "Horizontal", "Vertical", "Vertical"],
        )
        with self.assertRaises(RuntimeError):
            call("sketch_add", sketch="Sketch", geometry=[{"type": "spiral"}])

    def test_fully_constrained_plate_extrudes(self):
        constrained = self.constrain_plate()
        self.assertTrue(constrained["solver"]["fullyConstrained"])
        self.assertEqual(constrained["created"], [8, 9, 10, 11, 12, 13])
        named = [c for c in constrained["constraints"] if c.get("name") == "Length"][0]
        self.assertEqual((named["type"], named["value"]), ("DistanceX", 40.0))
        self.assertEqual(constrained["closedWires"], 2)

        call(
            "object_create",
            type="Part::Extrusion",
            name="Ext",
            properties={"Base": "Sketch", "LengthFwd": 5, "Solid": True},
        )
        volume = call("geometry_info", objects=["Ext"])["objects"][0]["volume"]
        self.assertAlmostEqual(volume, 40 * 20 * 5 - 3.141592653589793 * 16 * 5, places=3)

        python = [c["python"] for c in call("changes_get")["changes"] if c["kind"] == "sketch"]
        self.assertIn("Sketcher.Constraint('DistanceX'", python[1])

    def test_conflict_is_reported_and_removed(self):
        self.constrain_plate()
        bad = call(
            "sketch_constrain",
            sketch="Sketch",
            constraints=[{"type": "DistanceX", "elements": [2], "value": -30}],
        )
        self.assertIn("warning", bad)
        self.assertIn(bad["created"][0], bad["solver"]["conflicting"])
        fixed = call("sketch_remove", sketch="Sketch", constraints=[bad["created"][0]])
        self.assertTrue(fixed["solver"]["fullyConstrained"])
        self.assertEqual(len(fixed["constraints"]), 14)
        undone = call("document_undo")
        self.assertEqual(undone["transaction"], "sketch_remove Sketch")
        self.assertFalse(call("sketch_solve", sketch="Sketch")["solver"]["fullyConstrained"])

    def test_remove_geometry_and_angles(self):
        call(
            "sketch_add",
            sketch="Sketch",
            geometry=[
                {"type": "line", "start": [0, 0], "end": [10, 0]},
                {"type": "line", "start": [0, 0], "end": [10, 10]},
                {"type": "arc", "center": [0, 0], "radius": 5, "startAngle": 0, "endAngle": 90},
            ],
        )
        angled = call(
            "sketch_constrain",
            sketch="Sketch",
            constraints=[
                {"type": "Coincident", "elements": [[0, "start"], [1, "start"]]},
                {"type": "Angle", "elements": [0, 1], "value": 30},
            ],
        )
        angle = [c for c in angled["constraints"] if c["type"] == "Angle"][0]
        self.assertAlmostEqual(angle["value"], 30.0, places=6)
        arc = angled["geometry"][2]
        self.assertAlmostEqual(arc["endAngle"], 90.0)
        removed = call("sketch_remove", sketch="Sketch", geometry=[1])
        self.assertEqual([g["type"] for g in removed["geometry"]], ["LineSegment", "ArcOfCircle"])
        self.assertEqual(removed["constraints"], [])
        with self.assertRaises(RuntimeError):
            call("sketch_remove", sketch="Sketch", geometry=[9])
        with self.assertRaises(RuntimeError):
            call("sketch_get", sketch="NoSketch")


def make_addon_registry(folder):
    """A local registry with two addons as zip archives, one of them unsafe."""
    import zipfile

    good = os.path.join(folder, "demo.zip")
    with zipfile.ZipFile(good, "w") as archive:
        archive.writestr(
            "DemoAddon-main/Init.py", "import FreeCAD\nFreeCAD.__agentic_demo_loaded__ = True\n"
        )
        archive.writestr(
            "DemoAddon-main/InitGui.py",
            "class DemoWorkbench(Workbench):\n"
            "    MenuText = 'Agentic Demo'\n"
            "    def Initialize(self):\n"
            "        pass\n"
            "    def GetClassName(self):\n"
            "        return 'Gui::PythonWorkbench'\n"
            "Gui.addWorkbench(DemoWorkbench())\n",
        )
        archive.writestr("DemoAddon-main/README.md", "# Demo addon\nA test addon.\n")
        archive.writestr(
            "DemoAddon-main/package.xml",
            '<?xml version="1.0"?><package format="1" xmlns="https://wiki.freecad.org/Package_Metadata">'
            "<name>Demo</name><version>1.2.3</version><description>demo</description>"
            "<license>LGPL-2.1</license><python_dependency>numpy</python_dependency></package>",
        )
        archive.writestr("DemoAddon-main/parts/bolt.txt", "M3\n")
    evil = os.path.join(folder, "evil.zip")
    with zipfile.ZipFile(evil, "w") as archive:
        archive.writestr("Evil-main/Init.py", "")
        archive.writestr("Evil-main/../../escaped.txt", "gotcha")
    registry = os.path.join(folder, "registry.json")
    with open(registry, "w") as handle:
        json.dump(
            {
                "_meta": {"description": "test registry"},
                "Demo": [
                    {
                        "repository": "https://example.invalid/demo",
                        "git_ref": "main",
                        "zip_url": "file://" + good,
                        "freecad_min": "0.21.0",
                    }
                ],
                "Evil": [
                    {
                        "repository": "https://example.invalid/evil",
                        "git_ref": "main",
                        "zip_url": "file://" + evil,
                    }
                ],
                "Future": [
                    {
                        "repository": "https://example.invalid/f",
                        "zip_url": "file://" + good,
                        "freecad_min": "99.0.0",
                    }
                ],
            },
            handle,
        )
    return registry


class AddonToolsTest(unittest.TestCase):
    PREFS = "User parameter:BaseApp/Preferences/Mod/Agentic"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        prefs = FreeCAD.ParamGet(self.PREFS)
        self.addCleanup(
            prefs.SetBool, "UseDefaultRegistry", prefs.GetBool("UseDefaultRegistry", True)
        )
        self.addCleanup(prefs.SetString, "Registries", prefs.GetString("Registries", ""))
        prefs.SetBool("UseDefaultRegistry", False)  # stay offline
        prefs.SetString("Registries", "")
        self.root = os.path.realpath(os.path.join(self.tmp.name, "project"))
        call("project_open", path=self.root, create=True)
        self.registry = make_addon_registry(os.path.realpath(self.tmp.name))

    def test_registry_catalog_install_files_remove(self):
        call("addon_registry_add", location=self.registry)
        regs = call("addon_registry_list")["registries"]
        self.assertEqual([(r["location"], r["scope"]) for r in regs], [(self.registry, "project")])
        with open(os.path.join(self.root, ".freecad-project.json")) as handle:
            self.assertEqual(json.load(handle)["registries"], [self.registry])

        catalog = call("addon_catalog")
        self.assertEqual([a["id"] for a in catalog["addons"]], ["Demo", "Evil", "Future"])
        future = [a for a in catalog["addons"] if a["id"] == "Future"][0]
        self.assertFalse(future["entries"][0]["compatible"])
        self.assertEqual(
            [a["id"] for a in call("addon_catalog", filter="demo")["addons"]], ["Demo"]
        )

        installed = call("addon_install", id="Demo")
        self.assertEqual(installed["method"], "zip")
        self.assertEqual(installed["path"], "Mod/Demo")
        self.assertEqual(installed["package"]["version"], "1.2.3")
        self.assertIn(
            {"kind": "python_dependency", "name": "numpy"}, installed["package"]["dependencies"]
        )
        self.assertEqual(installed["loaded"]["init"], "ok")
        self.assertTrue(getattr(FreeCAD, "__agentic_demo_loaded__", False))
        self.assertTrue(os.path.exists(os.path.join(self.root, "Mod", "Demo", "Init.py")))
        with self.assertRaises(RuntimeError):
            call("addon_install", id="Demo")  # already installed
        self.assertEqual(call("addon_install", id="Demo", force=True)["method"], "zip")

        listed = call("addon_installed")["addons"]
        self.assertEqual([a["id"] for a in listed], ["Demo"])
        self.assertEqual(listed[0]["readme"], "README.md")
        self.assertEqual(listed[0]["source"]["method"], "zip")
        files = call("addon_files", id="Demo")["files"]
        self.assertIn("parts/bolt.txt", [f["path"] for f in files])
        readme = call("addon_files", id="Demo", path="README.md")
        self.assertIn("Demo addon", readme["content"])
        with self.assertRaises(RuntimeError):
            call("addon_files", id="Demo", path="../../.freecad-project.json")

        removed = call("addon_remove", id="Demo")
        self.assertTrue(removed["removed"])
        self.assertIsNotNone(removed["note"])
        self.assertEqual(call("addon_installed")["addons"], [])
        kinds = [c["kind"] for c in call("changes_get")["changes"]]
        self.assertEqual(kinds.count("addon"), 3)

    def test_unsafe_archive_and_unknown_addon_are_refused(self):
        call("addon_registry_add", location=self.registry)
        with self.assertRaises(RuntimeError) as err:
            call("addon_install", id="Evil", method="zip")
        self.assertIn("unsafe path", str(err.exception))
        self.assertFalse(os.path.exists(os.path.join(self.tmp.name, "escaped.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.root, "Mod", "Evil")))
        with self.assertRaises(RuntimeError):
            call("addon_install", id="NotThere")
        with self.assertRaises(RuntimeError):
            call("addon_install", id="../escape")

    def test_user_registry_scope(self):
        call("addon_registry_add", location=self.registry, scope="user")
        regs = call("addon_registry_list")["registries"]
        self.assertEqual([(r["location"], r["scope"]) for r in regs], [(self.registry, "user")])
        self.assertIn("Demo", [a["id"] for a in call("addon_catalog")["addons"]])
