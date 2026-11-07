# SPDX-License-Identifier: LGPL-2.1-or-later
"""Addon catalog, installation and loading for the Agentic tools.

The catalog uses the Addon Manager's format (FreeCAD/Addons Data/Index.json:
addon id -> list of {repository, git_ref, zip_url, freecad_min, freecad_max,
...}). Registries are files or URLs in that format: the default registry,
the user's (preference BaseApp/Preferences/Mod/Agentic/Registries) and the
project's (`registries` in .freecad-project.json). Remote registries are
cached under the XDG cache directory (or AGENTIC_FREECAD_CACHE).

Addons install into <project>/<addonFolder>/<id> from their zip archive over
TLS, or with `git clone` run without a shell when no archive is available,
and are loaded into the running session the way FreeCAD loads its modules at
startup. Installing an addon runs its code: that is what an addon is.
"""

import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

import FreeCAD

DEFAULT_REGISTRY = "https://raw.githubusercontent.com/FreeCAD/Addons/main/Data/Index.json"
PREFS = "User parameter:BaseApp/Preferences/Mod/Agentic"
MANIFEST = ".agentic-addon.json"
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
CACHE_SECONDS = 24 * 3600

_loaded = {}  # addon path -> load report


# ------------------------------------------------------------------ helpers


def _cache_dir():
    base = os.environ.get("AGENTIC_FREECAD_CACHE")
    if not base:
        xdg = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        base = os.path.join(xdg, "agentic-freecad")
    path = os.path.join(base, "registries")
    os.makedirs(path, exist_ok=True)
    return path


def _is_remote(location):
    return urllib.parse.urlparse(location).scheme in ("http", "https")


def _local_path(location):
    parsed = urllib.parse.urlparse(location)
    if parsed.scheme == "file":
        return urllib.request.url2pathname(parsed.path)
    return location


