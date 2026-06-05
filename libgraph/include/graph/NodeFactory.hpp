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
#include <expected>
#include <log4cxx/logger.h>
#include "core/ReflectionHelper.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeFactoryRegistry.hpp"
#include "graph/NodeProvider.hpp"
#include "graph/PortTypes.hpp"

namespace graph {

// Forward declarations
namespace nodes {
    class INode;
}
namespace config {
    class NodeFactoryRegistry;
}
class PluginRegistry;
class PluginLoader;
struct NodeFacade;

/**
 * @class NodeFactory
 * @brief Unified provider-backed factory for creating graph nodes
 *
 * NodeFactory provides a unified interface for creating nodes from:
 * - Compile-time typed nodes via templates
 * - Plugin-registered node types via string names
 *
 * This class bridges compile-time and provider-backed creation paths,
 * allowing graph orchestration code to treat all nodes uniformly.
 *
 * @see NodeFacade
 * @see PluginRegistry
 * @see PluginLoader
 */
class NodeFactory : public INodeProvider {
private:
    static log4cxx::LoggerPtr logger_;
    std::shared_ptr<PluginRegistry> plugin_registry_;
    std::vector<std::shared_ptr<PluginLoader>> loaders_;  // Multi-directory support
    std::shared_ptr<graph::config::NodeFactoryRegistry> unified_registry_;
    bool initialized_;

public:
    using NodeCreationError = graph::NodeCreationError;

    enum class PluginDirectoryError {
        EmptyPath = 1,
        RegistryCreationFailed = 2,
        LoaderCreationFailed = 3,
        NoDirectoriesRegistered = 4,
        PluginRegistryMissing = 5,
        Unknown = 99,
    };

    /**
     * Constructor with optional plugin registry
     *
        * @param plugin_registry Shared pointer to PluginRegistry for plugin-backed
        *                        node registration and availability queries.
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
     * Create a compile-time typed node with typed error reporting.
     */
    template <reflection::GraphNode NodeType, typename... Args>
        requires HasCompileTimePortCounts<NodeType>
    [[nodiscard]] std::expected<std::shared_ptr<NodeType>, NodeCreationError>
    CreateNodeExpected(Args&&... args) noexcept {
        try {
            auto node = std::make_shared<NodeType>(std::forward<Args>(args)...);
            LOG4CXX_TRACE(logger_, "Created compile-time node: "
                << typeid(NodeType).name());
            return node;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to create node: " << e.what());
            return std::unexpected(NodeCreationError::CreationFailed);
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Failed to create node: unknown error");
            return std::unexpected(NodeCreationError::Unknown);
        }
    }

    /**
     * Initialize the unified factory with plugin and static nodes
     *
     * Must be called once after construction, before creating nodes.
     * Registers all available plugin nodes and built-in static nodes
     * in the unified factory registry.
     *
     * Example:
     * @code
     * auto factory = std::make_shared<NodeFactory>(plugin_registry);
     * factory->Initialize();  // Must be called once
     * 
     * // Now can use unified CreateNodeExpected() for both plugin and static nodes
     * auto node1 = factory->CreateNodeExpected("DataInjectionAccelerometerNode");
     * auto node2 = factory->CreateNodeExpected("FlightFSMNode");
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
     * Example:
     * @code
     * // Both of these work the same way:
     * auto plugin_node = factory->CreateNodeExpected("DataInjectionAccelerometerNode");
     * auto static_node = factory->CreateNodeExpected("FlightFSMNode");
     * 
     * // Use identical interface for both
     * plugin_node.Init();
     * static_node.Init();
     * @endcode
     */
    [[nodiscard]] std::expected<NodeFacadeAdapter, NodeCreationError>
    CreateNodeExpected(const std::string& node_type_name) noexcept override;

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
    bool IsNodeTypeAvailable(const std::string& node_type_name) const override;

    /**
     * Get list of all available node types from plugins
     *
     * @return Vector of registered node type names
     */
    std::vector<std::string> GetAvailableNodeTypes() const override;

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

    /**
     * Add a plugin directory to the factory's search path
     *
     * Enables multi-directory plugin loading. Each directory is managed by a separate
     * PluginLoader instance, but all plugins register with the shared PluginRegistry.
     *
     * Can be called before Initialize() to set up multiple directories, or after
     * to dynamically add new plugin sources at runtime.
     *
     * @param directory_path Path to a directory containing .so/.dylib plugin files
     *
     * Example:
     * @code
     * auto factory = std::make_shared<NodeFactory>();
     * factory->AddPluginDirectoryExpected("/usr/local/lib/graphx/plugins");
     * factory->AddPluginDirectoryExpected("./plugins");
     * factory->AddPluginDirectoryExpected("./test_plugins");
     * factory->LoadAllPluginsFromDirectoriesExpected();
     * @endcode
     *
     * @note Does not immediately load plugins; call LoadAllPluginsFromDirectoriesExpected()
     * @note Multiple directories are searched in registration order
     * @note If the same plugin name exists in multiple directories, first match wins
     * @see LoadAllPluginsFromDirectoriesExpected()
     */
    [[nodiscard]] std::expected<void, PluginDirectoryError>
    AddPluginDirectoryExpected(const std::string& directory_path) noexcept;

    /**
     * Load all plugins from all registered directories
     *
     * Scans each directory added via AddPluginDirectoryExpected() and loads all .so/.dylib files.
     * Plugins from all directories register with the shared PluginRegistry.
     *
     * If a specific directory doesn't exist or fails to load, logs a warning but
     * continues with other directories.
     *
     * Must have called AddPluginDirectoryExpected() at least once before calling this method.
     *
     * Example:
     * @code
     * auto factory = std::make_shared<NodeFactory>();
     * factory->AddPluginDirectoryExpected(DIR1);
     * factory->AddPluginDirectoryExpected(DIR2);
     * factory->AddPluginDirectoryExpected(DIR3);
     * factory->LoadAllPluginsFromDirectoriesExpected();
     * factory->Initialize();
     * @endcode
     *
     * @see AddPluginDirectoryExpected()
     */
    [[nodiscard]] std::expected<void, PluginDirectoryError>
    LoadAllPluginsFromDirectoriesExpected() noexcept;

private:
    /**
     * Register all available plugin nodes in the unified registry
     *
     * Called by Initialize() to register plugin-based nodes.
     * For each node type in the plugin registry, creates a factory
    * function that delegates to plugin-registry creation.
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

[[nodiscard]] std::string ErrorMessage(NodeFactory::NodeCreationError error);

}  // namespace graph
