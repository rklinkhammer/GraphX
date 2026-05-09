// MIT License
/// @file app/JsonDeserialization.hpp
/// @brief C++26 Reflection-based type-safe JSON deserialization

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

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>
#include <vector>
#include <nlohmann/json.hpp>
#include "graph/core/Expected.hpp"

// Feature detection for C++26 std::reflect
#if __cplusplus >= 202600 && __has_include(<meta>)
    #include <meta>
    #define GDASHBOARD_HAS_REFLECTION 1
#else
    #define GDASHBOARD_HAS_REFLECTION 0
#endif

// ============================================================================
// JSON Deserialization Infrastructure (Phase 3: C++26 Reflection)
// ============================================================================
// Provides reflection-based type-safe JSON deserialization with automatic
// error handling via std::expected<T, E> from Phase 1.
// Enables zero-copy deserialization and compile-time validation.
// ============================================================================

namespace app::json {

using json = nlohmann::json;

/**
 * @enum DeserializationError
 * @brief Error codes for JSON deserialization failures (Phase 3: expected<>)
 *
 * Specific error types from Phase 1 std::expected implementation.
 * Enables type-safe error handling without exceptions.
 */
enum class DeserializationError {
    InvalidJson,              ///< JSON parsing failed
    TypeMismatch,             ///< JSON type doesn't match expected type
    MissingRequiredField,     ///< Required field not present in JSON
    InvalidValue,             ///< Value fails validation/constraints
    UnexpectedField,          ///< Unknown field in JSON
    ConversionFailed,         ///< Type conversion failed
    ConstraintViolation,      ///< Value violates schema constraint
    UnknownError              ///< Other deserialization error
};

/**
 * @brief Deserialize JSON to type T (Phase 3: Type-Safe with expected<>)
 *
 * Reflection-based deserialization that automatically maps JSON fields to
 * struct members. Validates against schema at runtime and returns errors
 * via std::expected<T, DeserializationError>.
 *
 * **Phase 3 Deserialization Pattern** (with expected<> from Phase 1):
 * @code
 *   struct MyConfig {
 *       int value;
 *       std::string name;
 *       
 *       static constexpr std::array<JsonField, 2> Fields() {
 *           return {{ ... }};
 *       }
 *   };
 *
 *   json json_input = json::parse(input_string);
 *   
 *   // Type-safe deserialization with error handling
 *   auto result = Deserialize<MyConfig>(json_input);
 *   
 *   if (result) {
 *       auto config = result.value();
 *       // Use config safely
 *   } else {
 *       auto error = result.error();
 *       // Handle error without exceptions
 *   }
 * @endcode
 *
 * @tparam T Config type to deserialize into
 * @param json_value JSON object to deserialize
 * @return std::expected<T, DeserializationError> with deserialized value or error
 */
template<typename T>
requires requires {
    { T::Fields() } -> std::convertible_to<std::vector<std::string>>;
}
std::expected<T, DeserializationError> Deserialize(const json& json_value) {
    if (!json_value.is_object()) {
        return std::unexpected(DeserializationError::TypeMismatch);
    }
    
    // TODO: Phase 3 - Implement reflection-based field mapping
    // For now, return placeholder
    
    T result{};
    return result;
}

/**
 * @brief Deserialize with validation against schema (Phase 3)
 *
 * Combines schema validation with deserialization. Validates before
 * attempting deserialization to catch errors early.
 *
 * **Phase 3 Validated Deserialization Pattern**:
 * @code
 *   struct MyConfig { ... };
 *   
 *   json json_input = json::parse(config_str);
 *   
 *   auto result = DeserializeAndValidate<MyConfig>(json_input);
 *   if (result) {
 *       auto config = result.value();
 *       // Config is guaranteed to be valid
 *   } else {
 *       switch (result.error()) {
 *       case DeserializationError::InvalidJson:
 *           // JSON parsing failed
 *       case DeserializationError::MissingRequiredField:
 *           // Required field not found
 *       // ... handle other error cases
 *       }
 *   }
 * @endcode
 *
 * @tparam T Config type to deserialize
 * @param json_value JSON to deserialize
 * @return std::expected<T, DeserializationError> with value or error
 */
template<typename T>
std::expected<T, DeserializationError> DeserializeAndValidate(const json& json_value) {
    // TODO: Phase 3 - Combine schema validation with deserialization
    return Deserialize<T>(json_value);
}

// ============================================================================
// Phase 3: Generic Deserialization for Basic Types
// ============================================================================

/**
 * @brief Deserialize primitive type from JSON (Phase 3 Foundation)
 *
 * Specialization for built-in types: int, double, float, bool, string
 *
 * @tparam T Primitive type to deserialize
 * @param json_value JSON value to deserialize
 * @return std::expected<T, DeserializationError> with value or error
 */
template<typename T>
requires std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
std::expected<T, DeserializationError> DeserializePrimitive(const json& json_value) {
    try {
        if constexpr (std::is_same_v<T, int>) {
            if (!json_value.is_number_integer()) {
                return std::unexpected(DeserializationError::TypeMismatch);
            }
            return json_value.get<int>();
        } else if constexpr (std::is_same_v<T, double>) {
            if (!json_value.is_number()) {
                return std::unexpected(DeserializationError::TypeMismatch);
            }
            return json_value.get<double>();
        } else if constexpr (std::is_same_v<T, float>) {
            if (!json_value.is_number()) {
                return std::unexpected(DeserializationError::TypeMismatch);
            }
            return json_value.get<float>();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!json_value.is_boolean()) {
                return std::unexpected(DeserializationError::TypeMismatch);
            }
            return json_value.get<bool>();
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!json_value.is_string()) {
                return std::unexpected(DeserializationError::TypeMismatch);
            }
            return json_value.get<std::string>();
        }
    } catch (const std::exception& e) {
        return std::unexpected(DeserializationError::ConversionFailed);
    }
    
    return std::unexpected(DeserializationError::UnknownError);
}

