// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "StderrLogger.h"

namespace Agentic
{

StderrLogger::StderrLogger()
{
    bMsg = true;
    bWrn = true;
    bErr = true;
    bLog = true;
}

void StderrLogger::sendLog(
    const std::string& notifiername,
    const std::string& msg,
    Base::LogStyle level,
    Base::IntendedRecipient /*recipient*/,
    Base::ContentType /*content*/
)
{
    const char* prefix = "";
    switch (level) {
        case Base::LogStyle::Warning:
            prefix = "warning: ";
            break;
        case Base::LogStyle::Error:
        case Base::LogStyle::Critical:
            prefix = "error: ";
            break;
        case Base::LogStyle::Log:
            prefix = "log: ";
            break;
        default:
            break;
    }
    if (notifiername.empty()) {
        std::fprintf(stderr, "%s%s", prefix, msg.c_str());
    }
    else {
        std::fprintf(stderr, "%s[%s] %s", prefix, notifiername.c_str(), msg.c_str());
    }
    if (msg.empty() || msg.back() != '\n') {
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
}

}  // namespace Agentic
