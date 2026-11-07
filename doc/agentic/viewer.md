# The agent viewer

In agent mode (`FreeCAD --agent`) a person does not operate FreeCAD; the
window shows what the agent does. The viewer is the upstream main window
reshaped through public Qt API by `ViewerShell` (`src/Mod/Agentic/Gui`):
no upstream widget is edited, and the 3D views are the GUI layer's own.

A plain `FreeCAD` launch is the classic GUI, untouched.

## The 3D view

- Pages that belong to no document (the Start page) are closed; the 3D
  views are tabbed, so the active one fills the window.
- The viewer follows the document the agent works on: every document event
  brings that document's 3D view to the front.
- The agent's camera is the reference. `camera_set` records it per
  document, and a capture without its own view renders from it, then puts
  back whatever a person had orbited to. A person can look around freely;
  it changes nothing the agent sees.

## Panels

The panels are fed by `ViewerBridge`, a `SessionObserver`: the viewer
watches the session and never drives it.

- **Activity**: every tool call with the time, the tool, a one-line
  summary of its arguments (file contents and code shown as their
  length), the duration and whether it succeeded.
- **Changes**: the project's change log, one row per mutation, the
  equivalent Python in the tooltip.

Activity and changes are kept per project and switch with the current
project, the one the last tool call addressed.

`AgenticGui.viewerState()` returns every panel's content as JSON.

## Checking it

`tests/agentic/viewer_check.py` runs inside an agent-mode FreeCAD, drives
the server in-process and checks the window as a person sees it:

```bash
xvfb-run -a build/relWithDebInfo/bin/FreeCAD --agent --agent-port 0 tests/agentic/viewer_check.py
```
