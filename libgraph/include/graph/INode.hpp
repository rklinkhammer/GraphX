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

/**
 * @file INode.hpp
 * @brief Abstract base interface for all graph nodes
 * 
 * This header provides the INode interface that all nodes must implement.
 * It defines the common lifecycle and port introspection API.
 */

#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include "PortTypes.hpp"

namespace graph
{
    /*
     * @enum LifecycleState
     * @brief Represents the lifecycle state of a node
    */
    enum class LifecycleState
    {
        Uninitialized,
        Initialized,
        Started,
        Stopped,
        Joined,
        Invalid
    };

    /**
     * @brief Abstract base class for all graph nodes
     *
     * INode defines the common interface that all nodes must implement.
     * It provides lifecycle management, port introspection, and graph
     * topology tracking capabilities.
     */
    class INode
    {
    public:
        /**
         * @brief Construct a node with an optional name
         */
        INode() {}

        virtual ~INode() = default;

        /**
         * @brief Get the current lifecycle state of the node
         * @return Current LifecycleState enum value
         */
        
        virtual LifecycleState GetLifecycleState() const = 0;

        /**
         * @brief Initialize the node and all its ports
         * @return true if initialization succeeded, false otherwise
         *
         * Thread safety: NOT thread-safe. Must be called before Start().
         * Call sequence:
         *   1. Configure()  (optional, after construction)
         *   2. Init()       (must be called once)
         *   3. Start()      (starts worker threads)
         *
         * Called once before Start(). Should allocate resources,
         * validate configuration, and prepare for execution.
         * No threads are running during Init().
         */
        virtual bool Init() = 0;

        /**
         * @brief Start all port threads
         * @return true if all threads started successfully
         *
         * Thread safety: NOT thread-safe. Call only once, after Init().
         * Happens-before: All resources allocated in Init() are visible to worker threads.
         * Side effects: Spawns worker threads for each port.
         * 
         * After Start() returns:
         * - All port threads are running
         * - Queues are accepting data
         * - Thread-safe access to public methods (GetOutputEdges, GetInputEdges, etc.)
         */
        virtual bool Start() = 0;

        /**
         * @brief Wait for all port threads to complete
         *
         * Thread safety: Safe to call multiple times (idempotent).
         * Must be called after Stop().
         * Call sequence:
         *   1. Stop()  (request graceful shutdown)
         *   2. Join()  (wait for threads to exit)
         *   3. ~Node() (safe destruction)
         *
         * Blocks until all threads have finished execution.
         * No resource cleanup until Join() completes.
         * 
         * Happens-before: All side effects from worker threads are visible after Join().
         */
        virtual void Join() = 0;

        /**
         * @brief Wait for all port threads to complete with timeout
         * @param timeout_ms Maximum milliseconds to wait
         * @return true if all threads completed within timeout, false if timeout exceeded
         *
         * Thread safety: Safe to call multiple times.
         * Does NOT block indefinitely if timeout expires (respects deadline).
         * If timeout expires and threads still running, returns false immediately.
         * 
         * Best-effort semantics:
         * - Returns false if threads don't stop in time
         * - Does not force-kill threads
         * - Caller must handle timeout case appropriately
         * 
         * Happens-before (on success): Same as Join()
         * Happens-before (on timeout): None (threads still running)
         */
        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) = 0;
   
        /**
         * @brief Request all port threads to stop gracefully
         *
         * Thread safety: Safe to call multiple times (idempotent).
         * Non-blocking call that signals threads to exit.
         * Does NOT wait for completion - use Join() for that.
         * 
         * Side effects:
         * - Sets stop flags (accessed via IsStopRequested())
         * - Disables input queues (Enqueue will fail)
         * - Disables output queues (Dequeue will fail)
         * - Threads will exit their event loops
         * 
         * Typical sequence:
         *   1. Stop()  (signal graceful shutdown)
         *   2. Join()  (wait for threads to exit)
         *   3. ~Node() (safe to destroy)
         *
         * Exception safety: No exceptions thrown (noexcept).
         * Memory ordering: Uses acquire-release semantics for flags.
         */
        virtual void Stop() = 0;

        virtual std::span<const PortInfo> OutputPorts() const { return std::span<const PortInfo>(); }
        virtual std::span<const PortInfo> InputPorts() const { return std::span<const PortInfo>(); }

        /**
         * @brief Register an output edge index for this node
         * @param port_id Output port number
         * @param edge_idx Edge index in graph
         *
         * Thread safety: Safe to call concurrently from multiple threads.
         * Protected by edge_mutex_ to prevent data races.
         * Typically called during graph construction before Start().
         * Can be called safely during concurrent graph operations.
         *
         * Called by Graph during edge construction to track which edges
         * are connected to each output port.
         * 
         * Memory ordering: Acquires mutex lock (full synchronization).
         * Exception safety: Strong guarantee (no partial updates on map).
         */
        void RegisterOutputEdge(std::size_t port_id, std::size_t edge_idx)
        {
            std::unique_lock<std::mutex> lock(edge_mutex_);
            output_edges_[port_id].push_back(edge_idx);
        }

