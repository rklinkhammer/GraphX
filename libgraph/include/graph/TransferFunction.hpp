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

#include "InputFunction.hpp"
#include "OutputFunction.hpp"
#include "ThreadMetrics.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <log4cxx/logger.h>

namespace graph
{
    /**
     * @brief Base interface for transfer (interior node) ports
     * @tparam P Port type
     *
     * ITransferFn is a marker interface that combines input and output
     * port behaviors for nodes that both consume and produce data.
     */
    template <typename P>
    class ITransferFn : public IFn<P>
    {
    public:
        using T = typename P::type;
        static constexpr std::size_t port_id = P::id;
        virtual ~ITransferFn() = default;
    };

    /**
     * @brief Transfer function for interior nodes (data transformers)
     * @tparam Pin Input port type
     * @tparam Pout Output port type
     *
     * TransferFn combines input consumption and output production in a single
     * bidirectional port. Used by interior nodes that both consume and produce data.
     * 
     * Implements Transfer() pattern where derived nodes transform input data
     * into output data. A dedicated thread runs TransferFnThreadFunc() which
     * dequeues input, calls Transfer(), and enqueues output.
     *
     * Thread-safe metrics collection tracks both input and output operations:
     * - Input metrics: consume_calls, total_consume_time_ns, transfer_calls
     * - Output metrics: produce_calls, total_produce_time_ns, transfer_calls
     */
    template <typename Pin, typename Pout>
    class TransferFn : public IInputFn<Pin>, public IOutputFn<Pout>
    {
    public:
        using TIn = typename Pin::type;
        using TOut = typename Pout::type;
        static constexpr std::size_t in_port_id = Pin::id;
        static constexpr std::size_t out_port_id = Pout::id;

