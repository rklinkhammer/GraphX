/**
 * @file GraphConfig.hpp
 * @brief Graph Config Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace graph {

/**
 * @struct NodeConfig
 * @brief Configuration for a single node in the graph
 *
 * Represents the declarative specification of a node from JSON,
 * before the node is instantiated.
 */
/**
 * @struct NodeConfig
 * @brief Node Config data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct NodeConfig {
    /// Unique node identifier (must be alphanumeric with underscores)
    std::string id;
    
    /// Node type name (e.g., "DataInjectionAccelerometerNode", "FlightFSMNode")
    std::string type;
    
    /// Human-readable name (optional)
    std::string name;
    
    /// Description (optional)
    std::string description;
    
    /// Node-specific configuration (type-dependent schema)
    nlohmann::json node_config;
    
    /// Per-port configuration overrides (optional)
    std::map<std::string, nlohmann::json> port_config;
    
    /**
     * Validate this node configuration
     * @return true if valid, false otherwise
     */
    bool Validate() const {
        // ID must be non-empty and alphanumeric with underscores
        if (id.empty()) {
            return false;
        }

        const bool is_letter = (id[0] >= 'a' && id[0] <= 'z') ||
                               (id[0] >= 'A' && id[0] <= 'Z') ||
                               id[0] == '_';
        if (!is_letter) {
            return false;
        }
        
        for (char c : id) {
            if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && 
                (c < '0' || c > '9') && c != '_') {
                return false;
            }
        }
        
        // Type must be non-empty
        return !type.empty();
    }
};

/**
 * @struct EdgeConfig
 * @brief Configuration for a single edge in the graph
 *
 * Specifies connectivity between two node ports.
 */
/**
 * @struct EdgeConfig
 * @brief Edge Config data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct EdgeConfig {
    /// Source node ID
    std::string source_node_id;
    
    /// Source port index (default: 0)
    std::size_t source_port = 0;

    /// Source port name (optional, preferred when non-empty)
    std::string source_port_name;
    
    /// Target node ID
    std::string target_node_id;
    
    /// Target port index (default: 0)
    std::size_t target_port = 0;

    /// Target port name (optional, preferred when non-empty)
    std::string target_port_name;
    
    /// Queue capacity (optional, default: 256)
    std::size_t buffer_size = 256;
    
    /// Enable backpressure handling (default: true)
    bool backpressure_enabled = true;

    /// Optional declared payload/edge contract for schema validation.
    std::string payload_contract;
    
    /// Description (optional)
    std::string description;
    
    /**
     * Validate this edge configuration
     * @return true if valid, false otherwise
     */
    bool Validate() const {
        return !source_node_id.empty() && !target_node_id.empty() &&
               buffer_size > 0;
    }
};

/**
 * @struct GraphConfig
 * @brief Complete graph specification from JSON
 *
 * Represents all information needed to instantiate a graph,
 * after parsing and validation but before node/edge creation.
 */
/**
 * @struct GraphConfig
 * @brief Graph Config data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct GraphConfig {
    /// Graph-level metadata
    struct Metadata {
        std::string version;        ///< Configuration version (e.g., "1.0")
        std::string description;    ///< Graph description
        std::string author;         ///< Author name (optional)
        std::string created;        ///< Creation timestamp (optional)
    } metadata;
    
    /// Graph name (must be unique)
    std::string name;
    
    /// Graph description (optional)
    std::string description;
    
    /// Number of threads for ThreadPool (0 = auto-detect)
    std::size_t num_threads = 0;

    /// Backend intent resolver controls for portable graph topologies.
    struct ResolverConfig {
        std::string execution_backend{"auto"};
        std::string backend_fallback_policy{"strict"};
        bool resolver_diagnostics{true};
        std::string edge_contract{};
    } resolver;

    /// Concrete backend variant for a portable node intent.
    struct ResolverBackendVariant {
        std::string backend;
        std::string concrete_type;
    };

    /// Dynamic resolver mapping supplied by config or future plugin metadata.
    struct ResolverMapping {
        std::string intent_type;
        std::string input_token_type;
        std::string output_token_type;
        std::vector<ResolverBackendVariant> variants;
    };

    std::vector<ResolverMapping> resolver_mappings;
    
    /// Deadlock detection configuration
    struct DeadlockDetectionConfig {
        bool enabled = true;
        uint32_t timeout_ms = 5000;
    } deadlock_detection;
    
    /// All nodes in the graph
    std::vector<NodeConfig> nodes;
    
    /// All edges in the graph
    std::vector<EdgeConfig> edges;
    
    /**
     * Validate the entire graph configuration
     * @return true if all nodes and edges are valid, false otherwise
     */
    bool Validate() const {
        // Graph must have at least one node
        if (nodes.empty()) {
            return false;
        }
        
        // Graph name must be non-empty
        if (name.empty()) {
            return false;
        }
        
        // Validate all nodes
        for (const auto& node : nodes) {
            if (!node.Validate()) {
                return false;
            }
        }
        
        // Validate all edges
        for (const auto& edge : edges) {
            if (!edge.Validate()) {
                return false;
            }
        }
        
        // Check for duplicate node IDs
        std::map<std::string, int> node_ids;
        for (const auto& node : nodes) {
            node_ids[node.id]++;
        }
        for (const auto& [id, count] : node_ids) {
            if (count > 1) {
                return false;  // Duplicate node ID
            }
        }
        
        return true;
    }
};

/**
 * @struct ValidationResult
 * @brief Result of graph configuration validation
 */
/**
 * @struct ValidationResult
 * @brief Validation Result data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct ValidationResult {
    bool valid = false;                      ///< Whether configuration is valid
    std::vector<std::string> errors;         ///< Error messages (if not valid)
    std::vector<std::string> warnings;       ///< Warning messages
};

}  // namespace graph::config
