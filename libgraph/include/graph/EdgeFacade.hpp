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
 * @file EdgeFacade.hpp
 * @brief Type-erased edge interfaces and convenient wrappers
 * 
 * ## Overview
 * 
 * EdgeFacade provides abstraction layers for working with heterogeneous Edge templates
 * after type erasure. Similar to NodeFacade for nodes, EdgeFacade provides:
 * 
 * 1. **IEdgeBase**: Virtual interface for type-erased edge storage in vectors
 *    - All edges of different template parameters can be stored as unique_ptr<IEdgeBase>
 *    - Enables polymorphic access to edge lifecycle, metadata, and metrics
 *    - Used internally by GraphManager for edge container
 * 
 * 2. **EdgeWrapper<>**: Template wrapper implementing IEdgeBase for concrete edges
 *    - CRTP pattern to capture compile-time information (ports, node types)
 *    - Delegates lifecycle operations to wrapped Edge<>
 *    - Stores metadata (source/dest node IDs) provided by GraphManager
 *    - Delegates metrics queries to internal EdgeMetrics or wrapped Edge's ThreadMetrics
 *    - Used internally by GraphManager::AddEdge()
 * 
 * 3. **EdgeFacadeAdapter**: C++ convenience wrapper for IEdgeBase pointers
 *    - Shorter method names (GetEnqueued vs GetMessagesEnqueued)
 *    - Status checks (IsReady, IsHealthy)
 *    - String representations for logging
 *    - Used by client code to access edges returned from GraphManager
 * 
 * ## Type Erasure Pattern
 * 
 * ```
 * Edge<Node1, 0, Node2, 0>  Edge<Node1, 0, Node3, 0>  Edge<Node2, 1, Node3, 1>
 *         |                           |                           |
 *         v                           v                           v
 * EdgeWrapper<Node1,0,Node2,0>  EdgeWrapper<Node1,0,Node3,0>  EdgeWrapper<Node2,1,Node3,1>
 *         |                           |                           |
 *         +---------------------------+---------------------------+
 *                           |
 *                           v
 *                  std::unique_ptr<IEdgeBase>
 *                           |
 *                   vector<unique_ptr<IEdgeBase>>
 *                  (GraphManager::edges_)
 * ```
 * 
 * All type information captured at compile-time in EdgeWrapper template parameters,
 * but stored polymorphically as IEdgeBase to enable mixed-type edge containers.
 * 
 * ## Metrics Flow
 * 
 * Edge metrics come from two sources:
 * 
 * 1. **Queue Metrics** (EdgeMetrics struct):
 *    - Stored in GraphManager and passed to EdgeWrapper via SetMetrics()
 *    - Tracks messages enqueued/dequeued, backpressure events, queue depth
 *    - Populated by Edge's Enqueue() and Dequeue() operations
 * 
 * 2. **Thread Metrics** (ThreadMetrics struct):
 *    - Embedded in Edge class, instrumented in EdgeThreadFunc()
 *    - Tracks transfer operations, timing, thread active flag
 *    - Returned by Edge::GetThreadMetrics() and delegated via IEdgeBase
 * 
 * EdgeFacadeAdapter provides convenient access to both metric streams.
 * 
 * ## Memory Model
 * 
 * **Ownership**:
 * - EdgeWrapper owns wrapped Edge<> via unique_ptr
 * - IEdgeBase pointer doesn't own - Graph owns wrapper
 * - EdgeMetrics shared_ptr allows metrics to outlive individual edges (aggregate tracking)
 * 
 * **Thread Safety**:
 * - All metrics access uses std::memory_order_relaxed (informational, not synchronization)
 * - Metadata is write-once in SetMetadata() - no synchronization needed
 * - State flags use appropriate memory ordering (release for write, relaxed for read)
 * 
 * **Lifetime**:
 * - IEdgeBase pointers valid only while GraphManager alive
 * - EdgeFacadeAdapter is non-owning wrapper - must outlive edge
 * - Always create EdgeFacadeAdapter from temporary: `EdgeFacadeAdapter(edge.get())`
 * 
 * ## Usage Examples
 * 
 * ### Accessing edge metrics during graph execution:
 * 
 * ```cpp
 * for (const auto& edge : graph.GetEdges()) {
 *     EdgeFacadeAdapter facade(edge.get());
 *     std::cout << facade.GetStatsString() << std::endl;
 * }
 * ```
 * 
 * ### Checking edge health:
 * 
 * ```cpp
 * for (const auto& edge : graph.GetEdges()) {
 *     EdgeFacadeAdapter facade(edge.get());
 *     if (!facade.IsHealthy()) {
 *         LOG4CXX_WARN(logger, "Edge backpressure: " << facade.GetBackpressure());
 *     }
 * }
 * ```
 * 
 * ### Detailed edge analysis:
 * 
 * ```cpp
 * EdgeFacadeAdapter edge(edge_ptr);
 * std::cout << "Messages: " << edge.GetEnqueued() << " -> " 
 *           << edge.GetDequeued() << std::endl;
 * std::cout << "Rejected: " << edge.GetRejected() << std::endl;
 * std::cout << "Queue depth: " << edge.GetQueueDepth() << std::endl;
 * ```
 * 
 * ## Comparison with NodeFacade
 * 
 * | Aspect | NodeFacade | EdgeFacade |
 * |--------|-----------|-----------|
 * | Virtual interface | INode | IEdgeBase |
 * | Type-erased wrapper | NodeAdapter<> | EdgeWrapper<> |
 * | Convenience wrapper | NodeFacadeAdapter | EdgeFacadeAdapter |
 * | C-compatible layer | Yes (NodeFacade struct) | Not needed (internal) |
 * | Plugin support | Yes (NodePluginInstance) | Future (EdgePluginInstance) |
 * | Use case | Both internal & plugin | Primarily internal |
 * 
 * ## Design Decisions
 * 
 * **Decision 1: Separate header from GraphManager**
 * - Rationale: Mirrors NodeFacade pattern for consistency
 * - Benefit: Smaller files, clearer separation of concerns
 * - Tradeoff: One more header to include (mitigated by GraphManager including it)
 * 
 * **Decision 2: IEdgeBase interface instead of std::any**
 * - Rationale: Type-safe, enables polymorphic dispatch
 * - Benefit: Compiler catches errors, better performance
 * - Tradeoff: More code, but necessary for runtime heterogeneity
 * 
 * **Decision 3: EdgeFacadeAdapter separate from IEdgeBase**
 * - Rationale: Different purposes (virtual dispatch vs convenience)
 * - Benefit: Flexibility in implementation, clear intent
 * - Tradeoff: Two wrapper layers (necessary for design)
 * 
 * **Decision 4: SetMetadata/SetMetrics methods instead of constructor**
 * - Rationale: GraphManager creates wrapper before knowing node IDs
 * - Benefit: Decouples wrapper creation from metadata availability
 * - Tradeoff: Two-phase initialization (documented)
 * 
 * @author Robert Klinkhammer
 * @date 2025-01-04
 * @see GraphManager.hpp for usage of IEdgeBase and EdgeWrapper
 * @see Metrics.hpp for EdgeMetrics and EdgeMetadata
 * @see Nodes.hpp for Edge<> template and ThreadMetrics
 */

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
#include <boost/assert.hpp>

