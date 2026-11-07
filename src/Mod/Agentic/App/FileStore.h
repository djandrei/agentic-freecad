// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

struct AgenticExport FileEntry
{
    std::string path;  ///< relative to the project root, '/' separated
    std::string name;
    bool isDirectory {false};
    std::uintmax_t size {0};
    std::string modified;  ///< ISO 8601 UTC
};

struct AgenticExport ReadResult
{
    std::string path;
    bool isText {false};
    std::string content;  ///< empty for binary files
    std::string version;  ///< content hash, also for binary files
    std::uintmax_t size {0};
};

struct AgenticExport WriteResult
{
    std::string path;
    std::string version;
    bool created {false};
    int startLine {0};  ///< 1-based first changed line, 0 when nothing changed
    int linesAdded {0};
    int linesRemoved {0};
};

struct AgenticExport TextEdit
{
    std::string oldText;
    std::string newText;
};

/// Thrown when a write's baseVersion does not match the file on disk.
class AgenticExport ConflictError: public std::runtime_error
{
public:
    ConflictError(const std::string& path, const std::string& current)
        : std::runtime_error(path + " changed since it was read (current version " + current + ")")
        , currentVersion(current)
    {}
    std::string currentVersion;
};

/// The file sandbox of a project.
///
/// Every path is resolved under the project root with symlinks followed;
/// anything that would leave the root is refused. Writes are atomic (a
/// temporary file is renamed over the target) and every result carries the
/// content hash of the file as its version, so a later write can detect that
/// the file changed in between. Listings skip hidden entries.
class AgenticExport FileStore
{
public:
    explicit FileStore(const std::filesystem::path& root);

    const std::filesystem::path& root() const;

    /// Resolves a project-relative path. Throws std::invalid_argument when the
    /// path escapes the root, or when mustExist is set and nothing is there.
    std::filesystem::path resolve(const std::string& relative, bool mustExist) const;

    std::vector<FileEntry> list(const std::string& relative, bool recursive) const;
    ReadResult read(const std::string& relative) const;

    WriteResult write(
        const std::string& relative,
        const std::string& content,
        const std::optional<std::string>& baseVersion
    );
    WriteResult edit(
        const std::string& relative,
        const std::vector<TextEdit>& edits,
        const std::optional<std::string>& baseVersion
    );
    void rename(const std::string& from, const std::string& to);
    void remove(const std::string& relative);

    static std::string versionOf(const std::string& content);
    std::string relativeOf(const std::filesystem::path& absolute) const;

    static constexpr std::uintmax_t maxTextBytes = 8 * 1024 * 1024;

private:
    WriteResult writeResolved(
        const std::filesystem::path& full,
        const std::string& relative,
        const std::string& content,
        const std::optional<std::string>& oldContent
    );
    static std::string readAll(const std::filesystem::path& full);

    std::filesystem::path rootPath;
};

}  // namespace Agentic
