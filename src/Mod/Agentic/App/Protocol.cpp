// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <chrono>

#include <App/Application.h>

#include "Protocol.h"
#include "Schema.h"

using json = nlohmann::json;

namespace Agentic
{

namespace
{

constexpr int parseError = -32700;
constexpr int invalidRequest = -32600;
constexpr int methodNotFound = -32601;
constexpr int invalidParams = -32602;
constexpr int resourceNotFound = -32002;
constexpr int internalError = -32603;

// Strings reach responses from FreeCAD (console messages, Python output,
// property values); one invalid UTF-8 byte must not make serialising throw.
std::string serialize(const json& value)
{
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace

Protocol::Protocol() = default;

void Protocol::addTool(Tool tool)
{
    const std::string name = tool.name;
    tools[name] = std::move(tool);
}

void Protocol::addResource(Resource resource)
{
    const std::string uri = resource.uri;
    resources[uri] = std::move(resource);
}

void Protocol::addResourceTemplate(ResourceTemplate resourceTemplate)
{
    templates.push_back(std::move(resourceTemplate));
}

void Protocol::addPrompt(Prompt prompt)
{
    const std::string name = prompt.name;
    prompts[name] = std::move(prompt);
}

bool Protocol::hasTool(const std::string& name) const
{
    return tools.contains(name);
}

const std::map<std::string, Protocol::Tool>& Protocol::toolList() const
{
    return tools;
}

void Protocol::setInstructions(std::string text)
{
    instructions = std::move(text);
}

void Protocol::setToolGate(ToolGate newGate)
{
    gate = std::move(newGate);
}

void Protocol::setToolHooks(ToolStartHook start, ToolFinishHook finish)
{
    onToolStart = std::move(start);
    onToolFinish = std::move(finish);
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
        return serialize(error(nullptr, parseError, "Parse error"));
    }
    json response;
    try {
        response = handleRequest(request);
    }
    catch (const std::exception& e) {
        // Nothing may escape into a transport: the server would stop.
        const json id = request.is_object() && request.contains("id") ? request["id"] : json();
        return serialize(error(id, internalError, std::string("Internal error: ") + e.what()));
    }
    if (response.is_discarded() || response.is_null()) {
        return {};
    }
    return serialize(response);
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
    if (method == "resources/list") {
        return listResources(id);
    }
    if (method == "resources/read") {
        return readResource(id, params);
    }
    if (method == "resources/templates/list") {
        return listResourceTemplates(id);
    }
    if (method == "prompts/list") {
        return listPrompts(id);
    }
    if (method == "prompts/get") {
        return getPrompt(id, params);
    }
    return error(id, methodNotFound, "Method not found: " + method);
}

json Protocol::initialize(const json& id) const
{
    json value = {
        {"protocolVersion", protocolVersion},
        {"capabilities",
         {{"tools", json::object()}, {"resources", json::object()}, {"prompts", json::object()}}},
        {"serverInfo", {{"name", serverName}, {"version", serverVersion()}}},
    };
    if (!instructions.empty()) {
        value["instructions"] = instructions;
    }
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

    if (gate) {
        const std::string refusal = gate(name);
        if (!refusal.empty()) {
            json refused = {
                {"content", json::array({{{"type", "text"}, {"text", refusal}}})},
                {"isError", true},
            };
            return result(id, refused);
        }
    }

    const json arguments = params.contains("arguments") ? params["arguments"] : json::object();
    if (auto problem = validateJson(it->second.inputSchema, arguments)) {
        return error(id, invalidParams, name + ": " + *problem);
    }

    if (onToolStart) {
        onToolStart(name, arguments);
    }
    const auto started = std::chrono::steady_clock::now();
    auto elapsed = [&started]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    };

    json value;
    try {
        value = it->second.handler(arguments);
    }
    catch (const std::exception& e) {
        if (onToolFinish) {
            onToolFinish(name, arguments, elapsed(), false);
        }
        json failure = {
            {"content", json::array({{{"type", "text"}, {"text", e.what()}}})},
            {"isError", true},
        };
        return result(id, failure);
    }
    if (onToolFinish) {
        onToolFinish(name, arguments, elapsed(), true);
    }

