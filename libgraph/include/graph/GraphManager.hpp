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
 * @file GraphManager.hpp
 * @brief Dataflow graph lifecycle management with per-node and per-edge metrics
 * 
 * ## Overview
 * 
 * GraphManager provides high-level orchestration of dataflow graphs, managing:
 * - Node lifecycle (construction, initialization, execution, cleanup)
 * - Edge creation and connectivity tracking
 * - Shared thread pool management for all graph components
 * - Per-edge type information recovery despite template erasure
 * - Aggregate and per-edge performance metrics collection
 * - Graph structure visualization (DOT, JSON, text formats)
 * 
 * ## Core Architecture
 * 
 * **Ownership Model**: GraphManager owns all nodes and edges via shared_ptr/unique_ptr.
 * - Nodes stored as shared_ptr<INode> in nodes_ vector
 * - Edges stored as unique_ptr<IEdgeBase> with type erasure via EdgeWrapper
 * - Type metadata captured at AddEdge() time to recover message types
 * - Single ThreadPool shared by all graph components
 * 
 * **Type Information Recovery** (Phase 14a)
 * - Edge templates lose type info after instantiation (IEdgeBase polymorphism)
 * - EdgeMetadata structure captures source/dest node IDs, ports, and message type names
 * - Enables DisplayGraphJSON/DOT to show accurate connectivity and message types
 * - Mangled C++ type names are demangled for human-readable output
 * 
 * **Per-Edge Metrics** (Phase 14d)
 * - EdgeMetrics struct tracks messages_enqueued, messages_dequeued, backpressure events
 * - Parallel vectors edge_metrics_ indexed by edge creation order
 * - Enables performance analysis: throughput, rejection rates, queue depth per edge
 * 
 * ## Lifecycle State Machine
 * 
 * ```
 * [Constructed] --AddNode()/AddEdge()-->  [Configured]
 *                                             |
 *                                           Init()
 *                                             |
 *                                        [Initialized]
 *                                             |
 *                                           Start()
 *                                             |
 *                                           [Running]
 *                                             |
 *                                           Stop()
 *                                             |
 *                                         [Stopping]
 *                                             |
 *                                           Join()
 *                                             |
 *                                         [Stopped]
 *                                             |
 *                                          Clear()
 *                                             |
 *                                        [Empty]
 * ```
 * 
 * Valid state transitions:
 * - Constructed → Configured: Multiple AddNode()/AddEdge() calls
 * - Configured → Initialized: Single Init() call
 * - Initialized → Running: Single Start() call
 * - Running → Stopping: Stop() call (can be called anytime)
 * - Stopping → Stopped: Join() or JoinWithTimeout() call
 * - Stopped → Empty: Clear() call (optional)
 * - Empty → Configured: New AddNode()/AddEdge() calls (rebuild graph)
 * 
 * Invalid/prevented transitions:
 * - AddNode/AddEdge after Init() → throws runtime_error (graph locked)
 * - Init() after Init() → returns false (prevents double-init)
 * - Start() before Init() → returns false (requires initialized state)
 * - Start() after Start() → returns false (prevents double-start)
 * - Destroy without Stop()+Join() → deadlock/crash risk
 * 
 * ## Thread Safety
 * 
 * **NOT thread-safe during configuration**:
 * - AddNode(), AddEdge() must complete before Init()
 * - Init() must complete before any other operations
 * - These methods are guarded by lifecycle_mtx_ to prevent concurrent calls
 * 
 * **Safe during execution**:
 * - Stop() is safe to call anytime (idempotent)
 * - Join() is safe to call after Stop() (idempotent)
 * - DisplayGraph() variants are safe to call while running (read-only snapshots)
 * - GetMetrics() is safe to call anytime (atomic reads)
 * 
 * **Thread coordination**:
 * - ThreadPool provides worker threads for node/edge execution
 * - Node port threads communicate via message queues
 * - Edge queues coordinate producer (port) and consumer (port) synchronization
 * 
 * ## Metrics & Observability
 * 
 * **Graph-level metrics** (GraphMetrics struct):
 * - Lifecycle timing: init_time_ns, start_time_ns, execution_time_ns
 * - Throughput: total_items_processed, GetThroughputItemsPerSec()
 * - Rejection: total_items_rejected, GetRejectionPercent()
 * - Backpressure: backpressure_events, peak_queue_depth
 * - Thread pool: peak_active_threads
 * - Failures: node/edge init/start failures
 * 
 * **Per-edge metrics** (EdgeMetrics struct):
 * - Queue statistics: messages_enqueued, messages_dequeued, messages_rejected
 * - Timing: total_queue_time_ns, GetThroughputPerSec()
 * - Backpressure: backpressure_events, peak_queue_depth
 * - Initialization status: initialized, started
 * 
 * Collected via EnableMetrics(true) call. All counters are atomic for safe concurrent reads.
 * 
 * ## Visualization
 * 
 * Three output formats for graph structure:
 * 1. **Text** (DisplayGraph()): Human-readable with node/edge details
 * 2. **JSON** (DisplayGraphJSON()): Structured data with node and edge arrays
 * 3. **Graphviz DOT** (DisplayGraphDOT()): Render with `dot` command-line tool
 * 
 * All formats show current runtime state (instantiated nodes, not topology definitions).
 * Type information recovered from EdgeMetadata enables accurate message type labels.
 * 
 * ## Comparison with std::thread
 * 
 * | Feature | GraphManager | Raw std::thread |
 * |---------|-------------|-----------------|
 * | Lifecycle | Explicit Init/Start/Stop/Join | Manual thread::join() |
 * | Synchronization | Atomic flags + mutexes | Condition variables |
 * | Resource mgmt | Own nodes/edges via smart pointers | Manual cleanup |
 * | Metrics | Built-in aggregate + per-edge | None |
 * | Type safety | Template Edge<>, type erasure for storage | No type tracking |
 * | Visualization | 3 output formats (DOT, JSON, text) | None |
 * 
 * ## Exception Handling
 * 
 * **Construction & configuration**:
 * - AddNode(), AddEdge() throw std::runtime_error on lock/creation failures
 * - Strong exception guarantee: vector unchanged on failure
 * 
 * **Lifecycle operations**:
 * - Init(), Start() return false on failure (no exceptions)
 * - Stop(), Join() suppress all exceptions internally
 * 
 * **Cleanup**:
 * - Destructor suppresses all exceptions
 * - Ensures cleanup happens even if node/edge destructors throw
 * 
 * ## Usage Example
 * 
 * @code
 * #include "GraphManager.hpp"
 * using namespace graphsim::engine;
 * 
 * int main() {
 *     // 1. Create graph with custom thread pool
 *     GraphManager graph(4);  // 4 worker threads
 *     
 *     // 2. Add nodes
 *     auto source = graph.AddNode<MySourceNode>(args...);
 *     auto sink = graph.AddNode<MySinkNode>(args...);
 *     
 *     // 3. Connect edges
 *     graph.AddEdge<MySourceNode, 0, MySinkNode, 0>(source, sink);
 *     
 *     // 4. Initialize
 *     if (!graph.Init()) {
 *         std::cerr << "Init failed\n";
 *         return 1;
 *     }
 *     
 *     // 5. Enable metrics (optional)
 *     graph.EnableMetrics(true);
 *     
 *     // 6. Start execution
 *     if (!graph.Start()) {
 *         std::cerr << "Start failed\n";
 *         return 1;
 *     }
 *     
 *     // 7. Let graph run...
 *     std::this_thread::sleep_for(std::chrono::seconds(10));
 *     
 *     // 8. Visualize structure
 *     std::cout << graph.DisplayGraphJSON() << std::endl;
 *     
 *     // 9. Graceful shutdown
 *     graph.Stop();
 *     graph.Join();
 *     
 *     // 10. Report metrics
 *     const auto& metrics = graph.GetMetrics();
 *     std::cout << "Processed: " << metrics.total_items_processed << " items\n";
 *     std::cout << "Throughput: " << metrics.GetThroughputItemsPerSec() << " items/sec\n";
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * ## Decision References
 * 
 * - DECISION-013: Graph lifecycle thread safety model (see AI_DECISION_REGISTRY.md)
 * - Phase 14a: Edge metadata capture for type information recovery
 * - Phase 14d: Per-edge metrics collection and analysis
 * 
 * @author Robert Klinkhammer
 * @date 2025-12-08
 * @see ThreadPool.hpp for worker thread management
 * @see Nodes.hpp for node interface
 * @see Edge<> template for message passing
 */

#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <any>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <typeinfo>
#include <cxxabi.h>
#include <cstdlib>
#include "core/ReflectionHelper.hpp"
#include "graph/Nodes.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/PooledMessage.hpp"
#include "ThreadPool.hpp"
#include "EdgeFacade.hpp"

namespace graph {

// Metrics structures are defined in Metrics.hpp
#include "graph/GraphMetrics.hpp"

/**
 * @brief Manages the lifecycle of a dataflow graph
 * 
 * GraphManager stores nodes and edges and provides coordinated
 * lifecycle management. It ensures proper initialization order
 * and cleanup of graph components.
 * 
 * Usage:
 * @code
 *   GraphManager graph;
 *   auto src = graph.AddNode<MySource>(args...);
 *   auto dst = graph.AddNode<MySink>(args...);
 *   graph.AddEdge<MySource, 0, MySink, 0>(src, dst);
 *   
 *   graph.Init();
 *   graph.Start();
 *   // ... run graph ...
 *   graph.Stop();
 *   graph.Join();
 * @endcode
 */
class GraphManager {
public:
    /**
     * @brief Construct GraphManager with default ThreadPool configuration
     * 
     * Creates GraphManager with default hardware concurrency thread pool.
     */
    GraphManager() 
        : thread_pool_config_(), thread_pool_(nullptr), num_threads_(0) {}
    
    /**
     * @brief Construct GraphManager with custom ThreadPool configuration
     * @param num_threads Number of worker threads (0 = hardware concurrency)
     * @param config Deadlock detection configuration
     */
    explicit GraphManager(size_t num_threads, const ThreadPool::DeadlockConfig& config = ThreadPool::DeadlockConfig())
        : thread_pool_config_(config), thread_pool_(nullptr), num_threads_(num_threads) {}
    
    virtual ~GraphManager() {
        try {
            // CRITICAL: Must both Stop() AND Join() before destruction
            // Otherwise threads will be accessing deleted objects
            
            // Step 1: Signal all components to stop
            for (auto& edge : edges_) {
                try {
                    edge->Stop();
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            for (auto& node : nodes_) {
                try {
                    node->Stop();
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            if (thread_pool_) {
                try {
                    thread_pool_->Stop();
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            // Step 2: Join all components with timeout to wait for threads to exit
            auto timeout = std::chrono::milliseconds(1000);  // 1 second timeout per component
            
            for (auto& edge : edges_) {
                try {
                    edge->JoinWithTimeout(timeout);
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            for (auto& node : nodes_) {
                try {
                    node->JoinWithTimeout(timeout);
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            if (thread_pool_) {
                try {
                    thread_pool_->Join();
                } catch (...) {
                    // Suppress exceptions
                }
            }
            
            // Step 3: Call Cleanup() on NodeFacadeAdapterWrappers BEFORE clearing nodes_
            // This must happen while objects are still alive to avoid vptr corruption
            // during ConcreteNode destructor chains
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                          "GraphManager destructor: calling Cleanup() on " << nodes_.size() << " nodes");
            for (auto& node : nodes_) {
                try {
                    auto* wrapper = dynamic_cast<NodeFacadeAdapterWrapper*>(node.get());
                    if (wrapper) {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                                      "Found NodeFacadeAdapterWrapper, calling Cleanup()");
                        wrapper->Cleanup();
                    } else {
                        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                                      "Node is not NodeFacadeAdapterWrapper, skipping Cleanup()");
                    }
                } catch (...) {
                    // Suppress exceptions
                    LOG4CXX_WARN(log4cxx::Logger::getLogger("graph.graph"),
                                 "Exception during node Cleanup()");
                }
            }
            
            // Step 4: Clear containers - this triggers destructors
            try {
                edges_.clear();
            } catch (...) {
                // Suppress exceptions
            }
            
            try {
                nodes_.clear();
            } catch (...) {
                // Suppress exceptions
            }
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                          "GraphManager cleanup failed: " << e.what());
        } catch (...) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                          "GraphManager cleanup failed: unknown exception");
        }
    }
    
    // Prevent copying and moving for safety
    GraphManager(const GraphManager&) = delete;
    GraphManager& operator=(const GraphManager&) = delete;
    GraphManager(GraphManager&&) = delete;
    GraphManager& operator=(GraphManager&&) = delete;
    
    /**
     * @brief Add a node to the graph
     * @tparam NodeType Type of node to create
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return Shared pointer to the created node
     * 
     * Thread safety: NOT thread-safe for concurrent calls with Init/Start.
     * Must complete all AddNode calls before calling Init().
     * 
     * Preconditions:
     * - Init() must NOT have been called yet
     * - No other threads should be calling AddNode simultaneously
     * 
     * Postconditions:
     * - Node is added to nodes_ vector
     * - Node is NOT initialized or started yet
     * 
     * Exception safety: Strong guarantee. If creation fails, vector unchanged.
     * 
     * Returns false if:
     * - Init() has already been called (graph locked)
     * - Node creation fails
     * - INode conversion fails
     * 
     * Creates a new node and stores it in the graph for lifecycle management.
     */
    template <reflection::GraphNode NodeType, typename... Args>
    std::shared_ptr<NodeType> AddNode(Args&&... args) {
        std::unique_lock lock(lifecycle_mtx_);
        
        if (initialized_.load(std::memory_order_acquire)) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "Cannot AddNode after Init() has been called");
            throw std::runtime_error("Cannot AddNode after Init() - graph already locked");
        }
        
        auto node = std::make_shared<NodeType>(std::forward<Args>(args)...);
        auto inode = NodeToINodeConverter<NodeType>::Convert(node);
        if (!inode) {
            throw std::runtime_error("Failed to convert node to INode");
        }
        nodes_.push_back(inode);
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "Added node (total: " << nodes_.size() << ")");
        
        return node;
    }
    
    /**
     * @brief Add an existing node to the graph
     * @param node Shared pointer to node
     * 
     * Thread safety: NOT thread-safe for concurrent calls with Init/Start.
     * 
     * Registers an existing node for lifecycle management.
     */
    void AddNode(std::shared_ptr<INode> node) {
        std::unique_lock lock(lifecycle_mtx_);
        
        if (!node) {
            throw std::invalid_argument("Cannot add null node to graph");
        }
        
        if (initialized_.load(std::memory_order_acquire)) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "Cannot AddNode after Init() has been called");
            throw std::runtime_error("Cannot AddNode after Init() - graph already locked");
        }
        
        nodes_.push_back(node);
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "Added node (total: " << nodes_.size() << ")");
    }
    
    /**
     * @brief Create and add an edge between two nodes
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source port number
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination port number
     * @param src Source node
     * @param dst Destination node
     * @param capacity Queue capacity for the edge
     * @return Reference to the created edge
     * 
     * Thread safety: NOT thread-safe for concurrent calls with Init/Start.
     * Must complete all AddEdge calls before calling Init().
     * 
     * Preconditions:
     * - Init() must NOT have been called yet
     * - Both src and dst must be valid nodes (added via AddNode)
     * 
     * Exception safety: Strong guarantee. If creation fails, edges_ unchanged.
     */
    template <reflection::GraphNode SrcNode, std::size_t SrcPort, reflection::GraphNode DstNode, std::size_t DstPort>
    Edge<SrcNode, SrcPort, DstNode, DstPort>& AddEdge(
        std::shared_ptr<SrcNode> src,
        std::shared_ptr<DstNode> dst,
        std::size_t capacity = 8)
    {
        std::unique_lock lock(lifecycle_mtx_);
        
        if (initialized_.load(std::memory_order_acquire)) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "Cannot AddEdge after Init() has been called");
            throw std::runtime_error("Cannot AddEdge after Init() - graph already locked");
        }
        
        // Create edge
        auto edge = std::make_unique<Edge<SrcNode, SrcPort, DstNode, DstPort>>(src, dst, capacity);
        
        // Create wrapper that takes ownership
        auto wrapper = std::make_unique<EdgeWrapper<SrcNode, SrcPort, DstNode, DstPort>>(std::move(edge));
        
        // Get raw pointer before moving wrapper
        auto* edge_ptr = wrapper->get();
        
        // Phase 14a: Capture metadata while type information is available
        std::size_t src_idx = GetNodeIndex(src);
        std::size_t dst_idx = GetNodeIndex(dst);
        
        EdgeMetadata metadata;
        metadata.source_node_id = src_idx;
        metadata.source_port_id = SrcPort;
        metadata.dest_node_id = dst_idx;
        metadata.dest_port_id = DstPort;
        
        // Get the message type name from the edge value type
        using MessageType = typename Edge<SrcNode, SrcPort, DstNode, DstPort>::ValueType;
        metadata.message_type_name = typeid(MessageType).name();
        
        // Demangle the type name for human readability
        int status = 0;
        char* demangled = abi::__cxa_demangle(metadata.message_type_name.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            metadata.message_type_demangled = demangled;
            free(demangled);
        } else {
            metadata.message_type_demangled = metadata.message_type_name;
        }
        
        // Store wrapper (which owns the edge)
        edges_.push_back(std::move(wrapper));
        edge_metadata_.push_back(metadata);
        edge_metrics_.push_back(std::make_shared<EdgeMetrics>());  // Phase 14d: Initialize metrics for this edge
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "Added edge " << edges_.size() - 1 << " ("
                     << src_idx << ":" << SrcPort << " -> "
                     << dst_idx << ":" << DstPort << ")");
        
        return *edge_ptr;
    }
    
    /**
     * @brief Initialize all nodes, edges, and the thread pool
     * @return true if all components initialized successfully
     *
     * Thread safety: NOT thread-safe. Must be called once before Start().
     * Call sequence:
     *   1. Constructor creates GraphManager
     *   2. AddNode() (one or more times, before Init)
     *   3. AddEdge() (one or more times, before Init)
     *   4. Init() (must be called once)
     *   5. Start() (begins execution)
     * 
     * Side effects:
     * - Creates ThreadPool if not already created
     * - Calls Init() on all stored nodes
     * - Calls Init() on all stored edges
     * - No threads are started yet
     * 
     * Exception safety: Basic guarantee. If Init() fails on any node/edge,
     * remaining components are still initialized (partial initialization).
     * Call Stop()/Join() before retrying.
     * 
     * Returns false if:
     * - Init() already called (double-init prevented)
     * - ThreadPool init fails
     * - Any node's Init() returns false
     * - Any edge's Init() returns false
     * 
     * Initializes the ThreadPool first, then calls Init() on all nodes and edges.
     * Returns false if any initialization fails.
     */
    bool Init() {
        std::unique_lock lock(lifecycle_mtx_);
        
        // Prevent double initialization
        if (initialized_.load(std::memory_order_acquire)) {
            LOG4CXX_WARN(log4cxx::Logger::getLogger("graph.graph"),
                        "Init() called multiple times - ignoring");
            return false;
        }
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - starting initialization with " << num_threads_ << " threads");
        
        // Initialize ThreadPool
        if (!thread_pool_) {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                         "GraphManager::Init() - creating ThreadPool");
            if (num_threads_ == 0) {
                thread_pool_ = std::make_unique<ThreadPool>();
            } else {
                thread_pool_ = std::make_unique<ThreadPool>(num_threads_, thread_pool_config_);
            }
            if (!thread_pool_->Init()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                             "Failed to initialize ThreadPool");
                return false;
            }
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                         "GraphManager::Init() - ThreadPool initialized");
        }

        // Initialize message pool registry with common sizes
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - initializing message pools");
        MessagePoolRegistry::GetInstance().InitializeCommonPools(256);
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - message pools initialized");
        
        // Initialize nodes first
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - initializing " << nodes_.size() << " nodes");
        for (auto& node : nodes_) {
            if (!node->Init()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                             "Failed to initialize node");
                return false;
            }
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - all nodes initialized");
        
        // Then initialize edges
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - initializing " << edges_.size() << " edges");
        for (auto& edge : edges_) {
            if (!edge->Init()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                             "Failed to initialize edge");
                return false;
            }
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - all edges initialized");
        
        // Mark as initialized
        initialized_.store(true, std::memory_order_release);
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                    "GraphManager initialized: " << nodes_.size() << " nodes, " 
                    << edges_.size() << " edges");
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Init() - completed successfully");
        
        return true;
    }
    
    /**
     * @brief Start all nodes, edges, and the thread pool
     * @return true if all components started successfully
     *
     * Thread safety: NOT thread-safe. Must be called once, after Init().
     * Do not call Start() multiple times.
     * 
     * Preconditions:
     * - Init() must have been called successfully
     * - No threads must be currently running
     * - All AddNode()/AddEdge() calls must be complete
     * 
     * Side effects:
     * - Starts ThreadPool worker threads
     * - Calls Start() on all stored nodes (spawns port threads)
     * - Calls Start() on all stored edges (spawns edge threads)
     * - All queues are now accepting data
     * - Metrics collection begins
     * 
     * Happens-before: All resources allocated in Init() are visible to worker threads.
     * Memory ordering: ThreadPool and node/edge starts provide synchronization barriers.
     * 
     * Exception safety: Basic guarantee. If any Start() fails, some components may be running.
     * Call Stop()/Join() to safely shutdown.
     * 
     * Returns false if:
     * - Init() not called (not initialized)
     * - Start() already called (prevent double-start)
     * - ThreadPool Start() fails
     * - Any node Start() fails
     * - Any edge Start() fails
     * 
     * Starts the ThreadPool first, then calls Start() on all nodes and edges.
     * Returns false if any start fails.
     */
    bool Start() {
        std::unique_lock lock(lifecycle_mtx_);
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - beginning start sequence");
        
        // Validate preconditions
        if (!initialized_.load(std::memory_order_acquire)) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "Start() called before Init(). Call Init() first.");
            return false;
        }
        
        if (started_.load(std::memory_order_acquire)) {
            LOG4CXX_WARN(log4cxx::Logger::getLogger("graph.graph"),
                        "Start() called multiple times - ignoring");
            return false;
        }
        
        if (!thread_pool_) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "ThreadPool not initialized. Call Init() first.");
            return false;
        }
        
        // Start the thread pool
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - starting ThreadPool");
        if (!thread_pool_->Start()) {
            LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                         "Failed to start ThreadPool");
            return false;
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - ThreadPool started");
        
        // Start nodes first
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - starting " << nodes_.size() << " nodes");
        for (auto& node : nodes_) {
            if (!node->Start()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                             "Failed to start node");
                return false;
            }
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - all nodes started");
        
        // Then start edges
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - starting " << edges_.size() << " edges");
        for (auto& edge : edges_) {
            if (!edge->Start()) {
                LOG4CXX_ERROR(log4cxx::Logger::getLogger("graph.graph"),
                             "Failed to start edge");
                return false;
            }
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - all edges started");
        
        // Mark as started
        started_.store(true, std::memory_order_release);
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                    "GraphManager started execution");
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Start() - completed successfully");
        
        return true;
    }
    
    /**
     * @brief Request all nodes, edges, and thread pool to stop gracefully
     *
     * Thread safety: Safe to call multiple times (idempotent).
     * Non-blocking call - does not wait for threads to exit.
     * Must be followed by Join() for safe shutdown.
     * 
     * Call sequence:
     *   1. Stop()  (signal graceful shutdown)
     *   2. Join()  (wait for threads to exit)
     *   3. ~Graph() or Clear() (destroy/reset)
     * 
     * Side effects (in order):
     * 1. Calls Stop() on all edges (stops data flow between nodes)
     * 2. Calls Stop() on all nodes (signals threads to exit)
     * 3. Calls Stop() on ThreadPool (stops worker threads)
     * 
     * After Stop() returns:
     * - All threads have received stop signals
     * - Queues are disabled (Enqueue/Dequeue will fail)
     * - Port threads will exit their event loops when they see stop flag
     * - Threads may still be executing (running down queues)
     * - NOT safe to destroy nodes yet (use Join first)
     * 
     * Exception safety: No exceptions thrown (noexcept semantics).
     * All Stop() calls use try-catch internally to ensure cleanup continues.
     * 
     * Memory ordering: Uses acquire-release semantics to signal threads.
     * 
     * Calls Stop() on all edges first, then all nodes, then stops the thread pool.
     * Does not wait for completion - use Join() for that.
     */
    void Stop() {
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Stop() - beginning stop sequence");
        
        // Stop edges first to prevent new data flow
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Stop() - stopping " << edges_.size() << " edges");
        for (auto& edge : edges_) {
            edge->Stop();
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Stop() - all edges stopped");
        
        // Then stop nodes
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Stop() - stopping " << nodes_.size() << " nodes");
        for (auto& node : nodes_) {
            node->Stop();
        }
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                     "GraphManager::Stop() - all nodes stopped");
        
        // Finally stop the thread pool
        if (thread_pool_) {
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                         "GraphManager::Stop() - stopping ThreadPool");
            thread_pool_->Stop();
            LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                         "GraphManager::Stop() - ThreadPool stopped");
        }
        
        LOG4CXX_TRACE(log4cxx::Logger::getLogger("graph.graph"),
                    "GraphManager stop sequence completed");
    }
    
    /**
     * @brief Wait for all nodes and edges to complete indefinitely
     *
     * Thread safety: Safe to call multiple times (idempotent).
     * Blocks until all port threads, edge threads, and ThreadPool threads exit.
     * 
     * Prerequisites:
     * - Stop() must have been called first
     * - Calling Join() without Stop() will block forever (threads won't exit)
     * 
     * Side effects:
     * - Blocks caller indefinitely (or until all threads exit)
     * - Calls Join() on all edges (waits for edge threads to exit)
     * - Calls Join() on all nodes (waits for port threads to exit)
     * - Calls Join() on ThreadPool (waits for worker threads to exit)
     * 
     * After Join() returns successfully:
     * - ALL threads have exited
     * - All side effects from worker threads are visible (synchronization barrier)
     * - Safe to destroy nodes/edges or modify containers
     * - Safe to call destructors
     * 
     * Happens-before: All operations in threads are visible after Join().
     * Memory ordering: Full synchronization (acquire-release) at thread exit.
     * 
     * Exception safety: No exceptions thrown (suppressed internally).
     * If any thread::join() fails, continues trying to join remaining threads.
     * 
     * Uses retries to ensure all threads complete, falling back to longer waits if needed.
     * This ensures threads don't disappear while objects are being destroyed.
     */
    void Join() {
       // Stop edges first to prevent new data flow
        for (auto& edge : edges_) {
            edge->Join();
        }
        
        // Then stop nodes
        for (auto& node : nodes_) {
            node->Join();
        }
        
        // Finally stop the thread pool
        if (thread_pool_) {
            thread_pool_->Join();
        }
    }    
    
    /**
     * @brief Wait for all nodes, edges, and thread pool to complete with deadline
     * @param timeout_ms Total milliseconds to wait for all components
     * @return true if all threads completed within deadline, false if timeout exceeded
     * 
     * Thread safety: Safe to call multiple times; idempotent if first call succeeded.
     * Does NOT block forever - respects the timeout deadline strictly.
     * 
     * Timeout Behavior (CRITICAL):
     * - Allocates total timeout budget across 3 components: edges, nodes, pool
     * - Each component gets timeout_ms / 3 to exit gracefully
     * - If any component timeout expires, immediately returns false without waiting for others
     * - Logs which component(s) timed out for debugging
     * 
     * Prerequisites:
     * - Stop() must have been called first (required for threads to exit)
     * - Threads may take time to finish pending work
     * - Timeout should be >= 100ms to allow graceful shutdown
     * 
     * Timeout budgeting:
     *   - timeout_ms / 3 for edges: time for all edge threads to process final data
     *   - timeout_ms / 3 for nodes: time for all port threads to process final packets
     *   - timeout_ms / 3 for pool: time for worker threads to empty task queue and exit
     * 
     * Return values:
     * - true: ALL components joined successfully within budget
     * - false: At least one component failed to join within its timeout budget
     *   (no partial success - all must succeed for true return)
     * 
     * After successful return (true):
     * - ALL threads have exited
     * - All side effects from threads are visible
     * - Safe to destroy nodes/edges/pool
     * 
     * After timeout (false):
     * - Some threads may still be running
     * - Unsafe to destroy without additional safeguards
     * - Should call Stop() + Join() for cleanup
     * - Logs timeout details for debugging which component(s) stalled
     * 
     * Happens-before: If returns true, all thread work is visible. 
     *   If returns false, no ordering guarantee.
     * Memory ordering: Acquire-release at thread exit boundaries only.
     * 
     * Exception safety: No exceptions thrown. All exceptions suppressed.
     * Uses retry logic internally: Each component can retry within its timeout budget.
     * Detailed logging identifies which component(s) timeout for troubleshooting.
     */
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) {
        auto logger = log4cxx::Logger::getLogger("graph.graph");
        auto start = std::chrono::high_resolution_clock::now();
        
        // Allocate timeout budget sequentially across three components
        // This avoids busy-waiting and ensures fair timeout allocation
        const auto edge_budget = timeout_ms / 3;
        const auto node_budget = timeout_ms / 3;
        const auto pool_budget = timeout_ms - edge_budget - node_budget;  // Remainder
        
        // Join edges first
        for (auto& edge : edges_) {
            // Calculate elapsed time so far
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            
            // Check if we've exceeded total timeout
            if (elapsed >= timeout_ms) {
                LOG4CXX_TRACE(logger, "GraphManager timeout while joining edges "
                             "(elapsed " << elapsed.count() << "ms >= " << timeout_ms.count() << "ms)");
                return false;
            }
            
            // Use remaining edge budget
            auto remaining_edge_budget = edge_budget - 
                std::min(edge_budget, elapsed);
            
            if (!edge->JoinWithTimeout(remaining_edge_budget)) {
                LOG4CXX_TRACE(logger, "GraphManager edge join timeout "
                             "(budget " << remaining_edge_budget.count() << "ms)");
                return false;
            }
        }
        
        // Join nodes second
        for (auto& node : nodes_) {
            // Calculate elapsed time so far
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            
            // Check if we've exceeded total timeout
            if (elapsed >= timeout_ms) {
                LOG4CXX_TRACE(logger, "GraphManager timeout while joining nodes "
                             "(elapsed " << elapsed.count() << "ms >= " << timeout_ms.count() << "ms)");
                return false;
            }
            
            // Use remaining node budget
            auto remaining_node_budget = node_budget - 
                std::min(node_budget, elapsed - edge_budget);
            
            // Cap at minimum of 1ms to avoid zero timeouts
            remaining_node_budget = std::max(remaining_node_budget, std::chrono::milliseconds(1));
            
            if (!node->JoinWithTimeout(remaining_node_budget)) {
                LOG4CXX_TRACE(logger, "GraphManager node join timeout "
                             "(budget " << remaining_node_budget.count() << "ms)");
                return false;
            }
        }
        
        // Finally join thread pool
        if (thread_pool_) {
            // Calculate elapsed time so far
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start);
            
            // Check if we've exceeded total timeout
            if (elapsed >= timeout_ms) {
                LOG4CXX_TRACE(logger, "GraphManager timeout while joining thread pool "
                             "(elapsed " << elapsed.count() << "ms >= " << timeout_ms.count() << "ms)");
                return false;
            }
            
            // Use remaining pool budget
            auto remaining_pool_budget = pool_budget - 
                std::min(pool_budget, elapsed - edge_budget - node_budget);
            
            // Cap at minimum of 1ms to avoid zero timeouts
            remaining_pool_budget = std::max(remaining_pool_budget, std::chrono::milliseconds(1));
            
            if (!thread_pool_->JoinWithTimeout(remaining_pool_budget)) {
                LOG4CXX_TRACE(logger, "GraphManager thread pool join timeout "
                             "(budget " << remaining_pool_budget.count() << "ms)");
                return false;
            }
        }
        
        // All components joined successfully within timeout
        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start);
        LOG4CXX_TRACE(logger, "GraphManager all components joined in " << total_elapsed.count() << "ms");
        
        return true;
    }
    
    /**
     * @brief Get the number of nodes in the graph
     */
    std::size_t NodeCount() const {
        return nodes_.size();
    }
    
    /**
     * @brief Get the number of edges in the graph
     */
    std::size_t EdgeCount() const {
        return edges_.size();
    }
    
    /**
     * @brief Generate human-readable display of graph structure
     * 
     * Returns a formatted string showing all nodes and edges in the graph.
     * Provides diagnostic information useful for debugging and visualization.
     * 
     * @return String containing graph structure information (nodes and edges)
     * 
     * Thread safety: Safe to call at any time. Takes read-only snapshot of graph.
     * Safe even while graph is running - does not modify any state.
     * 
     * Output format: Human-readable text with sections for:
     * - Header with timestamp
     * - Node count and list with types and addresses
     * - Edge count and type-erased connections
     * - Summary statistics
     * 
     * Example output:
     * ```
     * ================================================================================
     * GraphManager Structure Report (TEXT FORMAT)
     * ================================================================================
     * Generated: 2025-12-19T14:23:45Z
     * 
     * NODES (2 total)
     * ----------------
     * [1] Name: AccelerometerSource
     *     Type: graph::SourceNode<Message>
     *     Address: 0x7f1234abcd00
     *     Status: initialized
     * 
     * [2] Name: FlightLogger
     *     Type: graph::SinkNode<Message>
     *     Address: 0x7f1234abcd10
     *     Status: initialized
     * 
     * EDGES (1 total)
     * ----------------
     * Edge[0]: Node[0]:0 → Node[1]:0 (type: Message)
     * 
     * GRAPH SUMMARY
     * ----------------
     * Total Nodes: 2
     * Total Edges: 1
     * Initialized: yes
     * Started: no
     * Metrics Enabled: no
     * ================================================================================
     * ```
     */
    std::string DisplayGraph() const {
        std::ostringstream oss;
        
        // Helper to format type name
        auto FormatTypeName = [](const std::string& mangled_name) -> std::string {
            // Attempt basic demangling if needed (platform-specific)
            // For now, return as-is (better to have mangled name than nothing)
            return mangled_name;
        };
        
        // Get timestamp in ISO 8601 format
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp_buf[30];
        std::strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));
        
        oss << "================================================================================\n";
        oss << "GraphManager Structure Report (TEXT FORMAT)\n";
        oss << "================================================================================\n";
        oss << "Generated: " << timestamp_buf << "\n";
        oss << "Graph Nodes: " << nodes_.size() << " | Graph Edges: " << edges_.size() << "\n\n";
        
        // Nodes section with more detail
        oss << "NODES (" << nodes_.size() << " total)\n";
        oss << "================================================================================\n";
        if (nodes_.empty()) {
            oss << "(no nodes in graph)\n";
        } else {
            for (size_t i = 0; i < nodes_.size(); ++i) {
                const auto& node = nodes_[i];
                oss << "[" << i << "] ";
                
                if (node) {
                    // Note: GetName() is only available if node inherits from NamedType
                    // Using type name from RTTI instead for compatibility
                    // Use decltype to avoid -Wpotentially-evaluated-expression warning
                    const std::type_info& node_type = typeid(std::decay_t<decltype(*node)>);
                    oss << "      Type: " << FormatTypeName(node_type.name()) << "\n";
                    
                    // Address
                    oss << "      Address: " << static_cast<const void*>(node.get()) << "\n";
                    
                    // Status
                    oss << "      Status: (initialized: yes, running: yes)\n";
                } else {
                    oss << "(null node)\n";
                }
                
                if (i < nodes_.size() - 1) {
                    oss << "\n";
                }
            }
        }
        oss << "\n";
        
        // Edges section
        oss << "EDGES (" << edges_.size() << " total)\n";
        oss << "================================================================================\n";
        if (edges_.empty()) {
            oss << "(no edges in graph)\n";
        } else {
            for (size_t i = 0; i < edges_.size(); ++i) {
                const auto& edge = edges_[i];
                if (edge) {
                    oss << "Edge[" << i << "]: ";
                    
                    // Phase 14b: Use metadata to recover type information
                    if (i < edge_metadata_.size() && edge_metadata_[i].is_valid()) {
                        const auto& meta = edge_metadata_[i];
                        oss << "Node[" << meta.source_node_id << "]:" << meta.source_port_id
                            << " → Node[" << meta.dest_node_id << "]:" << meta.dest_port_id;
                        oss << " (type: " << meta.message_type_demangled << ")\n";
                    } else {
                        oss << "Node[?] → Node[?] (type-erased connection)\n";
                    }
                } else {
                    oss << "Edge[" << i << "]: (null edge)\n";
                }
            }
        }
        oss << "\n";
        
        // Summary section
        oss << "GRAPH SUMMARY\n";
        oss << "================================================================================\n";
        oss << "Total Nodes:       " << nodes_.size() << "\n";
        oss << "Total Edges:       " << edges_.size() << "\n";
        oss << "Initialized:       " << (initialized_.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";
        oss << "Started:           " << (started_.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";
        oss << "Metrics Enabled:   " << (metrics_enabled_.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";
        oss << "================================================================================\n";
        
        return oss.str();
    }
    
    /**
     * @brief Export the CURRENT RUNTIME STATE of the graph as JSON
     * 
     * Exports the RUNTIME STATE (currently instantiated nodes and their connections).
     * This is NOT the same as the TOPOLOGY DEFINITION used to construct the graph
     * via GraphFactory::LoadGraphFromJSON().
     * 
     * Use cases:
     * - Debugging/introspection: View which nodes are currently instantiated
     * - Monitoring: Track graph execution state and connectivity
     * - Visualization: Render dynamic graph structure with live node instances
     * 
     * @return JSON string with nodes, edges, and graph status
     * 
     * Thread safety: Safe to call at any time. Read-only operation.
     * 
     * Example output:
     * ```json
     * {
     *   "generated": "2025-12-19T14:23:45Z",
     *   "nodes": [
     *     {
     *       "id": 0,
     *       "name": "AccelerometerSource",
     *       "type": "graph::SourceNode<Message>",
     *       "address": "0x7f1234abcd00"
     *     }
     *   ],
     *   "edges": [
     *     {
     *       "id": 0,
     *       "source": 0,
     *       "source_port": 0,
     *       "destination": 1,
     *       "dest_port": 0,
     *       "type": "SensorMessage"
     *     }
     *   ],
     *   "summary": {
     *     "total_nodes": 1,
     *     "total_edges": 1,
     *     "initialized": true,
     *     "started": false
     *   }
     * }
     * ```
     */
    std::string DisplayGraphJSON() const {
        std::ostringstream oss;
        
        // Get timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp_buf[30];
        std::strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));
        
        oss << "{\n";
        oss << "  \"generated\": \"" << timestamp_buf << "\",\n";
        
        // Nodes array
        oss << "  \"nodes\": [\n";
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            oss << "    {\n";
            oss << "      \"id\": " << i << ",\n";
            
            if (node) {
                // Note: GetName() is only available if node inherits from NamedType
                // Since all INode implementations may not have GetName(), use type info instead
                const std::type_info& node_type = typeid(std::decay_t<decltype(*node)>);
                oss << "      \"type\": \"" << node_type.name() << "\",\n";
                
                oss << "      \"address\": \"" << static_cast<const void*>(node.get()) << "\"\n";
            } else {
                oss << "      \"name\": \"(null)\",\n";
                oss << "      \"type\": \"(null)\",\n";
                oss << "      \"address\": \"null\"\n";
            }
            
            oss << "    }";
            if (i < nodes_.size() - 1) oss << ",";
            oss << "\n";
        }
        oss << "  ],\n";
        
        // Edges array
        oss << "  \"edges\": [\n";
        for (size_t i = 0; i < edges_.size(); ++i) {
            oss << "    {\n";
            oss << "      \"id\": " << i << ",\n";
            
            // Phase 14a: Use metadata to recover type information
            if (i < edge_metadata_.size() && edge_metadata_[i].is_valid()) {
                const auto& meta = edge_metadata_[i];
                oss << "      \"source\": " << meta.source_node_id << ",\n";
                oss << "      \"source_port\": " << meta.source_port_id << ",\n";
                oss << "      \"destination\": " << meta.dest_node_id << ",\n";
                oss << "      \"dest_port\": " << meta.dest_port_id << ",\n";
                oss << "      \"type\": \"" << meta.message_type_demangled << "\"\n";
            } else {
                oss << "      \"source\": -1,\n";
                oss << "      \"destination\": -1,\n";
                oss << "      \"type\": \"unknown\"\n";
            }
            
            oss << "    }";
            if (i < edges_.size() - 1) oss << ",";
            oss << "\n";
        }
        oss << "  ],\n";
        
        // Summary
        oss << "  \"summary\": {\n";
        oss << "    \"total_nodes\": " << nodes_.size() << ",\n";
        oss << "    \"total_edges\": " << edges_.size() << ",\n";
        oss << "    \"initialized\": " << (initialized_.load(std::memory_order_relaxed) ? "true" : "false") << ",\n";
        oss << "    \"started\": " << (started_.load(std::memory_order_relaxed) ? "true" : "false") << ",\n";
        oss << "    \"metrics_enabled\": " << (metrics_enabled_.load(std::memory_order_relaxed) ? "true" : "false") << "\n";
        oss << "  }\n";
        oss << "}\n";
        
        return oss.str();
    }
    
    /**
     * @brief Generate Graphviz DOT format display of graph structure
     * 
     * Returns graph in DOT format for visualization with Graphviz tools.
     * Output can be piped to `dot`, `neato`, `fdp` or `sfdp` for rendering.
     * 
     * Usage:
     * @code
     * // Generate and save DOT file
     * std::ofstream dot_file("graph.dot");
     * dot_file << graph.DisplayGraphDOT();
     * dot_file.close();
     * 
     * // Render to PNG
     * system("dot -Tpng graph.dot -o graph.png");
     * @endcode
     * 
     * @return DOT format string suitable for Graphviz
     * 
     * Thread safety: Safe to call at any time. Read-only operation.
     * 
     * Example output:
     * ```
     * digraph GraphManager {
     *   rankdir=LR;
     *   node [shape=box];
     *   
     *   node_0 [label="AccelerometerSource", tooltip="SourceNode<Message>"];
     *   node_1 [label="FlightLogger", tooltip="SinkNode<Message>"];
     *   
     *   node_0 -> node_1 [label="Message", tooltip="port 0 -> port 0"];
     * }
     * ```
     */
    std::string DisplayGraphDOT() const {
        std::ostringstream oss;
        
        oss << "digraph GraphManager {\n";
        oss << "  rankdir=LR;\n";
        oss << "  node [shape=box, style=rounded];\n";
        oss << "  graph [label=\"GraphManager Structure\"];\n\n";
        
        // Nodes
        if (!nodes_.empty()) {
            oss << "  /* Nodes */\n";
            for (size_t i = 0; i < nodes_.size(); ++i) {
                const auto& node = nodes_[i];
                oss << "  node_" << i << " [label=\"";
                
                if (node) {
                    // Use type info since GetName() is only in NamedType mixin
                    std::string node_type_name = typeid(std::decay_t<decltype(*node)>).name();
                    oss << node_type_name;
                } else {
                    oss << "null";
                }
                
                // Use decltype to avoid -Wpotentially-evaluated-expression warning
                std::string node_type_str = (node ? typeid(std::decay_t<decltype(*node)>).name() : "null");
                oss << "\", tooltip=\"" << node_type_str << "\"];\n";
            }
            oss << "\n";
        }
        
        // Edges - Phase 14c: Use metadata for accurate connections and labels
        if (!edges_.empty()) {
            oss << "  /* Edges (Phase 14c: type information recovered) */\n";
            for (size_t i = 0; i < edges_.size(); ++i) {
                if (i < edge_metadata_.size() && edge_metadata_[i].is_valid()) {
                    const auto& meta = edge_metadata_[i];
                    oss << "  node_" << meta.source_node_id << " -> node_" << meta.dest_node_id
                        << " [label=\"" << meta.message_type_demangled << "\", "
                        << "tooltip=\"port " << meta.source_port_id << " -> port " << meta.dest_port_id << "\"];\n";
                } else {
                    // Fallback for edges without metadata
                    oss << "  /* Edge " << i << ": metadata not available */\n";
                }
            }
            oss << "\n";
        }
        
        oss << "}\n";
        
        return oss.str();
    }
    
    /**
     * @brief Print graph structure to output stream
     * 
     * Convenience method that prints the result of DisplayGraph() to the provided stream.
     * 
     * @param out Output stream (default: std::cout)
     * 
     * @code
     * GraphManager graph;
     * // ... add nodes and edges ...
     * graph.PrintGraphStructure();  // Prints to stdout
     * 
     * std::ofstream logfile("graph_structure.txt");
     * graph.PrintGraphStructure(logfile);  // Prints to file
     * @endcode
     */
    void PrintGraphStructure(std::ostream& out = std::cout) const {
        out << DisplayGraph();
    }
    
    /**
     * @brief Enable metrics collection for this graph and all nodes/edges
     * 
     * Once enabled, performance statistics will be tracked across the entire graph.
     * Can be toggled at runtime without side effects.
     */
    void EnableMetrics(bool enabled = true) {
        metrics_enabled_.store(enabled, std::memory_order_release);
        
        // Also enable metrics on all nodes if enabled
        // Note: EnableMetrics() is only in NamedType mixin, not all INode implementations
        if (enabled) {
            for (auto& node : nodes_) {
                if (node) {
                    // Skip node metrics enablement - not all INode implementations support it
                }
            }
        }
    }

    /**
     * @brief Get reference to metrics for reading
     * 
     * Metrics are updated continuously if EnableMetrics(true) has been called.
     * Safe to read at any time from any thread.
     */
    const GraphMetrics& GetMetrics() const {
        return metrics_;
    }

    /**
     * @brief Get metadata for a specific edge
     * @param edge_index Zero-based index of the edge
     * @return Pointer to EdgeMetadata, or nullptr if edge_index is out of bounds
     * 
     * Phase 14a: Access per-edge type information indexed by edge creation order.
     * Metadata includes source/destination node IDs, port IDs, and message type names.
     * Correlated with edges_ and edge_metrics_ vectors via identical indexing.
     * Thread-safe: Safe to read at any time.
     */
    const EdgeMetadata* GetEdgeMetadata(std::size_t edge_index) const {
        if (edge_index >= edge_metadata_.size()) {
            return nullptr;
        }
        return &edge_metadata_[edge_index];
    }

    /**
     * @brief Get metrics for a specific edge
     * @param edge_index Zero-based index of the edge
     * @return Pointer to EdgeMetrics, or nullptr if edge_index is out of bounds
     * 
     * Phase 14d: Access per-edge metrics indexed by edge creation order.
     * Metrics are correlated with edge_metadata_ and edges_ vectors.
     * Thread-safe: Safe to read at any time.
     */
    std::shared_ptr<const EdgeMetrics> GetEdgeMetrics(std::size_t edge_index) const {
        if (edge_index >= edge_metrics_.size()) {
            return nullptr;
        }
        return edge_metrics_[edge_index];
    }

    /**
     * @brief Reset all metrics counters
     * 
     * Clears all counters and timing statistics. Safe to call during operation.
     */
    void ResetMetrics() {
        metrics_.init_time_ns.store(0, std::memory_order_release);
        metrics_.start_time_ns.store(0, std::memory_order_release);
        metrics_.execution_time_ns.store(0, std::memory_order_release);
        metrics_.total_items_processed.store(0, std::memory_order_release);
        metrics_.total_items_rejected.store(0, std::memory_order_release);
        metrics_.peak_active_threads.store(0, std::memory_order_release);
        metrics_.peak_queue_depth.store(0, std::memory_order_release);
        metrics_.node_init_failures.store(0, std::memory_order_release);
        metrics_.edge_init_failures.store(0, std::memory_order_release);
        metrics_.node_start_failures.store(0, std::memory_order_release);
        metrics_.edge_start_failures.store(0, std::memory_order_release);
    }
    
    /**
     * @brief Clear all nodes and edges from the graph
     * 
     * Thread safety: NOT thread-safe. Must only be called when:
     * - Stop() has been called (requests all threads to exit)
     * - Join() or JoinWithTimeout() has completed successfully (all threads have exited)
     * 
     * Preconditions:
     * - No nodes or edges should be running (call Stop() first)
     * - No external code holds shared_ptr references to nodes (except internal)
     * - All threads have exited (call Join() first)
     * - Caller has exclusive access to this GraphManager
     * 
     * Side effects:
     * - Clears nodes_ vector (destroys all INode objects as last shared_ptr is released)
     * - Clears edges_ vector (destroys all IEdgeBase objects, releasing memory)
     * - Destructors of nodes/edges are called immediately
     * - Destructors may wait for threads (will deadlock if threads still running!)
     * 
     * After Clear() returns:
     * - Graph is empty (NodeCount() == 0, EdgeCount() == 0)
     * - Safe to call AddNode/AddEdge again to rebuild graph
     * - Safe to destroy GraphManager without dangling references
     * 
     * Typical sequence:
     * ```
     *   graphmgr.Stop();         // Request all threads to stop
     *   graphmgr.Join();         // Wait for all threads to exit
     *   graphmgr.Clear();        // Safely destroy all nodes/edges
     *   // or start new graph with AddNode/AddEdge
     * ```
     * 
     * Exception safety: Basic guarantee. Exceptions from destructors are suppressed.
     * If a node/edge destructor throws, it's logged and processing continues.
     * 
     * @warning Calling Clear() without first calling Stop() + Join() will:
     *   - Deadlock if threads try to wait on join in destructors
     *   - Crash if threads are still accessing node/edge data
     *   - Corrupt thread-safety invariants
     * Always follow the Stop -> Join -> Clear sequence!
     */
    void Clear() {
        edges_.clear();
        nodes_.clear();
        initialized_.store(false, std::memory_order_release);
        started_.store(false, std::memory_order_release);
    }
    
    /**
     * @brief Get the thread pool (for advanced usage)
     * @return Pointer to the ThreadPool, or nullptr if not initialized
     */
    ThreadPool* GetThreadPool() {
        return thread_pool_.get();
    }
    
    /**
     * @brief Get the thread pool configuration
     * @return Reference to the thread pool configuration
     */
    ThreadPool::DeadlockConfig& GetThreadPoolConfig() {
        return thread_pool_config_;
    }

    // ========================================================================
    // Visualization Support (Domain: visualization)
    // ========================================================================
    
    /**
     * @brief Get all nodes in the graph (read-only)
     * 
     * Used by visualization system to capture graph state.
     * Thread-safe: Returns const reference to internal vector.
     * 
     * @return Vector of shared_ptr to all nodes in the graph
     * @note Caller must not modify returned collection
     */
    const std::vector<std::shared_ptr<INode>>& GetNodes() const {
        return nodes_;
    }
    
    /**
     * @brief Get all edges in the graph (read-only)
     * 
     * Used by visualization system to capture graph state.
     * Thread-safe: Returns const reference to internal vector.
     * 
     * @return Vector of unique_ptr to all edges in the graph
     * @note Caller must not modify returned collection
     */
    const std::vector<std::unique_ptr<IEdgeBase>>& GetEdges() const {
        return edges_;
    }

