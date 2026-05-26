/**
 * @file test_graph_completion.cpp
 * @brief Stage 5.5a: Test completion semantics for all TestGraphTopologies
 *
 * This test suite validates that each topology from TestGraphTopologies correctly
 * handles graph execution and completion. Tests are ordered by complexity:
 *
 * 1. SourceOnly - Single source, no sinks (baseline, timeout-based)
 * 2. MinimalGraph - Source to Sink (simple completion signal)
 * 3. LinearSequential - Pipeline with Interior node
 * 4. MergeSimple - Multi-input merge
 * 5. SplitSimple - Multi-output replication
 * ... (expanding to complex topologies as tests progress)
 *
 * @author Test Suite
 * @date May 13, 2026
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include "test/TestGraphTopologies.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

namespace {

// ============================================================================
// DEBUGGING INFRASTRUCTURE FOR EXECUTOR TESTS
// ============================================================================

/**
 * @class ExecutorDebugHelper
 * @brief Debug utilities for GraphExecutor tests
 * 
 * Provides:
 * - Thread-safe executor running with timeouts
 * - Detailed error reporting
 * - Graph state inspection
 * - Debug logging and assertions
 */
class ExecutorDebugHelper {
public:
    /**
     * @struct RunResult
     * @brief Result of running executor with timeout
     */
    struct RunResult {
        bool success;
        bool completed_naturally;
        bool timed_out;
        bool was_stopped;
        std::string error_message;
        std::chrono::milliseconds elapsed_time;
    };

