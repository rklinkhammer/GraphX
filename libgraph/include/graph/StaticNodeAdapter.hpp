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

#include <memory>
#include <string>
#include <functional>

#include "graph/NodeFacade.hpp"
#include "graph/INode.hpp"

namespace graph::config {

/**
 * @class StaticNodeAdapter
 * @brief Wraps an INode instance to provide NodeFacadeAdapter interface
 * 
 * Adapts compile-time typed nodes (INode implementations) to the runtime
 * NodeFacadeAdapter interface, enabling them to be treated identically to
 * plugin-based nodes in JSON configurations.
 * 
 * This adapter creates a bridge between:
 * - INode interface (compile-time typed, statically compiled)
 * - NodeFacadeAdapter (runtime C-compatible interface)
 * 
 * The result is a NodeFacadeAdapter that wraps the INode and delegates
 * all lifecycle calls to it. From the JSON loader's perspective, both
 * plugin and static nodes are NodeFacadeAdapter instances.
 * 
 * Memory Model:
 * - The adapter does NOT own the INode (caller maintains ownership)
 * - The returned NodeFacadeAdapter holds a reference to the INode
 * - Node lifetime must exceed adapter lifetime
 * 
 * Usage Example:
 * @code
 * // Create a static node
 * auto node = std::make_shared<avionics::FlightFSMNode>();
 * 
 * // Adapt it to NodeFacadeAdapter
 * auto adapter = StaticNodeAdapter::Adapt(node, "FlightFSMNode");
 * 
 * // Use it like any plugin node
 * adapter.Init();
 * adapter.Start();
 * // ... execute ...
 * adapter.Stop();
 * adapter.Join();
 * @endcode
 * 
 * @since Phase 2 (Unified Factory Implementation)
 */
class StaticNodeAdapter {
public:
    /**
     * @class StaticNodeInstance
     * @brief Internal wrapper that implements NodeFacade C interface
     * 
     * This is an internal helper class that wraps an INode and provides
     * NodeFacade-compatible C function pointers for the adapter.
     * 
     * Implementation Details:
     * - Stores a shared_ptr to the INode to maintain ownership
     * - Implements a NodeFacade struct with bound function pointers
     * - Uses lambda functions to create closures over the INode
     * - Handles the C interface (void* handle, C-style function pointers)
     */
    class StaticNodeInstance {
    public:
        /**
         * Construct wrapper around INode
         * 
         * @param node The INode to wrap (shared_ptr for ownership)
         * @param type_name Runtime type name for debugging
         */
        StaticNodeInstance(
            std::shared_ptr<graph::INode> node,
            const std::string& type_name);

        /**
         * Get handle for C interface (void* pointing to this instance)
         */
        void* GetHandle() { return this; }

        /**
         * Get pointer to the NodeFacade structure with bound function pointers
         */
        const NodeFacade* GetFacade() const { return &facade_; }

        /**
         * Prevent copying (move-only semantics)
         */
        StaticNodeInstance(const StaticNodeInstance&) = delete;
        StaticNodeInstance& operator=(const StaticNodeInstance&) = delete;

        StaticNodeInstance(StaticNodeInstance&&) = default;
        StaticNodeInstance& operator=(StaticNodeInstance&&) = default;

    private:
        // The actual node being wrapped
        std::shared_ptr<graph::INode> node_;
        
        // Runtime type name
        std::string type_name_;

        // The C-compatible facade with bound function pointers
        NodeFacade facade_;

        // Helper methods for creating facade function pointers
        void BuildFacade();
    };

public:
    /**
     * Adapt a static INode to NodeFacadeAdapter
     * 
     * Creates a NodeFacadeAdapter wrapper that provides a C-compatible
     * facade for a typed C++ INode implementation.
     * 
     * The returned adapter is a standalone object that owns a reference
     * to the provided INode. The INode will continue to exist as long as
     * the adapter uses it.
     * 
     * @param node The INode instance to wrap (should be std::shared_ptr)
     * @param type_name Runtime type identifier (e.g., "FlightFSMNode")
     * @return A NodeFacadeAdapter wrapping the provided node
     * 
     * @throws std::invalid_argument if node is nullptr
     * @throws std::invalid_argument if type_name is empty
     * 
     * Thread Safety: Not thread-safe during adaptation. Ensure the node
     * is fully constructed before calling Adapt(). After Adapt() returns,
     * the returned adapter can be used concurrently.
     * 
     * Example:
     * @code
     * auto fsm_node = std::make_shared<avionics::FlightFSMNode>();
     * auto adapter = StaticNodeAdapter::Adapt(fsm_node, "FlightFSMNode");
     * 
     * // adapter.Init() will call fsm_node->Init()
     * // adapter.Start() will call fsm_node->Start()
     * // ... etc
     * @endcode
     */
    static NodeFacadeAdapter Adapt(
        std::shared_ptr<graph::INode> node,
        const std::string& type_name);
};

}  // namespace graph::config
