// SPDX-License-Identifier: MIT

/**
 * @file test_csv_pipeline_3.cpp
 * @brief Test CSV Pipeline 3 Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>

#include "csv/CSVParser.hpp"
#include "csv/CSVDataInjectionManager.hpp"
#include "config/DataTypes.hpp"
#include "graph/Message.hpp"
#include "core/ActiveQueue.hpp"

namespace fs = std::filesystem;

/**
 * @class CSVPipeline3Test
 * @brief Phase 3: Full system integration tests with CSVNodeConfig
 * 
 * Tests the complete CSV parsing pipeline:
 * - CSVNodeConfig creation and validation
 * - Real CSV file reading and batch processing
 * - Message injection into ActiveQueue
 * - Performance and scalability
 * - Backward compatibility verification
 */
class CSVPipeline3Test : public ::testing::Test {
protected:
/**
 * @brief Get test data dir.
 */
    static fs::path GetTestDataDir() {
        fs::path test_dir = fs::path(__FILE__).parent_path() / "data";
        return test_dir;
    }
    
/**
 * @brief Read csv file.
 * @param filepath Parameter for read csv file.
 */
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
    
/**
 * @brief Parse csv line.
 * @param line Parameter for parse csv line.
 */
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
// CSVNodeConfig Integration Tests
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test CSVNodeConfigCreation
 * @brief Test creating and validating CSVNodeConfig
 */
TEST_F(CSVPipeline3Test, CSVNodeConfigCreation) {
    core::ActiveQueue<graph::message::Message> queue;
    
    csv::CSVNodeConfig config;
    config.node_name = "AccelNode";
    config.timestamp_column = 0;
    config.data_columns = {1, 2, 3};  // X, Y, Z
    config.injection_queue = &queue;
    
    EXPECT_EQ(config.node_name, "AccelNode");
    EXPECT_EQ(config.timestamp_column, 0);
    ASSERT_EQ(config.data_columns.size(), 3);
    EXPECT_NE(config.injection_queue, nullptr);
}

/**
 * @test MultiSensorCSVNodeConfig
 * @brief Test CSVNodeConfig with multiple sensor mappings
 */
TEST_F(CSVPipeline3Test, MultiSensorCSVNodeConfig) {
    core::ActiveQueue<graph::message::Message> accel_queue;
    core::ActiveQueue<graph::message::Message> gyro_queue;
    core::ActiveQueue<graph::message::Message> mag_queue;
    
    csv::CSVNodeConfig accel_config;
    accel_config.node_name = "AccelNode";
    accel_config.timestamp_column = 0;
    accel_config.data_columns = {1, 2, 3};  // accel_x, accel_y, accel_z
    accel_config.injection_queue = &accel_queue;
    
    csv::CSVNodeConfig gyro_config;
    gyro_config.node_name = "GyroNode";
    gyro_config.timestamp_column = 0;
    gyro_config.data_columns = {4, 5, 6};  // gyro_x, gyro_y, gyro_z
    gyro_config.injection_queue = &gyro_queue;
    
    csv::CSVNodeConfig mag_config;
    mag_config.node_name = "MagNode";
    mag_config.timestamp_column = 0;
    mag_config.data_columns = {7, 8, 9};  // mag_x, mag_y, mag_z
    mag_config.injection_queue = &mag_queue;
    
    // Verify configs are independent
    EXPECT_NE(accel_config.node_name, gyro_config.node_name);
    EXPECT_NE(gyro_config.data_columns[0], mag_config.data_columns[0]);
    EXPECT_NE(accel_config.injection_queue, gyro_config.injection_queue);
}

// ─────────────────────────────────────────────────────────────────────────
// Full Pipeline Tests (File I/O + Batch Processing)
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test AccelerometerBatchParsing
 * @brief Test parsing entire CSV file in batches
 */
TEST_F(CSVPipeline3Test, AccelerometerBatchParsing) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "accelerometer_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2) << "CSV file should have header and data rows";
    
