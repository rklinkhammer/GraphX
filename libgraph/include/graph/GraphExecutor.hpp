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

#include "graph/GraphManager.hpp"
#include "graph/DefaultCapabilityBus.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/ExecutionResult.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "graph/ExecutionState.hpp"
#include <memory>
#include <chrono>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <functional>

namespace graph {

/**
 * @class GraphExecutor
 * @brief Orchestrates execution of a directed acyclic graph (DAG)
 *
 * GraphExecutor is the main engine for executing computational graphs, coordinating
 * node execution, managing state transitions, and publishing metrics through
 * capabilities (MetricsCapability, GraphCapability).
 *
 * Execution States:
 * - **STOPPED**: Not executing, ready to start
 * - **INITIALIZING**: Running Init() to prepare nodes
 * - **RUNNING**: Active execution of graph nodes
 * - **PAUSED**: Suspended execution (nodes retain state)
 * - **STOPPING**: Graceful shutdown in progress
 *
 * Execution Flow:
 * 1. Init() → Initialize graph and nodes
 * 2. Start() → Begin execution
 * 3. Run() → Execute nodes (blocking)
 * 4. Stop() → Request graceful shutdown
 * 5. Join() → Wait for completion
 *
 * Policies:
 * The executor uses an ExecutionPolicyChain to customize behavior:
 * - MetricsPolicy: Publishes metrics updates
 * - CompletionPolicy: Detects when execution is complete
 * - CommandPolicy: Handles external commands
 * - Other policies can inject data or modify behavior
 *
 * Thread Safety:
 * - Init(), Start(), Stop(), Join() are not thread-safe for concurrent calls
 * - State queries (IsRunning(), GetState()) are thread-safe
 * - Policies may run in executor threads and must handle synchronization
 *
 * @see ExecutionPolicyChain, IExecutionPolicy
 * @see MetricsCapability, GraphCapability
 *
 * @example
 *   auto graph = std::make_shared<GraphManager>();
 *   // ... configure graph ...
 *   
 *   auto executor = std::make_unique<GraphExecutor>(policies);
 *   executor->Init();
 *   executor->Start();
 *   
 *   // Run blocking execution
 *   auto result = executor->Run();
 *   
 *   executor->Stop();
 *   executor->Join();
 */
class GraphExecutor {
public:

    /**
     * @brief Construct executor with execution policies
     *
     * @param policy_chain Unique pointer to execution policy chain containing
     *                      all policies that customize executor behavior
     *
     * Preconditions:
     * - policy_chain must be a valid ExecutionPolicyChain
     * - policy_chain must not be nullptr
     *
     * Postconditions:
     * - Executor holds the policy chain
     * - Ready to call Init()
     * - State is STOPPED
     *
     * @throws std::invalid_argument if policy_chain is nullptr
     */
    explicit GraphExecutor(std::unique_ptr<ExecutionPolicyChain> policy_chain,
                           std::shared_ptr<app::capabilities::GraphCapability> graph_capability);

    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     *
     * Ensures Stop() and Join() are called before destruction.
     * Implementations should clean up policy resources.
     */
    virtual ~GraphExecutor() = default;
    
    /**
     * @brief Initialize the executor and all graph nodes
     *
     * Prepares the executor for execution:
     * 1. Calls Init() on all policies in chain
     * 2. Initializes all nodes in the graph
     * 3. Validates graph structure and connections
     * 4. Transitions to INITIALIZED or STOPPED state
     *
     * @return InitializationResult with status and error message if failed
     *
     * @pre State must be STOPPED
     * @post State is INITIALIZED or STOPPED (on error)
     */
    InitializationResult Init();
    
    /**
     * @brief Start graph execution
     *
     * Begins the execution loop in the executor's worker thread.
     * The Run() method must be called to actually execute the graph.
     *
     * @return ExecutionResult with status
     *
     * @pre State must be INITIALIZED
     * @post State is RUNNING (or error)
     */
    ExecutionResult Start();
    
    /**
     * @brief Execute graph nodes (blocking)
     *
     * Main execution loop that:
     * 1. Runs registered policies on each iteration
     * 2. Executes ready nodes
     * 3. Processes completion signals
     * 4. Continues until graph is complete or Stop() is called
     *
     * @return ExecutionResult with final state and any error
     *
     * @pre State must be RUNNING
     * @post State is STOPPED or other terminal state
     */
    ExecutionResult Run();
    
    /**
     * @brief Request graceful shutdown
     *
     * Signals executor to stop after current iteration.
     * Does not wait for completion; call Join() to wait.
     *
     * @return ExecutionResult with status
     *
     * @post State transitions to STOPPING
     */
    ExecutionResult Stop();
    ExecutionResult Join();
    
    // ========== Query Methods (Lock-Free, Thread-Safe) ==========

    /**
     * @brief Check if execution is currently running
     *
     * @return True if in RUNNING or PAUSED state (graph executing)
     *
     * Thread-Safety: Lock-free atomic read, safe from any thread
     * Time Complexity: O(1)
     */
    bool IsRunning() const;

    /**
     * @brief Check if execution is paused
     *
     * @return True if in PAUSED state
     *
     * Thread-Safety: Lock-free atomic read, safe from any thread
     * Time Complexity: O(1)
     */
    bool IsPaused() const;

    /**
     * @brief Check if in error state
     *
     * @return True if in ERROR state
     *
     * Thread-Safety: Lock-free atomic read, safe from any thread
     * Time Complexity: O(1)
     */
    bool IsInError() const;

    void DisplayResults(const ExecutionResult& results) const;
    

    ExecutionResult Execute();

    // std::shared_ptr<app::AppContext> GetAppContext() const {
    //     return context_;
    // }
    
    template<typename CapabilityT>
    std::shared_ptr<CapabilityT> GetCapability() const {

        return graph_capability_->GetCapabilityBus().Get<CapabilityT>();
    }
    
    template<typename CapabilityT>
    bool Has() const {
        return graph_capability_->GetCapabilityBus().Has<CapabilityT>();
    }

    template<typename CapabilityT>
    void Register(std::shared_ptr<CapabilityT> capability) {
        graph_capability_->GetCapabilityBus().Register<CapabilityT>(capability);
    }
    
    std::shared_ptr<graph::GraphManager> GetGraphManager() const {
        return graph_manager_;
    }

    void SetGraphManager(std::shared_ptr<graph::GraphManager> graph_manager) {
        graph_manager_ = graph_manager;
        graph_capability_->SetGraphManager(graph_manager_);
    }

    void SetExecutionState(graph::ExecutionState state) {
        current_state_ = state;
    }

    graph::ExecutionState GetExecutionState() const {
        return current_state_;
    }   

private:

    int CountNodesinLifecycleState(graph::LifecycleState state) const;

    std::unique_ptr<ExecutionPolicyChain> policy_chain_;
    std::shared_ptr<graph::GraphManager> graph_manager_;
    std::shared_ptr<app::capabilities::GraphCapability>  graph_capability_;
    ExecutionState current_state_ = graph::ExecutionState::STOPPED;

    mutable std::atomic<bool> is_stopped{false};

};

}  // namespace graph

