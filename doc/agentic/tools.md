# Tool reference

Generated from the server's registry by `src/Mod/Agentic/tools_reference.py`; do not edit by hand.

13 tools, 1 resources, 1 prompts.

## Tools

### `changes_get`

The change log of a project: every mutating operation in order, with the equivalent Python where there is one. Pass `next` back as `since` for new entries.

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | no | Maximum number of entries. Default 200. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |
| `since` | integer | no | First sequence number to return. Default 1. |

### `file_delete`

Delete a file or an empty folder of the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to delete. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |

### `file_list`

List a folder of the project. Hidden entries (dot files) are skipped.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | no | Folder relative to the project root. Default: the root. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |
| `recursive` | boolean | no | Descend into sub-folders. Default false. |

### `file_read`

Read a text file of the project. Returns the content and its version (a content hash) to pass as baseVersion to file_write. Binary files return only size and version.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | File path relative to the project root. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |

### `file_rename`

Rename or move a file or folder inside the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `from` | string | yes | Current path. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |
| `to` | string | yes | New path. |

### `file_write`

Write a text file of the project, atomically. Give either `content` (whole file) or `edits` (exact-text replacements, each oldText must occur exactly once). With `baseVersion` the write fails if the file changed since that version was read. Returns the new version and the changed hunk.

| Argument | Type | Required | Description |
|---|---|---|---|
| `baseVersion` | string | no | Version from file_read or a previous write. |
| `content` | string | no | The whole new content. |
| `edits` | array of object | no | Exact-text edits applied in order. |
| `path` | string | yes | File path relative to the project root; folders are created. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |

### `log_get`

Messages FreeCAD printed (errors, warnings, messages, log lines), newest last. Pass the `next` value of a previous call as `since` to get only new ones.

| Argument | Type | Required | Description |
|---|---|---|---|
| `level` | string ("all", "warning", "error") | no | Minimum severity: all, warning (warnings and errors) or error. Default all. |
| `limit` | integer | no | Maximum number of entries. Default 200. |
| `since` | integer | no | First sequence number to return. Default 1. |

### `project_info`

Settings and contents of a project: name, main document, scripts and addon folders, autoSave, autoRecompute, the .FCStd documents in the folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |

### `project_list`

List the open projects and which one is current.

No arguments.

### `project_open`

Open a project folder and make it current. The folder's `.freecad-project.json` is created when missing; with `create` the folder itself is created too. Returns the project info.

| Argument | Type | Required | Description |
|---|---|---|---|
| `create` | boolean | no | Create the folder when it does not exist. Default false. |
| `path` | string | yes | Absolute path of the project folder. |

### `project_set`

Change project settings and save them to `.freecad-project.json`.

| Argument | Type | Required | Description |
|---|---|---|---|
| `addonFolder` | string | no | Folder for per-project addon installs. |
| `autoRecompute` | boolean | no | Recompute after object_set and object_create. |
| `autoSave` | boolean | no | Save documents after every mutating tool call. |
| `mainDocument` | string | null | no | Relative path of the main .FCStd document, or null. |
| `name` | string | no | Project name. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |
| `registries` | array of string | no | Addon registry files or URLs. |
| `scriptsFolder` | string | no | Folder of the agent's Python scripts, relative to the project. |

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
