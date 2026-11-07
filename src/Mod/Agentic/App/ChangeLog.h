// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// One mutating operation on a project.
struct AgenticExport ChangeEntry
{
    std::uint64_t seq {0};
    std::string time;       ///< ISO 8601 UTC
    std::string tool;       ///< the tool that caused it
    std::string kind;       ///< "file", "python", "property", "object", "command", "document", ...
    std::string summary;    ///< one line for a person
    std::string python;     ///< the equivalent Python line(s), when there is one
    nlohmann::json detail;  ///< kind-specific data (a file hunk, an object name, ...)

    nlohmann::json toJson() const;
};

/// The per-project record of everything the agent changed, in order.
class AgenticExport ChangeLog
{
public:
    const ChangeEntry& add(ChangeEntry entry);
    std::uint64_t nextSeq() const;
    std::vector<ChangeEntry> since(std::uint64_t since, std::size_t limit = 0) const;

    static constexpr std::size_t capacity = 10000;

private:
    std::deque<ChangeEntry> entries;
    std::uint64_t next {1};
};

}  // namespace Agentic
