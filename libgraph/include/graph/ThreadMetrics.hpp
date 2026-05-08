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
 * @file ThreadMetrics.hpp
 * @brief Thread performance metrics for node port functions
 * 
 * This header provides the ThreadMetrics data structure for collecting
 * timing and call count statistics on port operations.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace graph
{

    /**
     * @brief Thread-safe metrics for measuring node operation performance
     * 
     * ThreadMetrics collects timing and call count statistics for node operations:
     * - Produce (output-side data generation)
     * - Consume (input-side data processing)
     * - Transfer (transformation in interior nodes)
     * 
     * All counters use std::atomic for thread-safe access from multiple threads.
     * Helpers compute averages from cumulative timing data.
     * 
     * Related to requirements:
     * - Thread utilization tracking
     * - Method call interval averaging (Produce, Consume, Transfer)
     */
    struct ThreadMetrics {
        // ====================================================================
        // Operation counters (atomic for thread safety)
        // ====================================================================
        
        /// Total number of main loop iterations executed
        std::atomic<uint64_t> total_iterations{0};
        
        /// Total number of Produce() method invocations
        std::atomic<uint64_t> produce_calls{0};
        
        /// Total number of Consume() method invocations
        std::atomic<uint64_t> consume_calls{0};
        
        /// Total number of Transfer() method invocations
        std::atomic<uint64_t> transfer_calls{0};
        
        // ====================================================================
        // Timing in nanoseconds (use atomic for thread safety)
        // ====================================================================
        
        /// Cumulative duration of all Produce() method calls
        std::atomic<uint64_t> total_produce_time_ns{0};
        
        /// Cumulative duration of all Consume() method calls
        std::atomic<uint64_t> total_consume_time_ns{0};
        
        /// Cumulative duration of all Transfer() method calls
        std::atomic<uint64_t> total_transfer_time_ns{0};
        
        /// Total time spent waiting for data to become available
        std::atomic<uint64_t> total_idle_time_ns{0};
        
        /// Total time spent blocked waiting for queue space
        std::atomic<uint64_t> total_queue_wait_ns{0};
        
        // ====================================================================
        // Thread state tracking
        // ====================================================================
        
        /// Is the worker thread currently active?
        std::atomic<bool> thread_active{false};
        
        /// Number of currently active threads
        std::atomic<uint64_t> active_thread_count{0};
        
        // ====================================================================
        // Computed helpers for human-readable output
        // ====================================================================
        
        /**
         * @brief Calculate average time per Produce() call in microseconds
         * @return Average duration in microseconds (0.0 if no calls yet)
         * 
         * Divides total_produce_time_ns by produce_calls and converts to microseconds.
         * Thread-safe: uses relaxed memory ordering (approximate value acceptable).
         */
        double GetAverageProduceTimeUs() const {
            uint64_t total_ns = total_produce_time_ns.load(std::memory_order_relaxed);
            uint64_t count = produce_calls.load(std::memory_order_relaxed);
            return (count == 0) ? 0.0 : static_cast<double>(total_ns) / (1000.0 * count);
        }
        
        /**
         * @brief Calculate average time per Consume() call in microseconds
         * @return Average duration in microseconds (0.0 if no calls yet)
         */
        double GetAverageConsumeTimeUs() const {
            uint64_t total_ns = total_consume_time_ns.load(std::memory_order_relaxed);
            uint64_t count = consume_calls.load(std::memory_order_relaxed);
            return (count == 0) ? 0.0 : static_cast<double>(total_ns) / (1000.0 * count);
        }
        
        /**
         * @brief Calculate average time per Transfer() call in microseconds
         * @return Average duration in microseconds (0.0 if no calls yet)
         */
        double GetAverageTransferTimeUs() const {
            uint64_t total_ns = total_transfer_time_ns.load(std::memory_order_relaxed);
            uint64_t count = transfer_calls.load(std::memory_order_relaxed);
            return (count == 0) ? 0.0 : static_cast<double>(total_ns) / (1000.0 * count);
        }
        
        /**
         * @brief Calculate thread utilization percentage
         * @param total_time_ns Total elapsed time in nanoseconds
         * @return Percentage (0-100) of time spent in actual work vs idle/waiting
         * 
         * Busy time = produce_time + consume_time + transfer_time
         * Utilization = (busy_time / total_time) * 100
         */
        double GetThreadUtilizationPercent(uint64_t total_time_ns) const {
            uint64_t busy_ns = total_produce_time_ns.load(std::memory_order_relaxed)
                             + total_consume_time_ns.load(std::memory_order_relaxed)
                             + total_transfer_time_ns.load(std::memory_order_relaxed);
            if (total_time_ns == 0) return 0.0;
            return (static_cast<double>(busy_ns) / total_time_ns) * 100.0;
        }
    };

} // namespace graph

