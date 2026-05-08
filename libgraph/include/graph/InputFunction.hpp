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

#include "IFnBase.hpp"
#include "ThreadMetrics.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <log4cxx/logger.h>

namespace graph
{
    /**
     * @brief Input port interface that uses a shared unified queue (for MergeNode)
     * @tparam P Port type (must have P::type and P::id)
     * 
     * Unlike IInputFn, IInputCommonFn does NOT create its own queue.
     * Instead, it inherits from IFnBase and uses a queue provided via constructor.
     * This is used by MergeNode where all N input ports share a single unified queue.
     */
    template <typename P>
    class IInputCommonFn : public IFnBase<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;

        /// Constructor that takes the unified queue reference
        /// @param unified_queue The shared queue used by all input ports
        explicit IInputCommonFn(core::ActiveQueue<T>& unified_queue)
            : IFnBase<P>(unified_queue) {}

        virtual ~IInputCommonFn() = default;
    };

    /**
     * @brief Input port with thread synchronization primitives
     * @tparam P Port type
     *
     * IInputFn adds thread management (std::thread, mutex, condition_variable)
     * to the base queue management. Used by InputFn for single-instance input ports.
     */
    template <typename P>
    class IInputFn : public IFn<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;
        virtual ~IInputFn() = default;

    protected:
        std::thread thread_;
        std::mutex mtx_;
        std::condition_variable cv_;
    };

    /**
     * @brief Concrete input port with metrics collection and consumer pattern
     * @tparam P Port type
     *
     * InputFn implements the consumer pattern with an abstract Consume() method
     * that derived nodes override. A dedicated thread runs InputFnThreadFunc()
     * which dequeues messages and calls Consume().
     *
     * Thread-safe metrics collection tracks:
     * - consume_calls: Number of Consume() invocations
     * - total_consume_time_ns: Total time spent in Consume()
     * - total_iterations: Number of dequeue operations
     * - thread_active: Whether thread is actively processing
     */
    template <typename P>
    class InputFn : public IInputFn<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;

        virtual ~InputFn() {
            try {
                if (this->thread_.joinable()) {
                    this->thread_.join();
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "InputFn thread join failed: " << e.what());
            } catch (...) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "InputFn thread join failed: unknown exception");
            }
        }

        /// Abstract method for derived nodes to implement message handling
        /// @param value The dequeued message
        /// @param Port identifier (integral_constant for compile-time port ID)
        /// @return true to continue processing, false to stop this input port
        virtual bool Consume(const T &value, std::integral_constant<std::size_t, port_id>) = 0;

        /// Start the consumer thread
        bool Start()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InputFn port " << port_id << " Start.");
            auto t = [this]() -> void
            {
                InputFnThreadFunc();
            };
            this->thread_ = std::thread(t);
            return true;
        }

        /// Join consumer thread (blocking)
        void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InputFn port " << port_id << " Join.");
            if (this->thread_.joinable()) {
                this->thread_.join();
            }
        }

        /// Join consumer thread with timeout
        /// @param timeout Maximum time to wait for thread completion
        /// @return true if thread completed, false if timeout exceeded
        bool JoinWithTimeout(std::chrono::milliseconds timeout) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InputFn port " << port_id << " JoinWithTimeout.");
            if (!this->thread_.joinable())
                return true;

            std::unique_lock lock(this->mtx_);
            bool stopped = this->cv_.wait_for(lock, timeout, [&] { return this->IsStopRequested(); });
            lock.unlock();  // Release lock before joining

            if (!stopped)
            {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "InputFn thread did not finish within timeout");
                return false;  // Don't join if timeout expired
            }

            if (this->thread_.joinable()) {
                this->thread_.join();
            }
            return true;
        }

        // ================================================================
        // METRICS: Enable/disable metrics collection
        // ================================================================
        /// Enable/disable metrics collection for this port
        void EnableMetrics(bool enabled = true) {
            metrics_enabled_.store(enabled, std::memory_order_release);
            IInputFn<P>::GetQueue().EnableMetrics(enabled);
        }

        /// Retrieve collected metrics (thread-safe read)
        const graph::ThreadMetrics& GetThreadMetrics() const {
            return thread_metrics_;
        }

        /// Reset all collected metrics
        void ResetMetrics() {
            thread_metrics_.total_iterations.store(0, std::memory_order_release);
            thread_metrics_.consume_calls.store(0, std::memory_order_release);
            thread_metrics_.total_consume_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_idle_time_ns.store(0, std::memory_order_release);
            IInputFn<P>::GetQueue().ResetMetrics();
        }

    private:
        mutable graph::ThreadMetrics thread_metrics_;
        std::atomic<bool> metrics_enabled_{false};

        void InputFnThreadFunc()
        {
            const std::size_t port_id = InputFn<P>::port_id;
            bool metrics_enabled = metrics_enabled_.load(std::memory_order_acquire);
            
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "InputFn port " << port_id << " running.");
            
            while (!IInputFn<P>::IsStopRequested())
            {
                thread_metrics_.thread_active.store(true, std::memory_order_release);
                
                T data;
                if (IInputFn<P>::GetQueue().Dequeue(data))
                {
                    // ================================================================
                    // METRICS: Start timer for Consume() operation
                    // ================================================================
                    auto consume_start = metrics_enabled 
                        ? std::chrono::high_resolution_clock::now() 
                        : std::chrono::high_resolution_clock::time_point{};
                    
                    bool continue_processing = Consume(data, 
                        std::integral_constant<std::size_t, port_id>{});
                    
                    // ================================================================
                    // METRICS: End timer for Consume() and record statistics
                    // ================================================================
                    if (metrics_enabled && consume_start != std::chrono::high_resolution_clock::time_point{}) {
                        auto consume_end = std::chrono::high_resolution_clock::now();
                        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            consume_end - consume_start).count();
                        
                        // Record atomically for thread safety
                        thread_metrics_.consume_calls.fetch_add(1, std::memory_order_acq_rel);
                        thread_metrics_.total_consume_time_ns.fetch_add(duration_ns, 
                            std::memory_order_acq_rel);
                    }
                    
                    if (!continue_processing)
                    {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                                    "InputFn port " << port_id << " Consume Stop.");
                        IInputFn<P>::SetStopRequest();
                        break; // Consumption signaled to stop
                    }
                }
                else
                {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                                "InputFn port " << port_id << " Queue Disabled.");
                    IInputFn<P>::SetStopRequest();
                    break; // Queue disabled
                }
                
                // ================================================================
                // METRICS: Increment iteration counter
                // ================================================================
                if (metrics_enabled) {
                    thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            
            thread_metrics_.thread_active.store(false, std::memory_order_release);
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "InputFn port " << port_id << " stopped.");
        }
    };

}
