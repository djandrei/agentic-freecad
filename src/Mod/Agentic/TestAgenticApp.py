# SPDX-License-Identifier: LGPL-2.1-or-later
"""Console tests of the Agentic module: the MCP protocol answered in-process."""

import json
import unittest

import FreeCAD
import Agentic


def rpc(method, params=None, request_id=1):
    request = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        request["params"] = params
    response = Agentic.handleRequest(json.dumps(request))
    return json.loads(response)


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
