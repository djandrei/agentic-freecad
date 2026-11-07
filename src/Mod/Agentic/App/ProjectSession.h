// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

#include "ChangeLog.h"
#include "FileStore.h"
#include "Project.h"

namespace Agentic
{

class DocumentEngine;
class ScriptEngine;

/// What a tool can do with an open project.
///
/// Tools are written against this interface only. The one implementation is
/// EngineSession; the viewer observes it through SessionObserver.
class AgenticExport ProjectSession
{
public:
    virtual ~ProjectSession() = default;

    virtual const Project& project() const = 0;

    /// Project settings and folder contents as JSON.
    virtual nlohmann::json info() const = 0;
    /// Applies the given settings keys and saves the settings file.
    virtual nlohmann::json setSettings(const nlohmann::json& changes) = 0;

    virtual std::vector<FileEntry> listFiles(const std::string& path, bool recursive) const = 0;
    virtual ReadResult readFile(const std::string& path) const = 0;
    virtual WriteResult writeFile(
        const std::string& path,
        const std::string& content,
        const std::optional<std::string>& baseVersion
    ) = 0;
    virtual WriteResult editFile(
        const std::string& path,
        const std::vector<TextEdit>& edits,
        const std::optional<std::string>& baseVersion
    ) = 0;
    virtual void renameFile(const std::string& from, const std::string& to) = 0;
    virtual void deleteFile(const std::string& path) = 0;

    /// Records a change made by a tool and tells the observers.
    virtual const ChangeEntry& recordChange(ChangeEntry entry) = 0;
    virtual const ChangeLog& changes() const = 0;

    /// Documents, objects and properties of the project.
    virtual DocumentEngine& documents() = 0;
    /// Python execution in the project's namespace.
    virtual ScriptEngine& scripts() = 0;
    /// Resolves a project-relative path inside the sandbox (see FileStore::resolve).
    virtual std::filesystem::path resolvePath(const std::string& relative, bool mustExist) const = 0;
    /// Tells the observers that a document changed.
    virtual void notifyDocument(const std::string& document, const std::string& event) = 0;
};

}  // namespace Agentic
