// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "DocumentEngine.h"
#include "EngineSession.h"
#include "ScriptEngine.h"

using json = nlohmann::json;

namespace Agentic
{

EngineSession::EngineSession(
    const std::filesystem::path& root,
    const ObserverList& observerList,
    MessageLog& messages
)
    : proj(root)
    , store(root)
    , observers(observerList)
    , docs(std::make_unique<DocumentEngine>(*this, messages))
    , scriptEngine(std::make_unique<ScriptEngine>(*this, messages))
{
    proj.load();
    if (!Project::exists(root)) {
        proj.save();
    }
}

EngineSession::~EngineSession() = default;

DocumentEngine& EngineSession::documents()
{
    return *docs;
}

ScriptEngine& EngineSession::scripts()
{
    return *scriptEngine;
}

std::filesystem::path EngineSession::resolvePath(const std::string& relative, bool mustExist) const
{
    return store.resolve(relative, mustExist);
}

void EngineSession::notifyDocument(const std::string& document, const std::string& event)
{
    observers.each([this, &document, &event](SessionObserver& o) {
        o.documentChanged(proj, document, event);
    });
}

const Project& EngineSession::project() const
{
    return proj;
}

Project& EngineSession::mutableProject()
{
    return proj;
}

json EngineSession::info() const
{
    json value = proj.info();
    value["changes"] = log.nextSeq() - 1;
    return value;
}

json EngineSession::setSettings(const json& changes)
{
    proj.settings().fromJson(changes);
    proj.save();
    observers.each([this](SessionObserver& o) { o.projectSettingsChanged(proj); });
    ChangeEntry entry;
    entry.tool = currentTool;
    entry.kind = "project";
    entry.summary = "settings changed: " + changes.dump();
    entry.detail = changes;
    recordChange(std::move(entry));
    return proj.info();
}

std::vector<FileEntry> EngineSession::listFiles(const std::string& path, bool recursive) const
{
    return store.list(path, recursive);
}

ReadResult EngineSession::readFile(const std::string& path) const
{
    return store.read(path);
}

void EngineSession::recordWrite(const WriteResult& result)
{
    observers.each([this, &result](SessionObserver& o) { o.fileWritten(proj, result); });
    ChangeEntry entry;
    entry.tool = currentTool;
    entry.kind = "file";
    entry.summary = (result.created ? "created " : "wrote ") + result.path;
    if (result.startLine) {
        entry.summary += " (line " + std::to_string(result.startLine) + ": +"
            + std::to_string(result.linesAdded) + " -" + std::to_string(result.linesRemoved) + ")";
    }
    entry.detail = {
        {"path", result.path},
        {"version", result.version},
        {"created", result.created},
        {"startLine", result.startLine},
        {"linesAdded", result.linesAdded},
        {"linesRemoved", result.linesRemoved},
    };
    recordChange(std::move(entry));
}

WriteResult EngineSession::writeFile(
    const std::string& path,
    const std::string& content,
    const std::optional<std::string>& baseVersion
)
{
    WriteResult result = store.write(path, content, baseVersion);
    recordWrite(result);
    return result;
}

WriteResult EngineSession::editFile(
    const std::string& path,
    const std::vector<TextEdit>& edits,
    const std::optional<std::string>& baseVersion
)
{
    WriteResult result = store.edit(path, edits, baseVersion);
    recordWrite(result);
    return result;
}

void EngineSession::renameFile(const std::string& from, const std::string& to)
{
    store.rename(from, to);
    observers.each([this, &from, &to](SessionObserver& o) { o.fileRenamed(proj, from, to); });
    ChangeEntry entry;
    entry.tool = currentTool;
    entry.kind = "file";
    entry.summary = "renamed " + from + " to " + to;
    entry.detail = {{"from", from}, {"to", to}};
    recordChange(std::move(entry));
}

void EngineSession::deleteFile(const std::string& path)
{
    store.remove(path);
    observers.each([this, &path](SessionObserver& o) { o.fileRemoved(proj, path); });
    ChangeEntry entry;
    entry.tool = currentTool;
    entry.kind = "file";
    entry.summary = "deleted " + path;
    entry.detail = {{"path", path}};
    recordChange(std::move(entry));
}

const ChangeEntry& EngineSession::recordChange(ChangeEntry entry)
{
    if (entry.tool.empty()) {
        entry.tool = currentTool;
    }
    const ChangeEntry& stored = log.add(std::move(entry));
    observers.each([this, &stored](SessionObserver& o) { o.changeLogged(proj, stored); });
    return stored;
}

const ChangeLog& EngineSession::changes() const
{
    return log;
}

void EngineSession::setCurrentTool(const std::string& name)
{
    currentTool = name;
}

}  // namespace Agentic
