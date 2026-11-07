// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QDateTime>

#include "ChangeLog.h"

using json = nlohmann::json;

namespace Agentic
{

json ChangeEntry::toJson() const
{
    return {
        {"seq", seq},
        {"time", time},
        {"tool", tool},
        {"kind", kind},
        {"summary", summary},
        {"python", python.empty() ? json() : json(python)},
        {"detail", detail.is_null() ? json::object() : detail},
    };
}

const ChangeEntry& ChangeLog::add(ChangeEntry entry)
{
    entry.seq = next++;
    if (entry.time.empty()) {
        entry.time = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    }
    entries.push_back(std::move(entry));
    while (entries.size() > capacity) {
        entries.pop_front();
    }
    return entries.back();
}

std::uint64_t ChangeLog::nextSeq() const
{
    return next;
}

std::vector<ChangeEntry> ChangeLog::since(std::uint64_t since, std::size_t limit) const
{
    std::vector<ChangeEntry> out;
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

}  // namespace Agentic
