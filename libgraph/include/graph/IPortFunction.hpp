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
 * @file IPortFunction.hpp
 * @brief Unified interface combining port metadata, queue management, and threading
 *
 * This file defines IPortFunction: a virtual interface that unifies:
 * - Port metadata (ID, type name, direction)
 * - Queue operations (capacity, size, enqueue/dequeue)
 * - Thread management (init, start, stop, join)
 * - Type-safe queue access
 *
 * IPortFunction eliminates the need to store separate PortInfo structures by
 * embedding metadata directly into the port function instance. This enables:
 * - Heterogeneous port collections (vector<unique_ptr<IPortFunction>>)
 * - Polymorphic runtime dispatch
 * - Dynamic port management
 * - Simplified architecture
 *
 * @see PortFunction.hpp for concrete template implementation
 * @see Nodes.hpp for usage in node classes
 */

#pragma once

#include <cstddef>
#include <chrono>
#include <string_view>
#include "graph/PortSpec.hpp"
#include "core/ActiveQueue.hpp"
#include "core/TypeInfo.hpp"

namespace graph {

    // ===================================================================================
    // Port Function Interface - Unified Metadata + Queue + Threading
    // -----------------------------------------------------------------------------------
    // Abstract virtual interface for port functions. Combines all metadata and
    // operations into a single polymorphic entity.
    // ===================================================================================

    /**
     * @brief Virtual interface for port functions with integrated metadata
     *
     * IPortFunction provides a unified interface for port-level operations:
     *
     * **Metadata Access** (from PortInfo):
     * - Port ID (0-based index within node)
     * - Type name (human-readable string)
     * - Direction (Input or Output)
     *
     * **Queue Operations** (from IFn):
     * - Capacity management (SetCapacity)
     * - Size queries (GetQueueSize)
     * - Initialization (Init)
     *
     * **Thread Management** (from IFn):
     * - Start worker thread
     * - Stop graceful shutdown
     * - Join with blocking wait
     * - JoinWithTimeout with deadline
     *
     * **Type-Safe Access**:
     * - GetQueueIfType<T>() for safe downcast to queue<T>
     * - Uses runtime type checking to prevent unsafe casts
     *
     * Concrete implementations are provided by PortFunction<P> template.
     *
     * Design Rationale:
     * - Eliminates variadic inheritance in node definitions
     * - Enables heterogeneous port collections
     * - Supports arbitrary port counts without specialization
     * - Moves metadata from compile-time arrays to instance data
     *
     * @see PortFunction<P> for template implementation
     * @see Port<T, ID> for type-safe port definition
     */
    class IPortFunction {
    public:
        virtual ~IPortFunction() = default;

        // ====================================================================
        // Metadata Access - Unified Information About This Port
        // ====================================================================

        /**
         * @brief Get the port's unique identifier within the node
         * @return Port ID (0-based index)
         *
         * The port ID is assigned at compile-time (via Port<T, ID>::id)
         * and corresponds to the position in the node's port collection.
         */
        virtual std::size_t GetPortId() const = 0;

        /**
         * @brief Get human-readable type name of data flowing through this port
         * @return String view of type name (e.g., "int", "std::string", "Message")
         *
         * Type name is extracted from the port's data type at compile-time
         * using TypeInfo utilities. Suitable for logging, visualization, and reflection.
         */
        virtual std::string_view GetTypeName() const = 0;

        /**
         * @brief Get direction of data flow through this port
         * @return PortDirection::Input or PortDirection::Output
         *
         * Determines whether this port receives (Input) or produces (Output) data.
         */
        virtual PortDirection GetDirection() const = 0;

        // ====================================================================
        // Queue Operations - Manage Data Storage
        // ====================================================================

        /**
         * @brief Set the maximum capacity of this port's queue
         * @param capacity Maximum number of items the queue can hold
         *
         * Used for backpressure control. Queues may block or drop items
         * when capacity is exceeded, depending on configuration.
         */
        virtual void SetCapacity(std::size_t capacity) = 0;

        /**
         * @brief Get the current number of items in this port's queue
         * @return Number of queued items (size_t)
         *
         * Thread-safe snapshot of queue size at call time.
         * Used for monitoring and debugging.
         */
        virtual std::size_t GetQueueSize() const = 0;

        /**
         * @brief Initialize this port's queue and resources
         * @return true if initialization succeeded, false otherwise
         *
         * Called once before Start(). Prepares the queue for use
         * (e.g., allocates buffers, enables the queue).
         */
        virtual bool Init() = 0;

        // ====================================================================
        // Thread Management - Lifecycle Operations
        // ====================================================================

        /**
         * @brief Start the worker thread for this port
         * @return true if thread started successfully, false on error
         *
         * Spawns the worker thread that processes Produce/Consume operations.
         * Must be called after Init() and before any data operations.
         */
        virtual bool Start() = 0;

        /**
         * @brief Request graceful shutdown of this port's worker thread
         *
         * Signals the worker thread to stop, disables the queue,
         * and begins cleanup. Does not wait for thread termination.
         * Must call Join() to wait for actual thread exit.
         */
        virtual void Stop() = 0;

        /**
         * @brief Wait for this port's worker thread to complete
         *
         * Blocks until the worker thread exits. Must be called after Stop()
         * to ensure all resources are cleaned up.
         *
         * Safe to call multiple times (idempotent after first call).
         */
        virtual void Join() = 0;

        /**
         * @brief Wait for worker thread with timeout
         * @param timeout_ms Maximum time to wait in milliseconds
         * @return true if thread completed within timeout, false if timeout exceeded
         *
         * Non-blocking alternative to Join(). Useful for shutdown with deadlines.
         * If timeout exceeded, thread may still be running.
         */
        virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) = 0;

        // ====================================================================
        // Type-Safe Queue Access - Polymorphic Downcast
        // ====================================================================

        /**
         * @brief Attempt type-safe access to the queue as ActiveQueue<T>
         * @tparam T Expected data type flowing through this port
         * @return Pointer to ActiveQueue<T> if type matches, nullptr otherwise
         *
         * Performs runtime type checking by comparing type names.
         * Allows safe polymorphic access to the underlying typed queue.
         *
         * Example:
         * @code
         *   IPortFunction& port = ...;  // Unknown type
         *   if (auto* queue = port.GetQueueIfType<int>()) {
         *       queue->Enqueue(42);  // Safe: type checked at runtime
         *   }
         * @endcode
         *
         * This method provides type safety without RTTI (dynamic_cast),
         * using string-based type comparison instead.
         */
        template <typename T>
        core::ActiveQueue<T>* GetQueueIfType() {
            // Use TypeName<T> from TypeInfo to compare at runtime
            if (GetTypeName() == TypeName<T>()) {
                return const_cast<core::ActiveQueue<T>*>(
                    static_cast<const core::ActiveQueue<T>*>(GetQueueVoid())
                );
            }
            return nullptr;
        }

        template <typename T>
        const core::ActiveQueue<T>* GetQueueIfType() const {
            if (GetTypeName() == TypeName<T>()) {
                return static_cast<const core::ActiveQueue<T>*>(GetQueueVoid());
            }
            return nullptr;
        }

    protected:
        /**
         * @brief Internal unsafe access to queue void pointer
         * @return Opaque pointer to the underlying ActiveQueue<T>
         *
         * Used internally by GetQueueIfType<T> to access the typed queue.
         * Implementations must override this to return &queue_ cast to void*.
         *
         * This is an internal implementation detail. Use GetQueueIfType<T>()
         * from the public interface for type-safe access.
         */
        virtual const void* GetQueueVoid() const = 0;
    };

}  // namespace graph

