// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// JSON-RPC 2.0 handling for the Model Context Protocol.
///
/// The protocol object is transport-neutral: a transport hands it one request
/// (as a line of JSON or an already parsed value) and gets the response back,
/// or an empty result when the request was a notification. Tools, resources
/// and prompts are registered by name. Tool arguments are validated against
/// the tool's input schema before the handler runs; a handler returns the
/// structured result or throws to report a tool error.
class AgenticExport Protocol
{
public:
    using ToolHandler = std::function<nlohmann::json(const nlohmann::json& arguments)>;
    using ResourceReader = std::function<std::string()>;
    using PromptRenderer = std::function<std::string(const nlohmann::json& arguments)>;

    struct Tool
    {
        std::string name;
        std::string description;
        nlohmann::json inputSchema;
        ToolHandler handler;
    };

    struct Resource
    {
        std::string uri;
        std::string name;
        std::string description;
        std::string mimeType;
        ResourceReader reader;
    };

    struct PromptArgument
    {
        std::string name;
        std::string description;
        bool required {false};
    };

    struct Prompt
    {
        std::string name;
        std::string description;
        std::vector<PromptArgument> arguments;
        PromptRenderer render;
    };

    static constexpr const char* protocolVersion = "2025-06-18";
    static constexpr const char* serverName = "agentic-freecad";

    Protocol();

    void addTool(Tool tool);
    void addResource(Resource resource);
    void addPrompt(Prompt prompt);
    bool hasTool(const std::string& name) const;
    const std::map<std::string, Tool>& toolList() const;

    void setInstructions(std::string text);

    /// Handles one line of input. Returns the serialized response, or an empty
    /// string when no response is due (notification).
    std::string handleLine(const std::string& line);

    /// Handles one parsed request. Returns the response, or a discarded value
    /// when no response is due.
    nlohmann::json handleRequest(const nlohmann::json& request);

    static std::string serverVersion();

private:
    nlohmann::json initialize(const nlohmann::json& id) const;
    nlohmann::json listTools(const nlohmann::json& id) const;
    nlohmann::json callTool(const nlohmann::json& id, const nlohmann::json& params) const;
    nlohmann::json listResources(const nlohmann::json& id) const;
    nlohmann::json readResource(const nlohmann::json& id, const nlohmann::json& params) const;
    nlohmann::json listPrompts(const nlohmann::json& id) const;
    nlohmann::json getPrompt(const nlohmann::json& id, const nlohmann::json& params) const;

    static nlohmann::json result(const nlohmann::json& id, nlohmann::json value);
    static nlohmann::json error(const nlohmann::json& id, int code, const std::string& message);

    std::map<std::string, Tool> tools;
    std::map<std::string, Resource> resources;
    std::map<std::string, Prompt> prompts;
    std::string instructions;
};

}  // namespace Agentic
