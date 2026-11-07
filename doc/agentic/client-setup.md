# Connecting a client

The server speaks the Model Context Protocol over two transports. Both run
every request on FreeCAD's main thread, one at a time.

## stdio: the client starts FreeCAD

The client spawns FreeCAD and talks JSON-RPC over its stdin and stdout, one
message per line. No window is shown; the 3D views render offscreen.

```bash
claude mcp add freecad -- env QT_QPA_PLATFORM=offscreen \
    /path/to/build/bin/FreeCAD --agent-stdio --project /path/to/project
```

`FreeCADCmd --agent-stdio` works too, without the GUI tools (commands, task
panels, selection, camera, captures, edit mode, drawings): `status` reports
`gui: false` there.

The server owns stdout: on start it keeps a private duplicate of the
descriptor for its responses and points descriptor 1 at stderr, so a
`print` in an addon or a library writing to stdout cannot corrupt the
transport. Diagnostics go to stderr.

## HTTP: FreeCAD runs on its own, with the viewer

```bash
/path/to/build/bin/FreeCAD --agent --project /path/to/project        # viewer window
/path/to/build/bin/FreeCAD --agent --agent-port 7331                 # fixed port
QT_QPA_PLATFORM=offscreen FreeCAD --agent --agent-headless --agent-port 7331
```

On start the server prints three lines on stderr:

```
Agentic: MCP server at http://127.0.0.1:7331/mcp
Agentic: token 3f9c...
Agentic: claude mcp add --transport http freecad http://127.0.0.1:7331/mcp --header "Authorization: Bearer 3f9c..."
```

The last one is ready to paste. The server listens on `127.0.0.1` only,
requires the per-start bearer token, and refuses requests whose `Origin`
header is not local. Each request is a POST of one JSON-RPC message to
`/mcp`; there is no SSE stream.

## Without an MCP SDK

`tests/agentic/mcp_client.py` is a small client in the standard library, for
both transports; the end-to-end tests use it:

```python
from mcp_client import StdioClient

with StdioClient(["FreeCAD", "--agent-stdio", "--project", "/tmp/demo"]) as fc:
    fc.call("document_new", name="Demo")
    fc.call("object_create", type="Part::Box", properties={"Length": 20})
    print(fc.call("geometry_info")["objects"][0]["volume"])   # 2000.0 (20 x 10 x 10)
    fc.call("view_capture", width=400, height=300)
    png = fc.images[0]["data"]                                  # base64
```

`tests/agentic/e2e.py` is a longer example: one scenario through every tool
group.

## Containing the agent

The server's sandbox contains bugs, not the agent: `python_run` executes
arbitrary Python in a process that has FreeCAD's full file and network
access, and installing an addon runs its code. To contain the agent itself,
run FreeCAD in an operating-system sandbox. With bubblewrap on Linux:

```bash
PROJECT=/path/to/project
STATE=$HOME/.local/state/agentic-freecad      # FreeCAD settings and caches
mkdir -p "$PROJECT" "$STATE"
bwrap \
  --ro-bind / / \
  --dev /dev --proc /proc \
  --tmpfs /tmp \
  --bind "$PROJECT" "$PROJECT" \
  --bind "$STATE" "$STATE" \
  --setenv XDG_CONFIG_HOME "$STATE/config" \
  --setenv XDG_DATA_HOME "$STATE/data" \
  --setenv XDG_CACHE_HOME "$STATE/cache" \
  --setenv QT_QPA_PLATFORM offscreen \
  --unshare-all \
  --die-with-parent \
  /path/to/build/bin/FreeCAD --agent-stdio --project "$PROJECT"
```

The whole filesystem is read-only except the project folder, FreeCAD's state
folder and a private `/tmp`; `--unshare-all` also cuts the network, so addon
installs and wiki fetches fail cleanly (use `--share-net` to allow them).
Only the stdio transport works without a network: the HTTP server's
loopback is private to the sandbox.

Not verified on the development machine: `bwrap` could not create a user
namespace there (`setting up uid map: Permission denied`, which Ubuntu's
AppArmor restriction on unprivileged user namespaces produces). Where that
restriction applies, a container (Podman, Docker) with the project folder as
its only writable volume gives the same containment.