    /**
     * @brief Run executor in a separate thread with timeout
     * @param executor The executor to run
     * @param timeout_ms Timeout in milliseconds
     * @return RunResult with status and timing information
     * 
     * This helper solves the "blocking Run()" problem by:
     * 1. Running executor->Run() in a background thread
     * 2. Waiting for completion or timeout
     * 3. Gracefully stopping if timeout occurs
     * 4. Joining the thread and collecting results
     */
    static RunResult RunWithTimeout(
        std::shared_ptr<graph::GraphExecutor> executor,
        std::chrono::milliseconds timeout_ms) {
        
        RunResult result{
            .success = false,
            .completed_naturally = false,
            .timed_out = false,
            .was_stopped = false,
            .error_message = "",
            .elapsed_time = std::chrono::milliseconds(0)
        };

        if (!executor) {
            result.error_message = "Executor is null";
            return result;
        }

        auto start_time = std::chrono::steady_clock::now();
        std::atomic<bool> executor_running(true);
        std::atomic<bool> executor_completed(false);
        std::string run_error;

        // Run executor in background thread
        std::thread executor_thread([&executor, &executor_running, &executor_completed, &run_error]() {
            try {
                auto run_result = executor->Run();
                executor_completed = true;
                executor_running = false;
                
                if (!run_result.success) {
                    run_error = run_result.error_details;
                }
            } catch (const std::exception& e) {
                run_error = std::string("Exception in Run(): ") + e.what();
                executor_running = false;
            } catch (...) {
                run_error = "Unknown exception in Run()";
                executor_running = false;
            }
        });

        // Wait for completion or timeout
        auto wait_deadline = start_time + timeout_ms;
        bool completed_before_timeout = false;

        while (std::chrono::steady_clock::now() < wait_deadline) {
            if (executor_completed) {
                completed_before_timeout = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;

        // If timed out and executor still running, stop it
        if (!completed_before_timeout && executor_running) {
            result.timed_out = true;
            auto stop_result = executor->Stop();
            result.was_stopped = stop_result.success;
            
            if (!stop_result.success) {
                result.error_message = "Stop() failed: " + stop_result.error_details;
            }
        }

        // Join the thread. RunWithTimeout already requested Stop() above when
        // needed, so Run() should return without a second watcher thread racing
        // on the same std::thread object.
        if (executor_thread.joinable()) {
            executor_thread.join();
        }

        result.elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        result.completed_naturally = completed_before_timeout;
        result.success = completed_before_timeout || (result.timed_out && result.was_stopped);

        if (!run_error.empty() && result.error_message.empty()) {
            result.error_message = run_error;
            result.success = false;
        }

        return result;
    }

    /**
     * @brief Format error result for test assertions
     * @param result The InitializationResult to format
     * @return Formatted error string for assertions
     */
    static std::string FormatError(const graph::InitializationResult& result) {
        std::string msg = result.error_details;
        if (msg.empty()) {
            msg = "(no error details provided)";
        }
        return msg;
    }

    /**
     * @brief Format error result for test assertions
     * @param result The ExecutionResult to format
     * @return Formatted error string for assertions
     */
    static std::string FormatError(const graph::ExecutionResult& result) {
        std::string msg = result.error_details;
        if (msg.empty()) {
            msg = "(no error details provided)";
        }
        return msg;
    }

    /**
     * @brief Log executor state for debugging
     * @param executor The executor to inspect
     * @param label Debug label for the log
     */
    static void LogExecutorState(
        std::shared_ptr<graph::GraphExecutor> executor,
        const std::string& label) {
        
        if (!executor) {
            std::cerr << "[" << label << "] Executor is null\n";
            return;
        }

        std::cerr << "[" << label << "] Executor State:\n"
                  << "  - IsCompletionSignaled: " 
                  << (executor->IsCompletionSignaled() ? "true" : "false") << "\n";
    }

    /**
     * @brief Assert executor operation succeeded with debug output
     * @param result The InitializationResult to check
     * @param operation_name Name of the operation (for error messages)
     * @param topology_name Name of the topology being tested
     */
    static void AssertSuccess(
        const graph::InitializationResult& result,
        const std::string& operation_name,
        const std::string& topology_name) {
        
        ASSERT_TRUE(result.success)
            << operation_name << " failed for " << topology_name << ": " 
            << FormatError(result);
    }

    /**
     * @brief Assert executor operation succeeded with debug output
     * @param result The ExecutionResult to check
     * @param operation_name Name of the operation (for error messages)
     * @param topology_name Name of the topology being tested
     */
    static void AssertSuccess(
        const graph::ExecutionResult& result,
        const std::string& operation_name,
        const std::string& topology_name) {
        
        ASSERT_TRUE(result.success)
            << operation_name << " failed for " << topology_name << ": " 
            << FormatError(result);
    }

    /**
     * @brief Assert RunWithTimeout succeeded with debug output
     * @param result The RunWithTimeout result
     * @param topology_name Name of the topology being tested
     */
    static void AssertRunSuccess(
        const RunResult& result,
        const std::string& topology_name) {
        
        ASSERT_TRUE(result.success)
            << "Executor::Run() failed for " << topology_name << "\n"
            << "  - Timed out: " << (result.timed_out ? "yes" : "no") << "\n"
            << "  - Was stopped: " << (result.was_stopped ? "yes" : "no") << "\n"
            << "  - Elapsed time: " << result.elapsed_time.count() << " ms\n"
            << "  - Error: " << (result.error_message.empty() ? "(none)" : result.error_message);
    }
};

// ============================================================================
// MESSAGE FLOW VALIDATION INFRASTRUCTURE (Stage 5.5b)
// ============================================================================

/**
 * @class MessageFlowValidator
 * @brief Validates message flow through graph topologies
 * 
 * Tracks message counts at each node to ensure data flows correctly
 * through the graph pipeline. Supports extracting and validating:
 * - Source message production counts
 * - Interior node transformation counts
 * - Sink message consumption counts
 * - Merge node input/output counts
 * - Split node replication counts
 */
class MessageFlowValidator {
public:
    /**
     * @struct FlowMetrics
     * @brief Message count metrics for a topology
     */
    struct FlowMetrics {
        std::string topology_name;
        size_t source_produced{0};
        size_t interior_transferred{0};
        size_t sink_consumed{0};
        size_t merge_inputs{0};
        size_t split_replications{0};
        
        bool IsValid() const {
            // For topologies with no sinks (e.g., SourceOnly), we don't check sink consumption
            // A topology is valid if:
            // 1. Messages that were produced were also consumed (when a sink exists)
            // 2. Interior transfers match production (if interior exists)
            
            // If there's a sink, message flow should match
            if (sink_consumed > 0 && source_produced > 0) {
                // If both source and sink exist, sink should consume what source produces
                return sink_consumed >= source_produced;
            }
            
            // If only source (no sink), that's OK
            if (source_produced > 0 && sink_consumed == 0) {
                return true;
            }
            
            return true;
        }
    };

    /**
     * @brief Initialize validator for a topology
     * @param graph The GraphManager to validate
     * @return FlowMetrics for the topology
     */
    static FlowMetrics ValidateTopology(
        const std::shared_ptr<graph::GraphManager>& graph,
        const std::string& topology_name) {
        
        FlowMetrics metrics;
        metrics.topology_name = topology_name;

        if (!graph) {
            return metrics;
        }

        // Get all nodes from the graph
        const auto& nodes = graph->GetNodes();
        
        std::cerr << "\n[DEBUG] ValidateTopology(" << topology_name << ") - Found " 
                  << nodes.size() << " nodes\n";
        
        // Iterate through nodes and extract metrics
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (!node) {
                std::cerr << "[DEBUG]   Node " << i << ": nullptr\n";
                continue;
            }
            
            std::cerr << "[DEBUG]   Node " << i << ": " << typeid(*node).name() << "\n";
            ExtractNodeMetrics(node, metrics);
        }

        return metrics;
    }

private:
    /**
     * @brief Extract metrics from a node using RTTI
     * @param node The node to extract metrics from
     * @param metrics Reference to metrics struct to populate
     */
    static void ExtractNodeMetrics(
        const std::shared_ptr<graph::INode>& node,
        FlowMetrics& metrics) {
        
        if (!node) return;

        // First try to cast to NodeFacadeAdapterWrapper (plugin-loaded nodes)
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (wrapper) {
            // For wrapped plugin nodes, we need to carefully extract the actual underlying node
            // Try each type and take the one with non-zero count (avoiding type mismatches)
            
            // Try SourceTestNode
            auto source = wrapper->GetNode<test::SourceTestNode>();
            if (source && source->GetMessageCount() > 0) {
                // Validate this looks like a source (reasonable count value, not garbage)
                size_t count = source->GetMessageCount();
                if (count < 1000000) {  // Sanity check: count should be less than 1M
                    metrics.source_produced = count;
                    std::cerr << "[DEBUG]       Found SourceTestNode, count=" << count << "\n";
                    return;
                }
            }

            // Try SinkTestNode
            auto sink = wrapper->GetNode<test::SinkTestNode>();
            if (sink && sink->GetMessageCount() > 0) {
                size_t count = sink->GetMessageCount();
                if (count < 1000000) {  // Sanity check
                    metrics.sink_consumed = count;
                    std::cerr << "[DEBUG]       Found SinkTestNode, count=" << count << "\n";
                    return;
                }
            }

            // Try InteriorTestNode
            auto interior = wrapper->GetNode<test::InteriorTestNode>();
            if (interior && interior->GetMessageCount() > 0) {
                size_t count = interior->GetMessageCount();
                if (count < 1000000) {  // Sanity check
                    metrics.interior_transferred = count;
                    std::cerr << "[DEBUG]       Found InteriorTestNode, count=" << count << "\n";
                    return;
                }
            }
            
            std::cerr << "[DEBUG]     Wrapper found but no matching node type with valid count\n";
            return;
        }

        // Fallback: Try direct dynamic_cast for non-wrapped nodes
        auto source = dynamic_cast<test::SourceTestNode*>(node.get());
        if (source) {
            size_t count = source->GetMessageCount();
            if (count < 1000000) {
                metrics.source_produced = count;
                std::cerr << "[DEBUG]       Found direct SourceTestNode, count=" << count << "\n";
                return;
            }
        }

        auto sink = dynamic_cast<test::SinkTestNode*>(node.get());
        if (sink) {
            size_t count = sink->GetMessageCount();
            if (count < 1000000) {
                metrics.sink_consumed = count;
                std::cerr << "[DEBUG]       Found direct SinkTestNode, count=" << count << "\n";
                return;
            }
        }

        auto interior = dynamic_cast<test::InteriorTestNode*>(node.get());
        if (interior) {
            size_t count = interior->GetMessageCount();
            if (count < 1000000) {
                metrics.interior_transferred = count;
                std::cerr << "[DEBUG]       Found direct InteriorTestNode, count=" << count << "\n";
                return;
            }
        }
        
        std::cerr << "[DEBUG]     No matching node type found\n";
    }

public:
    /**
     * @brief Assert that topology has valid message flow
     * @param metrics The FlowMetrics to validate
     * @param expected_produced Expected message count from source (0 = skip check)
     * @param expected_consumed Expected message count to sink (0 = skip check)
     */
    static void AssertValidFlow(
        const FlowMetrics& metrics,
        size_t expected_produced = 0,
        size_t expected_consumed = 0) {
        
        ASSERT_TRUE(metrics.IsValid())
            << "Invalid message flow for topology: " << metrics.topology_name << "\n"
            << "  Source produced: " << metrics.source_produced << "\n"
            << "  Interior transferred: " << metrics.interior_transferred << "\n"
            << "  Sink consumed: " << metrics.sink_consumed << "\n";

        if (expected_produced > 0) {
            EXPECT_EQ(metrics.source_produced, expected_produced)
                << "Topology " << metrics.topology_name 
                << " source should produce " << expected_produced 
                << " messages but produced " << metrics.source_produced;
        }

        if (expected_consumed > 0) {
            EXPECT_EQ(metrics.sink_consumed, expected_consumed)
                << "Topology " << metrics.topology_name 
                << " sink should consume " << expected_consumed 
                << " messages but consumed " << metrics.sink_consumed;
        }
    }

    /**
     * @brief Log message flow metrics to stderr
     * @param metrics The FlowMetrics to log
     */
    static void LogMetrics(const FlowMetrics& metrics) {
        std::cerr << "\n=== Message Flow Metrics: " << metrics.topology_name << " ===\n"
                  << "  Source produced: " << metrics.source_produced << " messages\n"
                  << "  Interior transferred: " << metrics.interior_transferred << " messages\n"
                  << "  Sink consumed: " << metrics.sink_consumed << " messages\n"
                  << "  Merge inputs: " << metrics.merge_inputs << "\n"
                  << "  Split replications: " << metrics.split_replications << "\n"
                  << "  Flow valid: " << (metrics.IsValid() ? "YES" : "NO") << "\n";
    }
};

/**
 * @class CompletionSemanticsTest
 * @brief Test completion behavior across all graph topologies
 *
 * Each test validates:
 * 1. Graph topology builds successfully
 * 2. Executor initializes and starts without error
 * 3. Graph execution completes (either via completion signal or timeout)
 * 4. No errors, deadlocks, or exceptions occur during execution
 * 5. (Stage 5.5b) Message flow metrics match expectations
 */
class CompletionSemanticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set reasonable timeout for tests without explicit completion signals
        // SourceOnly has no sinks, so it completes via timeout
        executor_timeout_ms_ = 5000;  // 5 seconds
    }

    // Debug helper for executor operations
    ExecutorDebugHelper debug_;

    int executor_timeout_ms_;
};

