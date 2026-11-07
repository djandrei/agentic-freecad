// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <Base/Console.h>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// Console observer that writes warnings, errors and log lines to stderr.
///
/// In stdio mode the standard console observer is silenced because it prints
/// messages to stdout, which is the transport. This logger keeps the
/// diagnostics visible to whoever launched the process.
class AgenticExport StderrLogger: public Base::ILogger
{
public:
    StderrLogger();

    void sendLog(
        const std::string& notifiername,
        const std::string& msg,
        Base::LogStyle level,
        Base::IntendedRecipient recipient,
        Base::ContentType content
    ) override;
};

}  // namespace Agentic
