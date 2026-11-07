// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include <QCryptographicHash>
#include <QDateTime>
#include <QTimeZone>

#include "FileStore.h"

namespace fs = std::filesystem;

namespace
{

bool isHidden(const fs::path& path)
{
    const std::string name = path.filename().string();
    return !name.empty() && name[0] == '.';
}

bool isUnder(const fs::path& candidate, const fs::path& root)
{
    auto rootIt = root.begin();
    auto candIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candIt) {
        if (candIt == candidate.end() || *rootIt != *candIt) {
            return false;
        }
    }
    return true;
}

std::string isoTime(const fs::file_time_type& time)
{
    // file_clock to system_clock without clock_cast, which not every libstdc++ provides.
    const auto fileNow = fs::file_time_type::clock::now();
    const auto systemNow = std::chrono::system_clock::now();
    const auto system = systemNow
        + std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fileNow);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(system.time_since_epoch());
    return QDateTime::fromMSecsSinceEpoch(ms.count(), QTimeZone::utc())
        .toString(Qt::ISODateWithMs)
        .toStdString();
}

bool looksLikeText(const std::string& content)
{
    const size_t probe = std::min<size_t>(content.size(), 8192);
    for (size_t i = 0; i < probe; ++i) {
        if (content[i] == '\0') {
            return false;
        }
    }
    // UTF-8 validation of the whole content.
    size_t i = 0;
    while (i < content.size()) {
        const auto c = static_cast<unsigned char>(content[i]);
        size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        }
        else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        }
        else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        }
        else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        }
        else {
            return false;
        }
        if (i + extra >= content.size() + (extra == 0 ? 1 : 0) && extra > 0) {
            return false;
        }
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= content.size()
                || (static_cast<unsigned char>(content[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

}  // namespace

namespace Agentic
{

FileStore::FileStore(const fs::path& root)
    : rootPath(fs::weakly_canonical(root))
{}

const fs::path& FileStore::root() const
{
    return rootPath;
}

fs::path FileStore::resolve(const std::string& relative, bool mustExist) const
{
    fs::path given(relative);
    fs::path joined = given.is_absolute() ? given : rootPath / given;
    joined = joined.lexically_normal();
    if (joined.filename().empty() && joined.has_parent_path()) {
        joined = joined.parent_path();  // strip a trailing slash
    }

    // Resolve symlinks in the existing part of the path.
    fs::path resolved = fs::weakly_canonical(joined);
    if (!isUnder(resolved, rootPath)) {
        throw std::invalid_argument("path leaves the project folder: " + relative);
    }
    if (mustExist && !fs::exists(resolved)) {
        throw std::invalid_argument("no such file: " + relative);
    }
    return resolved;
}

std::string FileStore::relativeOf(const fs::path& absolute) const
{
    return absolute.lexically_relative(rootPath).generic_string();
}

std::vector<FileEntry> FileStore::list(const std::string& relative, bool recursive) const
{
    const fs::path dir = resolve(relative.empty() ? "." : relative, true);
    if (!fs::is_directory(dir)) {
        throw std::invalid_argument("not a folder: " + relative);
    }
    std::vector<FileEntry> entries;
    auto add = [&](const fs::directory_entry& entry) {
        FileEntry item;
        item.path = relativeOf(entry.path());
        item.name = entry.path().filename().string();
        item.isDirectory = entry.is_directory();
        item.size = item.isDirectory ? 0 : entry.file_size();
        item.modified = isoTime(entry.last_write_time());
        entries.push_back(std::move(item));
    };
    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(dir);
             it != fs::recursive_directory_iterator();
             ++it) {
            if (isHidden(it->path())) {
                if (it->is_directory()) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            add(*it);
        }
    }
    else {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!isHidden(entry.path())) {
                add(entry);
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.path < b.path;
    });
    return entries;
}

std::string FileStore::readAll(const fs::path& full)
{
    std::ifstream in(full, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + full.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string FileStore::versionOf(const std::string& content)
{
    const QByteArray hash = QCryptographicHash::hash(
        QByteArray::fromRawData(content.data(), static_cast<qsizetype>(content.size())),
        QCryptographicHash::Sha256
    );
    return hash.toHex().left(16).toStdString();
}

ReadResult FileStore::read(const std::string& relative) const
{
    const fs::path full = resolve(relative, true);
    if (!fs::is_regular_file(full)) {
        throw std::invalid_argument("not a file: " + relative);
    }
    ReadResult result;
    result.path = relativeOf(full);
    result.size = fs::file_size(full);
    if (result.size > maxTextBytes) {
        throw std::runtime_error(relative + " is larger than the 8 MiB limit of file_read");
    }
    const std::string content = readAll(full);
    result.version = versionOf(content);
    result.isText = looksLikeText(content);
    if (result.isText) {
        result.content = content;
    }
    return result;
}

WriteResult FileStore::writeResolved(
    const fs::path& full,
    const std::string& relative,
    const std::string& content,
    const std::optional<std::string>& oldContent
)
{
    fs::create_directories(full.parent_path());
    const fs::path temp = full.parent_path() / (".agentic-" + full.filename().string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write " + relative);
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    fs::rename(temp, full);

    WriteResult result;
    result.path = relative;
    result.version = versionOf(content);
    result.created = !oldContent.has_value();

    const std::vector<std::string> before = oldContent ? splitLines(*oldContent)
                                                       : std::vector<std::string>();
    const std::vector<std::string> after = splitLines(content);
    size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) {
        ++suffix;
    }
    result.linesRemoved = static_cast<int>(before.size() - prefix - suffix);
    result.linesAdded = static_cast<int>(after.size() - prefix - suffix);
    result.startLine = (result.linesRemoved || result.linesAdded) ? static_cast<int>(prefix) + 1 : 0;
    return result;
}

WriteResult FileStore::write(
    const std::string& relative,
    const std::string& content,
    const std::optional<std::string>& baseVersion
)
{
    const fs::path full = resolve(relative, false);
    if (fs::is_directory(full)) {
        throw std::invalid_argument("is a folder: " + relative);
    }
    std::optional<std::string> old;
    if (fs::exists(full)) {
        old = readAll(full);
        if (baseVersion && *baseVersion != versionOf(*old)) {
            throw ConflictError(relative, versionOf(*old));
        }
    }
    else if (baseVersion && !baseVersion->empty()) {
        throw ConflictError(relative, "");
    }
    return writeResolved(full, relativeOf(full), content, old);
}

WriteResult FileStore::edit(
    const std::string& relative,
    const std::vector<TextEdit>& edits,
    const std::optional<std::string>& baseVersion
)
{
    const fs::path full = resolve(relative, true);
    if (!fs::is_regular_file(full)) {
        throw std::invalid_argument("not a file: " + relative);
    }
    const std::string old = readAll(full);
    if (baseVersion && *baseVersion != versionOf(old)) {
        throw ConflictError(relative, versionOf(old));
    }
    std::string text = old;
    size_t index = 0;
    for (const auto& change : edits) {
        if (change.oldText.empty()) {
            throw std::invalid_argument("edit " + std::to_string(index) + ": oldText is empty");
        }
        const auto first = text.find(change.oldText);
        if (first == std::string::npos) {
            throw std::invalid_argument("edit " + std::to_string(index) + ": oldText not found");
        }
        if (text.find(change.oldText, first + 1) != std::string::npos) {
            throw std::invalid_argument(
                "edit " + std::to_string(index) + ": oldText occurs more than once"
            );
        }
        text.replace(first, change.oldText.size(), change.newText);
        ++index;
    }
    return writeResolved(full, relativeOf(full), text, old);
}

void FileStore::rename(const std::string& from, const std::string& to)
{
    const fs::path source = resolve(from, true);
    const fs::path target = resolve(to, false);
    if (fs::exists(target)) {
        throw std::invalid_argument("target exists: " + to);
    }
    fs::create_directories(target.parent_path());
    fs::rename(source, target);
}

void FileStore::remove(const std::string& relative)
{
    const fs::path full = resolve(relative, true);
    if (full == rootPath) {
        throw std::invalid_argument("refusing to delete the project folder");
    }
    if (fs::is_directory(full)) {
        if (!fs::is_empty(full)) {
            throw std::invalid_argument("folder is not empty: " + relative);
        }
        fs::remove(full);
        return;
    }
    fs::remove(full);
}

}  // namespace Agentic
