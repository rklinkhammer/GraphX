/**
 * @file GraphExecutor.cpp
 * @brief Graph Executor Graph runtime support.
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


#include "graph/GraphManagerCore.hpp"
#include "graph/GraphExecutor.hpp"

#include <chrono>
#include <exception>
#include <expected>
#include <string_view>
#include <thread>
#include <utility>
#include <log4cxx/logger.h>

namespace graph {

// ============================================================================
// Static Logger for GraphExecutor
// ============================================================================

static log4cxx::LoggerPtr logger_ = 
    log4cxx::Logger::getLogger("graph.GraphExecutor");

namespace {

template <typename ResultT, typename FuncT>
std::expected<ResultT, app::error::GraphExecutionFailure>
CaptureLifecycleFailure(std::string_view operation, FuncT&& func) noexcept {
    try {
        return std::forward<FuncT>(func)();
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "GraphExecutor::" << operation
                                                  << "() threw: " << e.what());
    } catch (...) {
        LOG4CXX_ERROR(logger_, "GraphExecutor::" << operation
                                                  << "() threw an unknown exception");
    }
    return std::unexpected(app::error::MakeGraphExecutionFailure(
        app::error::GraphExecutionError::Unknown,
        std::string("GraphExecutor::") + std::string(operation) + "() threw an exception"));
}

}  // namespace


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

GraphExecutor::~GraphExecutor() noexcept {
    try {
        if (graph_manager_) {
            (void)StopExpected();
            (void)JoinExpected();
        }
    } catch (const std::exception& e) {
        LOG4CXX_WARN(logger_, "Exception during GraphExecutor cleanup: " << e.what());
    } catch (...) {
        LOG4CXX_WARN(logger_, "Unknown exception during GraphExecutor cleanup");
    }
}

/**
 * @brief Init.
 */
InitializationResult GraphExecutor::Init() {
    auto result = InitExpected();
    if (result) {
        return *result;
    }

    InitializationResult init_result;
    init_result.success = false;
    init_result.message = result.error().message;
    init_result.error_details = init_result.message;
    return init_result;
}

std::expected<InitializationResult, app::error::GraphExecutionFailure>
GraphExecutor::InitExpected() noexcept {
    return CaptureLifecycleFailure<InitializationResult>("Init", [&]()
        -> std::expected<InitializationResult, app::error::GraphExecutionFailure> {
        auto start_time = std::chrono::high_resolution_clock::now();

        LOG4CXX_TRACE(logger_, "GraphExecutor::Init() called");
        InitializationResult init_result;
        bool policy_result = policy_chain_ ? policy_chain_->OnInit(*graph_capability_) : true;
        if(!policy_result) {
            init_result.success = false;
            init_result.message = "ExecutionPolicyChain::OnInit() failed";
            init_result.error_details = init_result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::PolicyFailed, init_result.message));
        }
        // Initialize the graph
        auto graph_init = graph_manager_->InitExpected();
        if (!graph_init) {
            init_result.success = false;
            init_result.message = graph_init.error().message;
            init_result.error_details = init_result.message;
            return std::unexpected(graph_init.error());
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
    });
}

/**
 * @brief Start.
 */
ExecutionResult GraphExecutor::Start() {
    auto result = StartExpected();
    if (result) {
        return *result;
    }

    ExecutionResult exec_result;
    exec_result.success = false;
    exec_result.current_state = GetExecutionState();
    exec_result.message = result.error().message;
    exec_result.error_details = exec_result.message;
    return exec_result;
}

