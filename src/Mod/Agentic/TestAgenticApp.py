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
