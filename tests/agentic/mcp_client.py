# SPDX-License-Identifier: LGPL-2.1-or-later
"""A minimal MCP client for agentic-freecad, standard library only.

It is what the end-to-end tests use and doubles as an example of talking to
the server without an MCP SDK:

    from mcp_client import StdioClient, HttpClient

    with StdioClient(["FreeCAD", "--agent-stdio", "--project", "/tmp/demo"]) as fc:
        fc.call("document_new", name="Demo")
        fc.call("object_create", type="Part::Box", properties={"Length": 20})
        print(fc.call("geometry_info")["objects"][0]["volume"])

    with HttpClient.launch(["FreeCAD", "--agent", "--agent-headless", "--agent-port", "0"]) as fc:
        ...

`call` returns a tool's structured result and raises ToolError when the tool
reports an error; `images` holds the image content of the last call.
"""

import json
import os
import re
import subprocess
import time
import urllib.error
import urllib.request


class ToolError(RuntimeError):
    """A tool ran and reported an error (isError), or the protocol refused the call."""


class _Client:
    def __init__(self):
        self._next_id = 0
        self.images = []
        self.server = None

    def _request_id(self):
        self._next_id += 1
        return self._next_id

    def initialize(self):
        result = self.request("initialize", {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "mcp_client.py", "version": "1"},
        })
        self.notify("notifications/initialized")
        self.server = result
        return result

    def request(self, method, params=None):
        message = {"jsonrpc": "2.0", "id": self._request_id(), "method": method}
        if params is not None:
            message["params"] = params
        response = self._exchange(message)
        if "error" in response:
            raise ToolError(f"{method}: {response['error']['message']}")
        return response["result"]

    def call(self, tool, **arguments):
        result = self.request("tools/call", {"name": tool, "arguments": arguments})
        self.images = [c for c in result.get("content", []) if c.get("type") == "image"]
        if result.get("isError"):
            raise ToolError(f"{tool}: {result['content'][0]['text']}")
        return result.get("structuredContent")

    def tools(self):
        return [tool["name"] for tool in self.request("tools/list")["tools"]]

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self._send_notification(message)

    def __enter__(self):
        self.initialize()
        return self

    def __exit__(self, *exc):
        self.close()


class StdioClient(_Client):
    """Spawns the server and talks JSON-RPC over its stdin and stdout."""

    def __init__(self, command, env=None, timeout=300):
        super().__init__()
        environment = dict(os.environ)
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        environment.update(env or {})
        self.timeout = timeout
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=environment, text=True, bufsize=1,
        )

    def _send(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def _send_notification(self, message):
        self._send(message)

    def _exchange(self, message):
        self._send(message)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self.process.stdout.readline()
            if line:
                response = json.loads(line)
                if response.get("id") == message["id"]:
                    return response
                continue
            if self.process.poll() is not None:
                break
        raise ToolError(f"no response to {message['method']}; server stderr:\n{self.process.stderr.read()[-4000:]}")

    def close(self):
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            self.process.kill()


class HttpClient(_Client):
    """Talks to a running server over HTTP with its bearer token."""

    def __init__(self, url, token, process=None, timeout=300):
        super().__init__()
        self.url = url
        self.token = token
        self.process = process
        self.timeout = timeout

    @classmethod
    def launch(cls, command, env=None, timeout=300):
        """Starts a server and reads its endpoint and token from stderr."""
        environment = dict(os.environ)
        environment.update(env or {})
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, env=environment, text=True, bufsize=1)
        url = token = None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and (url is None or token is None):
            line = process.stderr.readline()
            if not line:
                if process.poll() is not None:
                    raise ToolError("the server exited before announcing its endpoint")
                continue
            if match := re.match(r"Agentic: MCP server at (\S+)", line):
                url = match.group(1)
            if match := re.match(r"Agentic: token (\S+)", line):
                token = match.group(1)
        if url is None or token is None:
            process.kill()
            raise ToolError("the server announced no endpoint")
        return cls(url, token, process, timeout)

    def _post(self, message):
        data = json.dumps(message).encode()
        request = urllib.request.Request(self.url, data=data, method="POST")
        request.add_header("Content-Type", "application/json")
        request.add_header("Authorization", f"Bearer {self.token}")
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            body = response.read()
            return json.loads(body) if body else None

    def _exchange(self, message):
        return self._post(message)

    def _send_notification(self, message):
        self._post(message)

    def close(self):
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.process.kill()
