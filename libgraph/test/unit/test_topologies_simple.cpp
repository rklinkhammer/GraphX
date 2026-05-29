/**
 * @file test_topologies_simple.cpp
 * @brief Simplified topology completion tests
 *
 * Direct tests without wrapper infrastructure.
 * Each test: build → init → start → run → stop → join → verify
 */

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include "test/TestGraphTopologies.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"

namespace {

void AssertExecutionSuccess(const graph::ExecutionResult& result,
                            const char* operation) {
    ASSERT_TRUE(result.success) << operation << " failed: " << result.message;
}

void AssertInitializationSuccess(const graph::InitializationResult& result) {
    ASSERT_TRUE(result.success) << "Init failed: " << result.message;
}

/**
 * @test Topology 1: SourceOnly
 * Single source node, no sinks
 */
TEST(TopologiesSimple, Topology1_SourceOnly) {
    // Build topology
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::SourceOnly);
    ASSERT_NE(graph, nullptr) << "Failed to build SourceOnly topology";

    // Build executor
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(1))
        .Build();
    ASSERT_NE(executor, nullptr) << "Failed to build executor";

    // Init
    AssertInitializationSuccess(executor->Init());

    // Start
    AssertExecutionSuccess(executor->Start(), "Start");

    // SourceOnly has no completion callback, so Run() returns when the executor timeout expires.
    AssertExecutionSuccess(executor->Run(), "Run");

    // Stop
    AssertExecutionSuccess(executor->Stop(), "Stop");

    // Join
    AssertExecutionSuccess(executor->Join(), "Join");

    // Verify: SourceOnly has no sinks, so no completion signal expected
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_FALSE(is_signaled) << "SourceOnly should not signal completion (no sinks)";
}

/**
 * @test Topology 2: MinimalGraph
 * Source → Sink with completion semantics
 */
TEST(TopologiesSimple, Topology2_MinimalGraph) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::MinimalGraph);
    ASSERT_NE(graph, nullptr) << "Failed to build MinimalGraph topology";

    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    // MinimalGraph has sink that signals completion when 10 messages received
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MinimalGraph should signal completion";
}

/**
 * @test Topology 3: LinearSequential
 * Source → Interior → Sink
 */
TEST(TopologiesSimple, Topology3_LinearSequential) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::LinearSequential);
    ASSERT_NE(graph, nullptr) << "Failed to build LinearSequential topology";

    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "LinearSequential should signal completion";
}

/**
 * @test Topology 4: MergeSimple
 * Source1 + Source2 → Merge → Sink
 */
TEST(TopologiesSimple, Topology4_MergeSimple) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::MergeSimple);
    ASSERT_NE(graph, nullptr) << "Failed to build MergeSimple topology";

    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MergeSimple should signal completion";
}

/**
 * @test Topology 5: SplitSimple
 * Source → Split → Sink1 + Sink2
 */
TEST(TopologiesSimple, Topology5_SplitSimple) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::SplitSimple);
    ASSERT_NE(graph, nullptr) << "Failed to build SplitSimple topology";

    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "SplitSimple should signal completion";
}

}  // namespace
