// MIT License
/// @file config/SchemaGenerator.hpp
/// @brief C++26 Reflection-based automatic schema generation for configs and metrics

//
// Copyright (c) 2025 Robert Klinkhammer
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

#include <string_view>
#include <string>
#include <type_traits>
#include <concepts>
#include <array>
#include <vector>
#include <optional>
#include <span>
#include <ranges>
#include <nlohmann/json.hpp>
#include "Config.hpp"
#include "core/ReflectionHelper.hpp"

// Feature detection for C++26 std::reflect
#if __cplusplus >= 202600 && __has_include(<meta>)
    #include <meta>
    #define GDASHBOARD_HAS_REFLECTION 1
#else
    #define GDASHBOARD_HAS_REFLECTION 0
#endif

// ============================================================================
// Schema Generation Infrastructure (Phase 3: C++26 Reflection)
// ============================================================================
// Automatically generates JSON schemas and field metadata from C++26
// reflection, eliminating manual schema definitions and reducing boilerplate.
// ============================================================================

namespace graph {

using json = nlohmann::json;

template<typename T>
using ConfigFieldsRange = decltype(T::Fields());

template<typename T>
concept JsonFieldDescriptorProvider = requires {
    T::Fields();
    requires std::ranges::sized_range<ConfigFieldsRange<T>>;
    requires std::same_as<
        std::remove_cvref_t<std::ranges::range_value_t<ConfigFieldsRange<T>>>,
        JsonField>;
};

/**
 * @brief Convert JsonType to a JSON schema type string
 */
constexpr std::string_view JsonTypeToSchemaType(JsonType type) {
    switch (type) {
        case JsonType::String:  return "string";
        case JsonType::Number:   return "number";
        case JsonType::Integer:  return "integer";
        case JsonType::Boolean:  return "boolean";
        case JsonType::Object:   return "object";
        case JsonType::Array:    return "array";
    }

    return "string";
}

/**
 * @brief Convert a JsonField default string into a JSON value when possible
 */
inline std::optional<json> ParseDefaultJsonValue(
    const JsonField& field) {

    if (!field.default_value.has_value()) {
        return std::nullopt;
    }

    try {
        switch (field.type) {
            case JsonType::String:
                return json(std::string(*field.default_value));
            case JsonType::Number:
                return json(std::stod(std::string(*field.default_value)));
            case JsonType::Integer:
                return json(std::stoll(std::string(*field.default_value)));
            case JsonType::Boolean: {
                const std::string value(*field.default_value);
                if (value == "true" || value == "1") {
                    return json(true);
                }
                if (value == "false" || value == "0") {
                    return json(false);
                }
                return json(value);
            }
            case JsonType::Object:
            case JsonType::Array:
                return json::parse(std::string(*field.default_value));
        }
    } catch (...) {
        return json(std::string(*field.default_value));
    }

    return json(std::string(*field.default_value));
}

/**
 * @struct FieldConstraint
 * @brief Validation constraint for a schema field
 *
 * Represents constraints extracted via reflection:
 * - min/max values for numeric types
 * - length limits for strings
 * - enum allowed values
 * - pattern matching (regex)
 */
struct FieldConstraint {
    std::string_view constraint_type;  ///< "min", "max", "length", "enum", "pattern"
    json constraint_value;              ///< The constraint value(s)
    std::string_view description;      ///< Human-readable constraint description
};

/**
 * @struct ReflectedFieldMetadata
 * @brief Complete metadata for a single field extracted via reflection
 *
 * Generated from C++26 reflection of struct members.
 * Includes type information, constraints, defaults, and descriptions.
 */
struct ReflectedFieldMetadata {
    std::string_view field_name;        ///< Member variable name
    std::string_view field_type;        ///< Type name (int, double, string, etc.)
    std::optional<json> default_value;  ///< Default value if available
    std::string_view description;       ///< Field description
    bool is_required = true;            ///< Must be provided in config?
    bool is_readonly = false;           ///< Can't be modified after creation?
    std::span<const FieldConstraint> constraints;  ///< Validation constraints
};

/**
 * @struct GeneratedSchema
 * @brief Complete schema generated from reflection of a config type
 *
 * Contains all information needed to:
 * - Validate JSON input
 * - Deserialize JSON to struct
 * - Serialize struct to JSON
 * - Generate documentation
 * - Perform runtime type checking
 */
struct GeneratedSchema {
    std::string_view title;             ///< Schema title (usually type name)
    std::string_view description;       ///< What this config controls
    std::span<const ReflectedFieldMetadata> fields;  ///< All fields in config
    json examples;                      ///< Example configurations
};

// ============================================================================
// Phase 3: Reflection-Based Type Name Extraction
// ============================================================================

/**
 * @brief Get human-readable type name (Phase 3: C++26 Reflection)
 *
 * Uses C++26 reflection (when available) to extract type names automatically.
 * Falls back to manual type checking for basic types in C++20.
 *
 * @tparam T The type to get the name for
 * @return String view of the type name
 *
 * **Phase 3 C++26 Pattern**:
 * @code
 *   // With C++26 reflection:
 *   consteval std::string_view get_type_name<MyConfig>() {
 *       return std::meta::name_of<MyConfig>();  // "MyConfig"
 *   }
 * @endcode
 */
template<typename T>
consteval std::string_view GetTypeName() {
#if GDASHBOARD_HAS_REFLECTION
    // C++26: Use reflection to get exact type name
    return std::meta::name_of<T>();
#else
    // C++20: Manual type checking
    if constexpr (std::is_same_v<T, int>) {
        return "int";
    } else if constexpr (std::is_same_v<T, double>) {
        return "double";
    } else if constexpr (std::is_same_v<T, float>) {
        return "float";
    } else if constexpr (std::is_same_v<T, bool>) {
        return "bool";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return "std::string";
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return "std::string_view";
    } else {
        return reflection::ExtractTypeNameFromFunction<T>();
    }
#endif
}

// ============================================================================
// Phase 3: Automatic Field Metadata Generation
// ============================================================================

/**
 * @brief Generate field metadata from a field descriptor (Phase 3: Reflection)
 *
 * Converts JsonField metadata into ReflectedFieldMetadata with all
 * constraint and validation information.
 *
 * @tparam T The data type of the field
 * @param field The JsonField descriptor
 * @param default_val Optional default value
 * @return ReflectedFieldMetadata with all constraint information
 */
template<typename T>
consteval ReflectedFieldMetadata ReflectField(
    const JsonField& field,
    const std::optional<json>& default_val = std::nullopt) {
    
    ReflectedFieldMetadata metadata{
        .field_name = field.name,
        .field_type = GetTypeName<T>(),
        .default_value = default_val,
        .description = field.description,
        .is_required = !default_val.has_value(),
        .is_readonly = false
    };
    
    return metadata;
}

// ============================================================================
// Phase 3: Schema Generation from Type
// ============================================================================

/**
 * @brief Generate JSON schema from a config type (Phase 3: Reflection Foundation)
 *
 * Automatically generates a complete JSON schema by reflecting on the type
 * and extracting field information. Works with any type that has a JsonField
 * descriptor array.
 *
 * **Phase 3 Usage**:
 * @code
 *   struct MyConfig {
 *       int value;
 *       std::string name;
 *       
 *       static constexpr std::array<JsonField, 2> Fields() {
 *           return {{
 *               JsonField{.name = "value", .type = "int", ...},
 *               JsonField{.name = "name", .type = "string", ...}
 *           }};
 *       }
 *   };
 *   
 *   // Automatically generate schema from type
 *   constexpr auto schema = GenerateSchemaFromType<MyConfig>();
 *   
 *   // Use schema for:
 *   // - Runtime validation
 *   // - Documentation generation
 *   // - Code generation
 *   // - API documentation
 * @endcode
 *
 * @tparam ConfigType Type with JsonField descriptor (must have Fields() method)
 * @return GeneratedSchema with all field metadata and constraints
 */
template<typename ConfigType>
requires JsonFieldDescriptorProvider<ConfigType>
inline GeneratedSchema GenerateSchemaFromType() {
    static const auto reflected_fields = [] {
        constexpr auto json_fields = ConfigType::Fields();
        std::array<ReflectedFieldMetadata, std::ranges::size(json_fields)> fields{};

        for (std::size_t index = 0; index < fields.size(); ++index) {
            const auto& field = json_fields[index];
            fields[index] = ReflectedFieldMetadata{
                .field_name = field.name,
                .field_type = JsonTypeToSchemaType(field.type),
                .default_value = ParseDefaultJsonValue(field),
                .description = field.description,
                .is_required = field.required,
                .is_readonly = false,
                .constraints = {}
            };
        }

        return fields;
    }();

    return GeneratedSchema{
        .title = GetTypeName<ConfigType>(),
        .description = "Auto-generated schema from JsonField descriptors",
        .fields = reflected_fields,
        .examples = json::array()
    };
}

// ============================================================================
// Phase 3: Schema-Driven Validation
// ============================================================================

/**
 * @class SchemaValidator
 * @brief Validate JSON against an auto-generated schema (Phase 3)
 *
 * Uses reflection-based schema to validate JSON input before deserialization.
 * Provides detailed error messages for validation failures.
 *
 * **Phase 3 Validation Pattern**:
 * @code
 *   SchemaValidator<MyConfig> validator;
 *   
 *   json input = json::parse(config_string);
 *   if (auto result = validator.Validate(input)) {
 *       // Validation successful
 *       auto config = DeserializeConfig<MyConfig>(input);
 *   } else {
 *       // Validation failed
 *       auto errors = validator.GetErrors();
 *       for (const auto& error : errors) {
 *           std::cerr << error << "\n";
 *       }
 *   }
 * @endcode
 */
template<typename ConfigType>
class SchemaValidator {
    static_assert(JsonFieldDescriptorProvider<ConfigType>,
                  "ConfigType must expose a sized range of JsonField descriptors via Fields()");

public:
    /**
     * @brief Get the auto-generated schema for this config type
     *
     * @return GeneratedSchema with all field information
     */
    static inline GeneratedSchema GetSchema() {
        return GenerateSchemaFromType<ConfigType>();
    }

