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

#include <string>
#include <vector>
#include <optional>
#include <expected>
#include <cstddef>
#include <map>
#include <functional>
#include <any>

#include "config/DataTypes.hpp"
#include "graph/IDataInjectionSource.hpp"
#include "graph/Message.hpp"

namespace csv {

// Forward declarations
struct CSVNodeConfig;

/**
 * @brief Header information parsed from CSV file
 *
 * Describes the structure and format of a CSV file for sensor data.
 * Used to determine how to parse rows and route data to nodes.
 */
struct CSVHeader {
    /// Column names from the header row
    std::vector<std::string> columns;

    /// Index of timestamp column (typically 0)
    size_t timestamp_column = 0;

    /// Indices of data columns (sensor-specific values)
    std::vector<size_t> data_columns;

    /// Detected CSV format: "unified" or "consolidated"
    /// - Unified: Each file has one sensor type, columns match sensor fields
    /// - Consolidated: Single file with all sensor types, data_type column
    std::string format;
};

// ========== C++26 Error Handling ==========

/**
 * @enum ParsingError
 * @brief Error codes for CSV parsing operations
 * 
 * Used with std::expected<T, ParsingError> for type-safe error handling.
 * Replaces try-catch patterns with composable error types.
 * 
 * Example:
 * ```cpp
 * auto result = ParseAccelerometerRowExpected(values, config);
 * if (!result) {
 *     switch (result.error()) {
 *         case ParsingError::EmptyColumn: ...break;
 *         case ParsingError::InvalidNumber: ...break;
 *     }
 * }
 * ```
 */
enum class ParsingError {
    /// One or more required columns are empty or missing
    EmptyColumn,
    
    /// Value cannot be converted to number (invalid format)
    InvalidNumber,
    
    /// Header missing required columns for this sensor type
    MissingRequiredColumns,
    
    /// File cannot be opened or read
    FileAccessError,
    
    /// Header format does not match expected schema
    HeaderParseError,
    
    /// Configuration is invalid or missing
    ConfigurationError,
    
    /// Row has fewer columns than expected
    InsufficientColumns,
    
    /// Other parsing error (see error message for details)
    UnknownError
};

// ========== File I/O ==========

/**
 * @brief Read CSV file and split into lines
 *
 * Reads entire CSV file into memory. Each line is a separate string.
 * Used for both header detection and data row processing.
 *
 * Error Handling:
 * - Returns empty vector if file cannot be opened
 * - Returns lines read so far if EOF or read error occurs partway through
 *
 * @param path Path to CSV file
 * @return Vector of lines (header + data rows), empty if file cannot be read
 */
std::vector<std::string> ReadCSVFile(const std::string& path);

// ========== Header Parsing ==========

/**
 * @brief Parse CSV header line to extract column information
 *
 * Splits header line and identifies column positions.
 * Does NOT determine format (use DetectFormat for that).
 *
 * Example:
 * Input: "timestamp_ns,accel_x_mss,accel_y_mss,accel_z_mss"
 * Output: CSVHeader with columns = ["timestamp_ns", "accel_x_mss", ...]
 *
 * @param header_line First line of CSV file
 * @return CSVHeader with column information
 */
CSVHeader ParseHeader(const std::string& header_line);

/**
 * @brief Detect CSV format from header
 *
 * Determines if CSV is in unified or consolidated format.
 *
 * Detection Logic:
 * - If "data_type" column exists → consolidated format
 * - Otherwise → unified format (one sensor type per file)
 *
 * @param header Parsed header information
 * @return "unified" or "consolidated"
 */
std::string DetectFormat(const CSVHeader& header);

// ========== Row Splitting ==========

/**
 * @brief Split CSV row into column values
 *
 * Handles:
 * - Comma-separated values
 * - Trimming whitespace
 * - Empty columns (represented as empty strings)
 *
 * Does NOT handle:
 * - Quoted fields (values with embedded commas)
 * - Escaped quotes
 *
 * Example:
 * Input: " 123456789 , 1.0 , 2.0 , 3.0 "
 * Output: ["123456789", "1.0", "2.0", "3.0"]
 *
 * @param line CSV data row
 * @return Vector of column values (trimmed)
 */
std::vector<std::string> SplitCSVLine(const std::string& line);

// ========== Validation ==========

/**
 * @brief Validate CSV row has required columns
 *
 * Checks if row has enough columns and required fields are non-empty.
 *
 * Validation Rules:
 * - Row must have at least data_columns.size() + 1 elements (including timestamp)
 * - Timestamp column must be non-empty
 * - At least one data column must be non-empty
 *
 * Used to skip malformed rows without logging spam.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return true if row is valid, false if malformed
 */
bool ValidateCSVRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

// ========== C++26 Expected<> API ==========

/**
 * @brief Parse accelerometer data with error handling (C++26)
 *
 * Returns std::expected<> for composable error handling.
 *
 * Example:
 * ```cpp
 * auto result = ParseAccelerometerRowExpected(values, config);
 * if (!result) {
 *     LOG_ERROR("Parse failed: " << static_cast<int>(result.error()));
 * } else {
 *     auto payload = result.value();
 *     // Use payload...
 * }
 * ```
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseAccelerometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse gyroscope data with error handling (C++26)
 *
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseGyroscopeRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse GPS position data with error handling (C++26)
 *
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseGPSPositionRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse barometric data with error handling (C++26)
 *
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseBarometricRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse magnetometer data with error handling (C++26)
 *
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseMagnetometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse row in unified format with error handling (C++26)
 *
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseRowUnifiedExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

// ========== PHASE 1: Generalized CSV Parsing Infrastructure ==========

/**
 * @struct ColumnMapping
 * @brief Maps CSV header names to data extraction functions
 *
 * Replaces hardcoded column indices with flexible, user-specified mappings.
 * Each field is mapped to a column index and a converter function that
 * transforms string CSV values to the appropriate type.
 *
 * Example usage:
 * ```cpp
 * auto accel_mapping = csv::ColumnMapping{
 *     .field_to_column = {
 *         {"timestamp_ns", 0},
 *         {"accel_x_mss", 1},
 *         {"accel_y_mss", 2},
 *         {"accel_z_mss", 3}
 *     },
 *     .converters = {
 *         {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
 *         {"accel_x_mss", [](const std::string& s) -> std::any { return std::stod(s); }},
 *         {"accel_y_mss", [](const std::string& s) -> std::any { return std::stod(s); }},
 *         {"accel_z_mss", [](const std::string& s) -> std::any { return std::stod(s); }}
 *     }
 * };
 * ```
 * 
 * Or using the converter factory functions:
 * ```cpp
 * auto accel_mapping = csv::ColumnMapping{
 *     .field_to_column = {
 *         {"timestamp_ns", 0},
 *         {"accel_x_mss", 1},
 *         {"accel_y_mss", 2},
 *         {"accel_z_mss", 3}
 *     },
 *     .converters = {
 *         {"timestamp_ns", csv::converters::MakeUInt64Converter()},
 *         {"accel_x_mss", csv::converters::MakeDoubleConverter()},
 *         {"accel_y_mss", csv::converters::MakeDoubleConverter()},
 *         {"accel_z_mss", csv::converters::MakeDoubleConverter()}
 *     }
 * };
 * ```
 */
struct ColumnMapping {
    /// Maps field name to column index in CSV row
    /// Example: {"timestamp_ns": 0, "accel_x": 1, "accel_y": 2, "accel_z": 3}
    std::map<std::string, size_t> field_to_column;