        virtual ~TransferFn() {
            try {
                // STEP 1: Signal all threads to stop
                SetStopRequest(true);
                IInputFn<Pin>::Stop();
                IOutputFn<Pout>::Stop();
                
                // STEP 2: Wait for threads to exit (now safe - they won't access members)
                if (thread_.joinable()) {
                    thread_.join();
                }
                if (IInputFn<Pin>::thread_.joinable()) {
                    IInputFn<Pin>::thread_.join();
                }
                if (IOutputFn<Pout>::thread_.joinable()) {
                    IOutputFn<Pout>::thread_.join();
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "TransferFn thread cleanup failed: " << e.what());
            } catch (...) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.node"),
                              "TransferFn thread cleanup failed: unknown exception");
            }
        }

        /// Abstract method for derived nodes to implement data transformation
        /// @param value The dequeued input message
        /// @param Input port identifier
        /// @param Output port identifier
        /// @return Transformed message or std::nullopt to skip output for this event
        virtual std::optional<TOut> Transfer(const TIn &value,
                                             std::integral_constant<std::size_t, in_port_id>, 
                                             std::integral_constant<std::size_t, out_port_id>) = 0;

        /// Initialize both input and output ports
        bool Init()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " Init.");
            return IInputFn<Pin>::Init() && IOutputFn<Pout>::Init();
        }

        /// Start the transfer thread
        bool Start()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " Start.");
            auto t = [this]() -> void
            {
                TransferFnThreadFunc();
            };
            thread_ = std::thread(t);
            return true;
        }

        /// Enable both input and output queues
        void EnableQueues()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " EnableQueues.");
            IInputFn<Pin>::EnableQueue();
            IOutputFn<Pout>::EnableQueue();
        }

        /// Disable both input and output queues
        void DisableQueues()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " DisableQueues.");
            IInputFn<Pin>::DisableQueue();
            IOutputFn<Pout>::DisableQueue();
        }

        /// Stop both input and output ports and the transfer thread
        void Stop()
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " Stop.");
            IInputFn<Pin>::Stop();
            IOutputFn<Pout>::Stop();
            SetStopRequest(true);
        }

        /// Join transfer thread (blocking)
        void Join() override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " Join.");
            IInputFn<Pin>::Join();
            IOutputFn<Pout>::Join();
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        /// Join transfer thread with timeout
        /// @param timeout_ms Maximum milliseconds to wait for thread completion
        /// @return true if thread completed within timeout, false otherwise
        bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override
        {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), "TransferFn ports " << in_port_id << "->" << out_port_id << " JoinWithTimeout.");
            bool input_ok = IInputFn<Pin>::JoinWithTimeout(timeout_ms);
            bool output_ok = IOutputFn<Pout>::JoinWithTimeout(timeout_ms);
      
            if (!input_ok || !output_ok)
            {
                LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"),
                             "TransferFn ports " << in_port_id << "->" << out_port_id << " timeout exceeded.");
                return false;
            }

            return true;
        }

        /// Check if stop has been requested
        bool IsStopRequested() const
        {
            return stop_requested_.load(std::memory_order_acquire);
        }

        /// Set/clear stop request
        void SetStopRequest(bool value)
        {
            stop_requested_.store(value, std::memory_order_release);
        }

        // ================================================================
        // METRICS: Enable/disable metrics collection for both sides
        // ================================================================
        
        /// Enable/disable metrics collection for input side (Consume operations)
        void EnableInputMetrics(bool enabled = true) {
            input_metrics_enabled_.store(enabled, std::memory_order_release);
            IInputFn<Pin>::GetQueue().EnableMetrics(enabled);
        }
        
        /// Enable/disable metrics collection for output side (Produce operations)
        void EnableOutputMetrics(bool enabled = true) {
            output_metrics_enabled_.store(enabled, std::memory_order_release);
            IOutputFn<Pout>::GetQueue().EnableMetrics(enabled);
        }
        
        /// Enable metrics for both sides
        void EnableMetrics(bool enabled = true) {
            EnableInputMetrics(enabled);
            EnableOutputMetrics(enabled);
        }
        
        /// Retrieve input-side metrics (Consume/Transfer timing)
        const graph::ThreadMetrics& GetInputMetrics() const {
            return input_thread_metrics_;
        }
        
        /// Retrieve output-side metrics (Produce/Transfer timing)
        const graph::ThreadMetrics& GetOutputMetrics() const {
            return output_thread_metrics_;
        }
        
        /// Reset input-side metrics
        void ResetInputMetrics() {
            input_thread_metrics_.total_iterations.store(0, std::memory_order_release);
            input_thread_metrics_.consume_calls.store(0, std::memory_order_release);
            input_thread_metrics_.transfer_calls.store(0, std::memory_order_release);
            input_thread_metrics_.total_consume_time_ns.store(0, std::memory_order_release);
            input_thread_metrics_.total_transfer_time_ns.store(0, std::memory_order_release);
            IInputFn<Pin>::GetQueue().ResetMetrics();
        }
        
        /// Reset output-side metrics
        void ResetOutputMetrics() {
            output_thread_metrics_.total_iterations.store(0, std::memory_order_release);
            output_thread_metrics_.transfer_calls.store(0, std::memory_order_release);
            output_thread_metrics_.produce_calls.store(0, std::memory_order_release);
            output_thread_metrics_.total_transfer_time_ns.store(0, std::memory_order_release);
            output_thread_metrics_.total_produce_time_ns.store(0, std::memory_order_release);
            IOutputFn<Pout>::GetQueue().ResetMetrics();
        }
        
        /// Reset all metrics (both sides)
        void ResetMetrics() {
            ResetInputMetrics();
            ResetOutputMetrics();
        }

        // ================================================================
        // QUEUE ACCESS: Public methods to access input and output queues
        // ================================================================
        
        /// Get reference to input queue (from IInputFn side)
        /// Disambiguates when Pin == Pout type by explicitly routing to IInputFn
        auto& GetInputQueue() {
            return IInputFn<Pin>::GetQueue();
        }
        
        /// Get const reference to input queue (from IInputFn side)
        const auto& GetInputQueue() const {
            return IInputFn<Pin>::GetQueue();
        }
        
        /// Get reference to output queue (from IOutputFn side)
        /// Disambiguates when Pin == Pout type by explicitly routing to IOutputFn
        auto& GetOutputQueue() {
            return IOutputFn<Pout>::GetQueue();
        }
        
        /// Get const reference to output queue (from IOutputFn side)
        const auto& GetOutputQueue() const {
            return IOutputFn<Pout>::GetQueue();
        }
        mutable graph::ThreadMetrics input_thread_metrics_;
        mutable graph::ThreadMetrics output_thread_metrics_;
        std::atomic<bool> input_metrics_enabled_{false};
        std::atomic<bool> output_metrics_enabled_{false};
        std::thread thread_;  // Separate worker thread for transfer function
        std::atomic<bool> stop_requested_{false};

        void TransferFnThreadFunc()
        {
            const std::size_t in_port_id = TransferFn<Pin, Pout>::in_port_id;
            const std::size_t out_port_id = TransferFn<Pin, Pout>::out_port_id;
            auto &inputq = IInputFn<Pin>::GetQueue();
            auto &outputq = IOutputFn<Pout>::GetQueue();
            
            bool input_metrics_enabled = input_metrics_enabled_.load(std::memory_order_acquire);
            bool output_metrics_enabled = output_metrics_enabled_.load(std::memory_order_acquire);

            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "TransferFn ports " << in_port_id << "->" << out_port_id << " TransferFnThreadFunc Entered.");
            
            while (!IsStopRequested())
            {
                input_thread_metrics_.thread_active.store(true, std::memory_order_release);
                output_thread_metrics_.thread_active.store(true, std::memory_order_release);
                
                TIn data_in;
                
                // ================================================================
                // METRICS: Track input dequeue timing (part of Consume)
                // ================================================================
                auto dequeue_start = input_metrics_enabled 
                    ? std::chrono::high_resolution_clock::now() 
                    : std::chrono::high_resolution_clock::time_point{};
                
                if (inputq.Dequeue(data_in))
                {
                    // ================================================================
                    // METRICS: Record dequeue time and increment consume counter
                    // ================================================================
                    if (input_metrics_enabled && dequeue_start != std::chrono::high_resolution_clock::time_point{}) {
                        auto dequeue_end = std::chrono::high_resolution_clock::now();
                        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            dequeue_end - dequeue_start).count();
                        
                        input_thread_metrics_.consume_calls.fetch_add(1, std::memory_order_acq_rel);
                        input_thread_metrics_.total_consume_time_ns.fetch_add(duration_ns, 
                            std::memory_order_acq_rel);
                    }
                    
                    // ================================================================
                    // METRICS: Start timer for Transfer() operation
                    // ================================================================
                    auto transfer_start = (input_metrics_enabled || output_metrics_enabled)
                        ? std::chrono::high_resolution_clock::now() 
                        : std::chrono::high_resolution_clock::time_point{};
                    
                    auto maybe_out = Transfer(data_in,
                                              std::integral_constant<std::size_t, in_port_id>{},
                                              std::integral_constant<std::size_t, out_port_id>{});
                    
                    // ================================================================
                    // METRICS: Record Transfer() timing for both sides
                    // ================================================================
                    if ((input_metrics_enabled || output_metrics_enabled) && 
                        transfer_start != std::chrono::high_resolution_clock::time_point{}) {
                        auto transfer_end = std::chrono::high_resolution_clock::now();
                        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            transfer_end - transfer_start).count();
                        
                        // Record on input side (consuming data)
                        if (input_metrics_enabled) {
                            input_thread_metrics_.transfer_calls.fetch_add(1, std::memory_order_acq_rel);
                            input_thread_metrics_.total_transfer_time_ns.fetch_add(duration_ns, 
                                std::memory_order_acq_rel);
                        }
                        
                        // Record on output side (producing data)
                        if (output_metrics_enabled) {
                            output_thread_metrics_.transfer_calls.fetch_add(1, std::memory_order_acq_rel);
                            output_thread_metrics_.total_transfer_time_ns.fetch_add(duration_ns, 
                                std::memory_order_acq_rel);
                        }
                    }
                    
                    if (maybe_out.has_value())
                    {
                        // ================================================================
                        // METRICS: Track output enqueue timing (part of Produce)
                        // ================================================================
                        auto enqueue_start = output_metrics_enabled 
                            ? std::chrono::high_resolution_clock::now() 
                            : std::chrono::high_resolution_clock::time_point{};
                        
                        // C++26: [[nodiscard]] return value intentionally unused (output queue always accepts)
                        static_cast<void>(outputq.Enqueue(maybe_out.value()));
                        
                        // ================================================================
                        // METRICS: Record enqueue time and increment produce counter
                        // ================================================================
                        if (output_metrics_enabled && enqueue_start != std::chrono::high_resolution_clock::time_point{}) {
                            auto enqueue_end = std::chrono::high_resolution_clock::now();
                            auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                enqueue_end - enqueue_start).count();
                            
                            output_thread_metrics_.produce_calls.fetch_add(1, std::memory_order_acq_rel);
                            output_thread_metrics_.total_produce_time_ns.fetch_add(duration_ns, 
                                std::memory_order_acq_rel);
                        }
                        
                        // ================================================================
                        // METRICS: Increment iteration counter
                        // ================================================================
                        if (input_metrics_enabled) {
                            input_thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
                        }
                        if (output_metrics_enabled) {
                            output_thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
                        }
                    }
                }
                else
                {
                    // Input queue disabled
                    break;
                }
            }
            
            input_thread_metrics_.thread_active.store(false, std::memory_order_release);
            output_thread_metrics_.thread_active.store(false, std::memory_order_release);
            
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.node"), 
                          "TransferFn ports " << in_port_id << "->" << out_port_id << " TransferFnThreadFunc Exited.");
        }
    };

}
