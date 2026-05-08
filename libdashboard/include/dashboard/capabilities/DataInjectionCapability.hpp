#pragma once

#include <string>
#include <memory>
#include <map>
#include "sensor/SensorClassificationType.hpp"
#include "core/ActiveQueue.hpp"
#include "sensor/SensorDataTypes.hpp"


namespace app::capabilities {

/**
 * @struct DataInjectionNodeConfig
 * @brief Per-node Data Injection configuration
 *
 * 
 * Holds information about nodes that support data injection,
 * including their injection queues and sensor classification.
 */

struct DataInjectionNodeConfig {
    /// Index of the node in the graph  
    size_t node_index;  
    /// Type of the node
    std::string node_type;
    /// Name of the node
    std::string node_name;        
    /// Sensor classification type if available
    sensors::SensorClassificationType sensor_classification_type = sensors::SensorClassificationType::UNKNOWN;
    /// Pointer to the injection queue
    core::ActiveQueue<sensors::SensorPayload>* injection_queue;
};

class DataInjectionCapability {
public:
    DataInjectionCapability() = default;    
    virtual ~DataInjectionCapability() = default;
    
    void RegisterDataInjectionNodeConfig(const DataInjectionNodeConfig& config) {
        data_injection_node_configs_[config.node_name] = config;
    }

    void DisableAllInjectionQueues() {
        for (auto& [node_name, config] : data_injection_node_configs_) {
            if (config.injection_queue) {
                config.injection_queue->Disable();
                config.injection_queue = nullptr;
            }
        }
    }   

    std::map<std::string, app::capabilities::DataInjectionNodeConfig> GetDataInjectionNodeConfigs() const {
        return data_injection_node_configs_;
    }
    
private:

    /// Data Injection Capable nodes
    std::map<std::string, app::capabilities::DataInjectionNodeConfig> data_injection_node_configs_;
};

}  // namespace app::capabilities
