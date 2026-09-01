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
#include "graph/GraphBuilder.hpp"
#include "graph/GraphConfigParser.hpp"
#include "graph/NodeProviderBootstrap.hpp"
#include "capabilities/CommandCapability.hpp"
#include "capabilities/DataInjectionCapability.hpp"
#include "capabilities/MetricsCapability.hpp"

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
    graph_manager_ = graph_capability_->GetGraphManager();
    graph_capability_->GetCapabilityBus().Register<capabilities::GraphCapability>(graph_capability_);
    if (graph_manager_) {
        coordinator_revision_.store(0, std::memory_order_release);
        graph_generation_.store(1, std::memory_order_release);
        if (auto metrics = graph_capability_->GetCapabilityBus()
                               .Get<capabilities::MetricsCapability>()) {
            metrics->ResetGeneration(1U);
        }
    }
}

GraphExecutor::~GraphExecutor() noexcept {
    if (graph_capability_) {
        if (auto commands =
                graph_capability_->GetCapabilityBus()
                    .Get<capabilities::CommandCapability>()) {
            commands->Shutdown(this);
        }
    }
    try {
        const auto state = GetExecutionState();
        if (state == ExecutionState::RUNNING) {
            RequestStop();
        }
        if (graph_manager_ &&
            (state == ExecutionState::INITIALIZED ||
             state == ExecutionState::RUNNING)) {
            (void)StopExpected();
            (void)JoinExpected();
        } else if (graph_manager_ && state == ExecutionState::STOPPING) {
            (void)JoinExpected();
        } else if (graph_manager_ && state == ExecutionState::ERROR) {
            graph_manager_->Stop();
            if (policy_chain_) {
                policy_chain_->OnStop(*graph_capability_);
            }
            graph_manager_->Join();
            if (policy_chain_) {
                policy_chain_->OnJoin(*graph_capability_);
            }
        }
    } catch (const std::exception& e) {
        LOG4CXX_WARN(logger_, "Exception during GraphExecutor cleanup: " << e.what());
    } catch (...) {
        LOG4CXX_WARN(logger_, "Unknown exception during GraphExecutor cleanup");
    }
    if (graph_capability_) {
        graph_capability_->GetCapabilityBus().Clear();
    }
}

void GraphExecutor::SetPluginDirectories(
    std::vector<std::string> directories) {
    std::scoped_lock lock(configuration_mutex_);
    plugin_directories_ = std::move(directories);
}

ExecutionResult GraphExecutor::ConfigureGraph(
    const GraphConfigurationSnapshot& snapshot) {
    ExecutionResult result;
    const auto state = GetExecutionState();
    if (state != ExecutionState::CONFIGURED &&
        !(state == ExecutionState::STOPPED &&
          joined_.load(std::memory_order_acquire))) {
        result.success = false;
        result.current_state = state;
        result.message =
            "GraphExecutor::ConfigureGraph() requires CONFIGURED or joined STOPPED state";
        result.error_details = result.message;
        return result;
    }

    const auto parsed =
        graph::config::GraphConfigParser::ParseSafe(snapshot.Document().dump());
    if (!parsed) {
        result.success = false;
        result.current_state = state;
        result.message = "Graph configuration could not be parsed";
        result.error_details = result.message;
        return result;
    }
    const auto validation =
        graph::config::GraphConfigParser::Validate(parsed.value());
    if (!validation.valid) {
        result.success = false;
        result.current_state = state;
        result.message = "Graph configuration validation failed";
        if (!validation.errors.empty()) {
            result.message += ": " + validation.errors.front();
        }
        result.error_details = result.message;
        return result;
    }

    {
        std::scoped_lock lock(configuration_mutex_);
        if (auto injection =
                graph_capability_->GetCapabilityBus()
                    .Get<capabilities::DataInjectionCapability>()) {
            injection->DisableAllInjectionQueues();
        }
        graph_capability_->GetCapabilityBus()
            .Unregister<capabilities::DataInjectionCapability>();
        configured_snapshot_ = snapshot;
        active_revision_.reset();
        graph_manager_.reset();
        graph_capability_->SetGraphManager(nullptr);
        graph_capability_->SetNodeProvider(nullptr);
        graph_capability_->SetNodeNames({});
        graph_capability_->SetEdgeDescriptions({});
        graph_capability_->SetGraphDocument(snapshot.Document());
        graph_capability_->ResetExecutionSignals();
        coordinator_revision_.store(snapshot.Revision(),
                                    std::memory_order_release);
        graph_generation_.fetch_add(1, std::memory_order_acq_rel);
        joined_.store(false, std::memory_order_release);
        SetExecutionState(ExecutionState::CONFIGURED);
    }
    if (auto metrics =
            GetCapability<capabilities::MetricsCapability>()) {
        metrics->ResetGeneration(GetGraphGeneration());
    }

    result.success = true;
    result.current_state = ExecutionState::CONFIGURED;
    result.message = "Graph configuration recorded";
    return result;
}

