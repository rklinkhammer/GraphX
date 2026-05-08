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

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <log4cxx/logger.h>
#include "core/ReflectionHelper.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeFactoryRegistry.hpp"

namespace graph {

// Forward declarations
namespace nodes {
    class INode;
}
namespace config {
    class NodeFactoryRegistry;
}
class PluginRegistry;
struct NodeFacade;

/**
 * @class NodeFactory
 * @brief Factory for creating nodes, both compile-time and dynamically loaded
 *
 * NodeFactory provides a unified interface for creating nodes from:
 * - Compile-time typed nodes via templates
 * - Dynamically loaded plugin nodes via string names
 *
 * This class bridges the compile-time and runtime worlds, allowing
 * GraphManager to treat all nodes uniformly regardless of their source.
 *
 * @see NodeFacade
 * @see PluginRegistry
 * @see PluginLoader
 * @see GraphManager
 */
class NodeFactory {
private:
    static log4cxx::LoggerPtr logger_;
    std::shared_ptr<PluginRegistry> plugin_registry_;
    std::shared_ptr<graph::config::NodeFactoryRegistry> unified_registry_;
    bool initialized_;

public:
    /**
     * Constructor with optional plugin registry
     *
     * @param plugin_registry Shared pointer to PluginRegistry for dynamic loading.
     *                         If nullptr, dynamic loading is disabled.
     */
    explicit NodeFactory(std::shared_ptr<PluginRegistry> plugin_registry = nullptr)
        : plugin_registry_(plugin_registry), 
          unified_registry_(std::make_shared<graph::config::NodeFactoryRegistry>()),
          initialized_(false) {}

    /**
     * Destructor
     */
    virtual ~NodeFactory() = default;

    /**
     * Create a compile-time typed node
     *
     * @tparam NodeType The concrete node class (e.g., MySourceNode)
     * @tparam Args Constructor argument types
     * @param args Arguments to pass to NodeType constructor
     * @return Shared pointer to the created node
     *
     * @requires NodeType is a GraphNode (satisfies move construction/assignment)
     */
    template <reflection::GraphNode NodeType, typename... Args>
    std::shared_ptr<NodeType> CreateNode(Args&&... args) {
        try {
            auto node = std::make_shared<NodeType>(std::forward<Args>(args)...);
            LOG4CXX_TRACE(logger_, "Created compile-time node: "
                << typeid(NodeType).name());
            return node;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to create node: " << e.what());
            return nullptr;
        }
    }

    /**
     * Create a dynamically loaded node
     *
     * Creates a node from a plugin by type name. Requires PluginLoader
     * to have already loaded the plugin containing this node type.
     *
     * @param node_type_name Name of the node type (e.g., "SensorNode")
     * @return NodeFacadeAdapter wrapping the created node
     *
     * @throws std::runtime_error if plugin_registry_ is nullptr or type not found
     */
    virtual NodeFacadeAdapter CreateDynamicNode(const std::string& node_type_name);

    /**
     * Initialize the unified factory with plugin and static nodes
     *
     * Must be called once after construction, before creating nodes.
     * Registers all available plugin nodes and built-in static nodes
     * in the unified factory registry.
     *
     * @throws std::runtime_error if plugin_registry is not set
     *
     * Example:
     * @code
     * auto factory = std::make_shared<NodeFactory>(plugin_registry);
     * factory->Initialize();  // Must be called once
     * 
     * // Now can use unified CreateNode() for both plugin and static nodes
     * auto node1 = factory->CreateNode("DataInjectionAccelerometerNode");  // Plugin
     * auto node2 = factory->CreateNode("FlightFSMNode");          // Static
     * @endcode
     */
    void Initialize();

    /**
     * Create a node by type name (unified factory path)
     *
     * Creates a node regardless of whether it's from a plugin or static/built-in.
     * Works identically for both node types - the caller receives a NodeFacadeAdapter.
     *
     * Must call Initialize() before using this method.
     *
     * @param node_type_name Name of the node type (e.g., "DataInjectionAccelerometerNode" or "FlightFSMNode")
     * @return NodeFacadeAdapter wrapping the created node
     *
     * @throws std::runtime_error if Initialize() not called
     * @throws std::runtime_error if node_type_name not found in unified registry
     *
     * Example:
     * @code
     * // Both of these work the same way:
     * auto plugin_node = factory->CreateNode("DataInjectionAccelerometerNode");   // From plugin
     * auto static_node = factory->CreateNode("FlightFSMNode");          // Static (adapted)
     * 
     * // Use identical interface for both
     * plugin_node.Init();
     * static_node.Init();
     * @endcode
     */
    virtual NodeFacadeAdapter CreateNode(const std::string& node_type_name);

    /**
     * Get metadata about a compile-time node type
     *
     * @tparam NodeType The node class
     * @return String containing type information
     */
    template <typename NodeType>
    std::string GetNodeTypeInfo() const {
        return typeid(NodeType).name();
    }

    /**
     * Check if a node type name is registered in the plugin system
     *
     * @param node_type_name The node type to check
     * @return true if the node type is available, false otherwise
     */
    bool IsNodeTypeAvailable(const std::string& node_type_name) const;

    /**
     * Get list of all available node types from plugins
     *
     * @return Vector of registered node type names
     */
    std::vector<std::string> GetAvailableNodeTypes() const;

    /**
     * Check if factory has been initialized
     *
     * @return true if Initialize() has been called successfully
     */
    bool IsInitialized() const {
        return initialized_;
    }

    /**
     * Get the unified factory registry
     *
     * @return Shared pointer to NodeFactoryRegistry
     */
    std::shared_ptr<graph::config::NodeFactoryRegistry> GetUnifiedRegistry() const {
        return unified_registry_;
    }

    /**
     * Set the plugin registry (for late initialization)
     *
     * @param registry Shared pointer to PluginRegistry
     */
    void SetPluginRegistry(std::shared_ptr<PluginRegistry> registry) {
        plugin_registry_ = registry;
    }

    /**
     * Get the plugin registry
     *
     * @return Shared pointer to PluginRegistry, or nullptr if not set
     */
    std::shared_ptr<PluginRegistry> GetPluginRegistry() const {
        return plugin_registry_;
    }

private:
    /**
     * Register all available plugin nodes in the unified registry
     *
     * Called by Initialize() to register plugin-based nodes.
     * For each node type in the plugin registry, creates a factory
     * function that delegates to CreateDynamicNode().
     */
    void RegisterPluginNodes();

    /**
     * Register all built-in static nodes in the unified registry
     *
     * Called by Initialize() to register statically-compiled nodes.
     * Each static node is wrapped via StaticNodeAdapter to provide
     * the NodeFacadeAdapter interface.
     */
    void RegisterStaticNodes();
};

}  // namespace graph

