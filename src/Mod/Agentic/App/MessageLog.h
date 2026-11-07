// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Base/Console.h>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

struct AgenticExport MessageEntry
{
    std::uint64_t seq {0};
    std::string time;   ///< ISO 8601 UTC
    std::string level;  ///< "message", "warning", "error", "log", "critical", "notification"
    std::string notifier;
    std::string text;

    nlohmann::json toJson() const;
};

/// Records every message FreeCAD prints, sequence-numbered, so a tool call
/// can return exactly the messages of its own run.
///
/// Console messages may arrive from other threads (the recompute worker
/// among them), so the log is locked.
class AgenticExport MessageLog: public Base::ILogger
{
public:
    MessageLog();

    void sendLog(
        const std::string& notifiername,
        const std::string& msg,
        Base::LogStyle level,
        Base::IntendedRecipient recipient,
        Base::ContentType content
    ) override;

    /// The sequence number the next message will get.
    std::uint64_t nextSeq() const;

    /// Messages with seq >= since, oldest first, at most limit (0 = all kept).
    std::vector<MessageEntry> since(std::uint64_t since, std::size_t limit = 0) const;

    /// Error and warning counts in [from, nextSeq()).
    void count(std::uint64_t from, int& errors, int& warnings) const;

    static constexpr std::size_t capacity = 10000;

private:
    mutable std::mutex mutex;
    std::deque<MessageEntry> entries;
    std::uint64_t next {1};
};

}  // namespace Agentic
