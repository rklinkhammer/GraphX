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


#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"

#include <chrono>
#include <thread>
#include <log4cxx/logger.h>

namespace graph {

// ============================================================================
// Static Logger for GraphExecutor
// ============================================================================

static log4cxx::LoggerPtr logger_ = 
    log4cxx::Logger::getLogger("graph.GraphExecutor");


// ============================================================================
// GraphExecutor Implementation
// ============================================================================

GraphExecutor::GraphExecutor( std::unique_ptr<ExecutionPolicyChain> policy_chain,
                              std::shared_ptr<capabilities::GraphCapability> graph_capability) :
            policy_chain_(std::move(policy_chain)),
            graph_capability_(std::move(graph_capability)) {
    LOG4CXX_TRACE(logger_, "GraphExecutor constructed");
    if(!graph_capability_) {
        throw std::invalid_argument("GraphExecutor requires a valid GraphCapability");
    }
    if(!graph_capability_->GetGraphManager()) {
        throw std::invalid_argument("GraphCapability must have a valid GraphManager");
    }
    graph_manager_ = graph_capability_->GetGraphManager();
    graph_capability_->GetCapabilityBus().Register<capabilities::GraphCapability>(graph_capability_);
}

InitializationResult GraphExecutor::Init() {
    auto start_time = std::chrono::high_resolution_clock::now();

    LOG4CXX_TRACE(logger_, "GraphExecutor::Init() called");
    InitializationResult init_result;
    bool policy_result = policy_chain_ ? policy_chain_->OnInit(*graph_capability_) : true;
    if(!policy_result) {
        init_result.success = false;
        init_result.message = "ExecutionPolicyChain::OnInit() failed";
        return init_result;
    }
    // Initialize the graph
    if (!graph_manager_->Init()) {
        init_result.success = false;
        init_result.message = "GraphManager::Init() failed";
        return init_result;
    }
    init_result.nodes_initialized = CountNodesinLifecycleState(graph::LifecycleState::Initialized);
    init_result.nodes_failed = CountNodesinLifecycleState(graph::LifecycleState::Invalid) +
                               CountNodesinLifecycleState(graph::LifecycleState::Uninitialized);
    SetExecutionState(ExecutionState::INITIALIZED);
    init_result.success = true;
    init_result.message = "GraphExecutor initialized successfully";
    LOG4CXX_TRACE(logger_, "GraphExecutor::Init() completed");

    auto end_time = std::chrono::high_resolution_clock::now();
    init_result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    return init_result;
}

ExecutionResult GraphExecutor::Start() {
    auto start_time = std::chrono::high_resolution_clock::now();

    LOG4CXX_TRACE(logger_, "GraphExecutor::Start() called");
    ExecutionResult result;
    bool policy_result = policy_chain_ ? policy_chain_->OnStart(*graph_capability_) : true;
    if(!policy_result) {
        result.success = false;
        result.message = "ExecutionPolicyChain::OnStart() failed";
        return result;
    }
    // Start the graph
    if (!graph_manager_->Start()) {
        result.success = false;
        result.message = "GraphManager::Start() failed";
        return result;
    }
    SetExecutionState(ExecutionState::RUNNING);
    LOG4CXX_TRACE(logger_, "GraphExecutor::Start() completed");

    result.success = true;
    result.message = "GraphExecutor started successfully";
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    return result;
}

ExecutionResult GraphExecutor::Run() {
    auto start_time = std::chrono::high_resolution_clock::now();

    LOG4CXX_TRACE(logger_, "GraphExecutor::Run() called");
    ExecutionResult result;   
    bool policy_result =  policy_chain_ ? policy_chain_->OnRun(*graph_capability_) : false;
    if(!policy_result) {
        result.success = false;
        result.message = "ExecutionPolicyChain::OnRun() failed";
        return result;
    }

    while(!graph_capability_->IsStopped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    LOG4CXX_TRACE(logger_, "GraphExecutor::Run() completed, success=" << result.success);
    result.success = true;
    result.message = "GraphExecutor run completed successfully";    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    return result;
}

ExecutionResult GraphExecutor::Stop() {
    auto start_time = std::chrono::high_resolution_clock::now();

    LOG4CXX_TRACE(logger_, "GraphExecutor::Stop() called");
    ExecutionResult result;
    // Stop the graph
    graph_manager_->Stop();
    if (policy_chain_) {
        policy_chain_->OnStop(*graph_capability_);
    }
    LOG4CXX_TRACE(logger_, "GraphExecutor::Stop() completed");

    result.success = true;
    result.message = "GraphExecutor stopped successfully";
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    return result;
}

ExecutionResult GraphExecutor::Join() {
    auto start_time = std::chrono::high_resolution_clock::now();

    LOG4CXX_TRACE(logger_, "GraphExecutor::Join() called");
    ExecutionResult result;

    // Join all threads
    graph_manager_->Join();
    if (policy_chain_) {
        policy_chain_->OnJoin(*graph_capability_);
    }
    LOG4CXX_TRACE(logger_, "GraphExecutor::Join() completed");

    result.success = true;
    result.message = "GraphExecutor joined successfully";
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    return result;
}

void GraphExecutor::DisplayResults(const ExecutionResult& results) const { 
    (void)results;
}

ExecutionResult GraphExecutor::Execute() {
    ExecutionResult result;

    auto init_result = Init();
    if (!init_result.success) {
        result.success = false;
        result.message = "Execute failed during Init(): " + init_result.message;
        LOG4CXX_ERROR(logger_, result.message);
        return result;
    }

    auto start_result = Start();
    if (!start_result.success) {
        result.success = false;
        result.message = "Execute failed during Start(): " + start_result.message;
        LOG4CXX_ERROR(logger_, result.message);
        return result;
    }

    auto run_result = Run();
    if (!run_result.success) {
        result.success = false;
        result.message = "Execute failed during Run(): " + run_result.message;
        LOG4CXX_ERROR(logger_, result.message);
        return result;
    }

    auto stop_result = Stop();
    if (!stop_result.success) {
        result.success = false;
        result.message = "Execute failed during Stop(): " + stop_result.message;
        LOG4CXX_ERROR(logger_, result.message);
        return result;
    }

    auto join_result = Join();
    if (!join_result.success) {
        result.success = false;
        result.message = "Execute failed during Join(): " + join_result.message;
        LOG4CXX_ERROR(logger_, result.message);
        return result;
    }

    result.success = true;
    result.message = "Execute completed successfully";
    return result;
}

// ========== Private Methods ==========

int GraphExecutor::CountNodesinLifecycleState(graph::LifecycleState state) const {
    int count = 0;
    auto graph_manager = graph_manager_;
    auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        if (node->GetLifecycleState() == state) {
            ++count;
        }
    }
    return count;
}

// ========== State Query Methods ==========

bool GraphExecutor::IsRunning() const {
    return current_state_ == ExecutionState::RUNNING || 
           current_state_ == ExecutionState::STEPPING;
}

bool GraphExecutor::IsInError() const {
    return current_state_ == ExecutionState::ERROR;
}

}  // namespace graph
