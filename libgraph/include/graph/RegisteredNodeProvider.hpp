/**
 * @file RegisteredNodeProvider.hpp
 * @brief Registered Node Provider Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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
#include <map>
#include <vector>
#include <expected>
#include <log4cxx/logger.h>
#include "core/ReflectionHelper.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeProvider.hpp"
#include "graph/PortTypes.hpp"

namespace graph {

// Forward declarations
namespace nodes {
    /**
     * @class INode
     * @brief Inode graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class INode;
}
/**
 * @class PluginRegistry
 * @brief Plugin Registry manager.
 *
 * @details Owns registration, lookup, or orchestration state for a GraphX subsystem. The class centralizes mutation so callers interact through stable query and update methods.
 */
class PluginRegistry;
/**
 * @struct NodeFacade
 * @brief Node Facade data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct NodeFacade;

/**
 * @class RegisteredNodeProvider
 * @brief Provider-backed node creator for registered graph node types
 *
 * RegisteredNodeProvider provides a unified interface for creating nodes from:
 * - Compile-time typed nodes via templates
 * - Plugin-registered node types via string names
 *
 * This class bridges compile-time and plugin-backed creation paths,
 * allowing graph orchestration code to treat all nodes uniformly.
 *
 * @see NodeFacade
 * @see PluginRegistry
 * @see PluginLoader
 */
/**
 * @class RegisteredNodeProvider
 * @brief Registered Node Provider graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class RegisteredNodeProvider : public INodeProvider {
private:
    static log4cxx::LoggerPtr logger_;
    std::shared_ptr<PluginRegistry> plugin_registry_;
    bool initialized_;

public:
    using NodeCreationError = graph::NodeCreationError;

    /**
     * Constructor with optional plugin registry
     *
        * @param plugin_registry Shared pointer to PluginRegistry for plugin-backed
        *                        node registration and availability queries.
     */
    explicit RegisteredNodeProvider(std::shared_ptr<PluginRegistry> plugin_registry = nullptr)
        : plugin_registry_(plugin_registry), 
          initialized_(false) {}

    /**
     * Destructor
     */
    virtual ~RegisteredNodeProvider() = default;

    /**
     * Create a compile-time typed node with typed error reporting.
     */
    template <reflection::GraphNode NodeType, typename... Args>
        requires HasCompileTimePortCounts<NodeType>
    [[nodiscard]] std::expected<std::shared_ptr<NodeType>, NodeCreationError>
    /**
     * @brief Creates or builds the object described by Create Node Expected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param args Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CreateNodeExpected(Args&&... args) noexcept {
        try {
            auto node = std::make_shared<NodeType>(std::forward<Args>(args)...);
            LOG4CXX_TRACE(logger_, "Created compile-time node: "
                << typeid(NodeType).name());
            return node;
        } catch (const std::exception& e) {
            /**
             * @brief Executes the Log4 Cxx Error operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_ERROR(logger_, "Failed to create node: " << e.what());
            return std::unexpected(NodeCreationError::CreationFailed);
        } catch (...) {
            /**
             * @brief Executes the Log4 Cxx Error operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_ERROR(logger_, "Failed to create node: unknown error");
            return std::unexpected(NodeCreationError::Unknown);
        }
    }

    /**
     * Initialize the node creator map with plugin and static nodes
     *
     * Must be called once after construction, before creating nodes.
     * Registers all available plugin nodes and built-in static nodes
     * in the provider's creator map.
     *
     * Example:
     * @code
     * auto provider = std::make_shared<RegisteredNodeProvider>(plugin_registry);
     * provider->Initialize();  // Must be called once
     * 
     * // Now can use CreateNodeExpected() for both plugin and static nodes
     * auto node1 = provider->CreateNodeExpected("DataInjectionAccelerometerNode");
     * auto node2 = provider->CreateNodeExpected("FlightFSMNode");
     * @endcode
     */
/**
 * @brief Initialize.
 */
    void Initialize();

    /**
     * Create a node by type name
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
     * auto plugin_node = provider->CreateNodeExpected("DataInjectionAccelerometerNode");
     * auto static_node = provider->CreateNodeExpected("FlightFSMNode");
     * 
     * // Use identical interface for both
     * plugin_node.Init();
     * static_node.Init();
     * @endcode
     */
    [[nodiscard]] std::expected<NodeFacadeAdapter, NodeCreationError>
    /**
     * @brief Creates or builds the object described by Create Node Expected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CreateNodeExpected(const std::string& node_type_name) noexcept override;

    /**
     * Get metadata about a compile-time node type
     *
     * @tparam NodeType The node class
     * @return String containing type information
     */
    template <typename NodeType>
    /**
     * @brief Returns the Node Type Info.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetNodeTypeInfo() const {
        return typeid(NodeType).name();
    }

    /**
     * Check if a node type name is registered in the plugin system
     *
     * @param node_type_name The node type to check
     * @return true if the node type is available, false otherwise
     */
/**
 * @brief Is node type available.
 * @param node_type_name Parameter for is node type available.
 * @return Result of the operation.
 */
    bool IsNodeTypeAvailable(const std::string& node_type_name) const override;

    /**
     * Get list of all available node types from plugins
     *
     * @return Vector of registered node type names
     */
/**
 * @brief Get available node types.
 * @return Result of the operation.
 */
    std::vector<std::string> GetAvailableNodeTypes() const override;

    /**
     * Check if provider has been initialized
     *
     * @return true if Initialize() has been called successfully
     */
    bool IsInitialized() const {
        return initialized_;
    }

};

[[nodiscard]] std::string ErrorMessage(RegisteredNodeProvider::NodeCreationError error);

}  // namespace graph
