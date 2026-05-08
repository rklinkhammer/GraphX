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
 * @file GraphMetrics.hpp
 * @brief Data structures for collecting graph and edge performance metrics
 * 
 * This header defines:
 * - EdgeMetrics: Per-edge queue and throughput statistics
 * - EdgeMetadata: Edge connectivity and type information
 * - GraphMetrics: Graph-level aggregate statistics
 * 
 * All metrics use std::atomic for thread-safe access from any thread.
 * 
 * Related to DECISION-013: Metrics collection at graph level
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include <limits>

namespace graph {

// Forward declarations
struct EdgeMetrics;
struct EdgeMetadata;
struct GraphMetrics;

/**
 * @struct EdgeMetrics
 * @brief Per-edge metrics collected during graph execution
 * 
 * Phase 14d enhancement: Track performance statistics for individual edges.
 * Enables type-aware metrics analysis and per-edge performance monitoring.
 * 
 * Note: Uses move_only pattern to work around atomic<T> non-copy/move limitations.
 */
struct EdgeMetrics {
    /// Messages enqueued on this edge
    std::atomic<uint64_t> messages_enqueued{0};
    
    /// Messages dequeued from this edge
    std::atomic<uint64_t> messages_dequeued{0};
    
    /// Messages rejected (queue full) on this edge
    std::atomic<uint64_t> messages_rejected{0};
    
    /// Cumulative time messages spent in this edge's queue (nanoseconds)
    std::atomic<uint64_t> total_queue_time_ns{0};
    
    /// Number of times the queue was full (backpressure events)
    std::atomic<uint64_t> backpressure_events{0};
    
    /// Maximum queue depth observed on this edge
    std::atomic<uint64_t> peak_queue_depth{0};
    
    /// Whether this edge successfully initialized
    std::atomic<bool> initialized{false};
    
    /// Whether this edge successfully started
    std::atomic<bool> started{false};
    
    /// Default constructor
    EdgeMetrics() = default;
    
    /// Explicitly deleted copy operations (atomics can't be copied)
    EdgeMetrics(const EdgeMetrics&) = delete;
    EdgeMetrics& operator=(const EdgeMetrics&) = delete;
    
    /// Explicitly deleted move operations (atomics can't be moved)
    EdgeMetrics(EdgeMetrics&&) = delete;
    EdgeMetrics& operator=(EdgeMetrics&&) = delete;
    
    /**
     * Get average latency per message on this edge
     * @return Average queue latency in microseconds (0.0 if no messages)
     */
    double GetAverageLatencyUs() const {
        uint64_t total_ns = total_queue_time_ns.load(std::memory_order_relaxed);
        uint64_t dequeued = messages_dequeued.load(std::memory_order_relaxed);
        if (dequeued == 0) return 0.0;
        return (static_cast<double>(total_ns) / dequeued) / 1000.0;
    }
    
    /**
     * Get rejection percentage for this edge
     * @return Percentage (0-100) of messages rejected due to queue full
     */
    double GetRejectionPercent() const {
        uint64_t rejected = messages_rejected.load(std::memory_order_relaxed);
        uint64_t attempted = messages_enqueued.load(std::memory_order_relaxed) +
                            messages_rejected.load(std::memory_order_relaxed);
        if (attempted == 0) return 0.0;
        return (static_cast<double>(rejected) / static_cast<double>(attempted)) * 100.0;
    }
};

/**
 * @struct EdgeMetadata
 * @brief Metadata describing edge connectivity and message types
 * 
 * Phase 14a: Capture metadata while type information is available.
 * Enables type-aware metrics analysis and graph visualization.
 */
struct EdgeMetadata {
    /// Zero-based index of source node in GraphManager::nodes_ vector
    std::size_t source_node_id = std::numeric_limits<std::size_t>::max();
    
    /// Source node port index
    std::size_t source_port_id = std::numeric_limits<std::size_t>::max();
    
    /// Zero-based index of destination node in GraphManager::nodes_ vector
    std::size_t dest_node_id = std::numeric_limits<std::size_t>::max();
    
    /// Destination node port index
    std::size_t dest_port_id = std::numeric_limits<std::size_t>::max();
    
    /// Mangled C++ type name of the message type (from typeid().name())
    std::string message_type_name;
    
    /// Demangled message type name for display (computed if needed)
    std::string message_type_demangled;
    
    /// Whether metadata is valid (all required fields populated)
    bool is_valid() const {
        return source_node_id != std::numeric_limits<std::size_t>::max() &&
               dest_node_id != std::numeric_limits<std::size_t>::max() &&
               source_port_id != std::numeric_limits<std::size_t>::max() &&
               dest_port_id != std::numeric_limits<std::size_t>::max() &&
               !message_type_name.empty();
    }
};

/**
 * @struct GraphMetrics
 * @brief Metrics for graph-level execution
 * 
 * Captures aggregate performance statistics across all nodes and edges.
 * All counters are atomic for thread-safe reads from any thread.
 * 
 * Related to DECISION-013: Metrics collection at graph level
 */
struct GraphMetrics {
    // ====================================================================
    // Lifecycle timing (nanoseconds)
    // ====================================================================
    
