/**
 * @file CSVParser.cpp
 * @brief GraphX source file.
 */

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

#include "csv/CSVParser.hpp"

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <map>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <log4cxx/logger.h>

#include "graph/IDataInjectionSource.hpp"
#include "csv/CSVDataInjectionManager.hpp"

static log4cxx::LoggerPtr g_logger = log4cxx::Logger::getLogger("graph.CSV.Parser");

namespace csv {

// ========== Utility Functions ==========

/// Trim whitespace from both ends of string
std::string Trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        ++start;
    }
    auto end = str.end();
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

/// Convert string to double, return nullopt if invalid
std::optional<double> StringToDouble(const std::string& str) {
    if (str.empty()) {
        return std::nullopt;
    }
    try {
        size_t idx = 0;
        double value = std::stod(str, &idx);
        // Ensure entire string was consumed
        if (idx != str.length()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

/// Convert string to uint64, return nullopt if invalid
std::optional<uint64_t> StringToUInt64(const std::string& str) {
    if (str.empty()) {
        return std::nullopt;
    }
    try {
        size_t idx = 0;
        uint64_t value = std::stoull(str, &idx);
        if (idx != str.length()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ========== File I/O ==========

/**
 * @brief Read csv file.
 * @param path Parameter for read csv file.
 */
std::vector<std::string> ReadCSVFile(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG4CXX_ERROR(g_logger, "Failed to open CSV file: " << path);
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    if (!file.eof() && file.bad()) {
        LOG4CXX_WARN(g_logger, "Error reading CSV file: " << path);
    }

    LOG4CXX_TRACE(g_logger, "Read " << lines.size() << " lines from " << path);
    return lines;
}

// ========== Header Parsing ==========

/**
 * @brief Parse header.
 * @param header_line Parameter for parse header.
 */
CSVHeader ParseHeader(const std::string& header_line) {
    CSVHeader header;
    header.columns = SplitCSVLine(header_line);
    header.format = "unified";  // Default, will be updated by DetectFormat

    // Find timestamp column
    for (size_t i = 0; i < header.columns.size(); ++i) {
        if (header.columns[i] == "timestamp_ns") {
            header.timestamp_column = i;
            break;
        }
    }

    // Data columns are all columns except timestamp
    for (size_t i = 0; i < header.columns.size(); ++i) {
        if (i != header.timestamp_column && header.columns[i] != "data_type") {
            header.data_columns.push_back(i);
        }
    }

    LOG4CXX_TRACE(g_logger, "Parsed header with " << header.columns.size() 
                                                   << " columns, timestamp at " 
                                                   << header.timestamp_column);
    return header;
}

/**
 * @brief Detect format.
 * @param header Parameter for detect format.
 */
std::string DetectFormat(const CSVHeader& header) {
    // Consolidated format has "data_type" column
    for (const auto& col : header.columns) {
        if (col == "data_type") {
            return "consolidated";
        }
    }
    return "unified";
}

// ========== Row Splitting ==========

/**
 * @brief Split csv line.
 * @param line Parameter for split csv line.
 */
std::vector<std::string> SplitCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string value;

    while (std::getline(ss, value, ',')) {
        result.push_back(Trim(value));
    }

    return result;
}

//timestamp_us,accel_x_ms2,accel_y_ms2,accel_z_ms2,gyro_x_rads,gyro_y_rads,gyro_z_rads,mag_x_ut,
// mag_y_ut,mag_z_ut,baro_pa,baro_temp_c,gps_lat_deg,gps_lon_deg,gps_alt_m,gps_speed_ms,gps_valid,gps_sats

// ========== Validation ==========

bool ValidateCSVRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {

    // Must have enough columns
    size_t required_columns = config.timestamp_column;
    if (!config.data_columns.empty()) {
        required_columns = std::max(required_columns, *std::max_element(
            config.data_columns.begin(), config.data_columns.end())) + 1;
    }

    if (row_values.size() < required_columns) {
        return false;
    }

    // Timestamp must be non-empty
    if (row_values[config.timestamp_column].empty()) {
        return false;
    }

    // At least one data column must be non-empty
    bool has_data = false;
    for (size_t col_idx : config.data_columns) {
        if (col_idx < row_values.size() && !row_values[col_idx].empty()) {
            has_data = true;
            break;
        }
    }

    return has_data;
}

// ========== C++26 Expected<> Implementations ==========

std::expected<sensors::SensorPayload, ParsingError> ParseAccelerometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    auto ts_opt = StringToUInt64(row_values[config.timestamp_column]);
    if (!ts_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid timestamp in accelerometer row");
        return std::unexpected(ParsingError::InvalidNumber);
    }
    if (config.data_columns.size() < 3) {
        LOG4CXX_TRACE(g_logger, "Accelerometer row missing data columns");
        return std::unexpected(ParsingError::InsufficientColumns);
    }

    auto x_opt = StringToDouble(row_values[config.data_columns[0]]);
    auto y_opt = StringToDouble(row_values[config.data_columns[1]]);
    auto z_opt = StringToDouble(row_values[config.data_columns[2]]);
    if (!x_opt || !y_opt || !z_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid acceleration values in row");
        return std::unexpected(ParsingError::InvalidNumber);
    }

    sensors::AccelerometerData data(sensors::Vector3D(
        static_cast<float>(*x_opt),
        static_cast<float>(*y_opt),
        static_cast<float>(*z_opt)));
    data.SetTimestamp(std::chrono::nanoseconds{*ts_opt});
    return sensors::SensorPayload(data);
}

std::expected<sensors::SensorPayload, ParsingError> ParseGyroscopeRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    auto ts_opt = StringToUInt64(row_values[config.timestamp_column]);
    if (!ts_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid timestamp in gyroscope row");
        return std::unexpected(ParsingError::InvalidNumber);
    }
    if (config.data_columns.size() < 3) {
        LOG4CXX_TRACE(g_logger, "Gyroscope row missing data columns");
        return std::unexpected(ParsingError::InsufficientColumns);
    }

    auto x_opt = StringToDouble(row_values[config.data_columns[0]]);
    auto y_opt = StringToDouble(row_values[config.data_columns[1]]);
    auto z_opt = StringToDouble(row_values[config.data_columns[2]]);
    if (!x_opt || !y_opt || !z_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid angular velocity values in row");
        return std::unexpected(ParsingError::InvalidNumber);
    }

    sensors::GyroscopeData data(
        static_cast<float>(*x_opt),
        static_cast<float>(*y_opt),
        static_cast<float>(*z_opt));
    data.SetTimestamp(std::chrono::nanoseconds{*ts_opt});
    return sensors::SensorPayload(data);
}

std::expected<sensors::SensorPayload, ParsingError> ParseGPSPositionRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    auto ts_opt = StringToUInt64(row_values[config.timestamp_column]);
    if (!ts_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid timestamp in GPS row");
        return std::unexpected(ParsingError::InvalidNumber);
    }
    if (config.data_columns.size() < 6) {
        LOG4CXX_TRACE(g_logger, "GPS row missing data columns");
        return std::unexpected(ParsingError::InsufficientColumns);
    }

    auto lat_opt = StringToDouble(row_values[config.data_columns[0]]);
    auto lon_opt = StringToDouble(row_values[config.data_columns[1]]);
    auto alt_opt = StringToDouble(row_values[config.data_columns[2]]);
    auto speed_opt = StringToDouble(row_values[config.data_columns[3]]);
    auto valid_opt = StringToUInt64(row_values[config.data_columns[4]]);
    auto sats_opt = StringToUInt64(row_values[config.data_columns[5]]);
    if (!lat_opt || !lon_opt || !alt_opt || !speed_opt || !valid_opt || !sats_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid GPS position values in row");
        return std::unexpected(ParsingError::InvalidNumber);
    }

    sensors::GPSPositionData data(
        *lat_opt,
        *lon_opt,
        *alt_opt,
        0.0f,
        0.0f,
        *speed_opt,
        0.0f,
        static_cast<uint8_t>(*sats_opt),
        (*valid_opt != 0));
    data.SetTimestamp(std::chrono::nanoseconds{*ts_opt});
    return sensors::SensorPayload(data);
}

std::expected<sensors::SensorPayload, ParsingError> ParseBarometricRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    auto ts_opt = StringToUInt64(row_values[config.timestamp_column]);
    if (!ts_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid timestamp in barometric row");
        return std::unexpected(ParsingError::InvalidNumber);
    }
    if (config.data_columns.size() < 2) {
        LOG4CXX_TRACE(g_logger, "Barometric row missing data columns");
        return std::unexpected(ParsingError::InsufficientColumns);
    }

    auto pressure_opt = StringToDouble(row_values[config.data_columns[0]]);
    auto temp_opt = StringToDouble(row_values[config.data_columns[1]]);
    if (!pressure_opt || !temp_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid barometric values in row");
        return std::unexpected(ParsingError::InvalidNumber);
    }

    sensors::BarometricData data(
        static_cast<float>(*pressure_opt),
        static_cast<float>(*temp_opt));
    data.SetTimestamp(std::chrono::nanoseconds{*ts_opt});
    return sensors::SensorPayload(data);
}

std::expected<sensors::SensorPayload, ParsingError> ParseMagnetometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    auto ts_opt = StringToUInt64(row_values[config.timestamp_column]);
    if (!ts_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid timestamp in magnetometer row");
        return std::unexpected(ParsingError::InvalidNumber);
    }
    if (config.data_columns.size() < 3) {
        LOG4CXX_TRACE(g_logger, "Magnetometer row missing data columns");
        return std::unexpected(ParsingError::InsufficientColumns);
    }

    auto x_opt = StringToDouble(row_values[config.data_columns[0]]);
    auto y_opt = StringToDouble(row_values[config.data_columns[1]]);
    auto z_opt = StringToDouble(row_values[config.data_columns[2]]);
    if (!x_opt || !y_opt || !z_opt) {
        LOG4CXX_TRACE(g_logger, "Invalid magnetic field values in row");
        return std::unexpected(ParsingError::InvalidNumber);
    }

    sensors::MagnetometerData data(
        static_cast<float>(*x_opt),
        static_cast<float>(*y_opt),
        static_cast<float>(*z_opt));
    data.SetTimestamp(std::chrono::nanoseconds{*ts_opt});
    return sensors::SensorPayload(data);
}

std::expected<sensors::SensorPayload, ParsingError> ParseRowUnifiedExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    if (!ValidateCSVRow(row_values, config)) {
        return std::unexpected(ParsingError::EmptyColumn);
    }
    LOG4CXX_TRACE(g_logger, "Unified CSV row parsing is not configured for this node");
    return std::unexpected(ParsingError::UnknownError);
}

}  // namespace csv
