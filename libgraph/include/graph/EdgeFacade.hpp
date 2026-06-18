/**
 * @file EdgeFacade.hpp
 * @brief Edge Facade Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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


#pragma once

#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <atomic>
#include <chrono>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cassert>

#include "graph/GraphMetrics.hpp"
#include "graph/Nodes.hpp"

namespace graph {

/**
 * @brief Base class for type-erased edge storage
 * 
 * Since Edge is templated, we need a common interface to store
 * edges of different types in a single container.
 */
/**
 * @class IEdgeBase
 * @brief I edge base implementation for GraphX.
 */
/**
 * @class IEdgeBase
 * @brief Iedge Base type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class IEdgeBase {
public:
    /**
     * @brief Releases resources owned by Iedge Base.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~IEdgeBase() = default;
    
    // === Lifecycle Methods (Existing) ===
/**
 * @brief Init.
 * @return Result of the operation.
 */
    virtual bool Init() = 0;
/**
 * @brief Start.
 * @return Result of the operation.
 */
    virtual bool Start() = 0;
/**
 * @brief Stop.
 */
    virtual void Stop() = 0;
/**
 * @brief Join.
 */
    virtual void Join() = 0;
/**
 * @brief Join with timeout.
 * @param timeout_ms Parameter for join with timeout.
 * @return Result of the operation.
 */
    virtual bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) = 0;
    
    // === Metadata Access (NEW) ===
    
    /// Get source node ID in GraphManager::nodes_ vector
    virtual std::size_t GetSourceNodeId() const = 0;
    
    /// Get source port index
    virtual std::size_t GetSourcePortId() const = 0;
    
    /// Get destination node ID in GraphManager::nodes_ vector
    virtual std::size_t GetDestNodeId() const = 0;
    
    /// Get destination port index
    virtual std::size_t GetDestPortId() const = 0;
    
    /// Get message type name (demangled for display)
    virtual std::string GetMessageTypeName() const = 0;
    
    /// Get human-readable description (e.g., "Node0:Port2 -> Node3:Port1 (int)")
    virtual std::string GetDescription() const = 0;
    
    // === Queue Metrics (NEW) ===
    
    /// Get current queue depth (messages waiting to be dequeued)
    virtual std::size_t GetQueueSize() const = 0;
    
    /// Get total messages successfully enqueued
    virtual uint64_t GetMessagesEnqueued() const = 0;
    
    /// Get total messages successfully dequeued
    virtual uint64_t GetMessagesDequeued() const = 0;
    
    /// Get total messages rejected due to queue full
    virtual uint64_t GetMessagesRejected() const = 0;
    
    /// Get total backpressure events (queue full on Enqueue)
    virtual uint64_t GetBackpressureEvents() const = 0;
    
    /// Get peak queue depth observed during execution
    virtual uint64_t GetPeakQueueDepth() const = 0;
    
    // === Thread Metrics (NEW) ===
    
    /// Get edge thread metrics (transfer operations, timing)
    /// @return Const reference to ThreadMetrics (valid until edge destroyed)
    /// @note Returns const reference to internal metrics
    virtual const graph::ThreadMetrics& GetEdgeThreadMetrics() const = 0;
    
    // === State Queries (NEW) ===
    
    /// Has this edge been successfully initialized?
    virtual bool IsInitialized() const = 0;
    
    /// Is this edge currently running (Started but not Stopped)?
    virtual bool IsRunning() const = 0;
};

/**
 * @brief Type-erased wrapper for Edge instances with ownership
 * @tparam SrcNode Source node type
 * @tparam SrcPort Source port number
 * @tparam DstNode Destination node type  
 * @tparam DstPort Destination port number
 */
template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
/**
 * @class EdgeWrapper
 * @brief Edge wrapper implementation for GraphX.
 */
