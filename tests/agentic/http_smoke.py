#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""End-to-end smoke test of the HTTP transport.

Spawns `FreeCAD --agent --agent-headless --agent-port 0`, reads the endpoint
and token from its stderr, then checks the bearer token and Origin policy and
performs the MCP handshake over HTTP.

    python3 tests/agentic/http_smoke.py build/relWithDebInfo/bin/FreeCAD
"""

import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request


def read_endpoint(proc, timeout=120.0):
    url = token = None
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and (url is None or token is None):
        line = proc.stderr.readline()
        if not line:
            if proc.poll() is not None:
                raise SystemExit("server exited before announcing its endpoint")
            continue
        m = re.match(r"Agentic: MCP server at (\S+)", line)
        if m:
            url = m.group(1)
        m = re.match(r"Agentic: token (\S+)", line)
        if m:
            token = m.group(1)
    if url is None or token is None:
        raise SystemExit("no endpoint announced within the timeout")
    return url, token


def post(url, body, headers):
    data = json.dumps(body).encode() if body is not None else b""
    request = urllib.request.Request(url, data=data, method="POST")
    request.add_header("Content-Type", "application/json")
    for key, value in headers.items():
        request.add_header(key, value)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            payload = response.read()
            return response.status, (json.loads(payload) if payload else None)
    except urllib.error.HTTPError as err:
        return err.code, None


def main(binary):
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    proc = subprocess.Popen(
        [binary, "--agent", "--agent-headless", "--agent-port", "0"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env=env,
        text=True,
        bufsize=1,
    )
    try:
        url, token = read_endpoint(proc)
        auth = {"Authorization": f"Bearer {token}"}

        status, _ = post(url, {"jsonrpc": "2.0", "id": 0, "method": "ping"}, {})
        assert status == 401, f"missing token should be 401, got {status}"
        status, _ = post(url, {"jsonrpc": "2.0", "id": 0, "method": "ping"},
                         {**auth, "Origin": "https://evil.example"})
        assert status == 403, f"foreign origin should be 403, got {status}"
        status, _ = post(url, {"jsonrpc": "2.0", "id": 0, "method": "ping"},
                         {"Authorization": "Bearer wrong"})
        assert status == 401, f"wrong token should be 401, got {status}"

        status, init = post(url, {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                  "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                                             "clientInfo": {"name": "http_smoke", "version": "0"}}},
                            {**auth, "Origin": "http://localhost"})
        assert status == 200 and init["result"]["serverInfo"]["name"] == "agentic-freecad", init
        status, none = post(url, {"jsonrpc": "2.0", "method": "notifications/initialized"}, auth)
        assert status == 202 and none is None, (status, none)

        status, tools = post(url, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}, auth)
        names = {t["name"] for t in tools["result"]["tools"]}
        assert {"version", "status"} <= names, names

        status, st = post(url, {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                                "params": {"name": "status", "arguments": {}}}, auth)
        state = st["result"]["structuredContent"]
        assert state["transport"] == "http" and state["url"] == url, state
        assert state["headless"] is True and state["gui"] is True, state

        status, bad = post(url, {"jsonrpc": "2.0", "id": 4, "method": "tools/call",
                                 "params": {"name": "version", "arguments": {"x": 1}}}, auth)
        assert bad["error"]["code"] == -32602, bad

        print(f"ok: HTTP transport at {url}; status={state}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sys.exit(main(sys.argv[1]))