    /// Maps field name to converter function
    /// Converts string CSV value to std::any (which can be std::any_cast to the target type)
    /// Example: {"timestamp_ns": [](const std::string& s) -> std::any { return std::stoull(s); }, ...}
    std::map<std::string, std::function<std::any(const std::string&)>> converters;

    /**
     * @brief Check if a field is present in the mapping
     * @param field Field name to check
     * @return true if field exists in column mapping
     */
    bool HasField(std::string_view field) const {
        return field_to_column.find(std::string(field)) != field_to_column.end();
    }

    /**
     * @brief Get column index for a field
     * @param field Field name
     * @return Column index if found, std::nullopt otherwise
     */
    std::optional<size_t> GetColumnIndex(std::string_view field) const {
        auto it = field_to_column.find(std::string(field));
        if (it != field_to_column.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Convert string value using appropriate converter
     * @param field Field name
     * @param value String value from CSV
     * @return Converted value as std::any on success, ParsingError on failure
     */
    std::expected<std::any, ParsingError> ConvertValue(
        std::string_view field,
        const std::string& value) const {
        
        if (value.empty()) {
            return std::unexpected(ParsingError::EmptyColumn);
        }

        auto field_str = std::string(field);
        auto it = converters.find(field_str);
        if (it != converters.end()) {
            try {
                return it->second(value);
            } catch (const std::exception&) {
                return std::unexpected(ParsingError::InvalidNumber);
            } catch (...) {
                return std::unexpected(ParsingError::UnknownError);
            }
        }
        return std::unexpected(ParsingError::ConfigurationError);
    }
};

// ========== Type Conversion Utilities ==========

/**
 * @brief Generic converter factory for creating std::any converters
 * 
 * Since the existing StringToDouble, StringToUInt64 etc. return std::optional,
 * they cannot be used directly with ColumnMapping converters which expect
 * std::function<std::any(const std::string&)>.
 * 
 * Users should provide lambda converters in ColumnMapping, such as:
 * 
 * Example converters:
 * ```cpp
 * // For uint64_t values
 * [](const std::string& s) -> std::any { return std::stoull(s); }
 * 
 * // For double values
 * [](const std::string& s) -> std::any { return std::stod(s); }
 * 
 * // For string values (identity)
 * [](const std::string& s) -> std::any { return s; }
 * 
 * // For int32_t values
 * [](const std::string& s) -> std::any { return static_cast<int32_t>(std::stol(s)); }
 * ```
 * 
 * These converters are embedded directly in the ColumnMapping:
 * ```cpp
 * auto mapping = ColumnMapping{
 *     .field_to_column = {{"timestamp_ns", 0}, {"accel_x", 1}, {"accel_y", 2}},
 *     .converters = {
 *         {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
 *         {"accel_x", [](const std::string& s) -> std::any { return std::stod(s); }},
 *         {"accel_y", [](const std::string& s) -> std::any { return std::stod(s); }}
 *     }
 * };
 * ```
 */

// Helper converters (not conflicting with existing functions)
namespace converters {

/**
 * @brief Create a converter for uint64_t values
 * @return Lambda function that converts string to uint64_t via std::any
 */
inline auto MakeUInt64Converter() {
    return [](const std::string& s) -> std::any { 
        return std::stoull(s); 
    };
}

/**
 * @brief Create a converter for int64_t values
 * @return Lambda function that converts string to int64_t via std::any
 */
inline auto MakeInt64Converter() {
    return [](const std::string& s) -> std::any { 
        return std::stoll(s); 
    };
}

/**
 * @brief Create a converter for uint32_t values
 * @return Lambda function that converts string to uint32_t via std::any
 */
inline auto MakeUInt32Converter() {
    return [](const std::string& s) -> std::any { 
        return static_cast<uint32_t>(std::stoul(s)); 
    };
}

/**
 * @brief Create a converter for int32_t values
 * @return Lambda function that converts string to int32_t via std::any
 */
inline auto MakeInt32Converter() {
    return [](const std::string& s) -> std::any { 
        return static_cast<int32_t>(std::stol(s)); 
    };
}

/**
 * @brief Create a converter for double values
 * @return Lambda function that converts string to double via std::any
 */
inline auto MakeDoubleConverter() {
    return [](const std::string& s) -> std::any { 
        return std::stod(s); 
    };
}

/**
 * @brief Create a converter for float values
 * @return Lambda function that converts string to float via std::any
 */
inline auto MakeFloatConverter() {
    return [](const std::string& s) -> std::any { 
        return std::stof(s); 
    };
}

/**
 * @brief Create an identity converter for string values
 * @return Lambda function that returns string as-is via std::any
 */
inline auto MakeStringConverter() {
    return [](const std::string& s) -> std::any { 
        return s; 
    };
}

} // namespace converters

// ========== Generic Row Parser ==========

/**
 * @brief Parse CSV row into typed data structure
 * 
 * Generic implementation that works for any data type.
 * No sensor-specific logic. This is the core of the generalized CSV parser.
 * 
 * @tparam T Output data type (user-defined)
 * @param row_values CSV column values (one per column)
 * @param mapping Column mapping (field→column, field→converter)
 * @param builder Function to construct T from extracted fields
 *                Must return std::expected<T, ParsingError>
 * 
 * @return Message<T> on success, ParsingError on failure
 * 
 * Example usage:
 * ```cpp
 * struct AccelerometerRecord {
 *     uint64_t timestamp_ns;
 *     float x, y, z;
 * };
 * 
 * auto mapping = ColumnMapping{...};
 * 
 * auto result = ParseRowGeneric<AccelerometerRecord>(
 *     row_values,
 *     mapping,
 *     [](const std::map<std::string, std::any>& fields) 
 *         -> std::expected<AccelerometerRecord, ParsingError> {
 *         try {
 *             return AccelerometerRecord{
 *                 .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
 *                 .x = static_cast<float>(std::any_cast<double>(fields.at("accel_x"))),
 *                 .y = static_cast<float>(std::any_cast<double>(fields.at("accel_y"))),
 *                 .z = static_cast<float>(std::any_cast<double>(fields.at("accel_z")))
 *             };
 *         } catch (const std::bad_any_cast&) {
 *             return std::unexpected(ParsingError::TypeMismatch);
 *         }
 *     }
 * );
 * 
 * if (result) {
 *     auto message = result.value();
 *     auto record = message.get<AccelerometerRecord>();
 *     // Use record...
 * } else {
 *     LOG_ERROR("Parse failed: " << static_cast<int>(result.error()));
 * }
 * ```
 */
template<typename T>
std::expected<graph::message::Message, ParsingError> ParseRowGeneric(
    const std::vector<std::string>& row_values,
    const ColumnMapping& mapping,
    std::function<std::expected<T, ParsingError>(const std::map<std::string, std::any>&)> builder) 
noexcept {
    try {
        std::map<std::string, std::any> extracted_fields;

        // Extract all mapped fields from row values
        for (const auto& [field, col_idx] : mapping.field_to_column) {
            if (col_idx >= row_values.size()) {
                return std::unexpected(ParsingError::InsufficientColumns);
            }

            auto converted = mapping.ConvertValue(field, row_values[col_idx]);
            if (!converted) {
                return std::unexpected(converted.error());
            }

            extracted_fields[field] = converted.value();
        }

        // Build target type from extracted fields using user-provided builder
        auto data = builder(extracted_fields);
        if (!data) {
            return std::unexpected(data.error());
        }

        // Wrap in Message<T> and return
        return graph::message::Message(data.value());

    } catch (const std::exception&) {
        return std::unexpected(ParsingError::UnknownError);
    }
}

}  // namespace csv
