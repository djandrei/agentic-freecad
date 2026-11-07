// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <App/Application.h>

#include "Protocol.h"

using json = nlohmann::json;

namespace Agentic
{

namespace
{

constexpr int parseError = -32700;
constexpr int invalidRequest = -32600;
constexpr int methodNotFound = -32601;
constexpr int invalidParams = -32602;

const char* instructions
    = "FreeCAD in agent mode. Call `version` and `status` to see what this server "
      "offers; the full tool set (documents, objects, Python, views, export) arrives "
      "in later stages of the agentic-freecad fork.";

}  // namespace

Protocol::Protocol() = default;

void Protocol::addTool(Tool tool)
{
    const std::string name = tool.name;
    tools[name] = std::move(tool);
}

bool Protocol::hasTool(const std::string& name) const
{
    return tools.contains(name);
}

std::string Protocol::serverVersion()
{
    const auto& config = App::Application::Config();
    auto it = config.find("ExeVersion");
    return it == config.end() ? std::string("unknown") : it->second;
}

std::string Protocol::handleLine(const std::string& line)
{
    json request = json::parse(line, nullptr, false);
    if (request.is_discarded()) {
        return error(nullptr, parseError, "Parse error").dump();
    }
    json response = handleRequest(request);
    if (response.is_discarded() || response.is_null()) {
        return {};
    }
    return response.dump();
}

json Protocol::handleRequest(const json& request)
{
    if (!request.is_object()) {
        return error(nullptr, invalidRequest, "Invalid Request");
    }

    const json id = request.contains("id") ? request["id"] : json();
    const bool isNotification = !request.contains("id");

    if (!request.contains("method") || !request["method"].is_string()) {
        return isNotification ? json(json::value_t::discarded)
                              : error(id, invalidRequest, "Invalid Request");
    }

    const std::string method = request["method"].get<std::string>();
    const json params = request.contains("params") ? request["params"] : json::object();

    if (isNotification) {
        // notifications/initialized, notifications/cancelled, ...: nothing to answer.
        return json(json::value_t::discarded);
    }

    if (method == "initialize") {
        return initialize(id);
    }
    if (method == "ping") {
        return result(id, json::object());
    }
    if (method == "tools/list") {
        return listTools(id);
    }
    if (method == "tools/call") {
        return callTool(id, params);
    }
    return error(id, methodNotFound, "Method not found: " + method);
}

json Protocol::initialize(const json& id) const
{
    json value = {
        {"protocolVersion", protocolVersion},
        {"capabilities", {{"tools", json::object()}}},
        {"serverInfo", {{"name", serverName}, {"version", serverVersion()}}},
        {"instructions", instructions},
    };
    return result(id, value);
}

json Protocol::listTools(const json& id) const
{
    json list = json::array();
    for (const auto& [name, tool] : tools) {
        list.push_back({
            {"name", name},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema},
        });
    }
    return result(id, {{"tools", list}});
}

json Protocol::callTool(const json& id, const json& params) const
{
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
        return error(id, invalidParams, "tools/call needs a tool name");
    }
    const std::string name = params["name"].get<std::string>();
    auto it = tools.find(name);
    if (it == tools.end()) {
        return error(id, invalidParams, "Unknown tool: " + name);
    }

    const json arguments = params.contains("arguments") ? params["arguments"] : json::object();
    json value;
    try {
        value = it->second.handler(arguments);
    }
    catch (const std::exception& e) {
        json failure = {
            {"content", json::array({{{"type", "text"}, {"text", e.what()}}})},
            {"isError", true},
        };
        return result(id, failure);
    }

    json success = {
        {"content", json::array({{{"type", "text"}, {"text", value.dump()}}})},
        {"structuredContent", value},
        {"isError", false},
    };
    return result(id, success);
}

json Protocol::result(const json& id, json value)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
}

json Protocol::error(const json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

}  // namespace Agentic