        /**
         * @brief Register an input edge index for this node
         * @param port_id Input port number
         * @param edge_idx Edge index in graph
         *
         * Thread safety: Safe to call concurrently from multiple threads.
         * Protected by edge_mutex_ to prevent data races.
         * Typically called during graph construction before Start().
         * Can be called safely during concurrent graph operations.
         *
         * Called by Graph during edge construction to track which edges
         * are connected to each input port.
         * 
         * Memory ordering: Acquires mutex lock (full synchronization).
         * Exception safety: Strong guarantee (no partial updates on map).
         */
        void RegisterInputEdge(std::size_t port_id, std::size_t edge_idx)
        {
            std::unique_lock<std::mutex> lock(edge_mutex_);
            input_edges_[port_id].push_back(edge_idx);
        }

        /**
         * @brief Get all edge indices connected to an output port
         * @param port_id Output port number
         * @return Copy of edge indices vector (safe for concurrent access)
         *
         * Thread safety: Safe to call from multiple threads concurrently.
         * Returns a VALUE (not reference) to avoid shared mutable state issues.
         * Caller gets independent copy that won't be invalidated by concurrent
         * RegisterOutputEdge calls.
         * 
         * Performance: O(n) where n = number of edges on this port.
         * The copy ensures no reference invalidation even during concurrent
         * register operations.
         * 
         * Memory ordering: Acquires mutex lock to ensure consistent snapshot.
         * Exception safety: Strong guarantee (copy succeeds or throws).
         */
        std::vector<std::size_t> GetOutputEdges(std::size_t port_id)
        {
            std::unique_lock<std::mutex> lock(edge_mutex_);
            auto it = output_edges_.find(port_id);
            return (it != output_edges_.end()) ? it->second : std::vector<std::size_t>();
        }

        /**
         * @brief Get all edge indices connected to an input port
         * @param port_id Input port number
         * @return Copy of edge indices vector (safe for concurrent access)
         *
         * Thread safety: Safe to call from multiple threads concurrently.
         * Returns a VALUE (not reference) to avoid shared mutable state issues.
         * Caller gets independent copy that won't be invalidated by concurrent
         * RegisterInputEdge calls.
         * 
         * Performance: O(n) where n = number of edges on this port.
         * The copy ensures no reference invalidation even during concurrent
         * register operations.
         * 
         * Memory ordering: Acquires mutex lock to ensure consistent snapshot.
         * Exception safety: Strong guarantee (copy succeeds or throws).
         */
        std::vector<std::size_t> GetInputEdges(std::size_t port_id)
        {
            std::unique_lock<std::mutex> lock(edge_mutex_);
            auto it = input_edges_.find(port_id);
            return (it != input_edges_.end()) ? it->second : std::vector<std::size_t>();
        }

        // =====================================================================
        // Visualization & Serialization Support
        // =====================================================================
        
        /**
         * @brief Get input port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all input ports
         */
        virtual std::vector<PortMetadata> GetInputPortMetadata() const {
            return std::vector<PortMetadata>();
        }
        
        /**
         * @brief Get output port metadata for visualization
         * 
         * Override in derived classes to provide runtime port information.
         * Used by the NodeSerializer template to export port list to JSON.
         * 
         * @return Vector of PortMetadata for all output ports
         */
        virtual std::vector<PortMetadata> GetOutputPortMetadata() const {
            return std::vector<PortMetadata>();
        }
    
    protected:
        /**
         * @brief Get the number of input ports
         * 
         * Must be implemented by derived classes (SourceNodeBase, SinkNodeBase, etc.)
         * 
         * @return Number of input ports (0 for source nodes)
         */
        //virtual int GetInputPortCount() const { return -1; }

        /**
         * @brief Get the number of output ports
         * 
         * Must be implemented by derived classes (SourceNodeBase, SinkNodeBase, etc.)
         * 
         * @return Number of output ports (0 for sink nodes)
         */
        //virtual int GetOutputPortCount() const { return -1; };

        /**
         * @brief Try to cast this node to a specific interface type
         * 
         * This method allows safe type queries at runtime. Derived classes can override
         * this to support casting to their specific interfaces (e.g., IDataInjectionSource).
         * 
         * The type_name parameter should match a known interface or base class name.
         * Common type names:
         * - "IDataInjectionSource" - returns pointer to IDataInjectionSource if this is a CSV node
         * 
         * @param type_name Null-terminated string with interface name to query for
         * @return void* pointer to the requested interface (cast as needed), or nullptr if not supported
         * 
         * Default implementation returns nullptr. CSV and other specialized nodes
         * should override this method.
         * 
         * Example usage (with proper includes):
         * @code
         * auto data_injection_config = static_cast<IDataInjectionSource*>(node->TryGetInterface("IDataInjectionSource"));
         * if (data_injection_config) {
         *     // This is a CSV node, use it as IDataInjectionSource
         * }
         * @endcode
         */
        virtual void* TryGetInterface(const char* /*type_name*/) {
            return nullptr;  // Default: no special interfaces
        }
    
    private:
        std::unordered_map<std::size_t, std::vector<std::size_t>> output_edges_;
        std::unordered_map<std::size_t, std::vector<std::size_t>> input_edges_;
        std::mutex edge_mutex_;  // Synchronizes concurrent edge registration
    };

} // namespace graph