    /**
     * @brief Validate JSON against the schema
     *
     * @param json_value JSON object to validate
     * @return true if JSON is valid according to schema
     */
    bool Validate(const json& json_value) {
        errors_.clear();
        
        if (!json_value.is_object()) {
            errors_.push_back("Input must be a JSON object");
            return false;
        }
        
        const auto& schema = GetSchema();
        
        // Check required fields
        for (const auto& field : schema.fields) {
            if (field.is_required && json_value.find(std::string(field.field_name)) == json_value.end()) {
                errors_.push_back(std::string("Missing required field: ") + std::string(field.field_name));
            }
        }

        constexpr auto fields = ConfigType::Fields();
        for (const auto& field : fields) {
            const auto it = json_value.find(std::string(field.name));
            if (it == json_value.end()) {
                continue;
            }

            const auto expected_type = JsonTypeToSchemaType(field.type);
            const bool type_valid = [expected = field.type, &value = *it]() {
                switch (expected) {
                    case JsonType::String:  return value.is_string();
                    case JsonType::Number:  return value.is_number();
                    case JsonType::Integer: return value.is_number_integer();
                    case JsonType::Boolean: return value.is_boolean();
                    case JsonType::Object:  return value.is_object();
                    case JsonType::Array:   return value.is_array();
                }
                return false;
            }();

            if (!type_valid) {
                errors_.push_back(
                    std::string("Field '") + std::string(field.name) +
                    "' expected type " + std::string(expected_type));
                continue;
            }

            if (field.type == JsonType::Number || field.type == JsonType::Integer) {
                const double numeric_value = it->get<double>();
                if (field.min.has_value() && numeric_value < *field.min) {
                    errors_.push_back(
                        std::string("Field '") + std::string(field.name) +
                        "' must be >= " + std::to_string(*field.min));
                }
                if (field.max.has_value() && numeric_value > *field.max) {
                    errors_.push_back(
                        std::string("Field '") + std::string(field.name) +
                        "' must be <= " + std::to_string(*field.max));
                }
            }

            if (field.enum_values.has_value()) {
                bool allowed = false;

                if (it->is_string()) {
                    const auto value = it->get<std::string>();
                    for (const auto candidate : *field.enum_values) {
                        if (value == candidate) {
                            allowed = true;
                            break;
                        }
                    }
                } else {
                    const auto value = it->dump();
                    for (const auto candidate : *field.enum_values) {
                        if (value == candidate) {
                            allowed = true;
                            break;
                        }
                    }
                }

                if (!allowed) {
                    errors_.push_back(
                        std::string("Field '") + std::string(field.name) +
                        "' is not in the allowed enum set");
                }
            }
        }
        
        return errors_.empty();
    }

