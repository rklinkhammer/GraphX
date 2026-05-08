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

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace graph {

/**
 * JSON field type enumeration.
 * Used in schema metadata to define what types of values are expected.
 */
enum class JsonType {
    String,
    Number,
    Integer,
    Boolean,
    Object,
    Array
};

/**
 * Metadata about a single JSON field.
 * 
 * Describes:
 * - Field name and type
 * - Whether it's required
 * - Constraints (min, max values)
 * - Default value
 * - Enum restrictions
 * - Human-readable description
 * 
 * Example:
 * @code
 * JsonField{
 *     .name = "q",
 *     .type = JsonType::Number,
 *     .required = true,
 *     .min = 0.0,
 *     .max = std::nullopt,
 *     .default_value = "0.01",
 *     .description = "Process noise covariance"
 * }
 * @endcode
 */
struct JsonField {
    /// Field name in JSON
    std::string_view name;
    
    /// Expected type
    JsonType type;
    
    /// Whether this field must be present
    bool required = true;
    
    /// Minimum value (for numbers)
    std::optional<double> min;
    
    /// Maximum value (for numbers)
    std::optional<double> max;
    
    /// Default value as string
    std::optional<std::string_view> default_value;
    
    /// Allowed enumeration values
    std::optional<std::span<const std::string_view>> enum_values;
    
    /// Human-readable field description
    std::string_view description;
};

/**
 * Complete JSON schema for a config type.
 * 
 * Describes the entire structure of a configuration,
 * including all fields and their metadata.
 * 
 * This is the schema generated from a config struct via:
 * @code
 * struct MyConfig {
 *     static constexpr std::array<JsonField, N> Fields() { ... }
 *     static constexpr JsonSchema Schema() {
 *         return {
 *             .title = "My Config",
 *             .description = "Description of my config",
 *             .fields = Fields()
 *         };
 *     }
 * };
 * @endcode
 */
struct JsonSchema {
    /// Short title for the config type
    std::string_view title;
    
    /// Detailed description of what this config does
    std::string_view description;
    
    /// All fields in this schema
    std::span<const JsonField> fields;
};

}  // namespace graph