#include "graph/GraphMetrics.hpp"
#include "graph/Nodes.hpp"

namespace graph {

/**
 * @brief Base class for type-erased edge storage
 * 
 * Since Edge is templated, we need a common interface to store
 * edges of different types in a single container.
 */
class IEdgeBase {
public:
    virtual ~IEdgeBase() = default;
    
    // === Lifecycle Methods (Existing) ===
    virtual bool Init() = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Join() = 0;
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
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override { 
        return edge_->JoinWithTimeout(timeout_ms); 
    }
    
    // === Metadata Access ===
    std::size_t GetSourceNodeId() const override {
        return source_node_id_;
    }
    
    std::size_t GetSourcePortId() const override {
        return SrcPort;  // Known at compile time
    }
    
    std::size_t GetDestNodeId() const override {
        return dest_node_id_;
    }
    
    std::size_t GetDestPortId() const override {
        return DstPort;  // Known at compile time
    }
    
    std::string GetMessageTypeName() const override {
        return message_type_demangled_;
    }
    
    std::string GetDescription() const override {
        std::ostringstream oss;
        oss << "Edge[" << source_node_id_ << ":" << SrcPort 
            << " -> " << dest_node_id_ << ":" << DstPort 
            << "] (" << message_type_demangled_ << ")";
        return oss.str();
    }
    
    // === Queue Metrics ===
    std::size_t GetQueueSize() const override {
        if (!metrics_) return 0;
        // Return messages enqueued minus dequeued as current depth estimate
        uint64_t enqueued = metrics_->messages_enqueued.load(std::memory_order_relaxed);
        uint64_t dequeued = metrics_->messages_dequeued.load(std::memory_order_relaxed);
        return (enqueued > dequeued) ? (enqueued - dequeued) : 0;
    }
    
    uint64_t GetMessagesEnqueued() const override {
        if (!metrics_) return 0;
        return metrics_->messages_enqueued.load(std::memory_order_relaxed);
    }
    
    uint64_t GetMessagesDequeued() const override {
        if (!metrics_) return 0;
        return metrics_->messages_dequeued.load(std::memory_order_relaxed);
    }
    
    uint64_t GetMessagesRejected() const override {
        if (!metrics_) return 0;
        return metrics_->messages_rejected.load(std::memory_order_relaxed);
    }
    
    uint64_t GetBackpressureEvents() const override {
        if (!metrics_) return 0;
        return metrics_->backpressure_events.load(std::memory_order_relaxed);
    }
    
    uint64_t GetPeakQueueDepth() const override {
        if (!metrics_) return 0;
        return metrics_->peak_queue_depth.load(std::memory_order_relaxed);
    }
    
    // === Thread Metrics ===
    const graph::ThreadMetrics& GetEdgeThreadMetrics() const override {
        // Delegate to wrapped edge's thread metrics (now available after Part 2 instrumentation)
        return edge_->GetThreadMetrics();
    }
    
    // === State Queries ===
    bool IsInitialized() const override {
        return initialized_.load(std::memory_order_relaxed);
    }
    
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
class EdgeFacadeAdapter {
private:
    IEdgeBase* edge_;  // Does not own
    
public:
    /// Constructor
    explicit EdgeFacadeAdapter(IEdgeBase* edge) 
        : edge_(edge) {
        BOOST_ASSERT(edge_ != nullptr);
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

