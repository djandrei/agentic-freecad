# SPDX-License-Identifier: LGPL-2.1-or-later
"""Documentation for the Agentic tools: API reference, commands, wiki, READMEs.

- The API index is generated at build time from the .pyi binding stubs
  (make_api_index.py) and installed next to this file as api_index.json.
- GUI commands come from the running command manager.
- Wiki pages are the Markdown sources of the FreeCAD wiki
  (FreeCAD/FreeCAD-documentation, the source the Help module reads), fetched
  over HTTPS on demand or in bulk with docs_update, and cached under
  AGENTIC_FREECAD_DOCS_CACHE or $XDG_CACHE_HOME/agentic-freecad/wiki.
- Installed addons' READMEs are searched in place.
"""

import io
import json
import os
import re
import time
import urllib.parse
import urllib.request
import zipfile

import FreeCAD

WIKI_RAW = "https://raw.githubusercontent.com/FreeCAD/FreeCAD-documentation/main/wiki"
WIKI_ARCHIVE = "https://github.com/FreeCAD/FreeCAD-documentation/archive/refs/heads/main.zip"
_api = None


def _api_index():
    global _api
    if _api is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "api_index.json")
        try:
            with open(path, encoding="utf-8") as handle:
                _api = json.load(handle)["entries"]
        except FileNotFoundError:
            _api = []
    return _api


def _wiki_dir():
    base = os.environ.get("AGENTIC_FREECAD_DOCS_CACHE")
    if not base:
        xdg = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        base = os.path.join(xdg, "agentic-freecad", "wiki")
    os.makedirs(base, exist_ok=True)
    return base


def _page_file(page):
    if not re.fullmatch(r"[A-Za-z0-9_().,'+-]+(/[a-z]{2}(-[a-z]{2,4})?)?", page):
        raise ValueError(
            f"not a wiki page name: {page!r} (use names like Part_Box or PartDesign_Pad)"
        )
    return os.path.join(_wiki_dir(), page.replace("/", "__") + ".md")