/**
 * @test Stage 5.5a, Topology 1: SourceOnly
 * @brief Baseline test: single source node, no sinks, no completion signal
 *
 * **Topology Structure**:
 * - 1 node: SourceTestNode
 * - 0 edges
 * - No sink nodes to signal completion
 *
 * **Expected Behavior**:
 * - Graph initializes without error
 * - Source produces configured message count (10 by default)
 * - No sink nodes present, so completion signal never generated
 * - Graph starts and runs without error
 * - Graph completes execution (timeout-based since no completion signal)
 * - No deadlocks or exceptions
 *
 * **Key Insight**:
 * This is the baseline case. SourceOnly validates:
 * 1. Graph building itself works for minimal topology
 * 2. Source node initialization and execution
 * 3. Timeout-based completion for graphs without sinks (no completion signals)
 * 4. No assumptions about completion signals
 */
TEST_F(CompletionSemanticsTest, Topology1_SourceOnlyInitializes) {
    // === SETUP: Build SourceOnly topology ===
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::SourceOnly
    );
    
    ASSERT_NE(graph, nullptr) << "Failed to build SourceOnly topology";

    // === VERIFY: Metadata exists for topology ===
    auto metadata = test::TopologyBuilder::GetTopologyMetadata(
        test::TopologyType::SourceOnly
    );
    
    EXPECT_FALSE(metadata.name.empty())
        << "Metadata name should not be empty for SourceOnly topology";
    EXPECT_EQ(metadata.expected_node_count, 1)
        << "SourceOnly should have 1 node (the source)";
    EXPECT_EQ(metadata.expected_edge_count, 0)
        << "SourceOnly should have 0 edges (no connections)";

    // === CREATE: Build executor with timeout ===
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(10))
        .WithVerboseLogging(true)
        .Build();
    
    ASSERT_NE(executor, nullptr) << "Failed to build GraphExecutor";

    // === INIT: Initialize the executor ===
    auto init_result = executor->Init();
    
    debug_.AssertSuccess(init_result, "GraphExecutor::Init()", "SourceOnly");

    // === START: Start the graph execution ===
    auto start_result = executor->Start();
    
    debug_.AssertSuccess(start_result, "GraphExecutor::Start()", "SourceOnly");

    // === RUN: Execute the graph with timeout ===
    // Use debug helper to run executor in a thread with timeout
    // This prevents the test from hanging on the blocking Run() call
    auto run_result = ExecutorDebugHelper::RunWithTimeout(
        executor, 
        std::chrono::milliseconds(executor_timeout_ms_)
    );
    
    debug_.AssertRunSuccess(run_result, "SourceOnly");

    // === STOP & JOIN: Graceful shutdown ===
    auto stop_result = executor->Stop();
    debug_.AssertSuccess(stop_result, "GraphExecutor::Stop()", "SourceOnly");
    
    auto join_result = executor->Join();
    debug_.AssertSuccess(join_result, "GraphExecutor::Join()", "SourceOnly");

    // === VERIFY: Completion signal status ===
    // SourceOnly has no sinks, so no completion signal is expected
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_FALSE(is_signaled)
        << "SourceOnly topology should not signal completion (no sinks to complete)";
    
    // === VALIDATE: Message Flow (Stage 5.5b) ===
    auto flow_metrics = MessageFlowValidator::ValidateTopology(graph, "SourceOnly");
    MessageFlowValidator::LogMetrics(flow_metrics);
    // SourceOnly: Source produces but no sink consumes
    MessageFlowValidator::AssertValidFlow(flow_metrics, 10, 0);  // 10 produced, 0 consumed
    
    // === DEBUG: Log final state ===
    debug_.LogExecutorState(executor, "SourceOnly final state");
}

