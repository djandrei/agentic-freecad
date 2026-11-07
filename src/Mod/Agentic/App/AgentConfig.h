// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// The agent-mode settings taken from the command line.
///
/// App::Application fills the configuration keys AgentMode, AgentStdio,
/// AgentHeadless, AgentPort and AgentProject while parsing the program
/// options; this struct reads them back in one place.
struct AgenticExport AgentConfig
{
    bool enabled {false};
    bool stdio {false};
    bool headless {false};
    int port {0};
    std::string project;

    static AgentConfig fromApplication();
};

}  // namespace Agentic
