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

#include <functional>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>

#include "graph/NodeFacade.hpp"

namespace graph::config {

/**
 * @class NodeFactoryRegistry
 * @brief Central registry for unified node creation (plugin and static)
 * 
 * Provides a single interface for creating nodes from both plugin-based sources
 * and statically compiled sources. All registered factory functions return
 * NodeFacadeAdapter instances, enabling uniform treatment of all node types.
 * 
 * This registry is the core of the Unified Factory pattern, allowing:
 * - Plugin nodes to be created through plugin system
 * - Static nodes to be created and wrapped as adapters
 * - JSON loader to treat all nodes identically
 * 
 * Thread Safety: This class is NOT thread-safe. Initialize all factories
 * during startup before concurrent access.
 * 
 * @since Phase 2 (Unified Factory Implementation)
 * 
 * Usage Example:
 * @code
 * NodeFactoryRegistry registry;
 * 
 * // Register a plugin node factory
 * registry.Register("DataInjectionAccelerometerNode", []() {
 *     return factory->CreateDynamicNode("DataInjectionAccelerometerNode");
 * });
 * 
 * // Register a static node factory
 * registry.Register("FlightFSMNode", []() {
 *     auto node = std::make_shared<avionics::FlightFSMNode>();
 *     return StaticNodeAdapter::Adapt(node, "FlightFSMNode");
 * });
 * 
 * // Create any node type uniformly
 * auto node = registry.Create("FlightFSMNode");
 * @endcode
 */
class NodeFactoryRegistry {
public:
    /**
     * Type alias for node factory function
     * 
     * A factory function takes no parameters and returns a new NodeFacadeAdapter
     * instance. It may throw std::runtime_error on failure.
     */
    using NodeFactoryFunction = std::function<NodeFacadeAdapter()>;

    /**
     * Default constructor
     * 
     * Creates an empty registry with no registered factories.
     */
    NodeFactoryRegistry() = default;

    /**
     * Destructor
     * 
     * Cleans up all registered factories.
     */
    ~NodeFactoryRegistry() = default;

    /**
     * Register a node factory function
     * 
     * Associates a type name with a factory function that creates instances
     * of that node type. Both plugin and static nodes use the same registration
     * interface, enabling uniform treatment.
     * 
     * If a factory is already registered for the given type_name, it will be
     * replaced with the new factory.
     * 
     * @param type_name The type identifier (e.g., "DataInjectionAccelerometerNode", "FlightFSMNode")
     * @param factory A callable that returns a new NodeFacadeAdapter instance
     * 
     * @throws std::invalid_argument if type_name is empty
     * 
     * Example:
     * @code
     * // Register a plugin node
     * registry.Register("MyPluginNode", [factory]() {
     *     return factory->CreateDynamicNode("MyPluginNode");
     * });
     * 
     * // Register a static node
     * registry.Register("MyStaticNode", []() {
     *     auto node = std::make_shared<MyStaticNode>();
     *     return StaticNodeAdapter::Adapt(node, "MyStaticNode");
     * });
     * @endcode
     */
    void Register(
        const std::string& type_name,
        NodeFactoryFunction factory);

    /**
     * Create a node instance by type name
     * 
     * Looks up the registered factory for the given type name and calls it
     * to create a new node instance. All nodes are returned as NodeFacadeAdapter,
     * regardless of whether they are plugin or static nodes.
     * 
     * The caller is responsible for initializing the returned node via Init(),
     * starting it via Start(), and cleaning up via Stop() and Join().
     * 
     * @param type_name The type of node to create (must be registered)
     * @return A new NodeFacadeAdapter instance
     * 
     * @throws std::runtime_error if type_name is not registered
     * @throws std::runtime_error if the factory function throws
     * 
     * Example:
     * @code
     * try {
     *     auto node = registry.Create("FlightFSMNode");
     *     node.Init();
     *     node.Start();
     *     // ... use node ...
     *     node.Stop();
     *     node.Join();
     * } catch (const std::runtime_error& e) {
     *     LOG(ERROR) << "Failed to create node: " << e.what();
     * }
     * @endcode
     */
    NodeFacadeAdapter Create(const std::string& type_name);

    /**
     * Check if a node type is available
     * 
     * Returns true if a factory has been registered for the given type_name,
     * false otherwise. This is useful for validation before attempting to create
     * a node.
     * 
     * @param type_name The type to check
     * @return true if a factory is registered for this type, false otherwise
     * 
     * Example:
     * @code
     * if (registry.IsAvailable("FlightFSMNode")) {
     *     auto node = registry.Create("FlightFSMNode");
     * } else {
     *     LOG(ERROR) << "FlightFSMNode not available";
     * }
     * @endcode
     */
    bool IsAvailable(const std::string& type_name) const;

    /**
     * Get all registered node type names
     * 
     * Returns a vector of all type names that have been registered in this
     * registry. Useful for introspection and validation.
     * 
     * The returned vector contains both plugin and static node types, treating
     * them identically.
     * 
     * @return Vector of registered type names (may be empty if no factories registered)
     * 
     * Example:
     * @code
     * auto types = registry.GetAvailableTypes();
     * for (const auto& type : types) {
     *     LOG(INFO) << "Available node type: " << type;
     * }
     * @endcode
     */
    std::vector<std::string> GetAvailableTypes() const;

    /**
     * Get the count of registered factories
     * 
     * Returns the number of node types currently registered in this registry.
     * 
     * @return Number of registered factories
     */
    size_t GetRegistrySize() const;

    /**
     * Clear all registered factories
     * 
     * Removes all registered factories from the registry. After this call,
     * the registry is equivalent to a newly constructed registry.
     * 
     * Use with caution - after clearing, Create() will fail for all types.
     */
    void Clear();

private:
    /// Map from type name to factory function
    std::map<std::string, NodeFactoryFunction> factories_;
};

}  // namespace graph::config
