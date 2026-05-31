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
