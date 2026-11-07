# Tracking upstream FreeCAD

The fork stays mergeable by keeping its code in its own module and touching
upstream files in as few places as possible. This page lists every upstream
edit and the procedure for merging upstream `main`.

## Baseline

Forked from FreeCAD `main` at commit `487bcfa1e0` (2026-11-07, version 27.1.0).

## Where the fork lives

| Path | Content |
|---|---|
| `src/Mod/Agentic/` | the module: `App/` (protocol, transports, runtime, core tools), `Gui/` (viewer shell), `Init.py`, `InitGui.py`, Python test suites |
| `tests/src/Mod/Agentic/` | GoogleTest sources (`Agentic_tests_run`) |
| `tests/agentic/` | end-to-end clients run against a built binary |
| `doc/agentic/` | this documentation |
| `.github/workflows/agentic-ci.yml` | the fork's own CI |

## Upstream files edited

Every hook is wrapped in `if(BUILD_AGENTIC)` or `#ifdef BUILD_AGENTIC`.

| File | Hook |
|---|---|
| `cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake` | the `BUILD_AGENTIC` option (default ON) |
| `src/App/CMakeLists.txt` | `BUILD_AGENTIC` compile definition for `FreeCADApp` only, so toggling the option does not rebuild the tree |
| `src/Mod/CMakeLists.txt` | `add_subdirectory(Agentic)` |
| `tests/CMakeLists.txt` | `Agentic_tests_run` in `TestExecutables` |
| `tests/src/Mod/CMakeLists.txt` | `add_subdirectory(Agentic)` |
| `src/App/Application.cpp` | the `--agent`, `--agent-stdio`, `--agent-port`, `--agent-headless` and `--project` options, stored as the `AgentMode`, `AgentStdio`, `AgentHeadless`, `AgentPort` and `AgentProject` configuration keys; stdio mode also silences `LoggingConsole` because stdout is the transport |
| `.pre-commit-config.yaml` | `src/Mod/Agentic` added to the formatting allowlist |
| `.gitignore` | `/doc/` narrowed to `/doc/*` with `!/doc/agentic/`, so this documentation is tracked while the Doxygen output folder stays ignored |

No hook is needed in `src/Main/MainGui.cpp` or `src/Gui/Application.cpp`:
the module's `Init.py` runs inside `App::Application::init()`, early enough
to select the `offscreen` Qt platform for a headless launch, and its
`InitGui.py` runs during GUI startup, early enough to queue a
zero-timer that starts the shell and the server when the event loop begins. The viewer
shell reshapes the main window through public Qt API only.

## Rules

1. Add a file rather than edit an upstream one.
2. When an edit is unavoidable, keep it to a few lines that call into the
   module, guarded by `BUILD_AGENTIC`, and list it in the table above.
3. A change that is useful to upstream on its own (a new seam, a bug fix)
   goes in its own commit without fork-specific code, so it can be offered
   upstream.
4. Upstream workflows under `.github/workflows/` are left untouched and
   disabled in the fork's Actions settings; `agentic-ci.yml` is the fork's CI.

## Merging upstream main

```bash
git remote add upstream https://github.com/FreeCAD/FreeCAD.git   # once
git fetch upstream
git merge upstream/main
# resolve conflicts, which can only be in the files of the table above
pixi run configure && pixi run build
build/relWithDebInfo/tests/Agentic_tests_run
build/relWithDebInfo/bin/FreeCADCmd -t TestAgenticApp
xvfb-run build/relWithDebInfo/bin/FreeCAD -t TestAgenticGui
python3 tests/agentic/stdio_smoke.py build/relWithDebInfo/bin/FreeCAD
```

Update the baseline line above after each merge.