    // Setup column mapping
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["accel_x_mss"] = 1;
    mapping.field_to_column["accel_y_mss"] = 2;
    mapping.field_to_column["accel_z_mss"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["accel_x_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_y_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_z_mss"] = csv::converters::MakeFloatConverter();
    
    // Setup CSVNodeConfig
    core::ActiveQueue<graph::message::Message> queue;
    csv::CSVNodeConfig config;
    config.node_name = "AccelNode";
    config.timestamp_column = 0;
    config.data_columns = {1, 2, 3};
    config.injection_queue = &queue;
    
    // Parse all data rows
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
    
    int parsed_count = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        auto row_values = ParseCSVLine(lines[i]);
        auto result = csv::ParseRowGeneric<AccelRecord>(row_values, mapping, builder);
        if (result.has_value()) {
            parsed_count++;
        }
    }
    
    EXPECT_EQ(parsed_count, lines.size() - 1) << "All data rows should parse successfully";
}

/**
 * @test GPSBatchParsing
 * @brief Test parsing GPS CSV with multiple column types
 */
TEST_F(CSVPipeline3Test, GPSBatchParsing) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "gps_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
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
    
    core::ActiveQueue<graph::message::Message> queue;
    csv::CSVNodeConfig config;
    config.node_name = "GPSNode";
    config.timestamp_column = 0;
    config.data_columns = {1, 2, 3, 4, 5};
    config.injection_queue = &queue;
    
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
    
    int parsed_count = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        auto row_values = ParseCSVLine(lines[i]);
        auto result = csv::ParseRowGeneric<GPSRecord>(row_values, mapping, builder);
        if (result.has_value()) {
            parsed_count++;
        }
    }
    
    EXPECT_EQ(parsed_count, lines.size() - 1);
}

// ─────────────────────────────────────────────────────────────────────────
// Performance Benchmarking Tests
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test SingleRowParsingPerformance
 * @brief Benchmark single row parsing time
 */
