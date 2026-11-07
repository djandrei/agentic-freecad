# Tool reference

Generated from the server's registry by `src/Mod/Agentic/tools_reference.py`; do not edit by hand.

43 tools, 1 resources, 1 prompts.

## Tools

### `changes_get`

The change log of a project: every mutating operation in order, with the equivalent Python where there is one. Pass `next` back as `since` for new entries.

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | no | Maximum number of entries. Default 200. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |
| `since` | integer | no | First sequence number to return. Default 1. |

### `command_info`

Details of one GUI command: menu text, tooltip, status tip, what's this, shortcut, active.

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Command name, e.g. Part_Box or PartDesign_Pad. |

### `command_list`

GUI commands with menu text, tooltip, shortcut and whether each is active for the current document and selection. With `workbench` the workbench is activated and only its toolbar commands are listed, each with its toolbar names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string | no | Case-insensitive substring of the name, menu text or tooltip. |
| `workbench` | string | no | Limit to the toolbars of this workbench (activates it). |

### `command_run`

Run a GUI command as its toolbar button would, against the current selection and active document. Returns the Python the command recorded, the objects it created, changed or deleted, the task panel it opened (drive it with task_get, task_set, task_accept, task_reject), modal dialogs that were auto-answered, and the console messages. Fails when the command is not active.

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Command name, e.g. Part_Box. |

### `document_close`

Close a document. Unsaved changes are lost unless autoSave is on.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_list`

Documents of the project: the open ones (with undo depth, object counts and errors) and the .FCStd files in the folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_new`

Create a new document in the project and make it active. With autoSave it is written to <name>.FCStd (or `file`) at once and after every change.

| Argument | Type | Required | Description |
|---|---|---|---|
| `file` | string | no | File path relative to the project root. Default: <name>.FCStd. |
| `name` | string | no | Document name (also the label). Default: FreeCAD chooses. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_open`

Open an .FCStd file of the project folder and make it the active document.

| Argument | Type | Required | Description |
|---|---|---|---|
| `file` | string | yes | File path relative to the project root. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_recompute`

Recompute a document. Returns the number of recomputed objects, every object in error with its status, and the console messages of the run.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `force` | boolean | no | Recompute even if nothing is touched. Default false. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_redo`

Redo the last undone transaction of a document.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_save`

Save a document to its file, or to `as` (relative to the project root).

| Argument | Type | Required | Description |
|---|---|---|---|
| `as` | string | no | New file path; the document keeps using it. |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `document_undo`

Undo the last transaction of a document (one mutating tool call).

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

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

### `object_create`

Create an object of a type (Part::Box, Part::Cylinder, PartDesign::Body, Sketcher::SketchObject, Part::Cut, App::Part, ...) and set initial properties in one transaction. Values by property type: numbers and strings as is; quantities as a number in internal units (mm, deg) or a string with unit ("10 mm"); vectors as [x, y, z]; placements as {base: [x,y,z], rotation: {axis: [x,y,z], angle: deg}}; links as an object name or {object, sub: ["Face1"]}; link lists as arrays of those; enumerations as the option string; colors as [r, g, b] in 0..1. {expression: "Box.Length * 2"} binds an expression, {expression: null} clears it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `name` | string | no | Internal name. Default: derived from the type. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `properties` | object | no | Initial property values. |
| `type` | string | yes | Object type id, e.g. Part::Box. |
| `view` | object | no | Initial view-provider property values. |

### `object_delete`

Delete an object. Objects that depended on it are reported and left touched.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `object` | string | yes | Object name or unique label. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `object_dependencies`

The dependency graph of a document, or of one object with everything it depends on and everything that depends on it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `object` | string | no | Limit to this object's neighbourhood. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `object_get`

Everything about one object: type, status, every property with type, group, documentation, value, expression and enumeration options, the view-provider properties under `view` when the GUI is available, dependencies, and a shape summary for geometry.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `object` | string | yes | Object name or unique label. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `properties` | array of string | no | Only these properties. Default: all. |
| `view` | boolean | no | Include view-provider properties. Default true. |

### `object_list`

Objects of a document with type, label, status, visibility and dependency names.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `type` | string | no | Only objects derived from this type, e.g. Part::Feature. |

### `object_set`