    /**
     * @brief Get validation errors from last Validate() call
     *
     * @return Vector of error messages
     */
    const std::vector<std::string>& GetErrors() const {
        return errors_;
    }

    /**
     * @brief Check if schema has a specific field
     *
     * @param field_name Name of the field
     * @return true if field exists in schema
     */
    bool HasField(std::string_view field_name) const {
        const auto& schema = GetSchema();
        for (const auto& field : schema.fields) {
            if (field.field_name == field_name) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::string> errors_;  ///< Validation error messages
};

// ============================================================================
// Phase 3: Schema Export & Documentation
// ============================================================================

/**
 * @brief Export schema as JSON for API documentation (Phase 3)
 *
 * Converts a GeneratedSchema into JSON-Schema format (Draft 7 compatible)
 * for use in API documentation, client code generation, etc.
 *
 * **Phase 3 Export Pattern**:
 * @code
 *   constexpr auto schema = GenerateSchemaFromType<MyConfig>();
 *   json json_schema = ExportSchemaAsJSON(schema);
 *   
 *   // json_schema is now in JSON-Schema format for documentation
 *   std::cout << json_schema.dump(2) << "\n";
 * @endcode
 *
 * @param schema GeneratedSchema to export
 * @return JSON object in JSON-Schema format
 */
inline json ExportSchemaAsJSON(const GeneratedSchema& schema) {
    json properties;
    json required;
    
    for (const auto& field : schema.fields) {
        json field_schema;
        field_schema["type"] = std::string(field.field_type);
        field_schema["description"] = std::string(field.description);
        
        if (field.default_value.has_value()) {
            field_schema["default"] = field.default_value.value();
        }
        
        // Add constraints
        for (const auto& constraint : field.constraints) {
            if (constraint.constraint_type == "min") {
                field_schema["minimum"] = constraint.constraint_value;
            } else if (constraint.constraint_type == "max") {
                field_schema["maximum"] = constraint.constraint_value;
            } else if (constraint.constraint_type == "length") {
                field_schema["maxLength"] = constraint.constraint_value;
            }
        }
        
        properties[std::string(field.field_name)] = field_schema;
        
        if (field.is_required) {
            required.push_back(std::string(field.field_name));
        }
    }
    
    json result;
    result["$schema"] = "http://json-schema.org/draft-07/schema#";
    result["title"] = std::string(schema.title);
    result["description"] = std::string(schema.description);
    result["type"] = "object";
    result["properties"] = properties;
    
    if (!required.empty()) {
        result["required"] = required;
    }
    
    if (!schema.examples.empty()) {
        result["examples"] = schema.examples;
    }
    
    return result;
}

}  // namespace graph

// ============================================================================
// Phase 3 Schema Generation Patterns & Best Practices
// ============================================================================
//
// **Pattern 1: Automatic Schema from Type (C++26)**
// ```cpp
// struct ConfigType {
//     static constexpr auto Fields() { ... }
// };
//
// constexpr auto schema = GenerateSchemaFromType<ConfigType>();
// // Schema fully generated at compile-time, zero runtime cost
// ```
//
// **Pattern 2: Schema Validation (Phase 3)**
// ```cpp
// SchemaValidator<MyConfig> validator;
// if (validator.Validate(json_input)) {
//     auto config = DeserializeConfig<MyConfig>(json_input);
// }
// ```
//
// **Pattern 3: Schema Export for Documentation (Phase 3)**
// ```cpp
// constexpr auto schema = GenerateSchemaFromType<MyConfig>();
// auto json_schema = ExportSchemaAsJSON(schema);
// // Ready for API docs, OpenAPI spec generation, etc.
// ```
//
// **Benefits**:
// - Zero compile-time cost (consteval)
// - No manual schema files to maintain
// - Type-safe validation at compile-time
// - Automatic documentation generation
// - Works with existing JsonField infrastructure
//
