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
