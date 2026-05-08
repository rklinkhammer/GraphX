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

#include "sensor/SensorDataTypes.hpp"
#include "graph/IDataInjectionSource.hpp"

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

// ========== Sensor-Specific Row Parsers ==========

/**
 * @brief Parse accelerometer data from CSV row
 *
 * Extracts timestamp and 3D acceleration from row values.
 * Creates AccelerometerData and wraps in SensorPayload variant.
 *
 * Expected columns: timestamp_ns, accel_x_mss, accel_y_mss, accel_z_mss
 * Format: Unified (one value per column)
 *
 * Error Handling:
 * - Returns nullopt if any column is empty
 * - Returns nullopt if values cannot be parsed as numbers
 * - Timestamp converted from ns to seconds (divide by 1e9)
 *
 * @param row_values CSV column values
 * @param config CSV configuration (column indices, etc.)
 * @return SensorPayload with AccelerometerData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseAccelerometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse gyroscope data from CSV row
 *
 * Extracts timestamp and 3D angular velocity from row values.
 * Creates GyroscopeData and wraps in SensorPayload variant.
 *
 * Expected columns: timestamp_ns, gyro_x_rads, gyro_y_rads, gyro_z_rads
 * Format: Unified (one value per column)
 *
 * Error Handling: Same as ParseAccelerometerRow
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with GyroscopeData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseGyroscopeRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse GPS position data from CSV row
 *
 * Extracts timestamp and position (lat, lon, alt) from row values.
 * Creates GPSPositionData and wraps in SensorPayload variant.
 *
 * Expected columns: timestamp_ns, latitude_deg, longitude_deg, altitude_m
 * Format: Unified (one value per column)
 *
 * Error Handling: Same as ParseAccelerometerRow
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with GPSPositionData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseGPSPositionRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse barometric data from CSV row
 *
 * Extracts timestamp and atmospheric measurements from row values.
 * Creates BarometricData and wraps in SensorPayload variant.
 *
 * Expected columns: timestamp_ns, pressure_pa, temperature_c, altitude_m
 * Format: Unified (one value per column)
 *
 * Error Handling: Same as ParseAccelerometerRow
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with BarometricData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseBarometricRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse magnetometer data from CSV row
 *
 * Extracts timestamp and 3D magnetic field from row values.
 * Creates MagnetometerData and wraps in SensorPayload variant.
 *
 * Expected columns: timestamp_ns, mag_x_ut, mag_y_ut, mag_z_ut
 * Format: Unified (one value per column)
 *
 * Error Handling: Same as ParseAccelerometerRow
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with MagnetometerData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseMagnetometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

// ========== Format-Specific Dispatch ==========

/**
 * @brief Parse row in unified format
 *
 * Unified format: Each CSV file contains one sensor type.
 * Dispatches to appropriate sensor parser based on config.type_identifier.
 *
 * Example unified CSV:
 * ```
 * timestamp_ns,accel_x_mss,accel_y_mss,accel_z_mss
 * 1000000000,0.1,0.2,0.3
 * 2000000000,0.2,0.3,0.4
 * ```
 *
 * @param row_values CSV column values
 * @param config CSV configuration (contains type_identifier)
 * @return Parsed SensorPayload, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseRowUnified(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse row in consolidated format
 *
 * Consolidated format: Single CSV file with all sensor types.
 * Uses "data_type" column to determine which sensor parser to use.
 *
 * Example consolidated CSV:
 * ```
 * timestamp_ns,data_type,accel_x_mss,accel_y_mss,accel_z_mss,gyro_x_rads,gyro_y_rads,gyro_z_rads,...
 * 1000000000,accel,0.1,0.2,0.3,,,,
 * 2000000000,gyro,,,,0.01,0.02,0.03
 * ```
 *
 * Each row specifies which sensor type the data belongs to.
 *
 * @param row_values CSV column values
 * @param config CSV configuration (contains column indices)
 * @return Parsed SensorPayload, or nullopt if parse failed or unknown type
 */
std::optional<sensors::SensorPayload> ParseRowConsolidated(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

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
 * Type-safe alternative to ParseAccelerometerRow().
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
 * Type-safe alternative to ParseGyroscopeRow().
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
 * Type-safe alternative to ParseGPSPositionRow().
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
 * Type-safe alternative to ParseBarometricRow().
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
 * Type-safe alternative to ParseMagnetometerRow().
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
 * Type-safe alternative to ParseRowUnified().
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseRowUnifiedExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief Parse row in consolidated format with error handling (C++26)
 *
 * Type-safe alternative to ParseRowConsolidated().
 * See ParseAccelerometerRowExpected() for usage examples.
 *
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseRowConsolidatedExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

}  // namespace csv
