// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "ProjectSession.h"
#include "SessionObserver.h"

namespace Agentic
{

/// The session of one open project: its folder, its files and its change log.
///
/// Document, script, command and view engines attach to it in later stages;
/// everything that mutates goes through here so the change log and the
/// observers see it.
class AgenticExport EngineSession: public ProjectSession
{
public:
    EngineSession(const std::filesystem::path& root, const ObserverList& observers);

    const Project& project() const override;
    Project& mutableProject();

    nlohmann::json info() const override;
    nlohmann::json setSettings(const nlohmann::json& changes) override;

    std::vector<FileEntry> listFiles(const std::string& path, bool recursive) const override;
    ReadResult readFile(const std::string& path) const override;
    WriteResult writeFile(
        const std::string& path,
        const std::string& content,
        const std::optional<std::string>& baseVersion
    ) override;
    WriteResult editFile(
        const std::string& path,
        const std::vector<TextEdit>& edits,
        const std::optional<std::string>& baseVersion
    ) override;
    void renameFile(const std::string& from, const std::string& to) override;
    void deleteFile(const std::string& path) override;

    const ChangeEntry& recordChange(ChangeEntry entry) override;
    const ChangeLog& changes() const override;

    /// The tool whose call is in progress, recorded on every change it makes.
    void setCurrentTool(const std::string& name);

private:
    void recordWrite(const WriteResult& result);

    Project proj;
    FileStore store;
    ChangeLog log;
    const ObserverList& observers;
    std::string currentTool;
};

}  // namespace Agentic