def _fetch(url, limit):
    request = urllib.request.Request(url, headers={"User-Agent": "agentic-freecad"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{url} is larger than {limit} bytes")
    return data


# --------------------------------------------------------------------- search


def _terms(text):
    return [t for t in re.findall(r"[a-z0-9_]+", text.lower()) if len(t) > 1]


def _score(terms, title, text):
    title_l = title.lower()
    text_l = text.lower()
    score = 0.0
    for term in terms:
        if term in title_l:
            score += 6.0
        hits = text_l.count(term)
        if hits:
            score += 1.0 + min(hits, 8) * 0.25
        else:
            return 0.0  # every term must occur somewhere
    if " ".join(terms) in title_l.replace("_", " ").replace(".", " "):
        score += 4.0
    return score


def _snippet(text, terms, width=220):
    lower = text.lower()
    positions = [lower.find(t) for t in terms if lower.find(t) >= 0]
    start = max(0, min(positions) - 60) if positions else 0
    snippet = " ".join(text[start : start + width].split())
    return ("..." if start else "") + snippet


def _api_documents():
    for entry in _api_index():
        if entry["kind"] == "class":
            members = " ".join(m["name"] for m in entry.get("members", []))
            yield {
                "source": "api",
                "id": entry["name"],
                "title": entry["name"],
                "text": entry.get("doc", "") + " " + members,
            }
            for member in entry.get("members", []):
                yield {
                    "source": "api",
                    "id": f"{entry['name']}.{member['name']}",
                    "title": f"{entry['name']}.{member['name']}",
                    "text": member.get("signature", "") + " " + member.get("doc", ""),
                }
        else:
            yield {
                "source": "api",
                "id": entry["name"],
                "title": entry["name"],
                "text": entry.get("signature", "") + " " + entry.get("doc", ""),
            }


def _command_documents():
    if not FreeCAD.GuiUp:
        return
    import FreeCADGui as Gui

    for name in Gui.listCommands():
        cmd = Gui.Command.get(name)
        try:
            info = cmd.getInfo() or {}
        except Exception:
            info = {}
        menu = (info.get("menuText") or "").replace("&", "")
        yield {
            "source": "command",
            "id": name,
            "title": f"{name} ({menu})",
            "text": " ".join([menu, info.get("toolTip") or "", info.get("statusTip") or ""]),
        }


def _wiki_documents():
    folder = _wiki_dir()
    for filename in os.listdir(folder):
        if not filename.endswith(".md"):
            continue
        page = filename[:-3].replace("__", "/")
        try:
            with open(os.path.join(folder, filename), encoding="utf-8") as handle:
                text = handle.read()
        except Exception:
            continue
        yield {"source": "wiki", "id": page, "title": page.replace("_", " "), "text": text}


def _readme_documents(root, addon_folder):
    if not root:
        return
    folder = os.path.join(root, addon_folder or "Mod")
    if not os.path.isdir(folder):
        return
    for addon in sorted(os.listdir(folder)):
        path = os.path.join(folder, addon)
        if not os.path.isdir(path):
            continue
        for name in os.listdir(path):
            if name.lower().startswith("readme"):
                try:
                    with open(
                        os.path.join(path, name), encoding="utf-8", errors="replace"
                    ) as handle:
                        text = handle.read(512 * 1024)
                except Exception:
                    continue
                yield {
                    "source": "addon",
                    "id": f"{addon}/{name}",
                    "title": f"{addon} README",
                    "text": text,
                }


def search(payload):
    args = json.loads(payload)
    terms = _terms(args["query"])
    if not terms:
        raise ValueError("the query has no searchable words")
    sources = set(args.get("sources") or ["api", "command", "wiki", "addon"])
    generators = []
    if "api" in sources:
        generators.append(_api_documents())
    if "command" in sources:
        generators.append(_command_documents())
    if "wiki" in sources:
        generators.append(_wiki_documents())
    if "addon" in sources:
        generators.append(_readme_documents(args.get("root"), args.get("addonFolder")))
    results = []
    for generator in generators:
        for doc in generator:
            score = _score(terms, doc["title"], doc["text"])
            if score:
                results.append((score, doc))
    results.sort(key=lambda item: (-item[0], item[1]["id"]))
    limit = int(args.get("limit") or 20)
    hits = [
        {
            "source": d["source"],
            "id": d["id"],
            "title": d["title"],
            "score": round(s, 2),
            "snippet": _snippet(d["text"], terms),
        }
        for s, d in results[:limit]
    ]
    notes = []
    if "wiki" in sources and not any(True for _ in _wiki_documents()):
        notes.append("no wiki pages are cached: docs_page fetches one, docs_update fetches many")
    if "command" in sources and not FreeCAD.GuiUp:
        notes.append("GUI commands are not searched: this server has no GUI layer")
    return json.dumps(
        {"query": args["query"], "matched": len(results), "results": hits, "notes": notes}
    )


# ------------------------------------------------------------------- lookups


def api(payload):
    args = json.loads(payload)
    name = args["name"]
    entries = _api_index()
    if not entries:
        raise RuntimeError("the API index is missing (it is generated at build time)")
    # Scripts use App and Gui for FreeCAD and FreeCADGui.
    for short, full in (("App.", "FreeCAD."), ("Gui.", "FreeCADGui.")):
        if name.startswith(short):
            name = full + name[len(short) :]
    by_name = {e["name"]: e for e in entries}
    for e in entries:
        for alias in e.get("aliases", []):
            by_name.setdefault(alias, e)
    if name in by_name:
        return json.dumps(by_name[name])
    # Class.member
    if "." in name:
        owner, member = name.rsplit(".", 1)
        if owner in by_name:
            for m in by_name[owner].get("members", []):
                if m["name"] == member:
                    return json.dumps(
                        dict(m, owner=by_name[owner]["name"], source=by_name[owner]["source"])
                    )
    # A bare class name (Shape, Vector) or a C++ class name (TopoShape).
    short = [
        e
        for e in entries
        if e["kind"] == "class" and (e["name"].rsplit(".", 1)[-1] == name or e.get("class") == name)
    ]
    if len(short) == 1:
        return json.dumps(short[0])
    candidates = sorted({e["name"] for e in entries if name.lower() in e["name"].lower()})[:30]
    if short:
        candidates = sorted({e["name"] for e in short}) + candidates
    raise ValueError(
        f"no API entry {name!r}; candidates: {', '.join(candidates) or 'none'} (try docs_search)"
    )


def command(payload):
    args = json.loads(payload)
    if not FreeCAD.GuiUp:
        raise RuntimeError("GUI commands need the GUI layer (this server reports gui: false)")
    import FreeCADGui as Gui

    name = args["name"]
    cmd = Gui.Command.get(name)
    if cmd is None:
        raise ValueError(f"no command named {name!r}")
    info = cmd.getInfo() or {}
    out = {
        "name": name,
        "menuText": (info.get("menuText") or "").replace("&", ""),
        "toolTip": info.get("toolTip") or "",
        "statusTip": info.get("statusTip") or "",
        "whatsThis": info.get("whatsThis") or "",
        "shortcut": info.get("shortcut") or "",
        "wikiPage": name,
        "wikiUrl": f"https://wiki.freecad.org/{name}",
    }
    cached = _page_file(name)
    if os.path.exists(cached):
        with open(cached, encoding="utf-8") as handle:
            out["wiki"] = handle.read()
    else:
        out["note"] = "the wiki page is not cached; docs_page fetches it"
    return json.dumps(out)


def page(payload):
    args = json.loads(payload)
    name = args["page"].replace(" ", "_")
    path = _page_file(name)
    fetched = False
    if args.get("refresh") or not os.path.exists(path):
        url = f"{WIKI_RAW}/{urllib.parse.quote(name)}.md"
        data = _fetch(url, 8 * 1024 * 1024)
        with open(path, "wb") as handle:
            handle.write(data)
        fetched = True
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    links = sorted(set(re.findall(r"\]\(([A-Z][A-Za-z0-9_]+)\)", text)))[:100]
    return json.dumps(
        {
            "page": name,
            "fetched": fetched,
            "url": f"https://wiki.freecad.org/{name}",
            "content": text,
            "links": links,
        }
    )


def status(payload):
    folder = _wiki_dir()
    pages = [f for f in os.listdir(folder) if f.endswith(".md")]
    newest = max((os.path.getmtime(os.path.join(folder, f)) for f in pages), default=None)
    entries = _api_index()
    return json.dumps(
        {
            "api": {
                "classes": sum(1 for e in entries if e["kind"] == "class"),
                "functions": sum(1 for e in entries if e["kind"] == "function"),
            },
            "wiki": {
                "cache": folder,
                "pages": len(pages),
                "updated": (
                    time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(newest)) if newest else None
                ),
            },
            "commands": len(__import__("FreeCADGui").listCommands()) if FreeCAD.GuiUp else None,
        }
    )


def update(payload):
    args = json.loads(payload)
    fetched, failed = [], []
    if args.get("archive"):
        data = _fetch(WIKI_ARCHIVE, 1024 * 1024 * 1024)
        folder = _wiki_dir()
        count = 0
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for info in archive.infolist():
                parts = info.filename.split("/")
                # <repo>-main/wiki/<Page>.md : English pages only.
                if len(parts) == 3 and parts[1] == "wiki" and parts[2].endswith(".md"):
                    name = parts[2][:-3]
                    try:
                        target = _page_file(name)
                    except ValueError:
                        continue
                    with archive.open(info) as source, open(target, "wb") as sink:
                        sink.write(source.read())
                    count += 1
        return json.dumps({"archive": True, "pages": count, "cache": folder})
    for name in args.get("pages") or []:
        try:
            json.loads(page(json.dumps({"page": name, "refresh": True})))
            fetched.append(name)
        except Exception as err:
            failed.append({"page": name, "error": str(err)})
    return json.dumps({"fetched": fetched, "failed": failed})
