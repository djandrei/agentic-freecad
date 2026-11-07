// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QDateTime>

#include "MessageLog.h"

using json = nlohmann::json;

namespace
{

const char* levelName(Base::LogStyle level)
{
    switch (level) {
        case Base::LogStyle::Warning:
            return "warning";
        case Base::LogStyle::Error:
            return "error";
        case Base::LogStyle::Log:
            return "log";
        case Base::LogStyle::Critical:
            return "critical";
        case Base::LogStyle::Notification:
            return "notification";
        default:
            return "message";
    }
}

}  // namespace

namespace Agentic
{

json MessageEntry::toJson() const
{
    return {
        {"seq", seq},
        {"time", time},
        {"level", level},
        {"notifier", notifier.empty() ? json() : json(notifier)},
        {"text", text},
    };
}

MessageLog::MessageLog()
{
    bMsg = true;
    bWrn = true;
    bErr = true;
    bLog = true;
}

void MessageLog::sendLog(
    const std::string& notifiername,
    const std::string& msg,
    Base::LogStyle level,
    Base::IntendedRecipient /*recipient*/,
    Base::ContentType /*content*/
)
{
    MessageEntry entry;
    entry.time = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    entry.level = levelName(level);
    entry.notifier = notifiername;
    entry.text = msg;
    if (!entry.text.empty() && entry.text.back() == '\n') {
        entry.text.pop_back();
    }

    std::lock_guard<std::mutex> lock(mutex);
    entry.seq = next++;
    entries.push_back(std::move(entry));
    while (entries.size() > capacity) {
        entries.pop_front();
    }
}

std::uint64_t MessageLog::nextSeq() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return next;
}

std::vector<MessageEntry> MessageLog::since(std::uint64_t since, std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<MessageEntry> out;
    for (const auto& entry : entries) {
        if (entry.seq >= since) {
            out.push_back(entry);
            if (limit && out.size() >= limit) {
                break;
            }
        }
    }
    return out;
}

void MessageLog::count(std::uint64_t from, int& errors, int& warnings) const
{
    std::lock_guard<std::mutex> lock(mutex);
    errors = 0;
    warnings = 0;
    for (const auto& entry : entries) {
        if (entry.seq < from) {
            continue;
        }
        if (entry.level == "error" || entry.level == "critical") {
            ++errors;
        }
        else if (entry.level == "warning") {
            ++warnings;
        }
    }
}

}  // namespace Agentic
