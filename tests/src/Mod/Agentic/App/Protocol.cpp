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
    json response = call(R"({"jsonrpc":"2.0","id":6,"method":"completion/complete"})");
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

TEST_F(ProtocolTest, initializeAnnouncesResourcesAndPrompts)
{
    json response = call(R"({"jsonrpc":"2.0","id":10,"method":"initialize","params":{}})");
    EXPECT_TRUE(response["result"]["capabilities"].contains("resources"));
    EXPECT_TRUE(response["result"]["capabilities"].contains("prompts"));
    EXPECT_FALSE(response["result"]["instructions"].get<std::string>().empty());
}

TEST_F(ProtocolTest, statusResourceReadsAsJson)
{
    json list = call(R"({"jsonrpc":"2.0","id":11,"method":"resources/list"})");
    bool found = false;
    for (const auto& res : list["result"]["resources"]) {
        found = found || res["uri"] == "agentic://status";
    }
    EXPECT_TRUE(found);
    json read = call(
        R"({"jsonrpc":"2.0","id":12,"method":"resources/read","params":{"uri":"agentic://status"}})"
    );
    const json& contents = read["result"]["contents"][0];
    EXPECT_EQ(contents["mimeType"], "application/json");
    json status = json::parse(contents["text"].get<std::string>());
    EXPECT_FALSE(status["agentMode"].get<bool>());
}

TEST_F(ProtocolTest, unknownResourceIsNotFound)
{
    json read = call(
        R"({"jsonrpc":"2.0","id":13,"method":"resources/read","params":{"uri":"agentic://nope"}})"
    );
    EXPECT_EQ(read["error"]["code"], -32002);
}

TEST_F(ProtocolTest, loopPromptIsListedAndRendered)
{
    json list = call(R"({"jsonrpc":"2.0","id":14,"method":"prompts/list"})");
    EXPECT_EQ(list["result"]["prompts"][0]["name"], "agentic-loop");
    json get = call(
        R"({"jsonrpc":"2.0","id":15,"method":"prompts/get","params":{"name":"agentic-loop"}})"
    );
    EXPECT_EQ(get["result"]["messages"][0]["role"], "user");
    EXPECT_FALSE(get["result"]["messages"][0]["content"]["text"].get<std::string>().empty());
}

TEST_F(ProtocolTest, invalidArgumentsAreRejectedBeforeTheHandler)
{
    Agentic::Protocol protocol;
    bool called = false;
    protocol.addTool({
        "sized",
        "needs a size",
        json(
            {{"type", "object"},
             {"properties", {{"size", {{"type", "integer"}}}}},
             {"required", {"size"}},
             {"additionalProperties", false}}
        ),
        [&called](const json&) -> json {
            called = true;
            return json::object();
        },
    });
    json response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"sized","arguments":{"size":"big"}}})"
    ));
    EXPECT_EQ(response["error"]["code"], -32602);
    EXPECT_NE(response["error"]["message"].get<std::string>().find("arguments.size"), std::string::npos);
    EXPECT_FALSE(called);
    response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":17,"method":"tools/call","params":{"name":"sized","arguments":{"size":3}}})"
    ));
    EXPECT_TRUE(called);
    EXPECT_FALSE(response["result"]["isError"].get<bool>());
}

TEST_F(ProtocolTest, promptMissingRequiredArgumentIsInvalidParams)
{
    Agentic::Protocol protocol;
    protocol.addPrompt({
        "greet",
        "greets",
        {{"who", "the name", true}},
        [](const json& args) { return "hello " + args["who"].get<std::string>(); },
    });
    json response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":18,"method":"prompts/get","params":{"name":"greet"}})"
    ));
    EXPECT_EQ(response["error"]["code"], -32602);
    response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":19,"method":"prompts/get","params":{"name":"greet","arguments":{"who":"box"}}})"
    ));
    EXPECT_EQ(response["result"]["messages"][0]["content"]["text"], "hello box");
}

TEST_F(ProtocolTest, imagesBecomeImageContent)
{
    Agentic::Protocol protocol;
    protocol.addTool({
        "pic",
        "returns an image",
        json({{"type", "object"}}),
        [](const json&) -> json {
            return {
                {"width", 2},
                {"_images", json::array({{{"data", "iVBORw0KGgo="}, {"mimeType", "image/png"}}})}
            };
        },
    });
    json response = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"pic"}})"
    ));
    const json& result = response["result"];
    ASSERT_EQ(result["content"].size(), 2U);
    EXPECT_EQ(result["content"][1]["type"], "image");
    EXPECT_EQ(result["content"][1]["data"], "iVBORw0KGgo=");
    EXPECT_EQ(result["content"][1]["mimeType"], "image/png");
    EXPECT_FALSE(result["structuredContent"].contains("_images"));
    EXPECT_EQ(result["structuredContent"]["width"], 2);
}

TEST_F(ProtocolTest, resourceTemplatesAreListedAndRead)
{
    Agentic::Protocol protocol;
    protocol.addResourceTemplate({
        "test://page/{name}",
        "Pages",
        "a page per name",
        "text/markdown",
        [](const std::string& uri) -> std::string {
            if (uri == "test://page/missing") {
                throw std::runtime_error("no such page");
            }
            return "# " + uri.substr(std::string("test://page/").size());
        },
    });
    json list = json::parse(
        protocol.handleLine(R"({"jsonrpc":"2.0","id":21,"method":"resources/templates/list"})")
    );
    EXPECT_EQ(list["result"]["resourceTemplates"][0]["uriTemplate"], "test://page/{name}");
    json read = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":22,"method":"resources/read","params":{"uri":"test://page/Part_Box"}})"
    ));
    EXPECT_EQ(read["result"]["contents"][0]["text"], "# Part_Box");
    EXPECT_EQ(read["result"]["contents"][0]["mimeType"], "text/markdown");
    json missing = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":23,"method":"resources/read","params":{"uri":"test://page/missing"}})"
    ));
    EXPECT_EQ(missing["error"]["code"], -32002);
    json bare = json::parse(protocol.handleLine(
        R"({"jsonrpc":"2.0","id":24,"method":"resources/read","params":{"uri":"test://page/"}})"
    ));
    EXPECT_EQ(bare["error"]["code"], -32002);
}
