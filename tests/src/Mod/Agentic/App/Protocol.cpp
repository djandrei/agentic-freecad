// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/App/Protocol.h>
#include <Mod/Agentic/App/Runtime.h>

using json = nlohmann::json;

class ProtocolTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    static json call(const std::string& line)
    {
        const std::string response = Agentic::Runtime::instance().protocol().handleLine(line);
        return response.empty() ? json() : json::parse(response);
    }
};

TEST_F(ProtocolTest, initializeAnnouncesServer)
{
    json response = call(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    EXPECT_EQ(response["id"], 1);
    EXPECT_EQ(response["result"]["protocolVersion"], Agentic::Protocol::protocolVersion);
    EXPECT_EQ(response["result"]["serverInfo"]["name"], "agentic-freecad");
    EXPECT_TRUE(response["result"]["capabilities"].contains("tools"));
}

TEST_F(ProtocolTest, toolsListHasCoreTools)
{
    json response = call(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    std::vector<std::string> names;
    for (const auto& tool : response["result"]["tools"]) {
        names.push_back(tool["name"].get<std::string>());
        EXPECT_TRUE(tool.contains("inputSchema"));
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "version"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "status"), names.end());
}

TEST_F(ProtocolTest, versionToolReturnsStructuredContent)
{
    json response = call(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"version","arguments":{}}})"
    );
    const json& result = response["result"];
    EXPECT_FALSE(result["isError"].get<bool>());
    EXPECT_EQ(result["content"][0]["type"], "text");
    EXPECT_EQ(result["structuredContent"]["server"], "agentic-freecad");
    EXPECT_EQ(result["structuredContent"]["version"], Agentic::Protocol::serverVersion());
}

TEST_F(ProtocolTest, statusToolReportsNoAgentMode)
{
    json response = call(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"status"}})"
    );
    const json& status = response["result"]["structuredContent"];
    EXPECT_FALSE(status["agentMode"].get<bool>());
    EXPECT_EQ(status["transport"], "none");
}

TEST_F(ProtocolTest, unknownToolIsInvalidParams)
{
    json response = call(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"nope"}})");
    EXPECT_EQ(response["error"]["code"], -32602);
}

TEST_F(ProtocolTest, unknownMethodIsMethodNotFound)
{
    json response = call(R"({"jsonrpc":"2.0","id":6,"method":"resources/list"})");
    EXPECT_EQ(response["error"]["code"], -32601);
}

TEST_F(ProtocolTest, parseErrorHasNullId)
{
    json response = call("{not json");
    EXPECT_EQ(response["error"]["code"], -32700);
    EXPECT_TRUE(response["id"].is_null());
}

TEST_F(ProtocolTest, notificationGetsNoResponse)
{
    const std::string response = Agentic::Runtime::instance().protocol().handleLine(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
    );
    EXPECT_TRUE(response.empty());
}

TEST_F(ProtocolTest, toolExceptionBecomesToolError)
{
    Agentic::Protocol protocol;
    protocol.addTool({
        "boom",
        "throws",
        json({{"type", "object"}}),
        [](const json&) -> json { throw std::runtime_error("kaboom"); },
    });
    json response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"boom"}})"
    ));
    EXPECT_TRUE(response["result"]["isError"].get<bool>());
    EXPECT_EQ(response["result"]["content"][0]["text"], "kaboom");
}
