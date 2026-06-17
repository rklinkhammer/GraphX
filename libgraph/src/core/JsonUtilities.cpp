// SPDX-License-Identifier: MIT

/**
 * @file JsonUtilities.cpp
 * @brief Implementation of safe JSON utilities with expected<> (Phase 5b)
 * @author Robert Klinkhammer
 * @date May 7, 2026
 */

#include "config/JsonUtilities.hpp"

#include <fstream>
#include <sstream>
#include <log4cxx/logger.h>
#include "core/FormatUtilities.hpp"

namespace app::json_util {

static log4cxx::LoggerPtr logger_ = 
    log4cxx::Logger::getLogger("app.json_util");

// ============================================================================
// ParseJsonSafe Implementation
// ============================================================================

std::expected<json, error::JsonParseError> 
ParseJsonSafe(std::string_view json_str) noexcept {
    try {
        if (json_str.empty()) {
            LOG4CXX_WARN(logger_, "Attempted to parse empty JSON string");
            return std::unexpected(error::JsonParseError::InvalidSyntax);
        }

        auto parsed = json::parse(json_str);
        LOG4CXX_TRACE(logger_, "Successfully parsed JSON string");
        return parsed;

    } catch (const json::parse_error& e) {
        std::string error_msg = format::FormatError(
            "JSONParse",
            e.what(),
            e.byte
        );
        LOG4CXX_WARN(logger_, error_msg);
        return std::unexpected(error::JsonParseError::InvalidSyntax);

    } catch (const json::type_error& e) {
        std::string error_msg = format::FormatError("JSONType", e.what());
        LOG4CXX_WARN(logger_, error_msg);
        return std::unexpected(error::JsonParseError::TypeMismatch);

    } catch (const json::out_of_range& e) {
        std::string error_msg = format::FormatError("JSONRange", e.what());
        LOG4CXX_WARN(logger_, error_msg);
        return std::unexpected(error::JsonParseError::TypeMismatch);

    } catch (const std::exception& e) {
        std::string error_msg = format::FormatError("JSONUnexpected", e.what());
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(error::JsonParseError::Unknown);

    } catch (...) {
        LOG4CXX_ERROR(logger_, "Unknown exception parsing JSON");
        return std::unexpected(error::JsonParseError::Unknown);
    }
}

// ============================================================================
// ParseJsonFile Implementation
// ============================================================================

std::expected<json, error::JsonParseError> 
ParseJsonFile(std::string_view filepath) noexcept {
    try {
        std::ifstream file(filepath.data());

        if (!file.is_open()) {
            std::string error_msg = std::format(
                "Cannot open JSON file: {}",
                filepath
            );
            LOG4CXX_WARN(logger_, error_msg);
            return std::unexpected(error::JsonParseError::InvalidSyntax);
        }

        // Read entire file
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_str = buffer.str();

        // Parse using safe parser
        return ParseJsonSafe(json_str);

    } catch (const std::exception& e) {
        std::string error_msg = format::FormatError(
            "FileRead",
            e.what()
        );
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(error::JsonParseError::Unknown);

    } catch (...) {
        LOG4CXX_ERROR(logger_, "Unknown exception reading JSON file");
        return std::unexpected(error::JsonParseError::Unknown);
    }
}

// ============================================================================
// ParseJsonDetailed Implementation
// ============================================================================

/**
 * @brief Parse json detailed.
 * @param json_str Parameter for parse json detailed.
 */
JsonParseResult ParseJsonDetailed(std::string_view json_str) noexcept {
    try {
        if (json_str.empty()) {
            return JsonParseResult{
                nullptr,
                error::JsonParseError::InvalidSyntax,
                "Empty JSON string",
                0, 0
            };
        }

        auto parsed = json::parse(json_str);
        auto data = std::make_shared<json>(parsed);
        
        return JsonParseResult{
            data,
            error::JsonParseError::Unknown,  // Success code
            "",
            0, 0
        };

    } catch (const json::parse_error& e) {
        return JsonParseResult{
            nullptr,
            error::JsonParseError::InvalidSyntax,
            std::string(e.what()),
            e.byte, 0
        };

    } catch (const json::type_error& e) {
        return JsonParseResult{
            nullptr,
            error::JsonParseError::TypeMismatch,
            std::string(e.what()),
            0, 0
        };

    } catch (const std::exception& e) {
        return JsonParseResult{
            nullptr,
            error::JsonParseError::Unknown,
            std::string(e.what()),
            0, 0
        };
    }
}

// ============================================================================
// ExtractObjectArray Implementation
// ============================================================================

std::expected<std::vector<json>, error::JsonParseError>
ExtractObjectArray(const json& json_obj, std::string_view field_name) noexcept {
    try {
        if (!json_obj.contains(field_name)) {
            return std::unexpected(error::JsonParseError::MissingRequiredField);
        }

        const auto& field = json_obj[field_name];

        if (!field.is_array()) {
            return std::unexpected(error::JsonParseError::TypeMismatch);
        }

        std::vector<json> result;
        result.reserve(field.size());

        for (const auto& element : field) {
            if (!element.is_object()) {
                return std::unexpected(error::JsonParseError::TypeMismatch);
            }
            result.push_back(element);
        }

        LOG4CXX_TRACE(logger_, std::format(
            "Extracted object array '{}' with {} elements",
            field_name, result.size()
        ));

        return result;

    } catch (const std::exception& e) {
        LOG4CXX_WARN(logger_, format::FormatError(
            "ArrayExtract",
            e.what()
        ));
        return std::unexpected(error::JsonParseError::Unknown);
    }
}

// ============================================================================
// Serialization Implementation
// ============================================================================

std::expected<std::string, error::JsonParseError>
SerializeJsonSafe(const json& json_obj, bool pretty) noexcept {
    try {
        std::string result = pretty 
            ? json_obj.dump(2)  // 2-space indentation
            : json_obj.dump();

        LOG4CXX_TRACE(logger_, std::format(
            "Serialized JSON to string ({} bytes)",
            result.size()
        ));

        return result;

    } catch (const std::exception& e) {
        std::string error_msg = format::FormatError(
            "JSONSerialize",
            e.what()
        );
        LOG4CXX_WARN(logger_, error_msg);
        return std::unexpected(error::JsonParseError::Unknown);
    }
}

std::expected<void, error::JsonParseError>
WriteJsonFile(std::string_view filepath, const json& json_obj,
             bool pretty) noexcept {
    try {
        // Serialize to string first
        auto serialize_result = SerializeJsonSafe(json_obj, pretty);
        if (!serialize_result) {
            return std::unexpected(serialize_result.error());
        }

        // Write to file
        std::ofstream file(filepath.data());
        if (!file.is_open()) {
            std::string error_msg = std::format(
                "Cannot open file for writing: {}",
                filepath
            );
            LOG4CXX_WARN(logger_, error_msg);
            return std::unexpected(error::JsonParseError::InvalidSyntax);
        }

        file << serialize_result.value();
        file.close();

        if (!file.good()) {
            std::string error_msg = std::format(
                "Error writing to file: {}",
                filepath
            );
            LOG4CXX_WARN(logger_, error_msg);
            return std::unexpected(error::JsonParseError::Unknown);
        }

        LOG4CXX_TRACE(logger_, std::format(
            "Successfully wrote JSON to file: {}",
            filepath
        ));

        return {};

    } catch (const std::exception& e) {
        std::string error_msg = format::FormatError(
            "FileWrite",
            e.what()
        );
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(error::JsonParseError::Unknown);
    }
}

// ============================================================================
// ValidateJsonStructure Implementation
// ============================================================================

error::JsonParseError
ValidateJsonStructure(const json& json_obj,
                     const std::vector<std::string>& required_fields) noexcept {
    try {
        if (!json_obj.is_object()) {
            return error::JsonParseError::UnexpectedStructure;
        }

        for (const auto& field : required_fields) {
            if (!json_obj.contains(field)) {
                std::string error_msg = std::format(
                    "Missing required field: '{}'",
                    field
                );
                LOG4CXX_WARN(logger_, error_msg);
                return error::JsonParseError::MissingRequiredField;
            }
        }

        LOG4CXX_TRACE(logger_, std::format(
            "JSON structure validation passed ({} required fields)",
            required_fields.size()
        ));

        return error::JsonParseError::Unknown;  // Success

    } catch (const std::exception& e) {
        LOG4CXX_WARN(logger_, format::FormatError(
            "ValidationError",
            e.what()
        ));
        return error::JsonParseError::Unknown;
    }
}

}  // namespace app::json_util
