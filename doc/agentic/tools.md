# Tool reference

Generated from the server's registry by `src/Mod/Agentic/tools_reference.py`; do not edit by hand.

74 tools, 5 resources, 2 prompts.

## Tools

### `addon_catalog`

Addons available from the registries (the FreeCAD addon index, the user's and the project's registries): id, repository, branch, archive URL, FreeCAD version range and whether each entry is compatible and already installed. Remote registries are cached for a day; `refresh` fetches them again.

| Argument | Type | Required | Description |
|---|---|---|---|
| `filter` | string | no | Case-insensitive substring of the id, repository or note. |
| `limit` | integer | no | Maximum number of addons returned. Default 200. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `refresh` | boolean | no | Fetch remote registries again. Default false. |

### `addon_files`

List the files of an installed addon, or read one (text up to 2 MiB): its README, its macros, its parts library.

| Argument | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Installed addon id. |
| `limit` | integer | no | Maximum number of files listed. Default 500. |
| `path` | string | no | File or folder inside the addon. Default the addon root. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `recursive` | boolean | no | List sub-folders too. Default true. |

### `addon_install`

Install an addon from the catalog into the project's addon folder (its zip archive over TLS, or `git clone` without a shell) and load it into this session: its Init.py runs, and its InitGui.py when the GUI is loaded, so its workbenches appear in workbench_list. Installing runs the addon's code. Python dependencies listed in package.xml are reported, not installed.

| Argument | Type | Required | Description |
|---|---|---|---|
| `force` | boolean | no | Replace an existing install. Default false. |
| `id` | string | yes | Addon id as addon_catalog lists it. |
| `load` | boolean | no | Load it into this session. Default true. |
| `method` | string ("auto", "zip", "git") | no | Download method. Default auto (zip, then git). |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `ref` | string | no | Branch or tag of a catalog entry; default the first compatible entry. |

### `addon_installed`

Addons installed in the project's addon folder, with their source, package.xml facts (version, dependencies), README file and load report.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `addon_registry_add`

