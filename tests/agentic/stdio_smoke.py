#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""End-to-end smoke test of the stdio transport.

Spawns `FreeCAD --agent-stdio` (or `FreeCADCmd --agent-stdio`), performs the
MCP handshake, lists the tools and calls `version` and `status`. Needs no
FreeCAD Python bindings: it is a plain client talking JSON-RPC over the
process's pipes.

    python3 tests/agentic/stdio_smoke.py build/relWithDebInfo/bin/FreeCAD
    python3 tests/agentic/stdio_smoke.py build/relWithDebInfo/bin/FreeCADCmd
"""

import json
import os
import subprocess
import sys
import time


def main(binary):
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    proc = subprocess.Popen(
        [binary, "--agent-stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=True,
        bufsize=1,
    )

    def send(request):
        proc.stdin.write(json.dumps(request) + "\n")
        proc.stdin.flush()

    def receive(timeout=120.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = proc.stdout.readline()
            if line:
                return json.loads(line)
            if proc.poll() is not None:
                break
        raise SystemExit(f"no response from the server\n--- stderr ---\n{proc.stderr.read()}")

    try:
        send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
              "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                         "clientInfo": {"name": "stdio_smoke", "version": "0"}}})
        init = receive()
        assert init["result"]["serverInfo"]["name"] == "agentic-freecad", init
        send({"jsonrpc": "2.0", "method": "notifications/initialized"})

        send({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        tools = {t["name"] for t in receive()["result"]["tools"]}
        assert {"version", "status"} <= tools, tools

        send({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
              "params": {"name": "version", "arguments": {}}})
        version = receive()["result"]["structuredContent"]
        assert version["application"] == "FreeCAD", version

        send({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
              "params": {"name": "status", "arguments": {}}})
        status = receive()["result"]["structuredContent"]
        assert status["agentMode"] is True, status
        assert status["transport"] == "stdio", status
        assert status["headless"] is True, status
        expect_gui = not os.path.basename(binary).startswith("FreeCADCmd")
        assert status["gui"] is expect_gui, status

        send({"jsonrpc": "2.0", "id": 5, "method": "resources/read",
              "params": {"uri": "agentic://status"}})
        contents = receive()["result"]["contents"][0]
        assert json.loads(contents["text"])["transport"] == "stdio", contents

        send({"jsonrpc": "2.0", "id": 6, "method": "prompts/get",
              "params": {"name": "agentic-loop"}})
        prompt = receive()["result"]
        assert prompt["messages"][0]["content"]["text"], prompt

        print(f"ok: FreeCAD {version['version']} answered over stdio; status={status}")
    finally:
        proc.stdin.close()
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
            raise SystemExit("the server did not exit when stdin was closed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sys.exit(main(sys.argv[1]))
