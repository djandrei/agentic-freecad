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

json statusValue(const Agentic::Runtime& runtime)
{
    const Agentic::AgentConfig& cfg = runtime.config();
    std::string transport = "none";
    if (cfg.stdio) {
        transport = "stdio";
    }
    else if (runtime.isRunning()) {
        transport = "http";
    }
    return {
        {"agentMode", cfg.enabled},
        {"transport", transport},
        {"port", cfg.port},
        {"url", runtime.httpUrl().empty() ? json() : json(runtime.httpUrl())},
        {"headless", cfg.headless},
        {"project", cfg.project.empty() ? json() : json(cfg.project)},
        {"gui", runtime.guiAvailable()},
    };
}

const char* loopText
    = "FreeCAD in agent mode. You drive FreeCAD through tools; there is no window to look at.\n"
      "\n"
      "The loop: write a Python script into the project folder (or call the structured "
      "document and object tools), run it, read back the messages and the status of every "
      "object, capture a view or measure the geometry to check the result, export when done.\n"
      "\n"
      "Call `status` to see which tool groups this server offers (view and capture tools "
      "need the GUI layer) and `version` for the FreeCAD version. Every tool returns "
      "structured JSON; errors from a tool come back as `isError` results with the message.";

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
        [&runtime](const json&) -> json { return statusValue(runtime); },
    });
}

void registerCoreResources(Protocol& protocol, const Runtime& runtime)
{
    protocol.addResource({
        "agentic://status",
        "Server status",
        "How this server was started and which tool groups are available.",
        "application/json",
        [&runtime]() { return statusValue(runtime).dump(2); },
    });
}

void registerCorePrompts(Protocol& protocol)
{
    protocol.setInstructions(loopText);
    protocol.addPrompt({
        "agentic-loop",
        "How to work with FreeCAD through this server: the write, run, inspect, capture, export "
        "loop.",
        {},
        [](const json&) { return std::string(loopText); },
    });
}

}  // namespace Agentic
