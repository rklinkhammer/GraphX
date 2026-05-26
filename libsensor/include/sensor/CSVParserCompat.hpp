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

/**
 * @file libsensor/include/sensor/CSVParserCompat.hpp
 * @brief Backward compatibility adapters for sensor-specific CSV parsing
 * 
 * This header provides sensor-specific CSV parsing functions that delegate to
 * the generic csv::ParseRowGeneric<T> function in libgraph.
 * 
 * Deprecated: Use csv::ParseRowGeneric<T> with custom data structures instead.
 * 
 * Architecture:
 * - libgraph provides: Generic CSV parsing infrastructure (csv::ParseRowGeneric<T>)
 * - libsensor provides: Sensor-specific adapters for backward compatibility
 * 
 * This separation allows:
 * - libgraph to remain sensor-agnostic and reusable
 * - libsensor to contain all sensor-specific logic
 * - New code to use libgraph directly with any data type
 * - Legacy code to continue using sensor-specific functions unchanged
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <expected>
#include <cstddef>
#include <map>
#include <any>

#include "config/DataTypes.hpp"
#include "csv/CSVParser.hpp"
#include "graph/IDataInjectionSource.hpp"

namespace csv {

/**
 * @namespace csv::compat
 * @brief Backward compatibility layer for sensor-specific CSV parsing
 * 
 * This namespace contains deprecated sensor-specific parsing functions
 * that maintain backward compatibility with existing code.
 * 
 * All functions delegate to csv::ParseRowGeneric<T> with appropriate
 * data structures and builders.
 * 
 * Example (deprecated but still supported):
 * ```cpp
 * auto result = csv::compat::ParseAccelerometerRow(values, config);
 * if (result) {
 *     auto payload = result.value();
 *     // Use SensorPayload...
 * }
 * ```
 * 
 * Recommended new approach:
 * ```cpp
 * struct AccelRecord { uint64_t ts; float x, y, z; };
 * auto mapping = csv::ColumnMapping{...};
 * auto result = csv::ParseRowGeneric<AccelRecord>(values, mapping, builder);
 * auto message = result.value();
 * auto record = message.get<AccelRecord>();
 * ```
 */
namespace compat {

/**
 * @brief Internal helper: Create ColumnMapping from CSVNodeConfig for accelerometer
 * 
 * Maps config column indices to generic column mapping format.
 * 
 * @param config CSV node configuration with column indices
 * @return ColumnMapping for accelerometer data (timestamp + x,y,z acceleration)
 */
ColumnMapping CreateAccelerometerMapping(const csv::CSVNodeConfig& config);

/**
 * @brief Internal helper: Create ColumnMapping from CSVNodeConfig for gyroscope
 * 
 * @param config CSV node configuration with column indices
 * @return ColumnMapping for gyroscope data (timestamp + x,y,z angular velocity)
 */
ColumnMapping CreateGyroscopeMapping(const csv::CSVNodeConfig& config);

/**
 * @brief Internal helper: Create ColumnMapping from CSVNodeConfig for GPS
 * 
 * @param config CSV node configuration with column indices
 * @return ColumnMapping for GPS data (timestamp + lat,lon,alt,speed,sats)
 */
ColumnMapping CreateGPSMapping(const csv::CSVNodeConfig& config);

/**
 * @brief Internal helper: Create ColumnMapping from CSVNodeConfig for barometric
 * 
 * @param config CSV node configuration with column indices
 * @return ColumnMapping for barometric data (timestamp + pressure,temp,alt)
 */
ColumnMapping CreateBarometricMapping(const csv::CSVNodeConfig& config);

/**
 * @brief Internal helper: Create ColumnMapping from CSVNodeConfig for magnetometer
 * 
 * @param config CSV node configuration with column indices
 * @return ColumnMapping for magnetometer data (timestamp + x,y,z magnetic field)
 */
ColumnMapping CreateMagnetometerMapping(const csv::CSVNodeConfig& config);

// ========== Backward Compatibility Wrappers ==========

/**
 * @brief [DEPRECATED] Parse accelerometer data from CSV row
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Maintained for backward compatibility. Implementation delegates to
 * csv::ParseRowGeneric<AccelerometerRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration (column indices, etc.)
 * @return SensorPayload with AccelerometerData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseAccelerometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse gyroscope data from CSV row
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Maintained for backward compatibility. Implementation delegates to
 * csv::ParseRowGeneric<GyroscopeRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with GyroscopeData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseGyroscopeRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse GPS position data from CSV row
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Maintained for backward compatibility. Implementation delegates to
 * csv::ParseRowGeneric<GPSRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with GPSPositionData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseGPSPositionRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse barometric data from CSV row
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Maintained for backward compatibility. Implementation delegates to
 * csv::ParseRowGeneric<BarometricRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with BarometricData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseBarometricRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse magnetometer data from CSV row
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Maintained for backward compatibility. Implementation delegates to
 * csv::ParseRowGeneric<MagnetometerRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload with MagnetometerData, or nullopt if parse failed
 */
std::optional<sensors::SensorPayload> ParseMagnetometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

// ========== C++26 Expected<> API (also deprecated, use ParseRowGeneric instead) ==========

/**
 * @brief [DEPRECATED] Parse accelerometer data with error handling (C++26)
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Type-safe alternative to ParseAccelerometerRow().
 * Delegates to csv::ParseRowGeneric<AccelerometerRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseAccelerometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse gyroscope data with error handling (C++26)
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Type-safe alternative to ParseGyroscopeRow().
 * Delegates to csv::ParseRowGeneric<GyroscopeRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseGyroscopeRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse GPS position data with error handling (C++26)
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Type-safe alternative to ParseGPSPositionRow().
 * Delegates to csv::ParseRowGeneric<GPSRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseGPSPositionRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse barometric data with error handling (C++26)
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Type-safe alternative to ParseBarometricRow().
 * Delegates to csv::ParseRowGeneric<BarometricRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseBarometricRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

/**
 * @brief [DEPRECATED] Parse magnetometer data with error handling (C++26)
 * 
 * @deprecated Use csv::ParseRowGeneric<T> with custom data structure instead
 * 
 * Type-safe alternative to ParseMagnetometerRow().
 * Delegates to csv::ParseRowGeneric<MagnetometerRecord>.
 * 
 * @param row_values CSV column values
 * @param config CSV configuration
 * @return SensorPayload on success, ParsingError on failure
 */
std::expected<sensors::SensorPayload, ParsingError> ParseMagnetometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config);

} // namespace compat
} // namespace csv
