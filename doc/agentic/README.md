# agentic-freecad

A fork of FreeCAD that turns the application into a tool for agents: a Model
Context Protocol (MCP) server drives FreeCAD, and a viewer window shows a
person what the agent is doing. The design is in `DESIGN.md` at the root of
the parent workspace; this folder documents what is built.

## Status: stage 1

Stage 1 completed the protocol and transports on top of the stage 0 skeleton:

- tool arguments are validated against each tool's `inputSchema` (type,
  required, enum, bounds, array items, nested objects, unknown properties)
  before the handler runs; a violation is a `-32602` error naming the path;
- resources (`resources/list`, `resources/read`) and prompts
  (`prompts/list`, `prompts/get`), with `agentic://status` and the
  `agentic-loop` prompt; the `initialize` response carries the loop as its
  instructions;
- the HTTP transport (`--agent`, `--agent-port N`): MCP Streamable HTTP
  without the SSE stream, POST `/mcp` on `127.0.0.1`, a per-start bearer
  token printed on stderr together with the `claude mcp add` line, foreign
  `Origin` headers refused, notifications answered with 202;
- `FreeCADCmd --agent-stdio`: the console binary serves too, through an
  internal script that runs a `QCoreApplication` loop; view tools report
  `gui: false` there;
- `doc/agentic/tools.md`, generated from the live registry by
  `src/Mod/Agentic/tools_reference.py`.

Stage 0 is the skeleton that the rest is built on:

- the `Agentic` module (`src/Mod/Agentic`), enabled by `BUILD_AGENTIC`;
- the command-line options `--agent`, `--agent-stdio`, `--agent-port`,
  `--agent-headless`, `--project`;
- a JSON-RPC 2.0 / MCP protocol layer with `initialize`, `ping`,
  `tools/list`, `tools/call`, and two tools, `version` and `status`;
- the stdio transport (`--agent-stdio`): one JSON message per line, requests
  executed on the main thread one at a time, the process exits when stdin
  closes;
- the viewer shell: in agent mode the main window loses its menu bar,
  toolbars and upstream dock windows and gains an Activity panel showing the
  request and response stream;
- tests: `Agentic_tests_run` (GoogleTest), `TestAgenticApp` and
  `TestAgenticGui` (FreeCAD unittest suites), and
  `tests/agentic/stdio_smoke.py` (end-to-end client).

The HTTP transport, projects, files, documents, Python execution, views,
captures, export, addons and documentation tools follow in the stages of
`PLAN.md`.

## Running

```bash
pixi run configure && pixi run build

# headless server for an MCP client (Claude Code shown)
claude mcp add freecad -- env QT_QPA_PLATFORM=offscreen \
    $PWD/build/relWithDebInfo/bin/FreeCAD --agent-stdio

# the same, by hand
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  | QT_QPA_PLATFORM=offscreen build/relWithDebInfo/bin/FreeCAD --agent-stdio

# HTTP transport: prints the endpoint, the token and the `claude mcp add` line on stderr
build/relWithDebInfo/bin/FreeCAD --agent                     # viewer window, ephemeral port
build/relWithDebInfo/bin/FreeCAD --agent --agent-port 7331   # fixed port
QT_QPA_PLATFORM=offscreen build/relWithDebInfo/bin/FreeCAD --agent --agent-headless --agent-port 7331

# the console binary serves stdio too (no view tools)
build/relWithDebInfo/bin/FreeCADCmd --agent-stdio

# the untouched upstream GUI
build/relWithDebInfo/bin/FreeCAD
```

## Tests

```bash
build/relWithDebInfo/tests/Agentic_tests_run
build/relWithDebInfo/bin/FreeCADCmd -t TestAgenticApp
xvfb-run build/relWithDebInfo/bin/FreeCAD -t TestAgenticGui
QT_QPA_PLATFORM=offscreen build/relWithDebInfo/bin/FreeCAD -t TestAgenticGui
python3 tests/agentic/stdio_smoke.py build/relWithDebInfo/bin/FreeCAD
python3 tests/agentic/stdio_smoke.py build/relWithDebInfo/bin/FreeCADCmd
python3 tests/agentic/http_smoke.py build/relWithDebInfo/bin/FreeCAD
build/relWithDebInfo/bin/FreeCADCmd -c "import tools_reference; tools_reference.main('doc/agentic/tools.md')"
```

See `upstream-merge.md` for the list of upstream files the fork edits.
