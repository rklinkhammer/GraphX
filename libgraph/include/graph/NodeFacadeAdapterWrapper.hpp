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

/**
 * @file NodeFacadeAdapterWrapper.hpp
 * @brief Wrapper to make NodeFacadeAdapter compatible with INode interface
 * 
 * This file provides a wrapper class that allows NodeFacadeAdapter instances
 * (returned from dynamic plugin loading) to be used as INode instances in
 * the typed graph system.
 *
 * @author GitHub Copilot
 * @date 2026-01-04
 */

#pragma once

#include <memory>
#include <chrono>
#include "graph/INode.hpp"
#include "graph/NodeFacade.hpp"

namespace graph {

/**
 * @class NodeFacadeAdapterWrapper
 * @brief Adapts NodeFacadeAdapter to implement INode interface
 * 
 * This is a bridge that allows NodeFacadeAdapter objects (from dynamic plugin loading)
 * to be stored and used as INode pointers in the typed graph system.
 * 
 * The wrapper owns the NodeFacadeAdapter and delegates all lifecycle and
 * execution methods to it.
 * 
 * @note NodeFacadeAdapter has move semantics only, so this wrapper must also use move semantics
 */
class NodeFacadeAdapterWrapper : public INode {
private:
    std::shared_ptr<NodeFacadeAdapter> adapter_;
    
public:
    /**
     * Construct wrapper with a NodeFacadeAdapter
     * 
     * @param adapter NodeFacadeAdapter to wrap (moved into this wrapper)
     */
    explicit NodeFacadeAdapterWrapper(std::shared_ptr<NodeFacadeAdapter> adapter)
        : adapter_(std::move(adapter)) {
    }
    
    virtual ~NodeFacadeAdapterWrapper() = default;
    
    /**
     * Get the current lifecycle state of the wrapped node
     */
    virtual graph::LifecycleState GetLifecycleState() const override {
        return static_cast<graph::LifecycleState>(adapter_->GetLifecycleState());
    }

    /**
     * Initialize the wrapped node
     */
    virtual bool Init() override {
        return adapter_->Init();
    }
    
    /**
     * Start the wrapped node
     */
    virtual bool Start() override {
        return adapter_->Start();
    }
    
    /**
     * Stop the wrapped node
     */
    virtual void Stop() override {
        adapter_->Stop();
    }
    
    /**
     * Cleanup the wrapped node (call Destroy callback)
     * 
     * This MUST be called before the wrapper is destroyed.
     * GraphManager will call this explicitly before clearing nodes_.
     */
    void Cleanup() {
        if (adapter_) {
            adapter_->Cleanup();
        }
    }
    
    /**
     * Join the wrapped node
     */
    virtual void Join() override {
        adapter_->Join();
    }
    
    /**
     * Join the wrapped node with timeout
     */
    virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
        return adapter_->JoinWithTimeout(timeout_ms);
    }
    
    /*
     * Get the type of this instance
    */
    const std::string GetType() const  {
        return adapter_->GetType();
    }
   
    /*
     * Get the name of this instance
     */
    const std::string GetName() const {
        return adapter_->GetName();
    }

    /**
     * Get access to the underlying NodeFacadeAdapter
     * 
     * CRITICAL: This returns the shared_ptr that was provided at construction.
     * The caller must ensure that this shared_ptr is properly managed and that
     * the NodeFacadeAdapterWrapper instance remains valid while using it.
     * 
     * This is safe to use because:
     * 1. The NodeFacadeAdapterWrapper holds the shared_ptr
     * 2. The shared_ptr is reference-counted
     * 3. As long as NodeFacadeAdapterWrapper exists, the adapter exists
     * 
     * @return Shared pointer to the wrapped adapter
     *         Returns valid shared_ptr if wrapper is properly constructed
     *         May return null if wrapper was moved-from (indicates misuse)
     */
    std::shared_ptr<NodeFacadeAdapter> GetAdapter() {
        return adapter_;
    }
    
    /**
     * Get const access to the underlying NodeFacadeAdapter
     * 
     * @return Const shared pointer to the wrapped adapter
     */
    const std::shared_ptr<NodeFacadeAdapter> GetAdapter() const {
        return adapter_;
    }
    
    /**
     * Safely extract and cast the wrapped adapter to a specific node type
     * 
     * This template method provides type-safe casting of the wrapped NodeFacadeAdapter
     * to a specific node type (e.g., DataInjectionAccelerometerNode, FlightFSMNode).
     * 
     * CRITICAL: Returns nullptr if:
     * 1. The adapter is null (wrapper not properly initialized)
     * 2. The adapter cannot provide a node of the requested type
     * 
     * Usage in callbacks:
     * @code
     * auto wrapper = dynamic_pointer_cast<NodeFacadeAdapterWrapper>(graph_->GetNodes()[idx]);
     * if (!wrapper) {
     *     LOG_ERROR("Node is not a wrapper");
     *     return false;
     * }
     * auto node = wrapper->GetNode<DataInjectionAccelerometerNode>();
     * if (!node) {
     *     LOG_ERROR("Cannot get DataInjectionAccelerometerNode from adapter");
     *     return false;
     * }
     * // Now safe to use 'node'
     * @endcode
     * 
     * @tparam NodeT The node type to cast to (e.g., DataInjectionAccelerometerNode)
     * @return shared_ptr to the node, or nullptr if cast fails
     */
    template<typename NodeT>
    std::shared_ptr<NodeT> GetNode() {
        if (!adapter_) {
            return nullptr;
        }
        // Note: For plugin-loaded adapters, this may return nullptr if the handle
        // wasn't created with the specific NodePluginInstance<NodeT> type.
        // This is expected behavior - only adapters created with the correct type
        // can be successfully cast back to their specific node type.
        return adapter_->GetNode<NodeT>();
    }
    
    /**
     * Const version of GetNode()
     * 
     * @tparam NodeT The node type to cast to
     * @return const shared_ptr to the node, or nullptr if cast fails
     */
    template<typename NodeT>
    std::shared_ptr<const NodeT> GetNode() const {
        if (!adapter_) {
            return nullptr;
        }
        return adapter_->GetNode<NodeT>();
    }
    
    /**
     * Get mutable access to the underlying NodeFacadeAdapter
     * 
     * @return Pointer to the wrapped adapter
     */
    NodeFacadeAdapter* operator->() {
        return adapter_.get();
    }
    
    /**
     * Get const access to the underlying NodeFacadeAdapter
     * 
     * @return Const pointer to the wrapped adapter
     */
    const NodeFacadeAdapter* operator->() const {
        return adapter_.get();
    }
    
    /**
     * Try to get a typed interface from the wrapped plugin node
     * 
     * Delegates to NodeFacadeAdapter::TryGetInterface<InterfaceT>().
     * This provides type-safe access to optional plugin interfaces like
     * CSVNodeConfig without requiring RTTI or virtual methods.
     * 
     * @tparam InterfaceT The interface type to query (e.g., graph::datasources::IDataInjectionSource)
     * @return shared_ptr to the interface, or nullptr if not supported
     * 
     * Example:
     * @code
     *   auto wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(node);
     *   if (wrapper) {
     *       auto csv_config = wrapper->TryGetInterface<graph::datasources::IDataInjectionSource>();
     *       if (csv_config) {
     *           // Use CSV configuration
     *       }
     *   }
     * @endcode
     */
    template<typename InterfaceT>
    std::shared_ptr<InterfaceT> TryGetInterface() const {
        if (!adapter_) {
            return nullptr;
        }
        return adapter_->TryGetInterface<InterfaceT>();
    }
};

}  // namespace graph