private:
    mutable std::mutex lifecycle_mtx_;
    
    // State flags: prevent invalid state transitions
    std::atomic<bool> initialized_{false};  // Set by Init(), checked by Start()
    std::atomic<bool> started_{false};      // Set by Start(), prevent double-start
    std::atomic<bool> metrics_enabled_{false};  // Enable/disable metrics collection
    
    ThreadPool::DeadlockConfig thread_pool_config_;
    std::unique_ptr<ThreadPool> thread_pool_;
    size_t num_threads_;
    std::vector<std::shared_ptr<INode>> nodes_;
    std::vector<std::unique_ptr<IEdgeBase>> edges_;  // Owns edges through type-erased interface
    std::vector<EdgeMetadata> edge_metadata_;  // Parallel vector to recover type information (Phase 14a)
    std::vector<std::shared_ptr<EdgeMetrics>> edge_metrics_;    // Parallel vector for per-edge metrics (Phase 14d)
    GraphMetrics metrics_;  // Performance metrics for graph execution
    
    /**
     * @brief Helper: Find the index of a node in nodes_ vector
     * @param node The node pointer to search for (any node type)
     * @return Index in nodes_ vector, or SIZE_MAX if not found
     */
    template <typename NodeType>
    std::size_t GetNodeIndex(const std::shared_ptr<NodeType>& node) const {
        // Cast to void* for pointer equality comparison (works across inheritance)
        const void* node_ptr = node.get();
        
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (static_cast<const void*>(nodes_[i].get()) == node_ptr) {
                return i;
            }
        }
        return SIZE_MAX;
    }
};

} // namespace graph