Set properties of an object (and of its view provider under `view`) in one transaction; recomputes and saves according to the project settings. Values by property type: numbers and strings as is; quantities as a number in internal units (mm, deg) or a string with unit ("10 mm"); vectors as [x, y, z]; placements as {base: [x,y,z], rotation: {axis: [x,y,z], angle: deg}}; links as an object name or {object, sub: ["Face1"]}; link lists as arrays of those; enumerations as the option string; colors as [r, g, b] in 0..1. {expression: "Box.Length * 2"} binds an expression, {expression: null} clears it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `object` | string | yes | Object name or unique label. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `properties` | object | no | Property name to value. |
| `view` | object | no | View-provider property name to value (ShapeAppearance, Transparency, LineWidth, DisplayMode, Visibility, ...). |

### `preferences_get`

Read a group of the user preferences (the parameter tree behind the Preferences dialog): its sub-groups and its values with types. Paths are relative to "User parameter:", e.g. BaseApp/Preferences/Units.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Group path, e.g. BaseApp/Preferences/Units. |

### `preferences_set`

Set one preference value. The type follows the JSON value (bool, int, float, string) unless `type` says otherwise; `unsigned` is for colour values. Pass `value: null` to remove the key.

| Argument | Type | Required | Description |
|---|---|---|---|
| `key` | string | yes | Value name inside the group. |
| `path` | string | yes | Group path, e.g. BaseApp/Preferences/Units. |
| `type` | string ("bool", "int", "unsigned", "float", "string") | no | Stored type; default from the value. |
| `value` | boolean | integer | number | string | null | yes | The new value. |

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

### `python_reset`

Forget the project's Python namespace (variables from earlier python_run calls).

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `python_run`

Run Python inside FreeCAD: a code string or a project file. The namespace persists per project between runs and has FreeCAD/App, FreeCADGui/Gui (when available), Part, `doc` (the active document) and `project_root` bound. Returns the value of a trailing expression, stdout, stderr, the exception with traceback, the objects created/changed/deleted, the documents saved, and the console messages. The run is one transaction on the active document and is rolled back on an exception. `timeoutSeconds` stops Python code (not a running recompute inside C++).

| Argument | Type | Required | Description |
|---|---|---|---|
| `code` | string | no | Python source to run. |
| `document` | string | no | Document bound to `doc` and holding the transaction; default the active document. |
| `file` | string | no | Project-relative path of a .py file to run instead of code. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `timeoutSeconds` | number | no | Abort Python execution after this many seconds. Default 60; 0 disables. |

### `selection_clear`

Clear the selection.

No arguments.

### `selection_get`

The current selection: objects with their selected sub-elements (Face1, Edge3, ...).

No arguments.

### `selection_set`

Select objects and sub-elements (replacing the selection unless `add`). This is how the agent picks faces and edges for commands; find element names with object_get or shape_elements.

| Argument | Type | Required | Description |
|---|---|---|---|
| `add` | boolean | no | Keep the existing selection. Default false. |
| `items` | array of object | yes | What to select. |

### `status`

Report how this server was started: transport, headless flag, project folder and whether the GUI layer (views, captures) is available.

No arguments.

### `task_accept`

Press OK on the open task panel; the feature is created or updated and saved according to the project settings.

No arguments.

### `task_get`

The open task panel: its boxes, buttons and input fields (id, kind, label, value, options). Null when no panel is open.

No arguments.

### `task_reject`

Press Cancel on the open task panel.

No arguments.

### `task_set`

Fill fields of the open task panel by id (from task_get). Quantities take a number in the field's unit or a string with unit; choices take an option text or index; booleans true/false; buttons any value to click them; lists an array of item texts.

| Argument | Type | Required | Description |
|---|---|---|---|
| `fields` | object | yes | Field id to value. |

### `version`

Report the FreeCAD version and the agentic-freecad server version.

No arguments.

### `workbench_activate`

Activate a workbench (loads its module and commands). Returns its toolbars and menus.

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Workbench name, e.g. PartWorkbench, PartDesignWorkbench, SketcherWorkbench. |

### `workbench_list`

The installed workbenches with their menu text, and which one is active.

No arguments.

## Resources

| URI | Name | Type | Description |
|---|---|---|---|
| `agentic://status` | Server status | application/json | How this server was started and which tool groups are available. |

## Prompts

### `agentic-loop`

How to work with FreeCAD through this server: the write, run, inspect, capture, export loop.
