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

#include "csv/CSVDataInjectionManager.hpp"
#include "graph/IDataInjectionSource.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "core/ActiveQueue.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/CapabilityDiscovery.hpp"

#include <log4cxx/logger.h>

static log4cxx::LoggerPtr g_logger = log4cxx::Logger::getLogger("graph.CSVDataInjectionManager");

namespace csv {

bool CSVDataInjectionManager::ProcessCSVInputs(
            std::vector<std::pair<std::string, std::string>>& csv_input_paths, 
            app::capabilities::GraphCapability& context) {

    LOG4CXX_TRACE(g_logger, "CSVDataInjectionManager::ProcessCSVInputs() - processing CSV inputs");
    (void)context;
    for (const auto& path_pair : csv_input_paths) {
        const std::string& file_path = path_pair.first;
        const std::string& node_name = path_pair.second;

        LOG4CXX_TRACE(g_logger, "Processing CSV input for node: " << node_name 
                         << " from file: " << file_path);

        auto lines = csv::ReadCSVFile(file_path);
        if (lines.empty()) {
            LOG4CXX_WARN(g_logger, "CSVDataInjectionManager::ProcessCSVInputs() - no data read from file: " 
                          << file_path);
            continue;
        }  
 
        csv_data_.push_back(lines);
        auto header = csv::ParseHeader(lines[0]);
        csv_headers_.push_back(header);
        csv_format_.push_back(csv::DetectFormat(header));
    }

    return true;
}

/*
 timestamp_us,accel_x_ms2,accel_y_ms2,accel_z_ms2,gyro_x_rads,gyro_y_rads,
 gyro_z_rads,mag_x_ut,mag_y_ut,mag_z_ut,baro_pa,baro_temp_c,
 gps_lat_deg,gps_lon_deg,gps_alt_m,gps_speed_ms,gps_valid,gps_sats

*/
bool CSVDataInjectionManager::BindCSVColumnsToDataInjectionNodes(
            const std::map<std::string, app::capabilities::DataInjectionNodeConfig>& node_configs,
            app::capabilities::GraphCapability& context) { 
    (void)context;
    for(auto& header : csv_headers_) {
        int col = 1;
        for(const auto& column_name : header.columns) {
            auto it = csv_type_mapping_.find(column_name);
            if(it == csv_type_mapping_.end()) {
                LOG4CXX_TRACE(g_logger, "No sensor type mapping for column: " << column_name);
                continue;
            }
            for(auto& [node_name, config] : node_configs) {
                if(config.sensor_classification_type != it->second) {
                    continue;
                }   
                auto it = csv_node_configs_.find(node_name);
                if(it == csv_node_configs_.end()) {
                    csv_node_configs_[node_name].timestamp_column = header.timestamp_column;
                    csv_node_configs_[node_name].sensor_classification_type = config.sensor_classification_type;
                    csv_node_configs_[node_name].node_name = node_name;
                    csv_node_configs_[node_name].injection_queue = config.injection_queue;
                }
                csv_node_configs_[node_name].data_columns.push_back(col);
            }
            col++;
        }
    }  
    for(auto& [node_name, config] : csv_node_configs_) {
        LOG4CXX_TRACE(g_logger, "Configured CSV node: " << node_name 
                         << " sensor type: " << static_cast<int>(config.sensor_classification_type)
                         << " timestamp column: " << config.timestamp_column
                         << " data columns: ");
        for(auto col : config.data_columns) {
            LOG4CXX_TRACE(g_logger, "   " << col);
        }
    }
    return true;          
}

bool CSVDataInjectionManager::InjectRowToNodes(app::capabilities::GraphCapability& context) {
    (void)context;
    auto line = csv_data_[0][current_row_index_];
    auto row_values = csv::SplitCSVLine(line);
    for(auto& [node_name, config] : csv_node_configs_) {
        auto payload_opt = csv::ParseRowUnified(row_values, config);
        if(!payload_opt) {
            LOG4CXX_WARN(g_logger, "CSVDataInjectionManager::InjectRowToNodes() - failed to parse row for node: " 
                          << node_name << " at row index: " << current_row_index_);
            continue;
        }
        // C++26: [[nodiscard]] return value intentionally unused (fire-and-forget data injection)
        static_cast<void>(config.injection_queue->Enqueue(*payload_opt));
        LOG4CXX_TRACE(g_logger, "Injected row index: " << current_row_index_ 
                         << " to node: " << node_name);
    }
    if(current_row_index_ + 1 >= csv_data_[0].size()) {
        LOG4CXX_TRACE(g_logger, "CSVDataInjectionManager::InjectRowToNodes() - reached end of CSV data");
        return false;
    }
    current_row_index_++;
    return true;
}

}  // namespace csv