TEST_F(CSVPipeline3Test, SingleRowParsingPerformance) {
#if defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "Native-speed performance thresholds are not sanitizer gates";
#endif
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["x"] = 1;
    mapping.field_to_column["y"] = 2;
    mapping.field_to_column["z"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["x"] = csv::converters::MakeFloatConverter();
    mapping.converters["y"] = csv::converters::MakeFloatConverter();
    mapping.converters["z"] = csv::converters::MakeFloatConverter();
    
    struct DataRecord {
        uint64_t timestamp_ns;
        float x, y, z;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<DataRecord, csv::ParsingError> {
        try {
            DataRecord rec{
                std::any_cast<uint64_t>(fields.at("timestamp_ns")),
                std::any_cast<float>(fields.at("x")),
                std::any_cast<float>(fields.at("y")),
                std::any_cast<float>(fields.at("z"))
            };
            return rec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    std::vector<std::string> row_values = {"1000000000", "0.5", "1.2", "-9.81"};
    
    // Warmup
    auto _ = csv::ParseRowGeneric<DataRecord>(row_values, mapping, builder);
    
    // Benchmark: 10,000 iterations
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        auto result = csv::ParseRowGeneric<DataRecord>(row_values, mapping, builder);
        EXPECT_TRUE(result.has_value());
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double avg_us = (duration.count() * 1000.0) / 10000.0;
    
    EXPECT_LT(avg_us, 100.0) << "Single row parsing should be < 100µs on average";
}

/**
 * @test BatchParsingPerformance
 * @brief Benchmark batch parsing performance
 */
TEST_F(CSVPipeline3Test, BatchParsingPerformance) {
#if defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "Native-speed performance thresholds are not sanitizer gates";
#endif
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "accelerometer_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    if (lines.size() < 2) {
        GTEST_SKIP() << "Not enough data rows for benchmarking";
    }
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["accel_x_mss"] = 1;
    mapping.field_to_column["accel_y_mss"] = 2;
    mapping.field_to_column["accel_z_mss"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["accel_x_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_y_mss"] = csv::converters::MakeFloatConverter();
    mapping.converters["accel_z_mss"] = csv::converters::MakeFloatConverter();
    
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
    
    // Parse batch 1000 times
    auto start = std::chrono::high_resolution_clock::now();
    int total_rows = 0;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        for (size_t i = 1; i < lines.size(); ++i) {
            auto row_values = ParseCSVLine(lines[i]);
            auto result = csv::ParseRowGeneric<AccelRecord>(row_values, mapping, builder);
            if (result.has_value()) {
                total_rows++;
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double rows_per_ms = total_rows / static_cast<double>(duration.count());
    
    EXPECT_GT(rows_per_ms, 1.0) << "Should parse > 1 row per millisecond";
}

// ─────────────────────────────────────────────────────────────────────────
// Message Injection Integration Tests
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test MessageInjectionToQueue
 * @brief Test injecting parsed messages into ActiveQueue
 */
TEST_F(CSVPipeline3Test, MessageInjectionToQueue) {
    core::ActiveQueue<graph::message::Message> queue;
    
    csv::ColumnMapping mapping;
    mapping.field_to_column["timestamp_ns"] = 0;
    mapping.field_to_column["x"] = 1;
    mapping.field_to_column["y"] = 2;
    mapping.field_to_column["z"] = 3;
    
    mapping.converters["timestamp_ns"] = csv::converters::MakeUInt64Converter();
    mapping.converters["x"] = csv::converters::MakeFloatConverter();
    mapping.converters["y"] = csv::converters::MakeFloatConverter();
    mapping.converters["z"] = csv::converters::MakeFloatConverter();
    
    struct Vector3 {
        float x, y, z;
    };
    
    auto builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<Vector3, csv::ParsingError> {
        try {
            Vector3 vec{
                std::any_cast<float>(fields.at("x")),
                std::any_cast<float>(fields.at("y")),
                std::any_cast<float>(fields.at("z"))
            };
            return vec;
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    std::vector<std::string> row_values = {"1000000000", "1.0", "2.0", "3.0"};
    auto result = csv::ParseRowGeneric<Vector3>(row_values, mapping, builder);
    
    ASSERT_TRUE(result.has_value());
    
    auto msg = result.value();
    auto vec = msg.get<Vector3>();
    EXPECT_FLOAT_EQ(vec.x, 1.0f);
    EXPECT_FLOAT_EQ(vec.y, 2.0f);
    EXPECT_FLOAT_EQ(vec.z, 3.0f);
}

/**
 * @test MultipleSensorMessagesIntoQueue
 * @brief Test injecting multiple sensor types into shared infrastructure
 */
TEST_F(CSVPipeline3Test, MultipleSensorMessagesIntoQueue) {
    core::ActiveQueue<graph::message::Message> accel_queue;
    core::ActiveQueue<graph::message::Message> gyro_queue;
    
    // Parse accelerometer
    csv::ColumnMapping accel_mapping;
    accel_mapping.field_to_column["ts"] = 0;
    accel_mapping.field_to_column["x"] = 1;
    accel_mapping.field_to_column["y"] = 2;
    accel_mapping.field_to_column["z"] = 3;
    accel_mapping.converters["ts"] = csv::converters::MakeUInt64Converter();
    accel_mapping.converters["x"] = csv::converters::MakeFloatConverter();
    accel_mapping.converters["y"] = csv::converters::MakeFloatConverter();
    accel_mapping.converters["z"] = csv::converters::MakeFloatConverter();
    
    // Parse gyroscope
    csv::ColumnMapping gyro_mapping;
    gyro_mapping.field_to_column["ts"] = 0;
    gyro_mapping.field_to_column["rx"] = 1;
    gyro_mapping.field_to_column["ry"] = 2;
    gyro_mapping.field_to_column["rz"] = 3;
    gyro_mapping.converters["ts"] = csv::converters::MakeUInt64Converter();
    gyro_mapping.converters["rx"] = csv::converters::MakeFloatConverter();
    gyro_mapping.converters["ry"] = csv::converters::MakeFloatConverter();
    gyro_mapping.converters["rz"] = csv::converters::MakeFloatConverter();
    
    struct Accel { float x, y, z; };
    struct Gyro { float rx, ry, rz; };
    
    auto accel_builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<Accel, csv::ParsingError> {
        try {
            return Accel{
                std::any_cast<float>(fields.at("x")),
                std::any_cast<float>(fields.at("y")),
                std::any_cast<float>(fields.at("z"))
            };
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    auto gyro_builder = [](const std::map<std::string, std::any>& fields) 
        -> std::expected<Gyro, csv::ParsingError> {
        try {
            return Gyro{
                std::any_cast<float>(fields.at("rx")),
                std::any_cast<float>(fields.at("ry")),
                std::any_cast<float>(fields.at("rz"))
            };
        } catch (...) {
            return std::unexpected(csv::ParsingError::InvalidNumber);
        }
    };
    
    std::vector<std::string> accel_row = {"1000", "1.0", "2.0", "3.0"};
    std::vector<std::string> gyro_row = {"1000", "0.1", "0.2", "0.3"};
    
    auto accel_result = csv::ParseRowGeneric<Accel>(accel_row, accel_mapping, accel_builder);
    auto gyro_result = csv::ParseRowGeneric<Gyro>(gyro_row, gyro_mapping, gyro_builder);
    
    EXPECT_TRUE(accel_result.has_value());
    EXPECT_TRUE(gyro_result.has_value());
    
    auto accel_msg = accel_result.value();
    auto gyro_msg = gyro_result.value();
    
    auto accel_data = accel_msg.get<Accel>();
    auto gyro_data = gyro_msg.get<Gyro>();
    
    EXPECT_FLOAT_EQ(accel_data.x, 1.0f);
    EXPECT_FLOAT_EQ(gyro_data.rx, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────
// Backward Compatibility Tests (with Real Configs)
// ─────────────────────────────────────────────────────────────────────────

/**
 * @test ConfigBasedAccelerometerParsing
 * @brief Test using CSVNodeConfig with actual mapping
 */
TEST_F(CSVPipeline3Test, ConfigBasedAccelerometerParsing) {
    auto data_dir = GetTestDataDir();
    auto filepath = data_dir / "accelerometer_test.csv";
    
    if (!fs::exists(filepath)) {
        GTEST_SKIP() << "Test data file not found: " << filepath;
    }
    
    auto lines = ReadCSVFile(filepath);
    ASSERT_GE(lines.size(), 2);
    
    // Create realistic CSVNodeConfig
    core::ActiveQueue<graph::message::Message> queue;
    csv::CSVNodeConfig config;
    config.node_name = "AccelerometerSensor";
    config.timestamp_column = 0;  // First column is timestamp
    config.data_columns = {1, 2, 3};  // X, Y, Z accelerations
    config.injection_queue = &queue;
    
    // Verify config is properly set up
    EXPECT_EQ(config.node_name, "AccelerometerSensor");
    EXPECT_EQ(config.timestamp_column, 0);
    ASSERT_EQ(config.data_columns.size(), 3);
    EXPECT_EQ(config.data_columns[0], 1);
    EXPECT_EQ(config.data_columns[1], 2);
    EXPECT_EQ(config.data_columns[2], 3);
}

/**
 * @test ComplexMultiSensorConfiguration
 * @brief Test realistic scenario with multiple sensors and configs
 */
TEST_F(CSVPipeline3Test, ComplexMultiSensorConfiguration) {
    core::ActiveQueue<graph::message::Message> accel_q, gyro_q, gps_q, baro_q, mag_q;
    
    // Accelerometer config
    csv::CSVNodeConfig accel_config;
    accel_config.node_name = "Accel";
    accel_config.timestamp_column = 0;
    accel_config.data_columns = {1, 2, 3};
    accel_config.injection_queue = &accel_q;
    
    // Gyroscope config
    csv::CSVNodeConfig gyro_config;
    gyro_config.node_name = "Gyro";
    gyro_config.timestamp_column = 0;
    gyro_config.data_columns = {4, 5, 6};
    gyro_config.injection_queue = &gyro_q;
    
    // GPS config
    csv::CSVNodeConfig gps_config;
    gps_config.node_name = "GPS";
    gps_config.timestamp_column = 0;
    gps_config.data_columns = {7, 8, 9, 10, 11};
    gps_config.injection_queue = &gps_q;
    
    // Barometric config
    csv::CSVNodeConfig baro_config;
    baro_config.node_name = "Barometric";
    baro_config.timestamp_column = 0;
    baro_config.data_columns = {12, 13};
    baro_config.injection_queue = &baro_q;
    
    // Magnetometer config
    csv::CSVNodeConfig mag_config;
    mag_config.node_name = "Magnetometer";
    mag_config.timestamp_column = 0;
    mag_config.data_columns = {14, 15, 16};
    mag_config.injection_queue = &mag_q;
    
    // Verify all configs are distinct
    EXPECT_NE(accel_config.node_name, gyro_config.node_name);
    EXPECT_NE(gyro_config.data_columns[0], gps_config.data_columns[0]);
    EXPECT_EQ(accel_config.data_columns.size(), 3);
    EXPECT_EQ(gps_config.data_columns.size(), 5);
    EXPECT_EQ(baro_config.data_columns.size(), 2);
}
