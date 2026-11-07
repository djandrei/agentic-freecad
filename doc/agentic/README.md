# agentic-freecad

A fork of FreeCAD that turns the application into a tool for agents: a Model
Context Protocol (MCP) server drives FreeCAD, and a viewer window shows a
person what the agent is doing. The design is in `DESIGN.md` at the root of
the parent workspace; this folder documents what is built.

## Status: stage 0

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

# viewer window plus stdio server (for watching an agent work)
build/relWithDebInfo/bin/FreeCAD --agent-stdio      # headless, no window
build/relWithDebInfo/bin/FreeCAD --agent            # window; HTTP transport arrives in stage 1

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
```

See `upstream-merge.md` for the list of upstream files the fork edits.
