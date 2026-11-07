// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/App/Schema.h>

using json = nlohmann::json;
using Agentic::validateJson;

namespace
{

const json boxSchema = {
    {"type", "object"},
    {"properties",
     {
         {"width", {{"type", "number"}, {"minimum", 0}}},
         {"count", {{"type", "integer"}}},
         {"name", {{"type", "string"}, {"minLength", 1}}},
         {"mode", {{"type", "string"}, {"enum", {"preview", "render"}}}},
         {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"maxItems", 2}}},
         {"nested", {{"type", "object"}, {"properties", {{"flag", {{"type", "boolean"}}}}}}},
     }},
    {"required", {"width"}},
    {"additionalProperties", false},
};

}  // namespace

TEST(SchemaTest, validObjectPasses)
{
    json value = {{"width", 2.5}, {"count", 3}, {"name", "a"}, {"mode", "render"}, {"tags", {"x"}}};
    EXPECT_FALSE(validateJson(boxSchema, value).has_value());
}

TEST(SchemaTest, missingRequiredIsReported)
{
    auto err = validateJson(boxSchema, json::object());
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("missing required property 'width'"), std::string::npos);
}

TEST(SchemaTest, wrongTypeNamesPathAndTypes)
{
    auto err = validateJson(boxSchema, {{"width", "wide"}});
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "arguments.width: expected number, got string");
}

TEST(SchemaTest, integerRejectsFloat)
{
    auto err = validateJson(boxSchema, {{"width", 1}, {"count", 1.5}});
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("arguments.count"), std::string::npos);
}

TEST(SchemaTest, integerIsANumber)
{
    EXPECT_FALSE(validateJson(boxSchema, {{"width", 1}}).has_value());
}

TEST(SchemaTest, unknownPropertyRejected)
{
    auto err = validateJson(boxSchema, {{"width", 1}, {"depth", 2}});
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("unknown property 'depth'"), std::string::npos);
}

TEST(SchemaTest, enumAndBoundsChecked)
{
    EXPECT_TRUE(validateJson(boxSchema, {{"width", 1}, {"mode", "wire"}}).has_value());
    EXPECT_TRUE(validateJson(boxSchema, {{"width", -1}}).has_value());
    EXPECT_TRUE(validateJson(boxSchema, {{"width", 1}, {"name", ""}}).has_value());
}

TEST(SchemaTest, arrayItemsAndNestedObjectsChecked)
{
    auto err = validateJson(boxSchema, {{"width", 1}, {"tags", {"a", 2}}});
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "arguments.tags[1]: expected string, got integer");
    EXPECT_TRUE(validateJson(boxSchema, {{"width", 1}, {"tags", {"a", "b", "c"}}}).has_value());
    err = validateJson(boxSchema, {{"width", 1}, {"nested", {{"flag", "yes"}}}});
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "arguments.nested.flag: expected boolean, got string");
}

TEST(SchemaTest, typeListAcceptsAny)
{
    json schema = {{"type", {"string", "null"}}};
    EXPECT_FALSE(validateJson(schema, nullptr).has_value());
    EXPECT_FALSE(validateJson(schema, "x").has_value());
    auto err = validateJson(schema, 1);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "arguments: expected string or null, got integer");
}
