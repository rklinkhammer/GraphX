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

#include "sensor/CSVParserCompat.hpp"
#include "csv/CSVDataInjectionManager.hpp"
#include <chrono>
#include <cmath>

namespace csv::compat {

// ========== Internal Data Structures ==========

/**
 * @brief Intermediate data structure for accelerometer parsing
 * 
 * Used to bridge from parsed CSV values (std::any) to SensorPayload.
 * This structure is only used internally during parsing, then discarded.
 */
struct AccelerometerRecord {
    uint64_t timestamp_ns;
    float x_mss, y_mss, z_mss;
};

/**
 * @brief Intermediate data structure for gyroscope parsing
 */
struct GyroscopeRecord {
    uint64_t timestamp_ns;
    float x_rads, y_rads, z_rads;
};

/**
 * @brief Intermediate data structure for GPS parsing
 */
struct GPSRecord {
    uint64_t timestamp_ns;
    float latitude_deg, longitude_deg, altitude_m;
    float speed_ms;
    uint32_t sats;
};

/**
 * @brief Intermediate data structure for barometric parsing
 */
struct BarometricRecord {
    uint64_t timestamp_ns;
    float pressure_pa, temperature_c, altitude_m;
};

/**
 * @brief Intermediate data structure for magnetometer parsing
 */
struct MagnetometerRecord {
    uint64_t timestamp_ns;
    float x_ut, y_ut, z_ut;
};

// ========== Mapping Creators ==========

ColumnMapping CreateAccelerometerMapping(const csv::CSVNodeConfig& config) {
    return ColumnMapping{
        .field_to_column = {
            {"timestamp_ns", config.timestamp_column},
            {"x_mss", config.data_columns[0]},
            {"y_mss", config.data_columns[1]},
            {"z_mss", config.data_columns[2]}
        },
        .converters = {
            {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
            {"x_mss", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"y_mss", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"z_mss", [](const std::string& s) -> std::any { return std::stod(s); }}
        }
    };
}

ColumnMapping CreateGyroscopeMapping(const csv::CSVNodeConfig& config) {
    return ColumnMapping{
        .field_to_column = {
            {"timestamp_ns", config.timestamp_column},
            {"x_rads", config.data_columns[0]},
            {"y_rads", config.data_columns[1]},
            {"z_rads", config.data_columns[2]}
        },
        .converters = {
            {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
            {"x_rads", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"y_rads", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"z_rads", [](const std::string& s) -> std::any { return std::stod(s); }}
        }
    };
}

ColumnMapping CreateGPSMapping(const csv::CSVNodeConfig& config) {
    return ColumnMapping{
        .field_to_column = {
            {"timestamp_ns", config.timestamp_column},
            {"latitude_deg", config.data_columns[0]},
            {"longitude_deg", config.data_columns[1]},
            {"altitude_m", config.data_columns[2]},
            {"speed_ms", config.data_columns[3]},
            {"sats", config.data_columns[4]}
        },
        .converters = {
            {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
            {"latitude_deg", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"longitude_deg", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"altitude_m", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"speed_ms", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"sats", [](const std::string& s) -> std::any { return static_cast<uint32_t>(std::stoul(s)); }}
        }
    };
}

ColumnMapping CreateBarometricMapping(const csv::CSVNodeConfig& config) {
    return ColumnMapping{
        .field_to_column = {
            {"timestamp_ns", config.timestamp_column},
            {"pressure_pa", config.data_columns[0]},
            {"temperature_c", config.data_columns[1]},
            {"altitude_m", config.data_columns[2]}
        },
        .converters = {
            {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
            {"pressure_pa", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"temperature_c", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"altitude_m", [](const std::string& s) -> std::any { return std::stod(s); }}
        }
    };
}

ColumnMapping CreateMagnetometerMapping(const csv::CSVNodeConfig& config) {
    return ColumnMapping{
        .field_to_column = {
            {"timestamp_ns", config.timestamp_column},
            {"x_ut", config.data_columns[0]},
            {"y_ut", config.data_columns[1]},
            {"z_ut", config.data_columns[2]}
        },
        .converters = {
            {"timestamp_ns", [](const std::string& s) -> std::any { return std::stoull(s); }},
            {"x_ut", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"y_ut", [](const std::string& s) -> std::any { return std::stod(s); }},
            {"z_ut", [](const std::string& s) -> std::any { return std::stod(s); }}
        }
    };
}

// ========== Builder Functions ==========

/**
 * @brief Build AccelerometerRecord from parsed fields
 */
static std::expected<AccelerometerRecord, ParsingError> BuildAccelRecord(
    const std::map<std::string, std::any>& fields) {
    try {
        return AccelerometerRecord{
            .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
            .x_mss = static_cast<float>(std::any_cast<double>(fields.at("x_mss"))),
            .y_mss = static_cast<float>(std::any_cast<double>(fields.at("y_mss"))),
            .z_mss = static_cast<float>(std::any_cast<double>(fields.at("z_mss")))
        };
    } catch (const std::bad_any_cast&) {
        return std::unexpected(ParsingError::InvalidNumber);
    } catch (const std::out_of_range&) {
        return std::unexpected(ParsingError::MissingRequiredColumns);
    }
}

/**
 * @brief Build GyroscopeRecord from parsed fields
 */
static std::expected<GyroscopeRecord, ParsingError> BuildGyroRecord(
    const std::map<std::string, std::any>& fields) {
    try {
        return GyroscopeRecord{
            .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
            .x_rads = static_cast<float>(std::any_cast<double>(fields.at("x_rads"))),
            .y_rads = static_cast<float>(std::any_cast<double>(fields.at("y_rads"))),
            .z_rads = static_cast<float>(std::any_cast<double>(fields.at("z_rads")))
        };
    } catch (const std::bad_any_cast&) {
        return std::unexpected(ParsingError::InvalidNumber);
    } catch (const std::out_of_range&) {
        return std::unexpected(ParsingError::MissingRequiredColumns);
    }
}

/**
 * @brief Build GPSRecord from parsed fields
 */
static std::expected<GPSRecord, ParsingError> BuildGPSRecord(
    const std::map<std::string, std::any>& fields) {
    try {
        return GPSRecord{
            .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
            .latitude_deg = static_cast<float>(std::any_cast<double>(fields.at("latitude_deg"))),
            .longitude_deg = static_cast<float>(std::any_cast<double>(fields.at("longitude_deg"))),
            .altitude_m = static_cast<float>(std::any_cast<double>(fields.at("altitude_m"))),
            .speed_ms = static_cast<float>(std::any_cast<double>(fields.at("speed_ms"))),
            .sats = std::any_cast<uint32_t>(fields.at("sats"))
        };
    } catch (const std::bad_any_cast&) {
        return std::unexpected(ParsingError::InvalidNumber);
    } catch (const std::out_of_range&) {
        return std::unexpected(ParsingError::MissingRequiredColumns);
    }
}

/**
 * @brief Build BarometricRecord from parsed fields
 */
static std::expected<BarometricRecord, ParsingError> BuildBarometricRecord(
    const std::map<std::string, std::any>& fields) {
    try {
        return BarometricRecord{
            .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
            .pressure_pa = static_cast<float>(std::any_cast<double>(fields.at("pressure_pa"))),
            .temperature_c = static_cast<float>(std::any_cast<double>(fields.at("temperature_c"))),
            .altitude_m = static_cast<float>(std::any_cast<double>(fields.at("altitude_m")))
        };
    } catch (const std::bad_any_cast&) {
        return std::unexpected(ParsingError::InvalidNumber);
    } catch (const std::out_of_range&) {
        return std::unexpected(ParsingError::MissingRequiredColumns);
    }
}

/**
 * @brief Build MagnetometerRecord from parsed fields
 */
static std::expected<MagnetometerRecord, ParsingError> BuildMagnetometerRecord(
    const std::map<std::string, std::any>& fields) {
    try {
        return MagnetometerRecord{
            .timestamp_ns = std::any_cast<uint64_t>(fields.at("timestamp_ns")),
            .x_ut = static_cast<float>(std::any_cast<double>(fields.at("x_ut"))),
            .y_ut = static_cast<float>(std::any_cast<double>(fields.at("y_ut"))),
            .z_ut = static_cast<float>(std::any_cast<double>(fields.at("z_ut")))
        };
    } catch (const std::bad_any_cast&) {
        return std::unexpected(ParsingError::InvalidNumber);
    } catch (const std::out_of_range&) {
        return std::unexpected(ParsingError::MissingRequiredColumns);
    }
}

// ========== Conversion Functions ==========

/**
 * @brief Convert parsed record to SensorPayload (AccelerometerData)
 */
static sensors::SensorPayload ConvertAccelToPayload(const AccelerometerRecord& rec) {
    sensors::AccelerometerData data(
        sensors::Vector3D(rec.x_mss, rec.y_mss, rec.z_mss)
    );
    data.SetTimestamp(std::chrono::nanoseconds{rec.timestamp_ns});
    return sensors::SensorPayload(data);
}

/**
 * @brief Convert parsed record to SensorPayload (GyroscopeData)
 */
static sensors::SensorPayload ConvertGyroToPayload(const GyroscopeRecord& rec) {
    sensors::GyroscopeData data(
        sensors::Vector3D(rec.x_rads, rec.y_rads, rec.z_rads)
    );
    data.SetTimestamp(std::chrono::nanoseconds{rec.timestamp_ns});
    return sensors::SensorPayload(data);
}

/**
 * @brief Convert parsed record to SensorPayload (GPSPositionData)
 */
static sensors::SensorPayload ConvertGPSToPayload(const GPSRecord& rec) {
    sensors::GPSPositionData data(rec.latitude_deg, rec.longitude_deg, rec.altitude_m);
    data.ground_speed = rec.speed_ms;
    data.num_satellites = rec.sats;
    data.SetTimestamp(std::chrono::nanoseconds{rec.timestamp_ns});
    return sensors::SensorPayload(data);
}

/**
 * @brief Convert parsed record to SensorPayload (BarometricData)
 */
static sensors::SensorPayload ConvertBarometricToPayload(const BarometricRecord& rec) {
    // Convert temperature from Celsius to Kelvin
    float temp_k = rec.temperature_c + 273.15f;
    sensors::BarometricData data(rec.pressure_pa, temp_k);
    data.SetTimestamp(std::chrono::nanoseconds{rec.timestamp_ns});
    return sensors::SensorPayload(data);
}

/**
 * @brief Convert parsed record to SensorPayload (MagnetometerData)
 */
static sensors::SensorPayload ConvertMagnetometerToPayload(const MagnetometerRecord& rec) {
    sensors::MagnetometerData data(
        sensors::Vector3D(rec.x_ut, rec.y_ut, rec.z_ut)
    );
    data.SetTimestamp(std::chrono::nanoseconds{rec.timestamp_ns});
    return sensors::SensorPayload(data);
}

// ========== Public Backward Compatibility Wrapper Functions ==========

std::optional<sensors::SensorPayload> ParseAccelerometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateAccelerometerMapping(config);
    auto result = csv::ParseRowGeneric<AccelerometerRecord>(
        row_values,
        mapping,
        BuildAccelRecord
    );
    
    if (!result) {
        return std::nullopt;
    }
    
    auto record = result.value().get<AccelerometerRecord>();
    return ConvertAccelToPayload(record);
}

std::optional<sensors::SensorPayload> ParseGyroscopeRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateGyroscopeMapping(config);
    auto result = csv::ParseRowGeneric<GyroscopeRecord>(
        row_values,
        mapping,
        BuildGyroRecord
    );
    
    if (!result) {
        return std::nullopt;
    }
    
    auto record = result.value().get<GyroscopeRecord>();
    return ConvertGyroToPayload(record);
}

std::optional<sensors::SensorPayload> ParseGPSPositionRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateGPSMapping(config);
    auto result = csv::ParseRowGeneric<GPSRecord>(
        row_values,
        mapping,
        BuildGPSRecord
    );
    
    if (!result) {
        return std::nullopt;
    }
    
    auto record = result.value().get<GPSRecord>();
    return ConvertGPSToPayload(record);
}

std::optional<sensors::SensorPayload> ParseBarometricRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateBarometricMapping(config);
    auto result = csv::ParseRowGeneric<BarometricRecord>(
        row_values,
        mapping,
        BuildBarometricRecord
    );
    
    if (!result) {
        return std::nullopt;
    }
    
    auto record = result.value().get<BarometricRecord>();
    return ConvertBarometricToPayload(record);
}

std::optional<sensors::SensorPayload> ParseMagnetometerRow(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateMagnetometerMapping(config);
    auto result = csv::ParseRowGeneric<MagnetometerRecord>(
        row_values,
        mapping,
        BuildMagnetometerRecord
    );
    
    if (!result) {
        return std::nullopt;
    }
    
    auto record = result.value().get<MagnetometerRecord>();
    return ConvertMagnetometerToPayload(record);
}

// ========== C++26 Expected<> API Wrappers ==========

std::expected<sensors::SensorPayload, ParsingError> ParseAccelerometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateAccelerometerMapping(config);
    auto result = csv::ParseRowGeneric<AccelerometerRecord>(
        row_values,
        mapping,
        BuildAccelRecord
    );
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto record = result.value().get<AccelerometerRecord>();
    return ConvertAccelToPayload(record);
}

