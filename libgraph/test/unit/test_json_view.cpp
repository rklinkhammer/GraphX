/**
 * @file test_json_view.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <gtest/gtest.h>

#include "config/ConfigError.hpp"
#include "config/JsonView.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

/**
 * @class JsonViewBasicTest
 * @brief Json view basic test implementation for GraphX.
 */
class JsonViewBasicTest : public ::testing::Test {
protected:
    json empty_object = json::object();
    json simple_object = json::parse(R"({
        "name": "test",
        "count": 42,
        "ratio": 3.14,
        "enabled": true
    })");
    json backing_storage;

/**
 * @brief Make view.
 * @param text Parameter for make view.
 */
    graph::JsonView MakeView(const std::string& text) {
        backing_storage = json::parse(text);
        return graph::JsonView(backing_storage);
    }
};

}  // namespace

TEST_F(JsonViewBasicTest, Constructor_EmptyObject) {
    graph::JsonView view(empty_object);
    EXPECT_TRUE(view.Raw().is_object());
    EXPECT_TRUE(view.Raw().empty());
}

TEST_F(JsonViewBasicTest, Constructor_PopulatedObject) {
    graph::JsonView view(simple_object);
    EXPECT_TRUE(view.Raw().is_object());
    EXPECT_FALSE(view.Raw().empty());
    EXPECT_EQ(view.Raw().size(), 4);
}

TEST_F(JsonViewBasicTest, Contains_ExistingField) {
    graph::JsonView view(simple_object);
    EXPECT_TRUE(view.Contains("name"));
    EXPECT_TRUE(view.Contains("count"));
}

TEST_F(JsonViewBasicTest, Contains_MissingField) {
    graph::JsonView view(simple_object);
    EXPECT_FALSE(view.Contains("nonexistent"));
}

TEST_F(JsonViewBasicTest, Contains_NullField) {
    auto view = MakeView(R"({"field": null})");
    EXPECT_FALSE(view.Contains("field"));
}

TEST_F(JsonViewBasicTest, Contains_FalsyButNotNull) {
    auto view = MakeView(R"({"zero": 0, "false": false, "empty": ""})");
    EXPECT_TRUE(view.Contains("zero"));
    EXPECT_TRUE(view.Contains("false"));
    EXPECT_TRUE(view.Contains("empty"));
}

TEST_F(JsonViewBasicTest, TryGetString_ValidValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetString("name");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), "test");
}

TEST_F(JsonViewBasicTest, TryGetString_DefaultValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetString("missing", "default_value");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), "default_value");
}

TEST_F(JsonViewBasicTest, TryGetString_TypeMismatch) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetString("count");
    EXPECT_FALSE(result);
    EXPECT_TRUE(result.error().what() != nullptr);
}

TEST_F(JsonViewBasicTest, TryGetFloat_ValidFloat) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetFloat("ratio");
    ASSERT_TRUE(result);
    EXPECT_FLOAT_EQ(result.value(), 3.14f);
}

TEST_F(JsonViewBasicTest, TryGetFloat_IntegerValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetFloat("count");
    ASSERT_TRUE(result);
    EXPECT_FLOAT_EQ(result.value(), 42.0f);
}

TEST_F(JsonViewBasicTest, TryGetFloat_DefaultValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetFloat("missing", 99.5f);
    ASSERT_TRUE(result);
    EXPECT_FLOAT_EQ(result.value(), 99.5f);
}

TEST_F(JsonViewBasicTest, TryGetFloat_MissingRequiredField) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetFloat("missing");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetFloat_TypeMismatch) {
    auto view = MakeView(R"({"field": "3.14"})");
    auto result = view.TryGetFloat("field");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetInt_ValidInteger) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetInt("count");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 42);
}

TEST_F(JsonViewBasicTest, TryGetInt_DefaultValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetInt("missing", 99);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 99);
}

TEST_F(JsonViewBasicTest, TryGetInt_RejectsFloat) {
    auto view = MakeView(R"({"field": 3.14})");
    auto result = view.TryGetInt("field");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetBool_TrueValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetBool("enabled");
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value());
}

TEST_F(JsonViewBasicTest, TryGetBool_DefaultValue) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetBool("missing", true);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value());
}

TEST_F(JsonViewBasicTest, TryGetBool_TypeMismatch) {
    auto view = MakeView(R"({"field": 1})");
    auto result = view.TryGetBool("field");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetObject_ValidNestedObject) {
    auto view = MakeView(R"({
        "config": {
            "name": "nested",
            "value": 123
        }
    })");
    auto result = view.TryGetObject("config");
    ASSERT_TRUE(result);
    auto name = result.value().TryGetString("name");
    auto value = result.value().TryGetInt("value");
    ASSERT_TRUE(name);
    ASSERT_TRUE(value);
    EXPECT_EQ(name.value(), "nested");
    EXPECT_EQ(value.value(), 123);
}

TEST_F(JsonViewBasicTest, TryGetObject_Missing) {
    graph::JsonView view(simple_object);
    auto result = view.TryGetObject("missing");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetStringArray_ValidArray) {
    auto view = MakeView(R"({"names": ["alice", "bob", "charlie"]})");
    auto result = view.TryGetStringArray("names");
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 3u);
    EXPECT_EQ(result.value()[0], "alice");
    EXPECT_EQ(result.value()[1], "bob");
    EXPECT_EQ(result.value()[2], "charlie");
}

TEST_F(JsonViewBasicTest, TryGetStringArray_TypeMismatch) {
    auto view = MakeView(R"({"field": "not_array"})");
    auto result = view.TryGetStringArray("field");
    EXPECT_FALSE(result);
}