std::expected<ExecutionResult, app::error::GraphExecutionFailure>
GraphExecutor::StartExpected() noexcept {
    return CaptureLifecycleFailure<ExecutionResult>("Start", [&]()
        -> std::expected<ExecutionResult, app::error::GraphExecutionFailure> {
        auto start_time = std::chrono::high_resolution_clock::now();

        LOG4CXX_TRACE(logger_, "GraphExecutor::Start() called");
        ExecutionResult result;
        if (GetExecutionState() != ExecutionState::INITIALIZED) {
            result.success = false;
            result.message = "GraphExecutor::Start() requires INITIALIZED state";
            LOG4CXX_WARN(logger_, result.message);
            result.error_details = result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState, result.message));
        }

        bool policy_result = policy_chain_ ? policy_chain_->OnStart(*graph_capability_) : true;
        if(!policy_result) {
            result.success = false;
            result.message = "ExecutionPolicyChain::OnStart() failed";
            result.error_details = result.message;
            SetExecutionState(ExecutionState::ERROR);
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::PolicyFailed, result.message));
        }
        // Start the graph
        auto graph_start = graph_manager_->StartExpected();
        if (!graph_start) {
            result.success = false;
            result.message = graph_start.error().message;
            result.error_details = result.message;
            SetExecutionState(ExecutionState::ERROR);
            return std::unexpected(graph_start.error());
        }
        SetExecutionState(ExecutionState::RUNNING);
        // Publish the exact caller-prepared attempt only after RUNNING and all
        // graph startup side effects are visible. A later attempt cannot
        // mistake this publication for its own milestone.
        startup_complete_attempt_.store(
            execution_attempt_.load(std::memory_order_acquire),
            std::memory_order_release);
        LOG4CXX_TRACE(logger_, "GraphExecutor::Start() completed");

        result.success = true;
        result.message = "GraphExecutor started successfully";
        result.current_state = GetExecutionState();
        auto end_time = std::chrono::high_resolution_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return result;
    });
}

/**
 * @brief Run.
 */
ExecutionResult GraphExecutor::Run() {
    auto result = RunExpected();
    if (result) {
        return *result;
    }

    ExecutionResult exec_result;
    exec_result.success = false;
    exec_result.current_state = GetExecutionState();
    exec_result.message = result.error().message;
    exec_result.error_details = exec_result.message;
    return exec_result;
}

std::expected<ExecutionResult, app::error::GraphExecutionFailure>
GraphExecutor::RunExpected() noexcept {
    return CaptureLifecycleFailure<ExecutionResult>("Run", [&]()
        -> std::expected<ExecutionResult, app::error::GraphExecutionFailure> {
        auto start_time = std::chrono::high_resolution_clock::now();

        LOG4CXX_TRACE(logger_, "GraphExecutor::Run() called");
        ExecutionResult result;   
        if (GetExecutionState() != ExecutionState::RUNNING) {
            result.success = false;
            result.message = "GraphExecutor::Run() requires RUNNING state";
            LOG4CXX_WARN(logger_, result.message);
            result.error_details = result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState, result.message));
        }

        bool policy_result =  policy_chain_ ? policy_chain_->OnRun(*graph_capability_) : false;
        if(!policy_result) {
            result.success = false;
            result.message = "ExecutionPolicyChain::OnRun() failed";
            result.error_details = result.message;
            SetExecutionState(ExecutionState::ERROR);
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::PolicyFailed, result.message));
        }

        while(!graph_capability_->IsStopped()) {
            // Keep completion-to-shutdown latency low so graphs stop promptly once
            // completion is signaled by policy callbacks.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        LOG4CXX_TRACE(logger_, "GraphExecutor::Run() completed, success=" << result.success);
        SetExecutionState(ExecutionState::STOPPED);
        result.success = true;
        result.message = "GraphExecutor run completed successfully";    
        result.current_state = GetExecutionState();
        auto end_time = std::chrono::high_resolution_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return result;
    });
}

/**
 * @brief Stop.
 */
ExecutionResult GraphExecutor::Stop() {
    auto result = StopExpected();
    if (result) {
        return *result;
    }

    ExecutionResult exec_result;
    exec_result.success = false;
    exec_result.current_state = GetExecutionState();
    exec_result.message = result.error().message;
    exec_result.error_details = exec_result.message;
    return exec_result;
}

void GraphExecutor::RequestStop() noexcept {
    if (graph_capability_) {
        graph_capability_->SetStopped();
    }
}

std::expected<ExecutionResult, app::error::GraphExecutionFailure>
GraphExecutor::StopExpected() noexcept {
    return CaptureLifecycleFailure<ExecutionResult>("Stop", [&]()
        -> std::expected<ExecutionResult, app::error::GraphExecutionFailure> {
        auto start_time = std::chrono::high_resolution_clock::now();

        LOG4CXX_TRACE(logger_, "GraphExecutor::Stop() called");
        stop_sequence_count_.fetch_add(1, std::memory_order_acq_rel);
        ExecutionResult result;
        SetExecutionState(ExecutionState::STOPPING);
        graph_capability_->SetStopped();
        // Stop the graph
        graph_manager_->Stop();
        if (policy_chain_) {
            policy_chain_->OnStop(*graph_capability_);
        }
        LOG4CXX_TRACE(logger_, "GraphExecutor::Stop() completed");
        SetExecutionState(ExecutionState::STOPPED);

        result.success = true;
        result.message = "GraphExecutor stopped successfully";
        result.current_state = GetExecutionState();
        auto end_time = std::chrono::high_resolution_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return result;
    });
}