std::expected<sensors::SensorPayload, ParsingError> ParseGyroscopeRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateGyroscopeMapping(config);
    auto result = csv::ParseRowGeneric<GyroscopeRecord>(
        row_values,
        mapping,
        BuildGyroRecord
    );
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto record = result.value().get<GyroscopeRecord>();
    return ConvertGyroToPayload(record);
}

std::expected<sensors::SensorPayload, ParsingError> ParseGPSPositionRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateGPSMapping(config);
    auto result = csv::ParseRowGeneric<GPSRecord>(
        row_values,
        mapping,
        BuildGPSRecord
    );
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto record = result.value().get<GPSRecord>();
    return ConvertGPSToPayload(record);
}

std::expected<sensors::SensorPayload, ParsingError> ParseBarometricRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateBarometricMapping(config);
    auto result = csv::ParseRowGeneric<BarometricRecord>(
        row_values,
        mapping,
        BuildBarometricRecord
    );
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto record = result.value().get<BarometricRecord>();
    return ConvertBarometricToPayload(record);
}

std::expected<sensors::SensorPayload, ParsingError> ParseMagnetometerRowExpected(
    const std::vector<std::string>& row_values,
    const csv::CSVNodeConfig& config) {
    
    auto mapping = CreateMagnetometerMapping(config);
    auto result = csv::ParseRowGeneric<MagnetometerRecord>(
        row_values,
        mapping,
        BuildMagnetometerRecord
    );
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto record = result.value().get<MagnetometerRecord>();
    return ConvertMagnetometerToPayload(record);
}

} // namespace csv::compat
