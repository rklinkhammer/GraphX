#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "csv/CSVParser.hpp"
#include "config/DataTypes.hpp"
#include "graph/Message.hpp"

namespace fs = std::filesystem;

/**
 * @class CSVIntegrationTest
 * @brief Integration tests for CSV parsing with actual CSV files
 * 
 * Tests the generalized CSVParser with real-world data, validating:
 * - Column mapping and parsing
 * - Type conversion accuracy
 * - Message<T> integration
 * - Error handling with malformed data
 */
class CSVIntegrationTest : public ::testing::Test {
protected:
    /// Get the test data directory path
    static fs::path GetTestDataDir() {
        // Build directory contains test data
        fs::path test_dir = fs::path(__FILE__).parent_path() / "data";
        return test_dir;
    }
    
    /// Read CSV file and return lines
    static std::vector<std::string> ReadCSVFile(const fs::path& filepath) {
        std::vector<std::string> lines;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath.string());
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    }
    
    /// Parse CSV line into tokens (simple comma-separated parsing)
    static std::vector<std::string> ParseCSVLine(const std::string& line) {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        return tokens;
    }
};

// ─────────────────────────────────────────────────────────────────────────
// Generic API Tests (ParseRowGeneric<T>)
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test ParseAccelerometerGeneric
 * @brief Test generic parsing of accelerometer CSV data
 */
