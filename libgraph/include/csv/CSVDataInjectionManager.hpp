/**
 * @file CSVDataInjectionManager.hpp
 * @brief GraphX source file.
 */

// MIT License
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

#include <memory>
#include <vector>
#include <map>
#include <string>
#include <log4cxx/logger.h>
#include "graph/IDataInjectionSource.hpp"
#include "graph/Message.hpp"
#include "core/ActiveQueue.hpp"
#include "csv/CSVParser.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/DataInjectionCapability.hpp"


namespace csv {

struct CSVNodeConfig {
    std::string node_name;
    //sensors::SensorClassificationType sensor_classification_type;
    int timestamp_column;
    std::vector<size_t> data_columns;  // sensor field -> CSV column index
    core::ActiveQueue<graph::message::Message>* injection_queue;
};

/**
 * @class CSVDataInjectionManager
 * @brief CSVDataInjectionManager class.
 */
/**
 * @class CSVDataInjectionManager
 * @brief Csv data injection manager implementation for GraphX.
 */
class CSVDataInjectionManager {
public:

    CSVDataInjectionManager() = default;
    virtual ~CSVDataInjectionManager() = default;

/**
 * @brief Process csv inputs.
 * @param std::vector<std::pair<std::string Parameter for process csv inputs.
 * @param csv_input_paths Parameter for process csv inputs.
 * @param context Parameter for process csv inputs.
 * @return Result of the operation.
 */
    bool ProcessCSVInputs(std::vector<std::pair<std::string, std::string>>& csv_input_paths, capabilities::GraphCapability& context);
    bool BindCSVColumnsToDataInjectionNodes(
            const std::map<std::string, capabilities::DataInjectionNodeConfig>& node_configs,
            capabilities::GraphCapability& context);
/**
 * @brief Inject row to nodes.
 * @param context Parameter for inject row to nodes.
 * @return Result of the operation.
 */
    bool InjectRowToNodes(capabilities::GraphCapability& context);

private:
// timestamp_us,accel_x_ms2,accel_y_ms2,accel_z_ms2,gyro_x_rads,
// gyro_y_rads,gyro_z_rads,mag_x_ut,mag_y_ut,mag_z_ut,baro_pa,baro_temp_c,
// gps_lat_deg,gps_lon_deg,gps_alt_m,gps_speed_ms,gps_valid,gps_sats
//
//    std::map<std::string, sensors::SensorClassificationType > csv_type_mapping_ = {
        // {"accel_x_ms2", sensors::SensorClassificationType::ACCELEROMETER},
        // {"accel_y_ms2", sensors::SensorClassificationType::ACCELEROMETER},
        // {"accel_z_ms2", sensors::SensorClassificationType::ACCELEROMETER},
        // {"gyro_x_rads", sensors::SensorClassificationType::GYROSCOPE},
        // {"gyro_y_rads", sensors::SensorClassificationType::GYROSCOPE},
        // {"gyro_z_rads", sensors::SensorClassificationType::GYROSCOPE},
        // {"mag_x_ut", sensors::SensorClassificationType::MAGNETOMETER},
        // {"mag_y_ut", sensors::SensorClassificationType::MAGNETOMETER},
        // {"mag_z_ut", sensors::SensorClassificationType::MAGNETOMETER},
        // {"baro_pa", sensors::SensorClassificationType::BAROMETRIC},
        // {"baro_temp_c", sensors::SensorClassificationType::BAROMETRIC},
        // {"gps_lat_deg", sensors::SensorClassificationType::GPS_POSITION},
        // {"gps_lon_deg", sensors::SensorClassificationType::GPS_POSITION},
        // {"gps_alt_m", sensors::SensorClassificationType::GPS_POSITION},
        // {"gps_speed_ms", sensors::SensorClassificationType::GPS_POSITION},
        // {"gps_valid", sensors::SensorClassificationType::GPS_POSITION},
        // {"gps_sats", sensors::SensorClassificationType::GPS_POSITION}
   // };
    std::vector<csv::CSVHeader> csv_headers_;
    std::vector<std::vector<std::string>> csv_data_;
    std::vector<std::string> csv_format_;
    std::map<std::string, CSVNodeConfig> csv_node_configs_;
    size_t current_row_index_ = 1;

};

}  // namespace csv