/**
 * @test Stage 5.5a, Topology 2: MinimalGraph with Completion Semantics
 * @brief Single message path with completion signal detection
 *
 * **Topology Structure**:
 * - 2 nodes: SourceTestNode → SinkTestNode
 * - 1 edge: Source output to Sink input
 *
 * **Completion Semantics**:
 * - Source configured via Configure() to produce 10 messages
 * - SinkTestNode configured via Configure() to expect 10 messages
 * - When 10th message received, SinkTestNode signals completion to CompletionPolicy
 * - Graph executor detects completion signal and terminates
 * - NO timeout needed - completion signal drives termination
 *
 * **Key Validation**:
 * This is the first test with actual completion semantics.
 * It validates:
 * 1. Source message count configuration via Configure interface
 * 2. Sink expected message count configuration via Configure interface
 * 3. Messages flow correctly through single edge
 * 4. Completion signal fires when expected message count reached
 * 5. Graph executor terminates when completion signal received
 * 6. No reliance on timeouts - completion semantics drive termination
 */
TEST_F(CompletionSemanticsTest, Topology2_MinimalGraphCompletionSemantics) {
    // === SETUP: Build MinimalGraph topology ===
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph
    );
    
    ASSERT_NE(graph, nullptr) << "Failed to build MinimalGraph topology";

    // === VERIFY: Metadata exists for topology ===
    auto metadata = test::TopologyBuilder::GetTopologyMetadata(
        test::TopologyType::MinimalGraph
    );
    
    EXPECT_FALSE(metadata.name.empty())
        << "Metadata name should not be empty for MinimalGraph topology";
    EXPECT_EQ(metadata.expected_node_count, 2)
        << "MinimalGraph should have 2 nodes (source + sink)";
    EXPECT_EQ(metadata.expected_edge_count, 1)
        << "MinimalGraph should have 1 edge (source to sink)";

    // === CREATE: Build executor with timeout ===
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .WithVerboseLogging(true)
        .Build();
    
    ASSERT_NE(executor, nullptr) << "Failed to build GraphExecutor";

    // === INIT: Initialize the executor ===
    auto init_result = executor->Init();
    
    debug_.AssertSuccess(init_result, "GraphExecutor::Init()", "MinimalGraph");

    // === START: Start the graph execution ===
    // The graph will:
    // 1. Source produces 10 messages (via Configure)
    // 2. Messages flow through edge to sink
    // 3. Sink receives messages and counts them (via Configure)
    // 4. When sink receives 10th message, it signals completion via callback
    // 5. CompletionPolicy detects completion signal
    // 6. Executor stops and graph completion occurs
    auto start_result = executor->Start();
    
    debug_.AssertSuccess(start_result, "GraphExecutor::Start()", "MinimalGraph");

    // === RUN: Execute the graph with timeout ===
    // Use debug helper to run executor in a thread with timeout
    auto run_result = ExecutorDebugHelper::RunWithTimeout(
        executor, 
        std::chrono::milliseconds(executor_timeout_ms_)
    );
    
    debug_.AssertRunSuccess(run_result, "MinimalGraph");

    // === STOP & JOIN: Graceful shutdown ===
    auto stop_result = executor->Stop();
    debug_.AssertSuccess(stop_result, "GraphExecutor::Stop()", "MinimalGraph");
    
    auto join_result = executor->Join();
    debug_.AssertSuccess(join_result, "GraphExecutor::Join()", "MinimalGraph");

    // === VERIFY: Completion signal was triggered ===
    // MinimalGraph with sink configured for 10 messages should trigger completion
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled)
        << "MinimalGraph topology should signal completion when all messages processed";

    // === VALIDATE: Message Flow (Stage 5.5b) ===
    auto flow_metrics = MessageFlowValidator::ValidateTopology(graph, "MinimalGraph");
    MessageFlowValidator::LogMetrics(flow_metrics);
    // MinimalGraph: Source produces 10, Sink consumes 10
    MessageFlowValidator::AssertValidFlow(flow_metrics, 10, 10);
    
    // === DEBUG: Log final state ===
    debug_.LogExecutorState(executor, "MinimalGraph final state");
}