/**
 * @class EdgeWrapper
 * @brief Edge Wrapper type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class EdgeWrapper : public IEdgeBase {
public:
    using EdgeType = graph::Edge<SrcNode, SrcPort, DstNode, DstPort>;
    using ValueType = typename EdgeType::ValueType;
    
    /// Constructor
    explicit EdgeWrapper(std::unique_ptr<EdgeType> edge,
                        const std::string& message_type_demangled = "")
        : edge_(std::move(edge)),
          message_type_demangled_(message_type_demangled),
          source_node_id_(SIZE_MAX),
          dest_node_id_(SIZE_MAX) {}
    
    // === Lifecycle (delegate to wrapped Edge) ===
    bool Init() override { return edge_->Init(); }
    bool Start() override { return edge_->Start(); }
    void Stop() override { edge_->Stop(); }
    void Join() override { edge_->Join(); }
    /**
     * @brief Executes the Join With Timeout operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param timeout_ms Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override { 
        return edge_->JoinWithTimeout(timeout_ms); 
    }
    
    // === Metadata Access ===
    /**
     * @brief Returns the Source Node ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetSourceNodeId() const override {
        return source_node_id_;
    }
    
    /**
     * @brief Returns the Source Port ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetSourcePortId() const override {
        return SrcPort;  // Known at compile time
    }
    
    /**
     * @brief Returns the Dest Node ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetDestNodeId() const override {
        return dest_node_id_;
    }
    
    /**
     * @brief Returns the Dest Port ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetDestPortId() const override {
        return DstPort;  // Known at compile time
    }
    
    /**
     * @brief Returns the Message Type Name.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetMessageTypeName() const override {
        return message_type_demangled_;
    }
    
    /**
     * @brief Returns the Description.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetDescription() const override {
        std::ostringstream oss;
        oss << "Edge[" << source_node_id_ << ":" << SrcPort 
            << " -> " << dest_node_id_ << ":" << DstPort 
            << "] (" << message_type_demangled_ << ")";
        return oss.str();
    }
    
    // === Queue Metrics ===
    /**
     * @brief Returns the Queue Size.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetQueueSize() const override {
        if (!metrics_) return 0;
        // Return messages enqueued minus dequeued as current depth estimate
        uint64_t enqueued = metrics_->messages_enqueued.load(std::memory_order_relaxed);
        uint64_t dequeued = metrics_->messages_dequeued.load(std::memory_order_relaxed);
        return (enqueued > dequeued) ? (enqueued - dequeued) : 0;
    }
    
    /**
     * @brief Returns the Messages Enqueued.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesEnqueued() const override {
        if (!metrics_) return 0;
        return metrics_->messages_enqueued.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Returns the Messages Dequeued.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesDequeued() const override {
        if (!metrics_) return 0;
        return metrics_->messages_dequeued.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Returns the Messages Rejected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesRejected() const override {
        if (!metrics_) return 0;
        return metrics_->messages_rejected.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Returns the Backpressure Events.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetBackpressureEvents() const override {
        if (!metrics_) return 0;
        return metrics_->backpressure_events.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Returns the Peak Queue Depth.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetPeakQueueDepth() const override {
        if (!metrics_) return 0;
        return metrics_->peak_queue_depth.load(std::memory_order_relaxed);
    }
    
    // === Thread Metrics ===
    /**
     * @brief Returns the Edge Thread Metrics.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    const graph::ThreadMetrics& GetEdgeThreadMetrics() const override {
        // Delegate to wrapped edge's thread metrics (now available after Part 2 instrumentation)
        return edge_->GetThreadMetrics();
    }
    
    // === State Queries ===
    /**
     * @brief Reports whether Is Initialized is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool IsInitialized() const override {
        return initialized_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Reports whether Is Running is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool IsRunning() const override {
        return running_.load(std::memory_order_relaxed);
    }
    
    // === Internal Access (for GraphManager only) ===
    
    EdgeType* get() { return edge_.get(); }
    const EdgeType* get() const { return edge_.get(); }
    
    /// Called by GraphManager after edge creation
    void SetMetadata(std::size_t src_node_id, std::size_t dst_node_id) {
        source_node_id_ = src_node_id;
        dest_node_id_ = dst_node_id;
    }
    
    /// Called by GraphManager to attach metrics
    void SetMetrics(std::shared_ptr<graph::EdgeMetrics> metrics) {
        metrics_ = metrics;
    }
    
    /// Called by edge when initialized
    void SetInitialized(bool initialized) {
        initialized_.store(initialized, std::memory_order_release);
    }
    
    /// Called by edge when started
    void SetRunning(bool running) {
        running_.store(running, std::memory_order_release);
    }
    
private:
    std::unique_ptr<EdgeType> edge_;
    std::string message_type_demangled_;
    std::shared_ptr<graph::EdgeMetrics> metrics_;
    
    // Metadata populated by GraphManager
    std::size_t source_node_id_;
    std::size_t dest_node_id_;
    
    // State tracking
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
};

/**
 * @class EdgeFacadeAdapter
 * @brief Convenient C++ wrapper for working with type-erased IEdgeBase
 * 
 * Provides common queries with shorter names and convenient
 * status checks. Useful for accessing edge metrics and state in a cleaner way.
 * 
 * Example usage:
 * @code
 * for (const auto& edge_wrapper : graph.GetEdges()) {
 *     EdgeFacadeAdapter edge(edge_wrapper.get());
 *     std::cout << edge.GetStatsString() << std::endl;
 *     
 *     if (!edge.IsHealthy()) {
 *         LOG4CXX_WARN(logger, "Edge has backpressure: " << edge.GetBackpressure());
 *     }
 * }
 * @endcode
 */
