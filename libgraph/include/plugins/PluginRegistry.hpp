// MIT License
/// @file PluginRegistry.hpp
/// @brief Plugin system support for PluginRegistry

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

#pragma once

//#include "plugins/plugin_common.h"
#include "graph/NodeFacade.hpp"
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"

namespace graph {

// ============================================================================
// PluginRegistry - Central registry for loaded plugin node types
// ============================================================================

/// @class PluginRegistry
/// @brief Central registry for all loaded plugin node types
///
/// Manages the lifecycle of plugin metadata after they are loaded.
/// When a plugin is loaded by PluginLoader, it registers its node types here.
///
/// PluginRegistry provides:
/// - Thread-safe storage of plugin information
/// - Node creation via registered creation functions
/// - Query interface for discovering available node types
/// - Automatic cleanup of plugin metadata
class PluginRegistry {
private:
    // ========================================================================
    // Private Members
    // ========================================================================
    
    /// @brief Metadata for a single registered plugin node type
    struct PluginNodeInfo {
        std::string type_name;           // e.g., "SensorNode"
        std::string description;         // e.g., "Generates synthetic data"
        std::string plugin_path;         // e.g., "/path/to/libsensor_node.so"
        std::string create_function;     // e.g., "plugin_create_sensor_node"
        std::string abi_tag;            // e.g., "libstdc++_v1"
        void* plugin_handle;            // From dlopen()
        CreateNodeFunc create_func;     // From dlsym() of create_function
        const NodeFacade* facade;       // Function pointer table
        std::string version;            // Plugin version
    };
    
    mutable std::mutex registry_mutex_;
    std::map<std::string, PluginNodeInfo> registered_types_;
    static log4cxx::LoggerPtr logger_;

public:
    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /// @brief Construct the plugin registry
    PluginRegistry();

    // ========================================================================
    // Type Registration and Querying
    // ========================================================================

    /// @brief Register a new node type from a loaded plugin
    /// @param type_name Name of the node type (e.g., "SensorNode")
    /// @param description Human-readable description
    /// @param plugin_path Full path to the loaded plugin file
    /// @param create_function Name of the creation function in the plugin
    /// @param abi_tag ABI compatibility tag (e.g., "libstdc++_v1")
    /// @param version Plugin version string
    /// @param plugin_handle Handle from dlopen()
    /// @param facade Function pointer table for all node operations
    /// @throws std::runtime_error if create function cannot be resolved
    ///
    /// Called by PluginLoader after successfully dlopen-ing a plugin.
    /// Validates that the create function can be resolved and stores
    /// metadata for later instantiation.
    void RegisterNodeType(
        const std::string& type_name,
        const std::string& description,
        const std::string& plugin_path,
        const std::string& create_function,
        const std::string& abi_tag,
        const std::string& version,
        void* plugin_handle,
        const NodeFacade* facade);

    /// @brief Create a node instance of the given type
    /// @param type_name Type of node to create (e.g., "SensorNode")
    /// @return Pair of (NodeHandle, NodeFacade*)
    /// @throws std::runtime_error if type not registered or creation fails
    ///
    /// Calls the plugin's creation function to instantiate a node.
    /// The returned handle and facade can be used with NodeFacadeAdapter.
    std::pair<NodeHandle, const NodeFacade*> CreateNode(
        const std::string& type_name);

    /// @brief Check if a node type is registered and available
    /// @param type_name Type to check
    /// @return true if the type is registered
    bool HasNodeType(const std::string& type_name) const;

    /// @brief Get list of all registered node types
    /// @return Vector of type names
    std::vector<std::string> GetRegisteredNodeTypes() const;

    /// @brief Node type information structure
    /// Contains metadata about a registered node type
    /// @member type_name The type identifier
    /// @member description Human-readable description
    /// @member plugin_path Full path to the plugin file
    /// @member version Plugin version string
    /// @member abi_tag ABI compatibility tag
    struct TypeInfo {
        std::string type_name;
        std::string description;
        std::string plugin_path;
        std::string version;
        std::string abi_tag;
    };

    /// @brief Get information about a registered node type
    /// @param type_name Type to query
    /// @return Struct with type information, or nullptr if not registered
    TypeInfo* GetNodeTypeInfo(const std::string& type_name);

    /// @brief Get count of registered types
    /// @return Number of registered node types
    size_t GetRegisteredTypeCount() const;

    /// @brief Unregister a node type (e.g., when plugin is unloaded)
    /// @param type_name Type to unregister
    /// @return true if type was unregistered, false if not found
    bool UnregisterNodeType(const std::string& type_name);

    /// @brief Clear all registered types (used during shutdown)
    void Clear();
};

}  // namespace graph