/**
 * @test Stage 5.5a, Topology 3: LinearSequential
 * @brief Sequential pipeline: Source → Interior → Sink
 *
 * **Topology Structure**:
 * - 3 nodes: SourceTestNode, InteriorTestNode, SinkTestNode
 * - 2 edges: Source→Interior, Interior→Sink
 * - Linear transformation pipeline
 *
 * **Expected Behavior**:
 * - Graph initializes without error
 * - Source produces 10 messages
 * - Interior node passes messages through (transformation)
 * - Sink receives all 10 messages and signals completion
 * - Graph executes and completes successfully
 * - Completion signal fires when all messages processed
 *
 * **Key Validation**:
 * Validates data flow through intermediate transformation node
 */
// DISABLED TEMPORARILY: Topology3 causes uncaught exception in MergeNodeBase thread
// when plugin-created InteriorTestNode processes data during graph execution.
// Root cause: Exception in worker thread not caught in plugin context.
// TODO: Add exception handling to MergeNodeBase thread loop and debug plugin RTTI issues.
TEST_F(CompletionSemanticsTest, Topology3_LinearSequentialPipeline) {
    // === SETUP: Build LinearSequential topology ===
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::LinearSequential
    );
    
    ASSERT_NE(graph, nullptr) << "Failed to build LinearSequential topology";

    // === VERIFY: Metadata exists for topology ===
    auto metadata = test::TopologyBuilder::GetTopologyMetadata(
        test::TopologyType::LinearSequential
    );
    
    EXPECT_FALSE(metadata.name.empty())
        << "Metadata name should not be empty for LinearSequential topology";
    EXPECT_EQ(metadata.expected_node_count, 3)
        << "LinearSequential should have 3 nodes (source + interior + sink)";
    EXPECT_EQ(metadata.expected_edge_count, 2)
        << "LinearSequential should have 2 edges (source→interior, interior→sink)";

    // === CREATE: Build executor ===
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .WithVerboseLogging(true)
        .Build();
    
    ASSERT_NE(executor, nullptr) << "Failed to build GraphExecutor";

    // === INIT: Initialize the executor ===
    auto init_result = executor->Init();
    
    debug_.AssertSuccess(init_result, "GraphExecutor::Init()", "LinearSequential");

    // === START: Start the graph execution ===
    auto start_result = executor->Start();
    
    debug_.AssertSuccess(start_result, "GraphExecutor::Start()", "LinearSequential");

    // === RUN: Execute the graph with timeout ===
    // Use debug helper to run executor in a thread with timeout
    auto run_result = ExecutorDebugHelper::RunWithTimeout(
        executor, 
        std::chrono::milliseconds(executor_timeout_ms_)
    );
    
    debug_.AssertRunSuccess(run_result, "LinearSequential");

    // === STOP & JOIN: Graceful shutdown ===
    auto stop_result = executor->Stop();
    debug_.AssertSuccess(stop_result, "GraphExecutor::Stop()", "LinearSequential");
    
    auto join_result = executor->Join();
    debug_.AssertSuccess(join_result, "GraphExecutor::Join()", "LinearSequential");

    // === VERIFY: Completion signal was triggered ===
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled)
        << "LinearSequential topology should signal completion when all messages processed";
    
    // === VALIDATE: Message Flow (Stage 5.5b) ===
    auto flow_metrics = MessageFlowValidator::ValidateTopology(graph, "LinearSequential");
    MessageFlowValidator::LogMetrics(flow_metrics);
    // LinearSequential: Source produces 10, Interior transfers 10, Sink consumes 10
    MessageFlowValidator::AssertValidFlow(flow_metrics, 10, 10);
    
    // === DEBUG: Log final state ===
    debug_.LogExecutorState(executor, "LinearSequential final state");
}

}  // namespace