TEST_F(JsonViewBasicTest, TryGetArray_ArrayOfObjects) {
    auto view = MakeView(R"({
        "items": [
            {"id": 1, "name": "first"},
            {"id": 2, "name": "second"}
        ]
    })");
    auto result = view.TryGetArray("items");
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 2u);
    auto first_id = result.value()[0].TryGetInt("id");
    auto second_id = result.value()[1].TryGetInt("id");
    ASSERT_TRUE(first_id);
    ASSERT_TRUE(second_id);
    EXPECT_EQ(first_id.value(), 1);
    EXPECT_EQ(second_id.value(), 2);
}

TEST_F(JsonViewBasicTest, TryGetArray_Chaining) {
    auto view = MakeView(R"({
        "configs": [
            {"name": "config1", "enabled": true},
            {"name": "config2", "enabled": false}
        ]
    })");
    auto result = view.TryGetArray("configs");
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().size(), 2u);
    auto first_name = result.value()[0].TryGetString("name");
    auto first_enabled = result.value()[0].TryGetBool("enabled");
    auto second_name = result.value()[1].TryGetString("name");
    auto second_enabled = result.value()[1].TryGetBool("enabled");
    ASSERT_TRUE(first_name);
    ASSERT_TRUE(first_enabled);
    ASSERT_TRUE(second_name);
    ASSERT_TRUE(second_enabled);
    EXPECT_EQ(first_name.value(), "config1");
    EXPECT_TRUE(first_enabled.value());
    EXPECT_EQ(second_name.value(), "config2");
    EXPECT_FALSE(second_enabled.value());
}

TEST_F(JsonViewBasicTest, GetOptionalString_ExistingField) {
    graph::JsonView view(simple_object);
    auto result = view.GetOptionalString("name");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), "test");
}

TEST_F(JsonViewBasicTest, GetOptionalString_MissingField) {
    graph::JsonView view(simple_object);
    EXPECT_FALSE(view.GetOptionalString("missing"));
}

TEST_F(JsonViewBasicTest, GetOptionalFloat_ExistingField) {
    graph::JsonView view(simple_object);
    auto result = view.GetOptionalFloat("ratio");
    ASSERT_TRUE(result);
    EXPECT_FLOAT_EQ(result.value(), 3.14f);
}

TEST_F(JsonViewBasicTest, GetOptionalInt_ExistingField) {
    graph::JsonView view(simple_object);
    auto result = view.GetOptionalInt("count");
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 42);
}

TEST_F(JsonViewBasicTest, GetOptionalBool_ExistingField) {
    graph::JsonView view(simple_object);
    auto result = view.GetOptionalBool("enabled");
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value());
}

TEST_F(JsonViewBasicTest, GetOptionalObject_ExistingField) {
    auto view = MakeView(R"({
        "config": {"name": "test"}
    })");
    auto result = view.GetOptionalObject("config");
    ASSERT_TRUE(result);
    auto name = result->TryGetString("name");
    ASSERT_TRUE(name);
    EXPECT_EQ(name.value(), "test");
}

TEST_F(JsonViewBasicTest, GetOptionalObject_MissingField) {
    graph::JsonView view(simple_object);
    EXPECT_FALSE(view.GetOptionalObject("missing"));
}

TEST_F(JsonViewBasicTest, ExpectedBasedErrorHandling_ChainedOperations) {
    auto view = MakeView(R"({
        "port": 8080,
        "timeout": 30.5,
        "name": "server"
    })");

    auto port_result = view.TryGetInt("port");
    auto timeout_result = view.TryGetFloat("timeout");
    auto name_result = view.TryGetString("name");

    ASSERT_TRUE(port_result);
    ASSERT_TRUE(timeout_result);
    ASSERT_TRUE(name_result);

    EXPECT_EQ(port_result.value(), 8080);
    EXPECT_FLOAT_EQ(timeout_result.value(), 30.5f);
    EXPECT_EQ(name_result.value(), "server");
}

TEST_F(JsonViewBasicTest, ExpectedWithErrorContextHandling) {
    auto view = MakeView(R"({"age": "not_a_number"})");
    auto result = view.TryGetInt("age");

    ASSERT_FALSE(result);
    std::string error_msg = result.error().what();
    EXPECT_TRUE(error_msg.find("age") != std::string::npos);
    EXPECT_TRUE(error_msg.find("integer") != std::string::npos);
}

TEST_F(JsonViewBasicTest, OptionalChaining_NestedObjects) {
    auto view = MakeView(R"({
        "database": {
            "host": "localhost",
            "port": 5432
        }
    })");

    auto db = view.GetOptionalObject("database");
    ASSERT_TRUE(db);
    auto host = db->GetOptionalString("host");
    auto port = db->GetOptionalInt("port");
    ASSERT_TRUE(host);
    ASSERT_TRUE(port);
    EXPECT_EQ(host.value(), "localhost");
    EXPECT_EQ(port.value(), 5432);
}

TEST_F(JsonViewBasicTest, ExpectedAndOptionalComparison) {
    auto view = MakeView(R"({
        "exists": "value",
        "missing_field": null
    })");

    auto expected_result = view.TryGetString("exists");
    auto optional_result = view.GetOptionalString("exists");

    ASSERT_TRUE(expected_result);
    ASSERT_TRUE(optional_result);
    EXPECT_EQ(expected_result.value(), optional_result.value());

    auto expected_missing = view.TryGetString("nonexistent", "default");
    auto optional_missing = view.GetOptionalString("nonexistent");

    ASSERT_TRUE(expected_missing);
    EXPECT_EQ(expected_missing.value(), "default");
    EXPECT_FALSE(optional_missing);
}
