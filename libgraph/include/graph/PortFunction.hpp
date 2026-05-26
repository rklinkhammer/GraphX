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
 * @file PortFunction.hpp
 * @brief Concrete template implementation of IPortFunction
 *
 * Provides PortFunction<P>: a typed wrapper combining:
 * - Queue management for type P::type
 * - Metadata from Port<T, ID>
 * - Thread management (start, stop, join)
 *
 * This is the concrete implementation that IFn functionality was previously
 * providing, but now through the polymorphic IPortFunction interface.
 *
 * @see IPortFunction.hpp for virtual interface definition
 * @see Nodes.hpp for Port<T, ID> definition
 */

#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "graph/IPortFunction.hpp"
#include "core/ActiveQueue.hpp"
#include "core/TypeInfo.hpp"

namespace graph {

    // ===================================================================================
    // Concrete Port Function Implementation
    // -----------------------------------------------------------------------------------
    // Template specialization of IPortFunction for a specific Port<T, ID> type.
    // ===================================================================================

    /**
     * @brief Concrete implementation of IPortFunction for a specific port type
     *
     * PortFunction<P> combines:
     * - Port metadata (extracted from P)
     * - Typed queue (ActiveQueue<typename P::type>)
     * - Thread management
     *
     * Template Parameters:
     * - P: Port type (must have P::type and P::id members)
     *
     * This replaces the role of IFn<P> + PortInfo + static port tables,
     * unifying them into a single polymorphic instance.
     *
     * Usage:
     * @code
     *   // Create a port function for Port<int, 0> as input
     *   auto port = std::make_unique<PortFunction<Port<int, 0>>>(PortDirection::Input);
     *   
     *   // Metadata access (polymorphic)
     *   std::cout << "Port " << port->GetPortId() 
     *             << " type: " << port->GetTypeName() << std::endl;
     *   
     *   // Queue operations
     *   port->Init();
     *   port->Start();
     *   
     *   // Type-safe queue access
     *   if (auto* q = port->GetQueueIfType<int>()) {
     *       q->Enqueue(42);
     *   }
     * @endcode
     *
     * @tparam P Port type (Port<T, ID>)
     */
    template <typename P>
    class PortFunction : public IPortFunction {
    public:
        // ====================================================================
        // Type Definitions
        // ====================================================================

        using PortType = P;                        ///< The Port<T, ID> type
        using T = typename P::type;                ///< Data type for this port
        static constexpr std::size_t port_id = P::id; ///< Port identifier

        // ====================================================================
        // Construction / Destruction
        // ====================================================================

        /**
         * @brief Construct a port function with specified direction
         * @param dir Port direction (Input or Output)
         */
        explicit PortFunction(PortDirection dir = PortDirection::Input)
            : direction_(dir) {}

        virtual ~PortFunction() {
            try {
                if (thread_.joinable()) {
                    Stop();
                    thread_.join();
                }
            } catch (...) {
                // Suppress exceptions in destructor
            }
        }

        // ====================================================================
        // Metadata Access (IPortFunction implementation)
        // ====================================================================

        std::size_t GetPortId() const override {
            return port_id;
        }

        std::string_view GetTypeName() const override {
            return TypeName<T>();
        }

        PortDirection GetDirection() const override {
            return direction_;
        }

        // ====================================================================
        // Queue Operations (IPortFunction implementation)
        // ====================================================================

        void SetCapacity(std::size_t capacity) override {
            queue_.SetCapacity(capacity);
        }

        std::size_t GetQueueSize() const override {
            return queue_.Size();
        }

        bool Init() override {
            queue_.Enable();
            return true;
        }

        // ====================================================================
        // Thread Management (IPortFunction implementation)
        // ====================================================================

        bool Start() override {
            // Subclasses (InputFn, OutputFn) will override to spawn actual worker threads
            // Default implementation: just return true (no worker thread)
            return true;
        }

        void Stop() override {
            queue_.Disable();
            SetStopRequest();
        }

        void Join() override {
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
            if (!thread_.joinable()) {
                return true;  // Already finished
            }

            // Use condition variable with timeout
            std::unique_lock<std::mutex> lock(mtx_);
            bool completed = cv_.wait_for(lock, timeout_ms, [this] {
                return IsStopRequested();
            });

            if (completed && thread_.joinable()) {
                thread_.join();
            }

            return completed;
        }

        // ====================================================================
        // Type-Safe Queue Access
        // ====================================================================

        /**
         * @brief Get mutable reference to the typed queue
         * @return Reference to ActiveQueue<T>
         *
         * This provides typed access for use within the node implementation.
         * For polymorphic access through IPortFunction, use GetQueueIfType<T>().
         */
        core::ActiveQueue<T>& GetQueue() {
            return queue_;
        }

        /**
         * @brief Get const reference to the typed queue
         */
        const core::ActiveQueue<T>& GetQueue() const {
            return queue_;
        }

        // ====================================================================
        // Stop Request Signaling
        // ====================================================================

        bool IsStopRequested() const {
            return stop_requested_.load(std::memory_order_acquire);
        }

        void SetStopRequest() {
            stop_requested_.store(true, std::memory_order_release);
        }

        void ClearStopRequest() {
            stop_requested_.store(false, std::memory_order_release);
        }

    protected:
        // ====================================================================
        // Internal Queue Void Pointer Access (IPortFunction implementation)
        // ====================================================================

        const void* GetQueueVoid() const override {
            return &queue_;
        }

        // ====================================================================
        // Protected Members for Subclass Use
        // ====================================================================

        std::thread thread_;
        std::mutex mtx_;
        std::condition_variable cv_;
        std::atomic<bool> stop_requested_{false};

    private:
        // ====================================================================
        // Private Members
        // ====================================================================

        PortDirection direction_;
        core::ActiveQueue<T> queue_;
    };

}  // namespace graph