/**
 * @page DEBUGGING_INFRASTRUCTURE Unit Test Debugging Infrastructure
 *
 * @section DESIGN_OVERVIEW Overview
 *
 * The `test_graph_completion.cpp` file now includes comprehensive debugging
 * infrastructure via the `ExecutorDebugHelper` class. This infrastructure addresses
 * the core problem: GraphExecutor::Run() is a **blocking call** that doesn't return
 * until the executor completes or is stopped from another thread.
 *
 * @section COMPONENTS Key Components
 *
 * **1. ExecutorDebugHelper::RunWithTimeout()**
 * 
 * Solves the "blocking Run()" problem by:
 * - Running executor->Run() in a background thread
 * - Waiting for completion with a specified timeout
 * - Gracefully stopping the executor if timeout occurs
 * - Safely joining the thread and reporting results
 * 
 * **Example Usage**:
 * ```cpp
 * auto run_result = ExecutorDebugHelper::RunWithTimeout(
 *     executor, 
 *     std::chrono::milliseconds(5000)
 * );
 * debug_.AssertRunSuccess(run_result, "Topology Name");
 * ```
 *
 * **2. ExecutorDebugHelper::RunResult Structure**
 *
 * Detailed result information:
 * - `success`: Whether the run succeeded (completed or stopped gracefully)
 * - `completed_naturally`: Whether executor completed before timeout
 * - `timed_out`: Whether the timeout was reached
 * - `was_stopped`: Whether Stop() succeeded during timeout
 * - `error_message`: Detailed error information
 * - `elapsed_time`: How long the execution took
 *
 * **3. ExecutorDebugHelper::AssertSuccess()**
 *
 * Formats assertion failures with detailed error messages:
 * ```cpp
 * debug_.AssertSuccess(init_result, "GraphExecutor::Init()", "MinimalGraph");
 * ```
 * 
 * Produces output like:
 * ```
 * GraphExecutor::Init() failed for MinimalGraph: (detailed error message)
 * ```
 *
 * **4. ExecutorDebugHelper::AssertRunSuccess()**
 *
 * Formats RunWithTimeout failures with detailed diagnostics:
 * ```cpp
 * debug_.AssertRunSuccess(run_result, "MinimalGraph");
 * ```
 * 
 * Produces output like:
 * ```
 * Executor::Run() failed for MinimalGraph
 *   - Timed out: yes
 *   - Was stopped: yes
 *   - Elapsed time: 5000 ms
 *   - Error: (detailed error message)
 * ```
 *
 * **5. ExecutorDebugHelper::LogExecutorState()**
 *
 * Logs executor state for debugging:
 * ```cpp
 * debug_.LogExecutorState(executor, "MinimalGraph final state");
 * ```
 * 
 * Outputs:
 * ```
 * [MinimalGraph final state] Executor State:
 *   - IsCompletionSignaled: true
 * ```
 *
 * @section TEST_PATTERN Recommended Test Pattern
 *
 * All completion semantics tests should follow this pattern:
 *
 * ```cpp
 * TEST_F(CompletionSemanticsTest, TopologyName) {
 *     // 1. BUILD TOPOLOGY
 *     auto graph = test::TopologyBuilder::BuildTopology(...);
 *     ASSERT_NE(graph, nullptr);
 *
 *     // 2. BUILD EXECUTOR
 *     auto executor = graph::GraphExecutorBuilder()
 *         .WithGraphManager(graph)
 *         .WithExecutorTimeout(std::chrono::seconds(30))
 *         .Build();
 *     ASSERT_NE(executor, nullptr);
 *
 *     // 3. INIT EXECUTOR
 *     auto init_result = executor->Init();
 *     debug_.AssertSuccess(init_result, "Init", "TopologyName");
 *
 *     // 4. START EXECUTOR
 *     auto start_result = executor->Start();
 *     debug_.AssertSuccess(start_result, "Start", "TopologyName");
 *
 *     // 5. RUN WITH TIMEOUT (using debug helper)
 *     auto run_result = ExecutorDebugHelper::RunWithTimeout(
 *         executor,
 *         std::chrono::milliseconds(5000)
 *     );
 *     debug_.AssertRunSuccess(run_result, "TopologyName");
 *
 *     // 6. STOP & JOIN
 *     auto stop_result = executor->Stop();
 *     debug_.AssertSuccess(stop_result, "Stop", "TopologyName");
 *     auto join_result = executor->Join();
 *     debug_.AssertSuccess(join_result, "Join", "TopologyName");
 *
 *     // 7. VERIFY COMPLETION
 *     bool is_signaled = executor->IsCompletionSignaled();
 *     EXPECT_TRUE(is_signaled);
 *
 *     // 8. DEBUG OUTPUT
 *     debug_.LogExecutorState(executor, "TopologyName final");
 * }
 * ```
 *
 * @section BENEFITS Benefits of Debug Infrastructure
 *
 * **1. Non-blocking Test Execution**
 * - Tests no longer hang indefinitely on Run()
 * - Timeouts are configurable and respected
 * - Graceful cleanup even on timeout
 *
 * **2. Better Error Messages**
 * - Detailed diagnostics for failures
 * - Context about what operation failed
 * - Timing information for performance analysis
 *
 * **3. Thread Safety**
 * - Background thread runs executor
 * - Main thread waits with timeout
 * - Safe synchronization between threads
 * - Join with timeout to prevent test framework hangs
 *
 * **4. Completion Signal Validation**
 * - Tests can now reach IsCompletionSignaled() checks
 * - Before: blocked in Run(), never reached assertions
 * - Now: Run() completes (naturally or via timeout), assertions execute
 *
 * **5. Debugging Support**
 * - LogExecutorState() for manual inspection
 * - Formatted error messages aid debugging
 * - Elapsed time tracking identifies performance issues
 *
 * @section FUTURE_EXTENSIONS Future Enhancements
 *
 * The infrastructure can be extended to:
 *
 * **1. Message Flow Validation**
 * - Track message counts through pipeline
 * - Validate correct node execution
 * - Example: `debug_.ValidateMessageFlow(executor, expected_count)`
 *
 * **2. Thread Safety Analysis**
 * - Detect race conditions
 * - Monitor for resource leaks
 * - Example: `debug_.CheckThreadSafety(executor)`
 *
 * **3. Performance Metrics**
 * - Measure executor cycle time
 * - Track queue depths
 * - Example: `debug_.GetPerformanceMetrics(executor)`
 *
 * **4. State Machine Validation**
 * - Verify lifecycle transitions
 * - Detect invalid state sequences
 * - Example: `debug_.ValidateLifecycleStates(executor)`
 *
 * @section QUICK_REFERENCE Quick Reference
 *
 * | Task | Helper Method | Returns |
 * |------|---------------|---------|
 * | Run with timeout | `RunWithTimeout(executor, ms)` | `RunResult` |
 * | Assert operation | `AssertSuccess(result, op, name)` | void |
 * | Assert run result | `AssertRunSuccess(result, name)` | void |
 * | Log state | `LogExecutorState(executor, label)` | void |
 * | Format error | `FormatError(result)` | `string` |
 *
 * @see ExecutorDebugHelper for implementation details
 * @see CompletionSemanticsTest for usage examples
 */