/**
 * @brief Join.
 */
ExecutionResult GraphExecutor::Join() {
    auto result = JoinExpected();
    if (result) {
        return *result;
    }

    ExecutionResult exec_result;
    exec_result.success = false;
    exec_result.current_state = GetExecutionState();
    exec_result.message = result.error().message;
    exec_result.error_details = exec_result.message;
    return exec_result;
}

std::expected<ExecutionResult, app::error::GraphExecutionFailure>
GraphExecutor::JoinExpected() noexcept {
    return CaptureLifecycleFailure<ExecutionResult>("Join", [&]()
        -> std::expected<ExecutionResult, app::error::GraphExecutionFailure> {
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
        result.current_state = GetExecutionState();
        auto end_time = std::chrono::high_resolution_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return result;
    });
}

/**
 * @brief Display results.
 * @param results Parameter for display results.
 */
void GraphExecutor::DisplayResults(const ExecutionResult& results) const { 
    (void)results;
}

/**
 * @brief Execute.
 */
ExecutionResult GraphExecutor::Execute() {
    auto result = ExecuteExpected();
    if (result) {
        return *result;
    }

    ExecutionResult exec_result;
    exec_result.success = false;
    exec_result.current_state = GetExecutionState();
    exec_result.message = result.error().message;
    exec_result.error_details = exec_result.message;
    return exec_result;
}

std::expected<ExecutionResult, app::error::GraphExecutionFailure>
GraphExecutor::ExecuteExpected() noexcept {
    return CaptureLifecycleFailure<ExecutionResult>("Execute", [&]()
        -> std::expected<ExecutionResult, app::error::GraphExecutionFailure> {
        auto execute_start_time = std::chrono::high_resolution_clock::now();
        ExecutionResult result;

        auto init_result = InitExpected();
        if (!init_result) {
            LOG4CXX_ERROR(logger_, "Execute failed during Init(): "
                                     << init_result.error().message);
            return std::unexpected(init_result.error());
        }
        result.init_elapsed_time_ms = init_result->elapsed_time_ms;

        auto start_result = StartExpected();
        if (!start_result) {
            LOG4CXX_ERROR(logger_, "Execute failed during Start(): "
                                     << start_result.error().message);
            return std::unexpected(start_result.error());
        }
        result.start_elapsed_time_ms = start_result->elapsed_time_ms;

        auto run_result = RunExpected();
        if (!run_result) {
            LOG4CXX_ERROR(logger_, "Execute failed during Run(): "
                                     << run_result.error().message);
            return std::unexpected(run_result.error());
        }
        result.run_elapsed_time_ms = run_result->elapsed_time_ms;

        auto stop_result = StopExpected();
        if (!stop_result) {
            LOG4CXX_ERROR(logger_, "Execute failed during Stop(): "
                                     << stop_result.error().message);
            return std::unexpected(stop_result.error());
        }
        result.stop_elapsed_time_ms = stop_result->elapsed_time_ms;

        auto join_result = JoinExpected();
        if (!join_result) {
            LOG4CXX_ERROR(logger_, "Execute failed during Join(): "
                                     << join_result.error().message);
            return std::unexpected(join_result.error());
        }
        result.join_elapsed_time_ms = join_result->elapsed_time_ms;

        result.success = true;
        result.message = "Execute completed successfully";
        result.current_state = GetExecutionState();
        auto execute_end_time = std::chrono::high_resolution_clock::now();
        result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            execute_end_time - execute_start_time).count();
        return result;
    });
}

// ========== Private Methods ==========

/**
 * @brief Count nodesin lifecycle state.
 * @param state Parameter for count nodesin lifecycle state.
 */
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

/**
 * @brief Is running.
 */
bool GraphExecutor::IsRunning() const {
    const auto state = GetExecutionState();
    return state == ExecutionState::RUNNING ||
           state == ExecutionState::STEPPING;
}

/**
 * @brief Is in error.
 */
bool GraphExecutor::IsInError() const {
    return GetExecutionState() == ExecutionState::ERROR;
}

}  // namespace graph