Add a registry (a JSON file in the Addon Manager's catalog format, by URL or by path) to the project's settings or to the user's preferences.

| Argument | Type | Required | Description |
|---|---|---|---|
| `location` | string | yes | https URL, file URL, or a path (relative paths are relative to the project). |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `scope` | string ("project", "user") | no | Where to record it. Default project. |

### `addon_registry_list`

The registries the catalog is built from: the default FreeCAD addon index, the user's and the project's, with whether remote ones are cached.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `addon_remove`

Remove an addon from the project's addon folder. Code it already loaded stays registered until FreeCAD restarts.

| Argument | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Installed addon id. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `camera_get`

The 3D view's camera: type, view and up directions, orientation quaternion, the Inventor camera string (to restore it with camera_set), view size, axis cross.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `camera_set`

Set the 3D view's camera: a named orientation, a view direction, an orientation, an Inventor camera string, the camera type, and fit to all, the selection or listed objects. This camera is the reference: captures without their own view use it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `direction` | array of number | no | View direction (from the camera into the scene). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `fit` | string | array | boolean | no | "all" (or true), "selection", or a list of object names to fit. |
| `inventor` | string | no | A camera as returned by camera_get.inventor. |
| `orientation` | object | no | {quaternion: [x,y,z,w]} or {axis: [x,y,z], angle: degrees}. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `type` | string ("Orthographic", "Perspective") | no | Camera type. |
| `view` | string ("isometric", "dimetric", "trimetric", "front", "rear", "top", "bottom", "left", "right") | no | A named orientation. |

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

### `docs_api`

The Python API of a class, module function or member: Part.Shape, Part.Shape.makeFillet, FreeCAD.Vector, FreeCAD.newDocument, Sketcher.Constraint. Classes come with every member's signature (all overloads) and documentation.

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Dotted name, a bare class name, or the C++ class name. |

### `docs_command`

A GUI command's documentation: menu text, tooltip, status tip, what's this, shortcut, and its wiki page when cached (docs_page fetches it).

| Argument | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Command name, e.g. PartDesign_Pad. |

### `docs_page`

A FreeCAD wiki page as Markdown (from the FreeCAD-documentation repository, the source the Help module reads), fetched over HTTPS once and cached. Page names use underscores: Part_Box, PartDesign_Pad, Sketcher_Workbench, Expressions.

| Argument | Type | Required | Description |
|---|---|---|---|
| `page` | string | yes | Wiki page name. |
| `refresh` | boolean | no | Fetch again even if cached. Default false. |

### `docs_search`

Search FreeCAD's documentation: the Python API (generated from the binding stubs of this build), the GUI commands, cached wiki pages and the READMEs of installed addons. Every word of the query must occur. Returns ranked hits with snippets; read a hit with docs_api, docs_command or docs_page.

| Argument | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | no | Maximum hits. Default 20. |
| `query` | string | yes | Words to look for, e.g. "fillet edges" or "Placement rotation". |
| `sources` | array of string ("api", "command", "wiki", "addon") | no | Where to search. Default all. |

### `docs_status`

What documentation is available: API classes and functions, cached wiki pages and the cache folder, GUI commands.

No arguments.

### `docs_update`

Fetch wiki pages into the cache: the listed `pages`, or with `archive: true` every English page from the documentation repository's archive (a large download).

| Argument | Type | Required | Description |
|---|---|---|---|
| `archive` | boolean | no | Download the whole wiki archive. Default false. |
| `pages` | array of string | no | Wiki page names to fetch. |

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

### `drawing_export`

Export a TechDraw page to SVG or PDF in the project folder.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `page` | string | yes | Page object name or label. |
| `path` | string | yes | Output path ending in .svg or .pdf, relative to the project root. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `export`

Export objects to a file in the project folder; the format follows the extension (step, iges, brep, stl, obj, 3mf, ply, amf, dxf, svg, dae, ifc, gltf, csg, ...). Default objects: the document's result shapes.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `objects` | array of string | no | Object names or labels; default the document's result shapes. |
| `path` | string | yes | Output path relative to the project root; folders are created. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `file_delete`

Delete a file or an empty folder of the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to delete. |
| `project` | string | no | Name or folder of an open project to address; defaults to the current project. |

### `file_formats`

The file extensions this FreeCAD can import and export (depends on the modules loaded).

No arguments.

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

### `geometry_check`

Check shapes for defects with OpenCASCADE's checker (BRepCheck and, by default, the boolean-operation check): invalid sub-shapes, self-intersections, open shells, inverted solids. Returns per object whether it is ok and the problems found.

| Argument | Type | Required | Description |
|---|---|---|---|
| `booleanCheck` | boolean | no | Also run the boolean-operation check (slower). Default true. |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `objects` | array of string | no | Object names or labels; default the document's result shapes. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `geometry_info`

Measured facts about shapes: volume, area, bounding box (min, max, size, center), center of mass, counts of solids, shells, faces, edges and vertices, validity and closedness. Default objects: the results of the document (shapes no other shape consumes); with several, a total volume and combined bounding box.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `objects` | array of string | no | Object names or labels; default the document's result shapes. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `import`

Import a file of the project folder (STEP, IGES, BREP, STL, OBJ, DXF, SVG, IFC, glTF, ...) into a document through FreeCAD's registered importer for its extension.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `path` | string | yes | File path relative to the project root. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

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

### `object_edit`

Enter an object's edit mode, as a double-click in the tree does: a sketch opens for editing, a feature opens its task panel, a placement its dialog. Returns the task panel that opened (drive it with task_get, task_set, task_accept). `mode` selects an alternative edit mode where the object has one (0 default, 1 transform, ...).

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name; default the project's active document. |
| `mode` | integer | no | Edit mode. Default 0. |
| `object` | string | yes | Object name or label. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

### `object_edit_end`

Leave edit mode (closing the edit task panel as Close would) and recompute.

| Argument | Type | Required | Description |
|---|---|---|---|
| `document` | string | no | Document name; default the project's active document. |
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

Open a project folder and make it current. The folder's `.freecad-project.json` is created when missing; with `create` the folder itself is created too. Opening a project loads its installed addons and opens its main document when one is set. Returns the project info.

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

### `shape_elements`

The faces, edges and vertices of an object with their names (Face6, Edge3, Vertex1) and geometry: surface or curve type, area or length, center of mass, bounding box, normal of planar faces, axis, center and radius of cylinders, cones and circles. This replaces clicking in the 3D view: to find the top face, pass direction [0,0,1]; directionMode `extreme` keeps the elements furthest along it, `facing` keeps planar faces whose normal points that way, `sort` only orders. Pass the names to selection_set or to a feature's link property.

| Argument | Type | Required | Description |
|---|---|---|---|
| `curve` | string | no | Only edges of this curve type: Line, Circle, Ellipse, BSplineCurve, ... |
| `direction` | array of number | no | Order (and filter, see directionMode) by position along this direction. |
| `directionMode` | string ("extreme", "facing", "sort") | no | How direction filters. Default extreme. |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `limit` | integer | no | Maximum number of elements returned. Default 100. |
| `nearPoint` | array of number | no | Order by distance of the center of mass from this point. |
| `object` | string | yes | Object name or label. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `surface` | string | no | Only faces of this surface type: Plane, Cylinder, Cone, Sphere, Toroid, BSplineSurface, ... |
| `tolerance` | number | no | Distance tolerance for directionMode extreme. Default 1e-6. |
| `types` | array of string ("Face", "Edge", "Vertex") | no | Element kinds; default all. |

### `sketch_add`

Add geometry to a sketch in sketch-plane coordinates (mm, angles in degrees): line {start, end}, circle {center, radius}, arc {center, radius, startAngle, endAngle}, point {at}, ellipse {center, majorRadius, minorRadius, angle}, bspline {poles, degree, periodic}, polyline {points, closed} (with coincident joints), rectangle {corner, width, height} (with coincident, horizontal and vertical constraints). `construction: true` makes construction geometry. Returns the new indices and the solver state.

| Argument | Type | Required | Description |
|---|---|---|---|
| `details` | boolean | no | Return the full geometry and constraint lists. Default true (false for sketch_solve). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `geometry` | array of object | yes | Geometry items, each with a `type`. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `sketch` | string | yes | Sketch object name or label. |

### `sketch_constrain`

Add constraints: Coincident, PointOnObject, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Symmetric, Block, Lock, Distance, DistanceX, DistanceY, Radius, Diameter, Angle. Each is {type, elements, value?, name?, driving?}; values are numbers (mm, degrees for Angle) or strings with units. Element references: a geometry index (an edge), [index, "start"|"end"|"center"] (a point), "xAxis", "yAxis", "origin", "external:N". Returns the new constraint indices and the solver state, with a warning when constraints conflict.

| Argument | Type | Required | Description |
|---|---|---|---|
| `constraints` | array of object | yes | Constraints to add. |
| `details` | boolean | no | Return the full geometry and constraint lists. Default true (false for sketch_solve). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `sketch` | string | yes | Sketch object name or label. |

### `sketch_get`

A sketch as data: geometry with indices (type, construction flag, points, radius, angles), constraints with indices (type, element references, value, name, driving), and the solver's diagnosis: solved, degrees of freedom, fully constrained, conflicting, redundant and malformed constraints (0-based indices, like everything else here), open vertices, closed wires, placement.

| Argument | Type | Required | Description |
|---|---|---|---|
| `details` | boolean | no | Return the full geometry and constraint lists. Default true (false for sketch_solve). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `sketch` | string | yes | Sketch object name or label. |

### `sketch_remove`

Remove constraints and geometry by index (0-based, as sketch_get lists them). Removing geometry also removes its constraints and renumbers the rest.

| Argument | Type | Required | Description |
|---|---|---|---|
| `constraints` | array of integer | no | Constraint indices. |
| `details` | boolean | no | Return the full geometry and constraint lists. Default true (false for sketch_solve). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `geometry` | array of integer | no | Geometry indices. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `sketch` | string | yes | Sketch object name or label. |

### `sketch_solve`

Recompute a sketch and report the solver's diagnosis and the sketch's status.

| Argument | Type | Required | Description |
|---|---|---|---|
| `details` | boolean | no | Return the full geometry and constraint lists. Default true (false for sketch_solve). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `sketch` | string | yes | Sketch object name or label. |

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

### `view_capture`

Render the 3D view offscreen to a PNG and return it as an image. Uses the session camera unless the capture gives its own view, direction, orientation or camera, which is then restored afterwards (a capture with its own view fits all unless `fit` says otherwise). `objects` shows only those objects in the picture. The image is also written to `path` in the project (default .agentic/captures/).

| Argument | Type | Required | Description |
|---|---|---|---|
| `background` | string | no | White, Black, Transparent, Current, or a colour name. Default White. |
| `direction` | array of number | no | View direction (from the camera into the scene). |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `fit` | string | array | boolean | no | "all" (or true), "selection", or a list of object names to fit. |
| `height` | integer | no | Height in pixels. Default 600. |
| `inline` | boolean | no | Return the image in the response. Default true. |
| `inventor` | string | no | A camera as returned by camera_get.inventor. |
| `objects` | array of string | no | Show only these objects. |
| `orientation` | object | no | {quaternion: [x,y,z,w]} or {axis: [x,y,z], angle: degrees}. |
| `path` | string | no | Where to keep the PNG, relative to the project root. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |
| `type` | string ("Orthographic", "Perspective") | no | Camera type. |
| `view` | string ("isometric", "dimetric", "trimetric", "front", "rear", "top", "bottom", "left", "right") | no | A named orientation. |
| `width` | integer | no | Width in pixels. Default 800. |

### `view_options`

Options of the 3D view: axis cross, corner cross, camera type, background colour, and the draw style (as-is, points, wireframe, hidden-line, no-shading, shaded, flat-lines).

| Argument | Type | Required | Description |
|---|---|---|---|
| `axisCross` | boolean | no | Show the axis cross at the origin. |
| `background` | array of number | no | Background colour [r, g, b] in 0..1. |
| `cameraType` | string ("Orthographic", "Perspective") | no | Camera type. |
| `cornerCross` | boolean | no | Show the orientation cross in the corner. |
| `document` | string | no | Document name or label; defaults to the project's active document. |
| `drawStyle` | string ("as-is", "points", "wireframe", "hidden-line", "no-shading", "shaded", "flat-lines") | no | Draw style of the view. |
| `project` | string | no | Name or folder of an open project; defaults to the current project. |

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
| `agentic://project/changes` | Change log | application/json | The current project's change log as JSON: every mutation with its Python. |
| `agentic://project/log` | Message log | application/json | The last 500 console messages FreeCAD printed, as JSON. |
| `agentic://status` | Server status | application/json | How this server was started and which tool groups are available. |
| `agentic://docs/api/{name}` | API reference entry | application/json | A class, function or member of FreeCAD's Python API as JSON (see docs_api). |
| `agentic://wiki/{page}` | Wiki page | text/markdown | A FreeCAD wiki page as Markdown (fetched and cached on first read). |

## Prompts

### `agentic-loop`

How to work with FreeCAD through this server: the write, run, inspect, capture, export loop.

### `freecad-object-model`

How FreeCAD models things: documents, objects, properties, view providers, recompute, expressions, shapes and sub-elements, PartDesign bodies, sketches, undo.
