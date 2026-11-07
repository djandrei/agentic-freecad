// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <Mod/Agentic/AgenticGlobal.h>

#include "AgentConfig.h"

namespace Agentic
{

class Protocol;
class StdioServer;
class StderrLogger;

/// The one agent-mode runtime of the process.
///
/// It owns the protocol object with its tools and, when agent mode was
/// requested on the command line, the transport that feeds it. The GUI part of
/// the module announces itself through setGuiAvailable() and can listen to the
/// request/response stream to show it.
class AgenticExport Runtime
{
public:
    using ActivityListener = std::function<void(const std::string& line)>;

    static Runtime& instance();

    const AgentConfig& config() const;
    Protocol& protocol();

    /// Starts the configured transport. Returns true when a transport is now
    /// running; false when agent mode is off or the transport is unavailable.
    bool start();
    bool isRunning() const;

    void setGuiAvailable(bool available);
    bool guiAvailable() const;

    void setActivityListener(ActivityListener listener);

private:
    Runtime();
    ~Runtime();

    AgentConfig cfg;
    std::unique_ptr<Protocol> proto;
    std::unique_ptr<StdioServer> stdio;
    std::unique_ptr<StderrLogger> logger;
    ActivityListener listener;
    bool gui {false};
};

}  // namespace Agentic
