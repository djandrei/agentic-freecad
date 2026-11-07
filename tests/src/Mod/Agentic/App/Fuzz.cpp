// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/App/Protocol.h>

using json = nlohmann::json;

// The protocol must answer anything a client can send with one well-formed
// JSON-RPC response (or nothing, for a notification), and never throw. The
// tools here are stand-ins: fuzzed arguments must not reach real tools that
// write files or open projects.
class FuzzTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        protocol.addTool({
            "typed",
            "a tool with a demanding schema",
            json(
                {{"type", "object"},
                 {"properties",
                  {{"n", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10}}},
                   {"s", {{"type", "string"}, {"enum", {"a", "b"}}}},
                   {"v",
                    {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                   {"o",
                    {{"type", "object"}, {"properties", {{"x", {{"type", {"string", "null"}}}}}}}}}},
                 {"required", {"n"}},
                 {"additionalProperties", false}}
            ),
            [](const json& args) -> json { return {{"n", args["n"]}}; },
        });
        protocol.addTool({
            "throws",
            "fails",
            json({{"type", "object"}}),
            [](const json&) -> json { throw std::runtime_error("boom"); },
        });
        protocol.addResourceTemplate({
            "fuzz://item/{id}",
            "items",
            "",
            "text/plain",
            [](const std::string& uri) -> std::string {
                if (uri.size() % 2) {
                    throw std::runtime_error("odd");
                }
                return uri;
            },
        });
    }

    void expectWellFormed(const std::string& line)
    {
        std::string response;
        ASSERT_NO_THROW(response = protocol.handleLine(line)) << line.substr(0, 200);
        if (response.empty()) {
            return;  // a notification
        }
        json parsed = json::parse(response, nullptr, false);
        ASSERT_FALSE(parsed.is_discarded()) << response.substr(0, 200);
        ASSERT_TRUE(parsed.is_object());
        EXPECT_EQ(parsed["jsonrpc"], "2.0");
        EXPECT_TRUE(parsed.contains("id"));
        EXPECT_NE(parsed.contains("result"), parsed.contains("error")) << response.substr(0, 200);
        if (parsed.contains("error")) {
            EXPECT_TRUE(parsed["error"]["code"].is_number_integer());
            EXPECT_TRUE(parsed["error"]["message"].is_string());
        }
    }

    json randomValue(int depth)
    {
        switch (pick(depth > 3 ? 6 : 8)) {
            case 0:
                return nullptr;
            case 1:
                return pick(2) == 1;
            case 2:
                return static_cast<long long>(rng()) - (1LL << 31);
            case 3:
                return std::uniform_real_distribution<double>(-1e9, 1e9)(rng);
            case 4:
                return randomString(pick(40));
            case 5:
                return std::string("a");
            case 6: {
                json array = json::array();
                for (int i = pick(5); i > 0; --i) {
                    array.push_back(randomValue(depth + 1));
                }
                return array;
            }
            default: {
                json object = json::object();
                const char* keys[] = {"n", "s", "v", "o", "x", "name", "arguments", "uri", "id", ""};
                for (int i = pick(5); i > 0; --i) {
                    object[keys[pick(10)]] = randomValue(depth + 1);
                }
                return object;
            }
        }
    }

    std::string randomString(int length)
    {
        std::string out;
        for (int i = 0; i < length; ++i) {
            out.push_back(static_cast<char>(pick(256)));
        }
        return out;
    }

    int pick(int bound)
    {
        return static_cast<int>(rng() % static_cast<unsigned>(bound));
    }

    Agentic::Protocol protocol;
    std::mt19937 rng {20261107};
};

TEST_F(FuzzTest, randomBytesAndBrokenJson)
{
    const std::vector<std::string> fixed = {
        "",
        " ",
        "{",
        "}",
        "[]",
        "[{}]",
        "null",
        "1",
        "\"x\"",
        "{\"jsonrpc\":\"2.0\"}",
        "{\"method\":1,\"id\":1}",
        "{\"method\":\"tools/call\",\"id\":{}}",
        "{\"id\":null}",
        "{\"method\":\"tools/call\",\"id\":1,\"params\":[]}",
        "{\"method\":\"tools/call\",\"id\":1,\"params\":{\"name\":[]}}",
        "{\"method\":\"resources/read\",\"id\":1,\"params\":{\"uri\":5}}",
        "{\"method\":\"prompts/get\",\"id\":1,\"params\":\"x\"}",
        std::string("{\"method\":\"ping\",\"id\":1}\0garbage", 34),
    };
    for (const auto& line : fixed) {
        expectWellFormed(line);
    }
    for (int i = 0; i < 2000; ++i) {
        expectWellFormed(randomString(pick(200)));
    }
}

TEST_F(FuzzTest, randomRequestsAndArguments)
{
    const char* methods[] = {
        "initialize",
        "ping",
        "tools/list",
        "tools/call",
        "resources/list",
        "resources/read",
        "resources/templates/list",
        "prompts/list",
        "prompts/get",
        "nope",
        "",
    };
    const char* tools[] = {"typed", "throws", "missing", ""};
    for (int i = 0; i < 5000; ++i) {
        json request = {{"jsonrpc", "2.0"}, {"method", methods[pick(11)]}};
        if (pick(5) != 0) {
            request["id"] = randomValue(3);
        }
        if (pick(2) == 0) {
            json params = json::object();
            params["name"] = pick(4) == 0 ? randomValue(2) : json(tools[pick(4)]);
            params["arguments"] = randomValue(1);
            params["uri"] = pick(2) ? json("fuzz://item/" + std::to_string(pick(100)))
                                    : randomValue(2);
            request["params"] = params;
        }
        else if (pick(2) == 0) {
            request["params"] = randomValue(1);
        }
        expectWellFormed(request.dump(-1, ' ', false, json::error_handler_t::replace));
    }
}

TEST_F(FuzzTest, deepAndLargeInputs)
{
    std::string deep(20000, '[');
    expectWellFormed(deep);
    json nested = json::object();
    json* cursor = &nested;
    for (int i = 0; i < 500; ++i) {
        (*cursor)["o"] = json::object();
        cursor = &(*cursor)["o"];
    }
    expectWellFormed(json(
                         {{"jsonrpc", "2.0"},
                          {"id", 1},
                          {"method", "tools/call"},
                          {"params", {{"name", "typed"}, {"arguments", nested}}}}
    ).dump());
    const std::string big(4 * 1024 * 1024, 'x');
    expectWellFormed(json(
                         {{"jsonrpc", "2.0"},
                          {"id", 2},
                          {"method", "tools/call"},
                          {"params", {{"name", "typed"}, {"arguments", {{"n", 1}, {"s", big}}}}}}
    ).dump());
}

TEST_F(FuzzTest, invalidUtf8InResultsIsReplacedNotThrown)
{
    protocol.addTool({
        "latin1",
        "returns bytes that are not UTF-8",
        json({{"type", "object"}}),
        [](const json&) -> json { return {{"text", std::string("caf\xe9 \xff")}}; },
    });
    std::string response;
    ASSERT_NO_THROW(
        response = protocol.handleLine(
            R"({"jsonrpc":"2.0","id":30,"method":"tools/call","params":{"name":"latin1"}})"
        )
    );
    json parsed = json::parse(response);
    EXPECT_FALSE(parsed["result"]["isError"].get<bool>());
    EXPECT_EQ(parsed["result"]["structuredContent"]["text"].get<std::string>().substr(0, 3), "caf");
}