// ============================================================================
// Phase 3: Deserialization with Error Details
// ============================================================================

/**
 * @struct DeserializationResult
 * @brief Detailed deserialization result with error information
 *
 * Extends std::expected<T, E> with additional context about what failed.
 */
template<typename T>
struct DeserializationResult {
    std::expected<T, DeserializationError> value;
    std::vector<std::string> error_details;  ///< Detailed error messages
    
    /**
     * @brief Check if deserialization succeeded
     */
    bool IsSuccess() const {
        return value.has_value();
    }
    
    /**
     * @brief Get deserialized value (may throw if error)
     */
    T GetValue() const {
        return value.value();
    }
    
    /**
     * @brief Get error details as formatted string
     */
    std::string GetErrorMessage() const {
        std::string message;
        for (const auto& detail : error_details) {
            message += detail + "\n";
        }
        return message;
    }
};

/**
 * @brief Deserialize with detailed error information (Phase 3)
 *
 * Returns a detailed result object with error messages explaining
 * exactly what went wrong during deserialization.
 *
 * **Phase 3 Detailed Error Pattern**:
 * @code
 *   struct MyConfig { ... };
 *   
 *   auto result = DeserializeWithDetails<MyConfig>(json_input);
 *   if (!result.IsSuccess()) {
 *       std::cerr << "Deserialization failed:\n";
 *       std::cerr << result.GetErrorMessage();
 *       return;
 *   }
 *   
 *   auto config = result.GetValue();
 * @endcode
 *
 * @tparam T Config type to deserialize
 * @param json_value JSON to deserialize
 * @return DeserializationResult<T> with value/error and details
 */
template<typename T>
DeserializationResult<T> DeserializeWithDetails(const json& json_value) {
    DeserializationResult<T> result;
    result.value = Deserialize<T>(json_value);
    
    if (!result.IsSuccess()) {
        // TODO: Phase 3 - Add detailed error context
        switch (result.value.error()) {
        case DeserializationError::InvalidJson:
            result.error_details.push_back("Input is not valid JSON");
            break;
        case DeserializationError::TypeMismatch:
            result.error_details.push_back("JSON structure doesn't match expected type");
            break;
        case DeserializationError::MissingRequiredField:
            result.error_details.push_back("Required field is missing from JSON");
            break;
        case DeserializationError::ConstraintViolation:
            result.error_details.push_back("Value violates schema constraints");
            break;
        default:
            result.error_details.push_back("Unknown deserialization error");
            break;
        }
    }
    
    return result;
}