/**
 * @class EdgeFacadeAdapter
 * @brief Edge Facade Adapter type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class EdgeFacadeAdapter {
private:
    IEdgeBase* edge_;  // Does not own
    
public:
    /// Constructor
    explicit EdgeFacadeAdapter(IEdgeBase* edge) 
        : edge_(edge) {
        /**
         * @brief Executes the Assert operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        assert(edge_ != nullptr);
    }
    
    /// Convenience reference version
    explicit EdgeFacadeAdapter(IEdgeBase& edge) 
        : edge_(&edge) {}
    
    // === Lifecycle ===
    
    /// Initialize the edge
    bool Init() { 
        return edge_->Init(); 
    }
    
    /// Start the edge thread
    bool Start() { 
        return edge_->Start(); 
    }
    
    /// Stop the edge thread
    void Stop() { 
        edge_->Stop(); 
    }
    
    /// Wait for edge to finish
    bool Join(std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        return edge_->JoinWithTimeout(timeout);
    }
    
    // === Convenient Metrics Access ===
    
    /// Get total messages enqueued
    uint64_t GetEnqueued() const { 
        return edge_->GetMessagesEnqueued(); 
    }
    
    /// Get total messages dequeued
    uint64_t GetDequeued() const { 
        return edge_->GetMessagesDequeued(); 
    }
    
    /// Get total messages rejected (queue full)
    uint64_t GetRejected() const { 
        return edge_->GetMessagesRejected(); 
    }
    
    /// Get total backpressure events
    uint64_t GetBackpressure() const { 
        return edge_->GetBackpressureEvents(); 
    }
    
    /// Get current queue depth
    std::size_t GetQueueDepth() const { 
        return edge_->GetQueueSize(); 
    }
    
    /// Get peak queue depth observed
    uint64_t GetPeakQueueDepth() const {
        return edge_->GetPeakQueueDepth();
    }
    
    // === Status Checks ===
    
    /// Is edge fully ready (initialized and running)?
    bool IsReady() const { 
        return edge_->IsInitialized() && edge_->IsRunning(); 
    }
    
    /// Is edge in healthy state (no rejected messages, no backpressure)?
    bool IsHealthy() const {
        return GetRejected() == 0 && GetBackpressure() == 0;
    }
    
    // === String Representation ===
    
    /// Get human-readable edge description
    std::string AsString() const { 
        return edge_->GetDescription(); 
    }
    
    /// Get comprehensive stats string suitable for logging
    std::string GetStatsString() const {
        std::ostringstream oss;
        oss << edge_->GetDescription()
            << " [Enqueued:" << GetEnqueued()
            << " Dequeued:" << GetDequeued()
            << " Queue:" << GetQueueDepth()
            << " PeakQueue:" << GetPeakQueueDepth()
            << " Rejected:" << GetRejected()
            << " Backpressure:" << GetBackpressure() << "]";
        return oss.str();
    }
    
    /// Stream operator for convenient output
    friend std::ostream& operator<<(std::ostream& out, const EdgeFacadeAdapter& adapter) {
        out << adapter.GetStatsString();
        return out;
    }
};

}  // namespace graph
