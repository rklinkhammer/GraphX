/**
 * @file PortFunction.hpp
 * @brief Port Function Graph runtime support.
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
/**
 * @class PortFunction
 * @brief Port Function type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
    /**
     * @class PortFunction
     * @brief Port Function type.
     *
     * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
     */
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

        /**
         * @brief Releases resources owned by Port Function.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         */
        virtual ~PortFunction() {
            try {
                if (thread_.joinable()) {
                    /**
                     * @brief Performs the Stop lifecycle step.
                     *
                     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                     * @return Method-specific result, status, or produced value when the signature provides one.
                     */
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

        /**
         * @brief Returns the Port ID.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::size_t GetPortId() const override {
            return port_id;
        }

        /**
         * @brief Returns the Type Name.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::string_view GetTypeName() const override {
            return TypeName<T>();
        }

        /**
         * @brief Returns the Direction.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        PortDirection GetDirection() const override {
            return direction_;
        }

        /**
         * @brief Returns the Transport Type Name.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::string_view GetTransportTypeName() const override {
            return TypeName<core::ActiveQueue<T>>();
        }

        // ====================================================================
        // Queue Operations (IPortFunction implementation)
        // ====================================================================

        /**
         * @brief Updates the Capacity.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param capacity Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void SetCapacity(std::size_t capacity) override {
            queue_.SetCapacity(capacity);
        }

        /**
         * @brief Returns the Queue Size.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::size_t GetQueueSize() const override {
            return queue_.Size();
        }

        /**
         * @brief Performs the Init lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool Init() override {
            queue_.Enable();
            return true;
        }

        // ====================================================================
        // Thread Management (IPortFunction implementation)
        // ====================================================================

        /**
         * @brief Performs the Start lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool Start() override {
            // Subclasses (InputFn, OutputFn) will override to spawn actual worker threads
            // Default implementation: just return true (no worker thread)
            return true;
        }

        /**
         * @brief Performs the Stop lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void Stop() override {
            queue_.Disable();
            /**
             * @brief Updates the Stop Request.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            SetStopRequest();
        }

        /**
         * @brief Performs the Join lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void Join() override {
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        /**
         * @brief Executes the Join With Timeout operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param timeout_ms Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
            if (!thread_.joinable()) {
                return true;  // Already finished
            }

            // Use condition variable with timeout
            /**
             * @brief Executes the Lock operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param mtx_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            std::unique_lock<std::mutex> lock(mtx_);
            bool completed = cv_.wait_for(lock, timeout_ms, [this] {
                return IsStopRequested();
            });

            if (completed && thread_.joinable()) {
                thread_.join();
            }

            return completed;
        }

        std::expected<void, RuntimePortConnectError>
        /**
         * @brief Executes the Connect To operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param destination Input or configuration value consumed by the method.
         * @param capacity Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        ConnectTo(IPortFunction& destination, std::size_t capacity) override {
            if (GetDirection() != PortDirection::Output ||
                destination.GetDirection() != PortDirection::Input) {
                return std::unexpected(RuntimePortConnectError::DirectionMismatch);
            }

            if (GetTypeName() != destination.GetTypeName()) {
                return std::unexpected(RuntimePortConnectError::PayloadTypeMismatch);
            }

            /**
             * @brief Updates the Capacity.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param capacity Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            SetCapacity(capacity);
            destination.SetCapacity(capacity);
            return {};
        }

        std::expected<bool, RuntimePortConnectError>
        /**
         * @brief Processes data through the Transfer To operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param destination Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        TransferTo(IPortFunction& destination) override {
            if (GetDirection() != PortDirection::Output ||
                destination.GetDirection() != PortDirection::Input) {
                return std::unexpected(RuntimePortConnectError::DirectionMismatch);
            }

            if (GetTypeName() != destination.GetTypeName()) {
                return std::unexpected(RuntimePortConnectError::PayloadTypeMismatch);
            }

            auto* destination_queue = destination.GetQueueIfType<T>();
            if (!destination_queue) {
                return std::unexpected(RuntimePortConnectError::NullDestination);
            }

            // Avoid dequeueing when destination cannot currently accept data.
            const auto destination_capacity = destination_queue->Capacity();
            if (destination_capacity > 0 &&
                destination_queue->Size() >= destination_capacity) {
                return std::unexpected(RuntimePortConnectError::TransferFailed);
            }

            T item{};
            if (!queue_.DequeueNonBlocking(item)) {
                return false;
            }

            if (!destination_queue->Enqueue(std::move(item))) {
                // Best-effort recovery to avoid dropping payloads if destination
                // became full between the capacity check and enqueue.
                static_cast<void>(queue_.Enqueue(std::move(item)));
                return std::unexpected(RuntimePortConnectError::TransferFailed);
            }

            return true;
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

        /**
         * @brief Reports whether Is Stop Requested is true.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        bool IsStopRequested() const {
            return stop_requested_.load(std::memory_order_acquire);
        }

        /**
         * @brief Updates the Stop Request.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void SetStopRequest() {
            stop_requested_.store(true, std::memory_order_release);
        }

        /**
         * @brief Executes the Clear Stop Request operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        void ClearStopRequest() {
            stop_requested_.store(false, std::memory_order_release);
        }

    protected:
        // ====================================================================
        // Internal Queue Void Pointer Access (IPortFunction implementation)
        // ====================================================================

        /**
         * @brief Returns the Queue Void.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
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

