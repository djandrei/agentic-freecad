// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Application.h>

#include "Protocol.h"
#include "Runtime.h"
#include "Tools.h"

using json = nlohmann::json;

namespace
{

std::string configValue(const char* key)
{
    const auto& config = App::Application::Config();
    auto it = config.find(key);
    return it == config.end() ? std::string() : it->second;
}

json emptyObjectSchema()
{
    return {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}};
}

}  // namespace

namespace Agentic
{

void registerCoreTools(Protocol& protocol, const Runtime& runtime)
{
    protocol.addTool({
        "version",
        "Report the FreeCAD version and the agentic-freecad server version.",
        emptyObjectSchema(),
        [](const json&) -> json {
            return {
                {"application", configValue("ExeName")},
                {"version", Protocol::serverVersion()},
                {"build",
                 {
                     {"major", configValue("BuildVersionMajor")},
                     {"minor", configValue("BuildVersionMinor")},
                     {"point", configValue("BuildVersionPoint")},
                     {"revision", configValue("BuildRevision")},
                 }},
                {"server", Protocol::serverName},
                {"protocolVersion", Protocol::protocolVersion},
            };
        },
    });

    protocol.addTool({
        "status",
        "Report how this server was started: transport, headless flag, project folder "
        "and whether the GUI layer (views, captures) is available.",
        emptyObjectSchema(),
        [&runtime](const json&) -> json {
            const AgentConfig& cfg = runtime.config();
            return {
                {"agentMode", cfg.enabled},
                {"transport", cfg.stdio ? "stdio" : (cfg.port > 0 ? "http" : "none")},
                {"port", cfg.port},
                {"headless", cfg.headless},
                {"project", cfg.project.empty() ? json() : json(cfg.project)},
                {"gui", runtime.guiAvailable()},
            };
        },
    });
}

}  // namespace Agentic
