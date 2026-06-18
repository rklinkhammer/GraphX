// SPDX-License-Identifier: MIT

/**
 * @file DataInjectionCapability.hpp
 * @brief Data Injection Capability Graph runtime support.
 *
 * @details Provides capability API used to share runtime services between policies, nodes, and executors. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <string>
#include <memory>
#include <map>
#include "core/ActiveQueue.hpp"
#include "graph/Message.hpp"
#include "config/DataTypes.hpp"


namespace capabilities {

/**
 * @struct DataInjectionNodeConfig
 * @brief Per-node Data Injection configuration
 *
 * 
 * Holds information about nodes that support data injection,
 * including their injection queues and sensor classification.
 */

/**

 * @struct DataInjectionNodeConfig

 * @brief Data Injection Node Config data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct DataInjectionNodeConfig {
    /// Index of the node in the graph  
    size_t node_index;  
    /// Type of the node
    std::string node_type;
    /// Name of the node
    std::string node_name;        
    /// Pointer to the injection queue
    core::ActiveQueue<graph::message::Message>* injection_queue;
};

/**
 * @class DataInjectionCapability
 * @brief Data Injection Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DataInjectionCapability {
public:
    /**
     * @brief Executes the Data Injection Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    DataInjectionCapability() = default;    
    /**
     * @brief Releases resources owned by Data Injection Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~DataInjectionCapability() = default;
    
    /**
     * @brief Updates or queries runtime registration through Register Data Injection Node Config.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param config Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void RegisterDataInjectionNodeConfig(const DataInjectionNodeConfig& config) {
        data_injection_node_configs_[config.node_name] = config;
    }

    /**
     * @brief Executes the Disable All Injection Queues operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void DisableAllInjectionQueues() {
        for (auto& [node_name, config] : data_injection_node_configs_) {
            if (config.injection_queue) {
                config.injection_queue->Disable();
                config.injection_queue = nullptr;
            }
        }
    }   

    /**
     * @brief Returns the Data Injection Node Configs.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::map<std::string, capabilities::DataInjectionNodeConfig> GetDataInjectionNodeConfigs() const {
        return data_injection_node_configs_;
    }
    
private:

    /// Data Injection Capable nodes
    std::map<std::string, capabilities::DataInjectionNodeConfig> data_injection_node_configs_;
};

}  // namespace capabilities