def _fetch(url, limit=MAX_ARCHIVE_BYTES, timeout=60):
    if urllib.parse.urlparse(url).scheme != "https":
        raise ValueError(f"refusing to download over {urllib.parse.urlparse(url).scheme}: {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "agentic-freecad"})
    with urllib.request.urlopen(request, timeout=timeout) as response:  # TLS verified by default
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{url} is larger than {limit} bytes")
    return data


def _version_tuple(text):
    parts = re.findall(r"\d+", text or "")
    return tuple(int(p) for p in parts[:3]) + (0,) * (3 - len(parts[:3]))


def _freecad_version():
    v = FreeCAD.Version()
    return _version_tuple(".".join(v[:3]))


def _compatible(entry):
    current = _freecad_version()
    if entry.get("freecad_min") and current < _version_tuple(entry["freecad_min"]):
        return False
    if entry.get("freecad_max") and current > _version_tuple(entry["freecad_max"]):
        return False
    return True


# --------------------------------------------------------------- registries


def _user_registries():
    text = FreeCAD.ParamGet(PREFS).GetString("Registries", "")
    return [line.strip() for line in text.splitlines() if line.strip()]


def _registries(project_registries):
    use_default = FreeCAD.ParamGet(PREFS).GetBool("UseDefaultRegistry", True)
    out = []
    if use_default:
        out.append({"location": DEFAULT_REGISTRY, "scope": "default"})
    out += [{"location": r, "scope": "user"} for r in _user_registries()]
    out += [{"location": r, "scope": "project"} for r in project_registries]
    return out


def _load_registry(location, refresh, root):
    """Returns (catalog dict, info) for one registry."""
    info = {"location": location}
    if _is_remote(location):
        cached = os.path.join(
            _cache_dir(), hashlib.sha256(location.encode()).hexdigest()[:24] + ".json"
        )
        fresh = os.path.exists(cached) and time.time() - os.path.getmtime(cached) < CACHE_SECONDS
        if refresh or not fresh:
            try:
                data = _fetch(location, limit=64 * 1024 * 1024)
                json.loads(data)  # validate before caching
                with open(cached, "wb") as handle:
                    handle.write(data)
                info["fetched"] = True
            except Exception as err:
                info["error"] = f"could not fetch: {err}"
        if not os.path.exists(cached):
            info.setdefault("error", "not cached and not reachable")
            return {}, info
        info["cachedAt"] = time.strftime(
            "%Y-%m-%dT%H:%M:%SZ", time.gmtime(os.path.getmtime(cached))
        )
        with open(cached, "rb") as handle:
            return json.load(handle), info
    path = _local_path(location)
    if not os.path.isabs(path):
        path = os.path.join(root, path)
    try:
        with open(path, "rb") as handle:
            return json.load(handle), info
    except Exception as err:
        info["error"] = str(err)
        return {}, info


def _catalog(args):
    root = args["root"]
    catalog = {}
    sources = []
    for registry in _registries(args.get("registries") or []):
        data, info = _load_registry(registry["location"], bool(args.get("refresh")), root)
        info["scope"] = registry["scope"]
        count = 0
        for addon_id, entries in data.items():
            if addon_id.startswith("_") or addon_id == "$schema" or not isinstance(entries, list):
                continue
            for entry in entries:
                if not isinstance(entry, dict):
                    continue
                item = dict(entry)
                item["registry"] = registry["location"]
                catalog.setdefault(addon_id, []).append(item)
                count += 1
        info["entries"] = count
        sources.append(info)
    return catalog, sources


def catalog(payload):
    args = json.loads(payload)
    entries, sources = _catalog(args)
    needle = (args.get("filter") or "").lower()
    installed = set(_installed_ids(args))
    out = []
    for addon_id in sorted(entries, key=str.lower):
        options = entries[addon_id]
        text = " ".join(
            [addon_id] + [o.get("repository", "") + " " + o.get("note", "") for o in options]
        ).lower()
        if needle and needle not in text:
            continue
        out.append(
            {
                "id": addon_id,
                "installed": addon_id in installed,
                "entries": [
                    {
                        k: o.get(k)
                        for k in (
                            "repository",
                            "git_ref",
                            "branch_display_name",
                            "zip_url",
                            "freecad_min",
                            "freecad_max",
                            "note",
                            "curated",
                            "registry",
                        )
                        if o.get(k) is not None
                    }
                    | {"compatible": _compatible(o)}
                    for o in options
                ],
            }
        )
    limit = int(args.get("limit") or 200)
    return json.dumps({"registries": sources, "count": len(out), "addons": out[:limit]})


def registries(payload):
    args = json.loads(payload)
    out = []
    for registry in _registries(args.get("registries") or []):
        item = dict(registry)
        if _is_remote(registry["location"]):
            cached = os.path.join(
                _cache_dir(),
                hashlib.sha256(registry["location"].encode()).hexdigest()[:24] + ".json",
            )
            item["cached"] = os.path.exists(cached)
        out.append(item)
    return json.dumps(
        {
            "registries": out,
            "useDefault": FreeCAD.ParamGet(PREFS).GetBool("UseDefaultRegistry", True),
        }
    )


def add_user_registry(payload):
    args = json.loads(payload)
    location = args["location"]
    current = _user_registries()
    if location not in current:
        current.append(location)
        FreeCAD.ParamGet(PREFS).SetString("Registries", "\n".join(current))
    return registries(json.dumps({"registries": args.get("registries") or []}))


# ------------------------------------------------------------ installation


def _addon_root(args):
    path = os.path.join(args["root"], args.get("addonFolder") or "Mod")
    return os.path.realpath(path)


def _installed_ids(args):
    folder = _addon_root(args)
    if not os.path.isdir(folder):
        return []
    return sorted(
        name
        for name in os.listdir(folder)
        if os.path.isdir(os.path.join(folder, name)) and not name.startswith(".")
    )


def _safe_extract(data, destination):
    """Extracts a zip archive, dropping its single top folder, refusing escapes and links."""
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = [n for n in archive.namelist() if n and not n.endswith("/")]
        if not names:
            raise ValueError("the archive is empty")
        tops = {n.split("/", 1)[0] for n in archive.namelist() if n}
        strip = len(tops) == 1 and all("/" in n for n in names)
        total = 0
        for info in archive.infolist():
            name = info.filename
            if strip:
                name = name.split("/", 1)[1] if "/" in name else ""
            if not name:
                continue
            mode = (info.external_attr >> 16) & 0o170000
            if mode == 0o120000:
                raise ValueError(f"the archive contains a symbolic link: {info.filename}")
            if (
                os.path.isabs(name)
                or name.startswith("\\")
                or ".." in name.replace("\\", "/").split("/")
            ):
                raise ValueError(f"the archive contains an unsafe path: {info.filename}")
            target = os.path.realpath(os.path.join(destination, name))
            if not target.startswith(os.path.realpath(destination) + os.sep):
                raise ValueError(f"the archive contains an unsafe path: {info.filename}")
            total += info.file_size
            if total > MAX_ARCHIVE_BYTES * 4:
                raise ValueError("the archive expands beyond the size limit")
            if info.is_dir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with archive.open(info) as source, open(target, "wb") as sink:
                shutil.copyfileobj(source, sink)


def _git_clone(repository, ref, destination):
    git = shutil.which("git")
    if not git:
        raise RuntimeError("git is not installed")
    if urllib.parse.urlparse(repository).scheme not in ("https", "file"):
        raise ValueError(f"refusing to clone {repository}")
    command = [git, "clone", "--depth", "1"]
    if ref:
        command += ["--branch", ref]
    command += ["--", repository, destination]
    result = subprocess.run(command, capture_output=True, text=True, timeout=600, shell=False)
    if result.returncode != 0:
        raise RuntimeError(f"git clone failed: {result.stderr.strip()[-500:]}")
    shutil.rmtree(os.path.join(destination, ".git"), ignore_errors=True)


def _read_package(path):
    """Name, version, dependencies and workbench class names from package.xml."""
    package = os.path.join(path, "package.xml")
    if not os.path.exists(package):
        return {}
    try:
        from xml.etree import ElementTree

        tree = ElementTree.parse(package)
        root = tree.getroot()

        def find(tag):
            for el in root.iter():
                if el.tag.split("}")[-1] == tag:
                    return el
            return None

        info = {}
        for tag in ("name", "version", "description", "license"):
            el = find(tag)
            if el is not None and el.text:
                info[tag] = el.text.strip()
        deps = []
        for el in root.iter():
            tag = el.tag.split("}")[-1]
            if tag in ("depend", "python_dependency") and el.text:
                deps.append({"kind": tag, "name": el.text.strip()})
        if deps:
            info["dependencies"] = deps
        return info
    except Exception as err:
        return {"error": f"package.xml: {err}"}


def _load_addon(path, gui):
    """Loads an addon into this session as FreeCAD's startup does for a Mod folder."""
    report = _loaded.get(path)
    if report is not None:
        return report
    report = {"path": path, "init": None, "initGui": None, "workbenches": []}
    if path not in sys.path:
        sys.path.append(path)
    try:
        if path not in FreeCAD.__ModDirs__:
            FreeCAD.__ModDirs__.append(path)
    except Exception:
        pass
    before = set()
    if gui:
        import FreeCADGui

        before = set(FreeCADGui.listWorkbenches())
    for script, key in (("Init.py", "init"), ("InitGui.py", "initGui")):
        if key == "initGui" and not gui:
            continue
        file = os.path.join(path, script)
        if not os.path.exists(file):
            continue
        namespace = {"__file__": file, "__name__": os.path.splitext(script)[0]}
        if key == "initGui":
            import FreeCADGui

            namespace.update(
                {"FreeCADGui": FreeCADGui, "Gui": FreeCADGui, "Workbench": FreeCADGui.Workbench}
            )
        namespace.update({"FreeCAD": FreeCAD, "App": FreeCAD})
        try:
            with open(file, encoding="utf-8") as handle:
                exec(compile(handle.read(), file, "exec"), namespace)
            report[key] = "ok"
        except Exception as err:
            report[key] = f"{type(err).__name__}: {err}"
    if gui:
        import FreeCADGui

        report["workbenches"] = sorted(set(FreeCADGui.listWorkbenches()) - before)
    _loaded[path] = report
    return report


def install(payload):
    args = json.loads(payload)
    addon_id = args["id"]
    if not re.fullmatch(r"[A-Za-z0-9_.+-]+", addon_id) or addon_id in (".", ".."):
        raise ValueError(f"not a valid addon id: {addon_id!r}")
    entries, _ = _catalog(args)
    options = entries.get(addon_id)
    if not options:
        raise ValueError(f"{addon_id} is not in any registry (see addon_catalog)")
    ref = args.get("ref")
    if ref:
        matching = [
            o for o in options if o.get("git_ref") == ref or o.get("branch_display_name") == ref
        ]
        if not matching:
            raise ValueError(f"{addon_id} has no entry for {ref!r}")
        entry = matching[0]
    else:
        compatible = [o for o in options if _compatible(o)]
        entry = (compatible or options)[0]

    folder = _addon_root(args)
    os.makedirs(folder, exist_ok=True)
    destination = os.path.join(folder, addon_id)
    if os.path.exists(destination):
        if not args.get("force"):
            raise ValueError(f"{addon_id} is already installed (force replaces it)")
        shutil.rmtree(destination)

    method = args.get("method") or "auto"
    staging = tempfile.mkdtemp(prefix=".agentic-", dir=folder)
    used = None
    errors = []
    try:
        if method in ("auto", "zip") and entry.get("zip_url"):
            try:
                url = entry["zip_url"]
                if urllib.parse.urlparse(url).scheme in ("", "file"):
                    path = _local_path(url)
                    if not os.path.isabs(path):
                        path = os.path.join(args["root"], path)
                    with open(path, "rb") as handle:
                        data = handle.read(MAX_ARCHIVE_BYTES + 1)
                else:
                    data = _fetch(url)
                _safe_extract(data, staging)
                used = "zip"
            except Exception as err:
                errors.append(f"zip: {err}")
                shutil.rmtree(staging, ignore_errors=True)
                os.makedirs(staging, exist_ok=True)
        if used is None and method in ("auto", "git") and entry.get("repository"):
            try:
                shutil.rmtree(staging, ignore_errors=True)
                _git_clone(entry["repository"], entry.get("git_ref"), staging)
                used = "git"
            except Exception as err:
                errors.append(f"git: {err}")
        if used is None:
            raise RuntimeError(
                f"could not install {addon_id}: "
                + "; ".join(errors or ["no zip_url or repository"])
            )
        os.rename(staging, destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    manifest = {
        "id": addon_id,
        "method": used,
        "repository": entry.get("repository"),
        "git_ref": entry.get("git_ref"),
        "zip_url": entry.get("zip_url"),
        "registry": entry.get("registry"),
        "installedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    with open(os.path.join(destination, MANIFEST), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)

    out = {
        "id": addon_id,
        "path": os.path.relpath(destination, args["root"]),
        "method": used,
        "package": _read_package(destination),
        "errors": errors,
    }
    if args.get("load", True):
        out["loaded"] = _load_addon(destination, FreeCAD.GuiUp)
    return json.dumps(out)


def installed(payload):
    args = json.loads(payload)
    folder = _addon_root(args)
    out = []
    for addon_id in _installed_ids(args):
        path = os.path.join(folder, addon_id)
        manifest = {}
        try:
            with open(os.path.join(path, MANIFEST), encoding="utf-8") as handle:
                manifest = json.load(handle)
        except Exception:
            pass
        out.append(
            {
                "id": addon_id,
                "path": os.path.relpath(path, args["root"]),
                "source": manifest,
                "package": _read_package(path),
                "loaded": _loaded.get(path),
                "readme": next(
                    (f for f in sorted(os.listdir(path)) if f.lower().startswith("readme")), None
                ),
            }
        )
    return json.dumps({"folder": os.path.relpath(folder, args["root"]), "addons": out})


def remove(payload):
    args = json.loads(payload)
    addon_id = args["id"]
    folder = _addon_root(args)
    path = os.path.realpath(os.path.join(folder, addon_id))
    if not path.startswith(folder + os.sep) or not os.path.isdir(path):
        raise ValueError(f"{addon_id} is not installed")
    shutil.rmtree(path)
    loaded = _loaded.pop(path, None)
    if path in sys.path:
        sys.path.remove(path)
    note = None
    if loaded:
        note = (
            "the addon's code was loaded into this session; its workbenches and commands "
            "stay registered until FreeCAD restarts"
        )
    return json.dumps({"id": addon_id, "removed": True, "note": note})


def files(payload):
    args = json.loads(payload)
    folder = _addon_root(args)
    base = os.path.realpath(os.path.join(folder, args["id"]))
    if not base.startswith(folder + os.sep) or not os.path.isdir(base):
        raise ValueError(f"{args['id']} is not installed")
    sub = args.get("path") or ""
    target = os.path.realpath(os.path.join(base, sub))
    if target != base and not target.startswith(base + os.sep):
        raise ValueError("path leaves the addon folder")
    if os.path.isfile(target):
        size = os.path.getsize(target)
        with open(target, "rb") as handle:
            data = handle.read(2 * 1024 * 1024)
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            return json.dumps({"path": sub, "size": size, "isText": False})
        return json.dumps(
            {
                "path": sub,
                "size": size,
                "isText": True,
                "content": text,
                "truncated": size > len(data),
            }
        )
    entries = []
    for dirpath, dirnames, filenames in os.walk(target):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for name in sorted(filenames):
            if name.startswith("."):
                continue
            full = os.path.join(dirpath, name)
            entries.append(
                {
                    "path": os.path.relpath(full, base).replace(os.sep, "/"),
                    "size": os.path.getsize(full),
                }
            )
            if len(entries) >= int(args.get("limit") or 500):
                break
        if not args.get("recursive", True) or len(entries) >= int(args.get("limit") or 500):
            break
    return json.dumps({"id": args["id"], "path": sub, "files": entries})


def load_project(payload):
    """Loads every installed addon of a project into the session (on project_open)."""
    args = json.loads(payload)
    folder = _addon_root(args)
    reports = []
    for addon_id in _installed_ids(args):
        reports.append(_load_addon(os.path.join(folder, addon_id), FreeCAD.GuiUp))
    return json.dumps({"loaded": reports})
