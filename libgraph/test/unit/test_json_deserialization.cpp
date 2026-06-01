// MIT License
//
// Copyright (c) 2026 graphlib contributors
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

#include <array>
#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "config/Config.hpp"
#include "config/JsonDeserialization.hpp"

namespace {

using json = nlohmann::json;

struct SampleConfig {
    static constexpr auto Fields() {
        return std::array{
            graph::JsonField{
                .name = "host",
                .type = graph::JsonType::String,
                .required = true,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = std::nullopt,
                .enum_values = std::nullopt,
                .description = "Host name"
            },
            graph::JsonField{
                .name = "port",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = std::string_view{"8080"},
                .enum_values = std::nullopt,
                .description = "Port number"
            }
        };
    }
};

}  // namespace

TEST(JsonDeserializationTest, DeserializeFromStringRejectsMissingRequiredFields) {
    const auto result = app::json::DeserializeFromString<SampleConfig>(R"({"port": 8080})");

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), app::json::DeserializationError::MissingRequiredField);
}

TEST(JsonDeserializationTest, DeserializeFromStringAcceptsRequiredFields) {
    const auto result = app::json::DeserializeFromString<SampleConfig>(R"({"host": "localhost"})");

    EXPECT_TRUE(result.has_value());
}

TEST(JsonDeserializationTest, DeserializeAndValidateRejectsUnexpectedFields) {
    const json input = json::parse(R"({"host": "localhost", "extra": true})");

    const auto result = app::json::DeserializeAndValidate<SampleConfig>(input);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), app::json::DeserializationError::UnexpectedField);
}

TEST(JsonDeserializationTest, DeserializeAndValidateAcceptsKnownFields) {
    const json input = json::parse(R"({"host": "localhost", "port": 9000})");

    const auto result = app::json::DeserializeAndValidate<SampleConfig>(input);

    EXPECT_TRUE(result.has_value());
}

TEST(JsonDeserializationTest, DeserializePrimitiveHandlesCommonTypes) {
    const auto int_result = app::json::DeserializePrimitive<int>(json(42));
    const auto double_result = app::json::DeserializePrimitive<double>(json(3.5));
    const auto bool_result = app::json::DeserializePrimitive<bool>(json(true));
    const auto string_result = app::json::DeserializePrimitive<std::string>(json("hello"));

    ASSERT_TRUE(int_result.has_value());
    ASSERT_TRUE(double_result.has_value());
    ASSERT_TRUE(bool_result.has_value());
    ASSERT_TRUE(string_result.has_value());

    EXPECT_EQ(int_result.value(), 42);
    EXPECT_DOUBLE_EQ(double_result.value(), 3.5);
    EXPECT_TRUE(bool_result.value());
    EXPECT_EQ(string_result.value(), "hello");
}

TEST(JsonDeserializationTest, DeserializeWithDetailsIncludesUnexpectedFieldName) {
    const json input = json::parse(R"({"host": "localhost", "extra": true})");

    const auto result = app::json::DeserializeWithDetails<SampleConfig>(input);

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.value.error(), app::json::DeserializationError::UnexpectedField);
    EXPECT_TRUE(std::any_of(result.error_details.begin(),
                            result.error_details.end(),
                            [](const std::string& detail) {
                                return detail.find("Unexpected field: extra") != std::string::npos;
                            }));
}

TEST(JsonDeserializationTest, DeserializeWithDetailsIncludesMissingFieldContext) {
    const json input = json::parse(R"({"port": 8080})");

    const auto result = app::json::DeserializeWithDetails<SampleConfig>(input);

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.value.error(), app::json::DeserializationError::MissingRequiredField);
    EXPECT_TRUE(std::any_of(result.error_details.begin(),
                            result.error_details.end(),
                            [](const std::string& detail) {
                                return detail.find("Missing required field: host") != std::string::npos;
                            }));
}

TEST(JsonDeserializationTest, DeserializeWithDetailsReportsTopLevelType) {
    const json input = json::parse(R"([{"host": "localhost"}])");

    const auto result = app::json::DeserializeWithDetails<SampleConfig>(input);

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.value.error(), app::json::DeserializationError::ConstraintViolation);
    EXPECT_TRUE(std::any_of(result.error_details.begin(),
                            result.error_details.end(),
                            [](const std::string& detail) {
                                return detail.find("Expected JSON object, got: array") != std::string::npos;
                            }));
}