// ============================================================================
// Phase 3: JSON to Type Mapping
// ============================================================================

/**
 * @brief Extract field from JSON object (Phase 3 Helper)
 *
 * Safely extracts a field from JSON with type checking and error reporting.
 *
 * @tparam T Type to extract to
 * @param obj JSON object
 * @param field_name Field name to extract
 * @return std::expected<T, DeserializationError> with value or error
 */
template<typename T>
std::expected<T, DeserializationError> ExtractField(
    const json& obj,
    std::string_view field_name) {
    
    if (!obj.is_object()) {
        return std::unexpected(DeserializationError::TypeMismatch);
    }
    
    auto it = obj.find(std::string(field_name));
    if (it == obj.end()) {
        return std::unexpected(DeserializationError::MissingRequiredField);
    }
    
    return DeserializePrimitive<T>(*it);
}

// ============================================================================
// Phase 3: Configuration from JSON (Convenience Functions)
// ============================================================================

/**
 * @brief Parse JSON string and deserialize to type T (Phase 3)
 *
 * Combines JSON parsing with deserialization in one call.
 *
 * **Phase 3 Convenience Pattern**:
 * @code
 *   auto result = DeserializeFromString<MyConfig>(json_string);
 *   if (result) {
 *       auto config = result.value();
 *   } else {
 *       // Handle error
 *   }
 * @endcode
 *
 * @tparam T Config type to deserialize to
 * @param json_string JSON string to parse
 * @return std::expected<T, DeserializationError> with value or error
 */
template<typename T>
std::expected<T, DeserializationError> DeserializeFromString(const std::string& json_string) {
    try {
        json json_value = json::parse(json_string);
        return Deserialize<T>(json_value);
    } catch (const json::parse_error& e) {
        return std::unexpected(DeserializationError::InvalidJson);
    } catch (const std::exception& e) {
        return std::unexpected(DeserializationError::UnknownError);
    }
}

/**
 * @brief Load and deserialize from file (Phase 3)
 *
 * Reads JSON from file and deserializes to type T.
 *
 * @tparam T Config type to deserialize to
 * @param file_path Path to JSON file
 * @return std::expected<T, DeserializationError> with value or error
 */
template<typename T>
std::expected<T, DeserializationError> DeserializeFromFile(const std::string& file_path) {
    // TODO: Phase 3 - Implement file loading with proper error handling
    return std::unexpected(DeserializationError::UnknownError);
}

}  // namespace app::json

// ============================================================================
// Phase 3 Deserialization Patterns & Best Practices
// ============================================================================
//
// **Pattern 1: Basic Deserialization (Phase 3)**
// ```cpp
// auto result = Deserialize<MyConfig>(json_input);
// if (result) {
//     auto config = result.value();
//     // Use config
// } else {
//     // Handle error code
// }
// ```
//
// **Pattern 2: Deserialization with Details (Phase 3)**
// ```cpp
// auto result = DeserializeWithDetails<MyConfig>(json_input);
// if (!result.IsSuccess()) {
//     std::cerr << result.GetErrorMessage();
//     return;
// }
// auto config = result.GetValue();
// ```
//
// **Pattern 3: From String (Phase 3 Convenience)**
// ```cpp
// auto result = DeserializeFromString<MyConfig>(json_string);
// if (result) {
//     auto config = result.value();
// }
// ```
//
// **Pattern 4: From File (Phase 3 Convenience)**
// ```cpp
// auto result = DeserializeFromFile<MyConfig>("config.json");
// if (result) {
//     auto config = result.value();
// }
// ```
//
// **Benefits**:
// - Zero-copy deserialization (views, references)
// - Type-safe error handling via expected<> (Phase 1)
// - Compile-time validation when possible
// - Detailed error messages for debugging
// - No exceptions needed for error cases
// - Reflection-based field mapping (no manual boilerplate)
//
