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
#include <optional>
#include <log4cxx/logger.h>

namespace graph
{
    /**
     * @brief Output port with thread synchronization primitives
     * @tparam P Port type
     *
     * IOutputFn adds thread management (std::thread, mutex, condition_variable)
     * to the base queue management. Used by OutputFn for single-instance output ports.
     * Provides Dequeue() method for pulling generated data from queue.
     */
    template <typename P>
    class IOutputFn : public IFn<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;
        virtual ~IOutputFn() = default;

        std::optional<T> Dequeue(std::integral_constant<std::size_t, port_id>)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "IOutputFn port " << port_id << " Dequeue.");
            T data;
            if (IFn<P>::Dequeue(data))
            {
                return data;
            }
            else
            {
                IFn<P>::SetStopRequest();
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "IOutputFn port " << port_id << " SetStopRequest.");
                return std::nullopt;
            }
        }

    protected:
        std::thread thread_;
        std::mutex mtx_;
        std::condition_variable cv_;
    };

    /**
     * @brief Concrete output port with metrics collection and producer pattern
     * @tparam P Port type
     *
     * OutputFn implements the producer pattern with an abstract Produce() method
     * that derived nodes override. A dedicated thread runs OutputFnThreadFunc()
     * which calls Produce() and enqueues the generated messages.
     *
     * Thread-safe metrics collection tracks:
     * - produce_calls: Number of Produce() invocations
     * - total_produce_time_ns: Total time spent in Produce()
     * - total_iterations: Number of enqueue operations
     * - thread_active: Whether thread is actively processing
     */
    template <typename P>
    class OutputFn : public IOutputFn<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;

        virtual ~OutputFn() {
            try {
                if (this->thread_.joinable()) {
                    this->thread_.join();
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "OutputFn thread join failed: " << e.what());
            } catch (...) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "OutputFn thread join failed: unknown exception");
            }
        }

        /// Abstract method for derived nodes to implement message generation
        /// @param Port identifier (integral_constant for compile-time port ID)
        /// @return Generated message or std::nullopt to stop generation
        virtual std::optional<T> Produce(std::integral_constant<std::size_t, port_id>) = 0;

        /// Start the producer thread
        bool Start()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "OutputFn port " << port_id << " Start.");
            auto t = [this]() -> void
            {
                OutputFnThreadFunc();
            };
            this->thread_ = std::thread(t);
            return true;
        }

        /// Enqueue a produced value into the output queue
        bool Enqueue(std::integral_constant<std::size_t, port_id>, T value)
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "OutputFn port " << port_id << " Enqueue.");
            return IFn<P>::Enqueue(value);
        }

        /// Join producer thread (blocking)
        void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "OutputFn port " << port_id << " Join.");
            if (this->thread_.joinable()) {
                this->thread_.join();
            }
        }

        /// Join producer thread with timeout
        /// @param timeout Maximum time to wait for thread completion
        /// @return true if thread completed, false if timeout exceeded
        bool JoinWithTimeout(std::chrono::milliseconds timeout) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "OutputFn port " << port_id << " JoinWithTimeout.");
            if (!this->thread_.joinable())
                return true;

            std::unique_lock lock(this->mtx_);
            bool stopped = this->cv_.wait_for(lock, timeout, [&] { return this->IsStopRequested(); });
            lock.unlock();  // Release lock before joining

            if (!stopped)
            {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "OutputFn thread did not finish within timeout");
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
            IFn<P>::GetQueue().EnableMetrics(enabled);
        }

        /// Retrieve collected metrics (thread-safe read)
        const graph::ThreadMetrics& GetThreadMetrics() const {
            return thread_metrics_;
        }

        /// Reset all collected metrics
        void ResetMetrics() {
            thread_metrics_.total_iterations.store(0, std::memory_order_release);
            thread_metrics_.produce_calls.store(0, std::memory_order_release);
            thread_metrics_.total_produce_time_ns.store(0, std::memory_order_release);
            thread_metrics_.total_idle_time_ns.store(0, std::memory_order_release);
            IFn<P>::GetQueue().ResetMetrics();
        }

    private:
        mutable ThreadMetrics thread_metrics_;
        std::atomic<bool> metrics_enabled_{false};

        void OutputFnThreadFunc()
        {
            const std::size_t port_id = OutputFn<P>::port_id;
            bool metrics_enabled = metrics_enabled_.load(std::memory_order_acquire);
            
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "OutputFn port " << port_id << " running.");
            
            while (!IOutputFn<P>::IsStopRequested())
            {
                thread_metrics_.thread_active.store(true, std::memory_order_release);
                
                // ================================================================
                // METRICS: Start timer for Produce() operation
                // ================================================================
                auto produce_start = metrics_enabled 
                    ? std::chrono::high_resolution_clock::now() 
                    : std::chrono::high_resolution_clock::time_point{};
                
                auto maybe = Produce(std::integral_constant<std::size_t, port_id>{});
                
                // ================================================================
                // METRICS: End timer for Produce() and record statistics
                // ================================================================
                if (metrics_enabled && produce_start != std::chrono::high_resolution_clock::time_point{}) {
                    auto produce_end = std::chrono::high_resolution_clock::now();
                    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        produce_end - produce_start).count();
                    
                    // Record atomically for thread safety
                    thread_metrics_.produce_calls.fetch_add(1, std::memory_order_acq_rel);
                    thread_metrics_.total_produce_time_ns.fetch_add(duration_ns, 
                        std::memory_order_acq_rel);
                }
                
                if (!maybe.has_value())
                {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                                "OutputFn port " << port_id << " Produce Stopped.");
                    IOutputFn<P>::SetStopRequest();
                    break; // Generation finished
                }
                if (!IFn<P>::Enqueue(maybe.value()))
                {
                    LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                                "OutputFn port " << port_id << " Queue Disabled.");
                    IOutputFn<P>::SetStopRequest();
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
                          "OutputFn port " << port_id << " stopped.");
        }
    };

}