    /// Time to execute Init() on all nodes and edges
    std::atomic<uint64_t> init_time_ns{0};
    
    /// Time to execute Start() on all nodes and edges
    std::atomic<uint64_t> start_time_ns{0};
    
    /// Time from Start to Stop
    std::atomic<uint64_t> execution_time_ns{0};
    
    // ====================================================================
    // Item throughput
    // ====================================================================
    
    /// Total items successfully processed through graph
    std::atomic<uint64_t> total_items_processed{0};
    
    /// Items rejected (queue full, etc)
    std::atomic<uint64_t> total_items_rejected{0};
    
    // ====================================================================
    // Aggregate message metrics from all ports
    // ====================================================================
    
    /// Total messages processed across all ports
    std::atomic<uint64_t> total_messages_processed{0};
    
    /// Total enqueued across all queues
    std::atomic<uint64_t> graph_total_enqueued{0};
    
    /// Total dequeued across all queues
    std::atomic<uint64_t> graph_total_dequeued{0};
    
    // ====================================================================
    // Timing aggregates
    // ====================================================================
    
    /// Cumulative time messages spent in queues
    std::atomic<uint64_t> total_queue_time_ns{0};
    
    /// Cumulative processing time
    std::atomic<uint64_t> total_process_time_ns{0};
    
    /// Cumulative thread execution time
    std::atomic<uint64_t> total_thread_time_ns{0};
    
    // ====================================================================
    // Backpressure tracking
    // ====================================================================
    
    /// Number of queue-full drops
    std::atomic<uint64_t> backpressure_events{0};
    
    // ====================================================================
    // Thread pool statistics
    // ====================================================================
    
    /// Maximum active threads observed
    std::atomic<uint64_t> peak_active_threads{0};
    
    /// Maximum queue depth observed across all edges
    std::atomic<uint64_t> peak_queue_depth{0};
    
    // ====================================================================
    // Error tracking
    // ====================================================================
    
    /// Nodes that failed to initialize
    std::atomic<uint64_t> node_init_failures{0};
    
    /// Edges that failed to initialize
    std::atomic<uint64_t> edge_init_failures{0};
    
    /// Nodes that failed to start
    std::atomic<uint64_t> node_start_failures{0};
    
    /// Edges that failed to start
    std::atomic<uint64_t> edge_start_failures{0};
    
    // ====================================================================
    // Computed helper methods
    // ====================================================================
    
    /**
     * Get throughput in items per second
     * @return Items processed per second (0.0 if no execution time)
     */
    double GetThroughputItemsPerSec() const {
        uint64_t exec_ns = execution_time_ns.load(std::memory_order_relaxed);
        uint64_t items = total_items_processed.load(std::memory_order_relaxed);
        if (exec_ns == 0) return 0.0;
        return (static_cast<double>(items) / static_cast<double>(exec_ns)) * 1e9;
    }
    
    /**
     * Get rejection percentage
     * @return Percentage (0-100) of items rejected due to queue full
     */
    double GetRejectionPercent() const {
        uint64_t rejected = total_items_rejected.load(std::memory_order_relaxed);
        uint64_t total = total_items_processed.load(std::memory_order_relaxed) +
                        rejected;
        if (total == 0) return 0.0;
        return (static_cast<double>(rejected) / static_cast<double>(total)) * 100.0;
    }
    
    /**
     * Get average latency per message in microseconds
     * @return Average time in queue per message (0.0 if no messages)
     */
    double GetAverageLatencyUs() const {
        uint64_t total_ns = total_queue_time_ns.load(std::memory_order_relaxed);
        uint64_t dequeued = graph_total_dequeued.load(std::memory_order_relaxed);
        if (dequeued == 0) return 0.0;
        return (static_cast<double>(total_ns) / static_cast<double>(dequeued)) / 1000.0;
    }
    
    /**
     * Get average throughput per message in seconds
     * @return Average time per message (0.0 if no messages)
     */
    double GetAverageMessageTimeSeconds() const {
        uint64_t total_ns = total_process_time_ns.load(std::memory_order_relaxed);
        uint64_t processed = total_messages_processed.load(std::memory_order_relaxed);
        if (processed == 0) return 0.0;
        return static_cast<double>(total_ns) / (1e9 * static_cast<double>(processed));
    }
    
    /**
     * Get effective queue utilization percentage
     * @return Percentage (0-100) of backpressure events vs total throughput
     */
    double GetQueueUtilizationPercent() const {
        uint64_t backpressure = backpressure_events.load(std::memory_order_relaxed);
        uint64_t dequeued = graph_total_dequeued.load(std::memory_order_relaxed);
        if (dequeued == 0) return 0.0;
        return (static_cast<double>(backpressure) / static_cast<double>(dequeued)) * 100.0;
    }
    
    /**
     * Get graph-level success rate
     * @return Percentage (0-100) of operations that succeeded
     */
    double GetSuccessPercent() const {
        return 100.0 - GetRejectionPercent();
    }
};

} // namespace graph