    // Images travel as MCP image content, not inside the structured result.
    json images = json::array();
    if (value.is_object() && value.contains("_images")) {
        images = value["_images"];
        value.erase("_images");
    }
    json content = json::array({{{"type", "text"}, {"text", serialize(value)}}});
    for (const auto& image : images) {
        content.push_back({
            {"type", "image"},
            {"data", image.value("data", std::string())},
            {"mimeType", image.value("mimeType", std::string("image/png"))},
        });
    }
    json success = {
        {"content", content},
        {"structuredContent", value},
        {"isError", false},
    };
    return result(id, success);
}

json Protocol::listResources(const json& id) const
{
    json list = json::array();
    for (const auto& [uri, resource] : resources) {
        list.push_back({
            {"uri", uri},
            {"name", resource.name},
            {"description", resource.description},
            {"mimeType", resource.mimeType},
        });
    }
    return result(id, {{"resources", list}});
}

json Protocol::readResource(const json& id, const json& params) const
{
    if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string()) {
        return error(id, invalidParams, "resources/read needs a uri");
    }
    const std::string uri = params["uri"].get<std::string>();
    std::string text;
    std::string mimeType;
    try {
        auto it = resources.find(uri);
        if (it != resources.end()) {
            text = it->second.reader();
            mimeType = it->second.mimeType;
        }
        else {
            const ResourceTemplate* match = nullptr;
            for (const auto& candidate : templates) {
                const std::string prefix
                    = candidate.uriTemplate.substr(0, candidate.uriTemplate.find('{'));
                if (uri.size() > prefix.size() && uri.compare(0, prefix.size(), prefix) == 0) {
                    match = &candidate;
                    break;
                }
            }
            if (!match) {
                return error(id, resourceNotFound, "Resource not found: " + uri);
            }
            text = match->reader(uri);
            mimeType = match->mimeType;
        }
    }
    catch (const std::exception& e) {
        return error(id, resourceNotFound, uri + ": " + e.what());
    }
    json contents = json::array({{{"uri", uri}, {"mimeType", mimeType}, {"text", text}}});
    return result(id, {{"contents", contents}});
}

json Protocol::listResourceTemplates(const json& id) const
{
    json list = json::array();
    for (const auto& t : templates) {
        list.push_back({
            {"uriTemplate", t.uriTemplate},
            {"name", t.name},
            {"description", t.description},
            {"mimeType", t.mimeType},
        });
    }
    return result(id, {{"resourceTemplates", list}});
}

json Protocol::listPrompts(const json& id) const
{
    json list = json::array();
    for (const auto& [name, prompt] : prompts) {
        json arguments = json::array();
        for (const auto& arg : prompt.arguments) {
            arguments.push_back(
                {{"name", arg.name}, {"description", arg.description}, {"required", arg.required}}
            );
        }
        list.push_back({{"name", name}, {"description", prompt.description}, {"arguments", arguments}});
    }
    return result(id, {{"prompts", list}});
}

json Protocol::getPrompt(const json& id, const json& params) const
{
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
        return error(id, invalidParams, "prompts/get needs a prompt name");
    }
    const std::string name = params["name"].get<std::string>();
    auto it = prompts.find(name);
    if (it == prompts.end()) {
        return error(id, invalidParams, "Unknown prompt: " + name);
    }
    const json arguments = params.contains("arguments") ? params["arguments"] : json::object();
    for (const auto& arg : it->second.arguments) {
        if (arg.required && !arguments.contains(arg.name)) {
            return error(id, invalidParams, name + ": missing required argument '" + arg.name + "'");
        }
    }
    std::string text;
    try {
        text = it->second.render(arguments);
    }
    catch (const std::exception& e) {
        return error(id, invalidParams, name + ": " + e.what());
    }
    json messages = json::array({
        {{"role", "user"}, {"content", {{"type", "text"}, {"text", text}}}},
    });
    return result(id, {{"description", it->second.description}, {"messages", messages}});
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
