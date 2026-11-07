# Tool reference

Generated from the server's registry by `src/Mod/Agentic/tools_reference.py`; do not edit by hand.

2 tools, 1 resources, 1 prompts.

## Tools

### `status`

Report how this server was started: transport, headless flag, project folder and whether the GUI layer (views, captures) is available.

No arguments.

### `version`

Report the FreeCAD version and the agentic-freecad server version.

No arguments.

## Resources

| URI | Name | Type | Description |
|---|---|---|---|
| `agentic://status` | Server status | application/json | How this server was started and which tool groups are available. |

## Prompts

### `agentic-loop`

How to work with FreeCAD through this server: the write, run, inspect, capture, export loop.