std::optional<std::uint64_t>
GraphExecutor::GetConfiguredRevision() const {
    std::scoped_lock lock(configuration_mutex_);
    if (!configured_snapshot_) {
        return std::nullopt;
    }
    return configured_snapshot_->Revision();
}

std::optional<std::uint64_t>
GraphExecutor::GetActiveRevision() const {
    std::scoped_lock lock(configuration_mutex_);
    return active_revision_;
}

bool GraphExecutor::IsConfigurationDirty() const noexcept {
    const auto configured = GetConfiguredRevision();
    return configured &&
           coordinator_revision_.load(std::memory_order_acquire) != *configured;
}

bool GraphExecutor::RollbackInitializationAttempt(
    const bool graph_initialization_started) noexcept {
    try {
        if (graph_initialization_started && graph_manager_) {
            graph_manager_->Stop();
        }
        if (policy_chain_) {
            policy_chain_->OnInitializationRollbackStop(*graph_capability_);
        }
        if (graph_initialization_started && graph_manager_) {
            graph_manager_->Join();
        }
        if (policy_chain_) {
            policy_chain_->OnInitializationRollbackJoin(*graph_capability_);
        }
        graph_manager_.reset();
        graph_capability_->SetGraphManager(nullptr);
        graph_capability_->SetNodeProvider(nullptr);
        graph_capability_->SetNodeNames({});
        graph_capability_->SetEdgeDescriptions({});
        graph_capability_->ResetExecutionSignals();
        SetExecutionState(ExecutionState::CONFIGURED);
        return true;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Initialization rollback failed: " << e.what());
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Initialization rollback failed with an unknown exception");
    }
    SetExecutionState(ExecutionState::ERROR);
    return false;
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
        bool initialization_attempt_started = false;
        bool graph_initialization_started = false;
        try {
        auto start_time = std::chrono::high_resolution_clock::now();

        LOG4CXX_TRACE(logger_, "GraphExecutor::Init() called");
        InitializationResult init_result;
        if (GetExecutionState() != ExecutionState::CONFIGURED) {
            init_result.message =
                "GraphExecutor::Init() requires CONFIGURED state";
            init_result.error_details = init_result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                init_result.message));
        }
        if (IsConfigurationDirty()) {
            init_result.message =
                "GraphExecutor::Init() rejected dirty configuration";
            init_result.error_details = init_result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                init_result.message));
        }
        initialization_attempt_started = true;

        if (!graph_manager_) {
            std::optional<GraphConfigurationSnapshot> snapshot;
            std::vector<std::string> plugin_directories;
            {
                std::scoped_lock lock(configuration_mutex_);
                snapshot = configured_snapshot_;
                plugin_directories = plugin_directories_;
            }
            if (!snapshot) {
                init_result.message =
                    "GraphExecutor::Init() has no configured graph snapshot";
                init_result.error_details = init_result.message;
                return std::unexpected(app::error::MakeGraphExecutionFailure(
                    app::error::GraphExecutionError::ConfigurationInvalid,
                    init_result.message));
            }

            auto provider_bootstrap =
                app::NodeProviderBootstrap::CreateProviderExpected(
                    plugin_directories);
            if (!provider_bootstrap) {
                init_result.message =
                    "Failed to bootstrap node provider and load plugins";
                init_result.error_details = init_result.message;
                return std::unexpected(app::error::MakeGraphExecutionFailure(
                    app::error::GraphExecutionError::BuilderFailed,
                    init_result.message));
            }
            graph_capability_->SetNodeProvider(provider_bootstrap->provider);
            graph_capability_->SetGraphDocument(snapshot->Document());
            auto graph_builder =
                std::make_shared<app::GraphBuilder>(graph_capability_);
            auto build_result = graph_builder->Build();
            if (!build_result.success || !build_result.graph) {
                static_cast<void>(
                    RollbackInitializationAttempt(false));
                init_result.message =
                    "Graph construction failed: " + build_result.error_message;
                init_result.error_details = init_result.message;
                return std::unexpected(app::error::MakeGraphExecutionFailure(
                    app::error::GraphExecutionError::BuilderFailed,
                    init_result.message));
            }
            graph_manager_ = build_result.graph;
            graph_capability_->SetNodeNames(build_result.node_names);
            graph_capability_->SetEdgeDescriptions(
                build_result.edge_descriptions);
            graph_capability_->SetGraphManager(graph_manager_);
        }

        bool policy_result = policy_chain_ ? policy_chain_->OnInit(*graph_capability_) : true;
        if(!policy_result) {
            init_result.success = false;
            init_result.message = "ExecutionPolicyChain::OnInit() failed";
            init_result.error_details = init_result.message;
            if (!RollbackInitializationAttempt(false)) {
                init_result.message =
                    "ExecutionPolicyChain::OnInit() failed and cleanup failed";
                init_result.error_details = init_result.message;
                return std::unexpected(
                    app::error::MakeGraphExecutionFailure(
                        app::error::GraphExecutionError::Unknown,
                        init_result.message));
            }
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::PolicyFailed, init_result.message));
        }
        // Initialize the graph
        graph_initialization_started = true;
        auto graph_init = graph_manager_->InitExpected();
        if (!graph_init) {
            init_result.success = false;
            init_result.message = graph_init.error().message;
            init_result.error_details = init_result.message;
            if (!RollbackInitializationAttempt(true)) {
                init_result.message =
                    graph_init.error().message + "; initialization cleanup failed";
                init_result.error_details = init_result.message;
                return std::unexpected(
                    app::error::MakeGraphExecutionFailure(
                        app::error::GraphExecutionError::Unknown,
                        init_result.message));
            }
            return std::unexpected(graph_init.error());
        }
        init_result.nodes_initialized = CountNodesinLifecycleState(graph::LifecycleState::Initialized);
        init_result.nodes_failed = CountNodesinLifecycleState(graph::LifecycleState::Invalid) +
                                   CountNodesinLifecycleState(graph::LifecycleState::Uninitialized);
        {
            std::scoped_lock lock(configuration_mutex_);
            active_revision_ = configured_snapshot_
                                   ? std::optional<std::uint64_t>{
                                         configured_snapshot_->Revision()}
                                   : std::optional<std::uint64_t>{0};
        }
        SetExecutionState(ExecutionState::INITIALIZED);
        init_result.success = true;
        init_result.message = "GraphExecutor initialized successfully";
        LOG4CXX_TRACE(logger_, "GraphExecutor::Init() completed");

        auto end_time = std::chrono::high_resolution_clock::now();
        init_result.elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return init_result;
        } catch (...) {
            if (initialization_attempt_started &&
                !RollbackInitializationAttempt(
                    graph_initialization_started)) {
                return std::unexpected(
                    app::error::MakeGraphExecutionFailure(
                        app::error::GraphExecutionError::Unknown,
                        "GraphExecutor::Init() threw and initialization cleanup failed"));
            }
            throw;
        }
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
        if (IsConfigurationDirty()) {
            result.message =
                "GraphExecutor::Start() rejected dirty configuration";
            result.error_details = result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                result.message));
        }

        bool policy_result = false;
        try {
            policy_result =
                policy_chain_ ? policy_chain_->OnStart(*graph_capability_)
                              : true;
        } catch (...) {
            SetExecutionState(ExecutionState::ERROR);
            throw;
        }
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
        if (IsConfigurationDirty()) {
            result.message =
                "GraphExecutor::Run() rejected dirty configuration";
            result.error_details = result.message;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                result.message));
        }

        bool policy_result = false;
        try {
            policy_result =
                policy_chain_ ? policy_chain_->OnRun(*graph_capability_)
                              : false;
        } catch (...) {
            SetExecutionState(ExecutionState::ERROR);
            throw;
        }
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
        ExecutionResult result;
        const auto state = GetExecutionState();
        if (state != ExecutionState::INITIALIZED &&
            state != ExecutionState::RUNNING) {
            result.message =
                "GraphExecutor::Stop() requires INITIALIZED or RUNNING state";
            result.error_details = result.message;
            result.current_state = state;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                result.message));
        }
        stop_sequence_count_.fetch_add(1, std::memory_order_acq_rel);
        SetExecutionState(ExecutionState::STOPPING);
        graph_capability_->SetStopped();
        // Stop the graph
        try {
            graph_manager_->Stop();
            if (policy_chain_) {
                policy_chain_->OnStop(*graph_capability_);
            }
        } catch (...) {
            SetExecutionState(ExecutionState::ERROR);
            throw;
        }
        LOG4CXX_TRACE(logger_, "GraphExecutor::Stop() completed");
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
        const auto state = GetExecutionState();
        if (state == ExecutionState::STOPPED &&
            joined_.load(std::memory_order_acquire)) {
            result.success = true;
            result.message = "GraphExecutor already joined";
            result.current_state = state;
            return result;
        }
        if (state != ExecutionState::STOPPING) {
            result.message =
                "GraphExecutor::Join() requires STOPPING state";
            result.error_details = result.message;
            result.current_state = state;
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                result.message));
        }

        // Join all threads
        try {
            graph_manager_->Join();
            if (policy_chain_) {
                policy_chain_->OnJoin(*graph_capability_);
            }
        } catch (...) {
            SetExecutionState(ExecutionState::ERROR);
            throw;
        }
        joined_.store(true, std::memory_order_release);
        SetExecutionState(ExecutionState::STOPPED);
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

        if (GetExecutionState() == ExecutionState::CONFIGURED) {
            auto init_result = InitExpected();
            if (!init_result) {
                LOG4CXX_ERROR(logger_, "Execute failed during Init(): "
                                         << init_result.error().message);
                return std::unexpected(init_result.error());
            }
            result.init_elapsed_time_ms = init_result->elapsed_time_ms;
        } else if (GetExecutionState() != ExecutionState::INITIALIZED) {
            return std::unexpected(app::error::MakeGraphExecutionFailure(
                app::error::GraphExecutionError::InvalidState,
                "GraphExecutor::Execute() requires CONFIGURED or INITIALIZED state"));
        }

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