TEST_F(CSVIntegrationTest, ParseAccelerometerGeneric) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "accelerometer_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2) << "CSV file should have header and data rows";
    
    // Parse header
    auto headers = ParseCSVLine(lines[0]);
    ASSERT_EQ(headers.size(), 4);  // timestamp_ns, accel_x_mss, accel_y_mss, accel_z_mss
    
    // Create column mapping
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["accel_x_mss"] = 1;
    mapping.field_to_column["accel_y_mss"] = 2;
    mapping.field_to_column["accel_z_mss"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["accel_x_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_y_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_z_mss"] = csv::converters::MakeFloatConverter();
    
    // Test parsing first data row
    auto row_values = ParseCSVLine(lines[1]);
    ASSERT_EQ(row_values.size(), 4);
    
    // Define builder for accelerometer record
    struct AccelRecord {
        uint64_t timestamp_ns;
        float x_mss, y_mss, z_mss;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<AccelRecord, csv::ParsingError> {
        try {
            AccelRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<float>(fields.at("accel_x_mss")),
                std::any_cast<float>(fields.at("accel_y_mss")),
                std::any_cast<float>(fields.at("accel_z_mss"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<AccelRecord>(row_values, mapping, builder);
    ASSERT_TRUE(result.has_value()) << "Parsing should succeed";
    
    auto msg = result.value();
    auto accel_rec = msg.get<AccelRecord>();
    EXPECT_EQ(accel_rec.timestamp_ns, 1000000000ULL);
    EXPECT_FLOAT_EQ(accel_rec.x_mss, 0.5f);
    EXPECT_FLOAT_EQ(accel_rec.y_mss, 1.2f);
    EXPECT_FLOAT_EQ(accel_rec.z_mss, -9.81f);
}

/**
 * @test ParseGyroscopeGeneric
 * @brief Test generic parsing of gyroscope CSV data
 */
TEST_F(CSVIntegrationTest, ParseGyroscopeGeneric) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "gyroscope_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
    auto headers = ParseCSVLine(lines[0]);
    ASSERT_EQ(headers.size(), 4);  // timestamp_ns, gyro_x_rads, gyro_y_rads, gyro_z_rads
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["gyro_x_rads"] = 1;
    mapping.field_to_column["gyro_y_rads"] = 2;
    mapping.field_to_column["gyro_z_rads"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["gyro_x_rads"] = csv::converters::MakeFloatConverter();
    mapping.converters["gyro_y_rads"] = csv::converters::MakeFloatConverter();
    mapping.converters["gyro_z_rads"] = csv::converters::MakeFloatConverter();
    
    auto row_values = ParseCSVLine(lines[1]);
    ASSERT_EQ(row_values.size(), 4);
    
    struct GyroRecord {
        uint64_t timestamp_ns;
        float x_rads, y_rads, z_rads;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<GyroRecord, csv::ParsingError> {
        try {
            GyroRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<float>(fields.at("gyro_x_rads")),
                std::any_cast<float>(fields.at("gyro_y_rads")),
                std::any_cast<float>(fields.at("gyro_z_rads"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<GyroRecord>(row_values, mapping, builder);
    ASSERT_TRUE(result.has_value());
    
    auto msg = result.value();
    auto gyro_rec = msg.get<GyroRecord>();
    EXPECT_EQ(gyro_rec.timestamp_ns, 1000000000ULL);
    EXPECT_FLOAT_EQ(gyro_rec.x_rads, 0.01f);
    EXPECT_FLOAT_EQ(gyro_rec.y_rads, 0.02f);
    EXPECT_FLOAT_EQ(gyro_rec.z_rads, -0.01f);
}

/**
 * @test ParseGPSGeneric
 * @brief Test generic parsing of GPS CSV data
 */
TEST_F(CSVIntegrationTest, ParseGPSGeneric) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "gps_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
    auto headers = ParseCSVLine(lines[0]);
    ASSERT_EQ(headers.size(), 6);  // timestamp_ns, lat, lon, alt, speed, sats
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["latitude_deg"] = 1;
    mapping.field_to_column["longitude_deg"] = 2;
    mapping.field_to_column["altitude_m"] = 3;
    mapping.field_to_column["speed_ms"] = 4;
    mapping.field_to_column["num_satellites"] = 5;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["latitude_deg"] = csv::converters::MakeDoubleConverter();
    mapping.converters["longitude_deg"] = csv::converters::MakeDoubleConverter();
    mapping.converters["altitude_m"] = csv::converters::MakeFloatConverter();
    mapping.converters["speed_ms"] = csv::converters::MakeFloatConverter();
    mapping.converters["num_satellites"] = csv::converters::MakeUInt32Converter();
    
    auto row_values = ParseCSVLine(lines[1]);
    ASSERT_EQ(row_values.size(), 6);
    
    struct GPSRecord {
        uint64_t timestamp_ns;
        double latitude_deg, longitude_deg;
        float altitude_m, speed_ms;
        uint32_t num_satellites;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<GPSRecord, csv::ParsingError> {
        try {
            GPSRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<double>(fields.at("latitude_deg")),
                std::any_cast<double>(fields.at("longitude_deg")),
                std::any_cast<float>(fields.at("altitude_m")),
                std::any_cast<float>(fields.at("speed_ms")),
                std::any_cast<uint32_t>(fields.at("num_satellites"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<GPSRecord>(row_values, mapping, builder);
    ASSERT_TRUE(result.has_value());
    
    auto msg = result.value();
    auto gps_rec = msg.get<GPSRecord>();
    EXPECT_EQ(gps_rec.timestamp_ns, 1000000000ULL);
    EXPECT_DOUBLE_EQ(gps_rec.latitude_deg, 37.7749);
    EXPECT_DOUBLE_EQ(gps_rec.longitude_deg, -122.4194);
    EXPECT_FLOAT_EQ(gps_rec.altitude_m, 10.5f);
    EXPECT_FLOAT_EQ(gps_rec.speed_ms, 2.3f);
    EXPECT_EQ(gps_rec.num_satellites, 12U);
}

/**
 * @test ParseBarometricGeneric
 * @brief Test generic parsing of barometric CSV data
 */
TEST_F(CSVIntegrationTest, ParseBarometricGeneric) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "barometric_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["pressure_pa"] = 1;
    mapping.field_to_column["temperature_c"] = 2;
    mapping.field_to_column["altitude_m"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["pressure_pa"] = csv::converters::MakeFloatConverter();
    mapping.converters["temperature_c"] = csv::converters::MakeFloatConverter();
    mapping.converters["altitude_m"] = csv::converters::MakeFloatConverter();
    
    auto row_values = ParseCSVLine(lines[1]);
    
    struct BaroRecord {
        uint64_t timestamp_ns;
        float pressure_pa, temperature_c, altitude_m;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<BaroRecord, csv::ParsingError> {
        try {
            BaroRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<float>(fields.at("pressure_pa")),
                std::any_cast<float>(fields.at("temperature_c")),
                std::any_cast<float>(fields.at("altitude_m"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<BaroRecord>(row_values, mapping, builder);
    ASSERT_TRUE(result.has_value());
    
    auto msg = result.value();
    auto baro_rec = msg.get<BaroRecord>();
    EXPECT_EQ(baro_rec.timestamp_ns, 1000000000ULL);
    EXPECT_FLOAT_EQ(baro_rec.pressure_pa, 101325.0f);
    EXPECT_FLOAT_EQ(baro_rec.temperature_c, 20.0f);
    EXPECT_FLOAT_EQ(baro_rec.altitude_m, 0.0f);
}

/**
 * @test ParseMagnetometerGeneric
 * @brief Test generic parsing of magnetometer CSV data
 */
TEST_F(CSVIntegrationTest, ParseMagnetometerGeneric) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "magnetometer_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["mag_x_ut"] = 1;
    mapping.field_to_column["mag_y_ut"] = 2;
    mapping.field_to_column["mag_z_ut"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["mag_x_ut"] = csv::converters::MakeFloatConverter();
    mapping.converters["mag_y_ut"] = csv::converters::MakeFloatConverter();
    mapping.converters["mag_z_ut"] = csv::converters::MakeFloatConverter();
    
    auto row_values = ParseCSVLine(lines[1]);
    
    struct MagRecord {
        uint64_t timestamp_ns;
        float x_ut, y_ut, z_ut;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<MagRecord, csv::ParsingError> {
        try {
            MagRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<float>(fields.at("mag_x_ut")),
                std::any_cast<float>(fields.at("mag_y_ut")),
                std::any_cast<float>(fields.at("mag_z_ut"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<MagRecord>(row_values, mapping, builder);
    ASSERT_TRUE(result.has_value());
    
    auto msg = result.value();
    auto mag_rec = msg.get<MagRecord>();
    EXPECT_EQ(mag_rec.timestamp_ns, 1000000000ULL);
    EXPECT_FLOAT_EQ(mag_rec.x_ut, 25.3f);
    EXPECT_FLOAT_EQ(mag_rec.y_ut, 12.1f);
    EXPECT_FLOAT_EQ(mag_rec.z_ut, 45.2f);
}

// ─────────────────────────────────────────────────────────────────────────
// Error Handling Tests
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test MalformedCSVHandlesGracefully
 * @brief Test error handling for malformed CSV data
 */
TEST_F(CSVIntegrationTest, MalformedCSVHandlesGracefully) {
    csv::ColumnMapping mapping;
    mapping.field_to_column["value"] = 0;
    mapping.converters["value"] = csv::converters::MakeUInt64Converter();
    
    // Invalid number format
    std::vector<std::string> bad_row = {"not_a_number"};
    
    struct SimpleRecord {
        uint64_t value;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<SimpleRecord, csv::ParsingError> {
        try {
            SimpleRecord rec{std::any_cast<uint64_t>(fields.at("value"))};
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<SimpleRecord>(bad_row, mapping, builder);
    EXPECT_FALSE(result.has_value()) << "Should fail on invalid number";
}

/**
 * @test MissingColumnsHandlesGracefully
 * @brief Test error handling for missing columns
 */
TEST_F(CSVIntegrationTest, MissingColumnsHandlesGracefully) {
    csv::ColumnMapping mapping;
    mapping.field_to_column["col1"] = 0;
    mapping.field_to_column["col2"] = 1;
    mapping.converters["col1"] = csv::converters::MakeUInt64Converter();
    mapping.converters["col2"] = csv::converters::MakeUInt64Converter();
    
    // Row with fewer columns than expected
    std::vector<std::string> incomplete_row = {"123"};  // Only one column
    
    struct TwoColumnRecord {
        uint64_t col1, col2;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<TwoColumnRecord, csv::ParsingError> {
        try {
            // This will fail because col2 won't be in the fields map
            TwoColumnRecord rec{
                std::any_cast<uint64_t>(fields.at("col1")),
                std::any_cast<uint64_t>(fields.at("col2"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto result = csv::ParseRowGeneric<TwoColumnRecord>(incomplete_row, mapping, builder);
    EXPECT_FALSE(result.has_value()) << "Should fail on missing column";
}
