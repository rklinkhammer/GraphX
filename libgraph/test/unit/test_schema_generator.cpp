/**
 * @file test_schema_generator.cpp
 * @brief GraphX source file.
 */

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
#include <string_view>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "config/Config.hpp"
#include "config/SchemaGenerator.hpp"

namespace {

using json = nlohmann::json;

struct SampleConfig {
/**
 * @brief Fields.
 */
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

inline constexpr std::array<std::string_view, 2> kModes{"auto", "manual"};

struct ConstraintConfig {
/**
 * @brief Fields.
 */
    static constexpr auto Fields() {
        return std::array{
            graph::JsonField{
                .name = "mode",
                .type = graph::JsonType::String,
                .required = true,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = std::string_view{"auto"},
                .enum_values = std::span<const std::string_view>(kModes),
                .description = "Processing mode"
            },
            graph::JsonField{
                .name = "gain",
                .type = graph::JsonType::Number,
                .required = true,
                .min = 0.0,
                .max = 1.0,
                .default_value = std::nullopt,
                .enum_values = std::nullopt,
                .description = "Normalized gain"
            },
            graph::JsonField{
                .name = "count",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = 10.0,
                .default_value = std::string_view{"5"},
                .enum_values = std::nullopt,
                .description = "Iteration count"
            }
        };
    }
};

}  // namespace

TEST(SchemaGeneratorTest, GeneratesSchemaFromJsonFieldDescriptors) {
    const auto schema = graph::GenerateSchemaFromType<SampleConfig>();

    EXPECT_NE(schema.title.find("SampleConfig"), std::string_view::npos);
    EXPECT_EQ(schema.fields.size(), 2u);
    EXPECT_EQ(schema.fields[0].field_name, "host");
    EXPECT_EQ(schema.fields[0].field_type, "string");
    EXPECT_TRUE(schema.fields[0].is_required);
    EXPECT_EQ(schema.fields[1].field_name, "port");
    EXPECT_EQ(schema.fields[1].field_type, "integer");
    EXPECT_FALSE(schema.fields[1].is_required);
    ASSERT_TRUE(schema.fields[1].default_value.has_value());
    EXPECT_EQ(schema.fields[1].default_value->get<int>(), 8080);
}

TEST(SchemaGeneratorTest, ValidatorRecognizesRequiredFields) {
    graph::SchemaValidator<SampleConfig> validator;

    EXPECT_TRUE(validator.HasField("host"));
    EXPECT_TRUE(validator.HasField("port"));
    EXPECT_FALSE(validator.HasField("missing"));

    json valid_input = json{{"host", "localhost"}};
    EXPECT_TRUE(validator.Validate(valid_input));
    EXPECT_TRUE(validator.GetErrors().empty());

    json invalid_input = json{{"port", 8080}};
    EXPECT_FALSE(validator.Validate(invalid_input));
    ASSERT_FALSE(validator.GetErrors().empty());
    EXPECT_NE(validator.GetErrors().front().find("host"), std::string::npos);
}

TEST(SchemaGeneratorTest, ValidatorAppliesNumericRangeConstraints) {
    graph::SchemaValidator<ConstraintConfig> validator;

    json valid_input = json{{"mode", "auto"}, {"gain", 0.25}, {"count", 3}};
    EXPECT_TRUE(validator.Validate(valid_input));

    json invalid_input = json{{"mode", "manual"}, {"gain", 1.5}};
    EXPECT_FALSE(validator.Validate(invalid_input));
    ASSERT_FALSE(validator.GetErrors().empty());
    EXPECT_NE(validator.GetErrors().front().find("gain"), std::string::npos);
}

TEST(SchemaGeneratorTest, ValidatorAppliesEnumConstraints) {
    graph::SchemaValidator<ConstraintConfig> validator;

    json invalid_input = json{{"mode", "invalid_mode"}, {"gain", 0.5}};
    EXPECT_FALSE(validator.Validate(invalid_input));

    const auto& errors = validator.GetErrors();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("mode"), std::string::npos);
}

TEST(SchemaGeneratorTest, ValidatorReportsTypeMismatchForConstrainedField) {
    graph::SchemaValidator<ConstraintConfig> validator;

    json invalid_input = json{{"mode", "auto"}, {"gain", "high"}};
    EXPECT_FALSE(validator.Validate(invalid_input));

    const auto& errors = validator.GetErrors();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("expected type number"), std::string::npos);
}
