// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Application.h>

#include "AgentConfig.h"

namespace
{

std::string configValue(const char* key)
{
    const auto& config = App::Application::Config();
    auto it = config.find(key);
    return it == config.end() ? std::string() : it->second;
}

}  // namespace

namespace Agentic
{

AgentConfig AgentConfig::fromApplication()
{
    AgentConfig cfg;
    cfg.enabled = configValue("AgentMode") == "1";
    cfg.stdio = configValue("AgentStdio") == "1";
    cfg.headless = configValue("AgentHeadless") == "1";
    cfg.project = configValue("AgentProject");
    const std::string port = configValue("AgentPort");
    if (!port.empty()) {
        cfg.port = std::stoi(port);
    }
    return cfg;
}

}  // namespace Agentic
