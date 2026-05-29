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
#include "capabilities/MetricsCapability.hpp"

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

    // Setup metrics
    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    test::TestMetricsSubscriber test_subscriber;
    
    if (metrics_cap) {
        metrics_cap->RegisterMetricsCallback(&test_subscriber);
    }

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
    
    // Verify metrics: SourceOnly should produce but not consume
    if (metrics_cap) {
        auto events = test_subscriber.GetEvents();
        size_t produced_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
    }
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

    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    test::TestMetricsSubscriber test_subscriber;
    
    if (metrics_cap) {
        metrics_cap->RegisterMetricsCallback(&test_subscriber);
        std::cerr << "[DEBUG] MetricsCapability is available for MinimalGraph\n";
    } else {
        std::cerr << "[DEBUG] WARNING: MetricsCapability not available for MinimalGraph\n";
    }

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    // MinimalGraph has sink that signals completion when 10 messages received
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MinimalGraph should signal completion";
    
    // Verify metrics were published
    if (metrics_cap) {
        auto events = test_subscriber.GetEvents();
        std::cerr << "[DEBUG] Total metrics events received: " << events.size() << "\n";
        
        // Count events by type
        size_t produced_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
            std::cerr << "[DEBUG] Event: source=" << event.source 
                     << " type=" << event.event_type 
                     << " timestamp=" << event.timestamp.time_since_epoch().count() << "\n";
        }
        
        std::cerr << "[DEBUG] Metrics summary: produced=" << produced_count 
                 << " consumed=" << consumed_count << "\n";
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
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

    // Setup metrics
    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    test::TestMetricsSubscriber test_subscriber;
    
    if (metrics_cap) {
        metrics_cap->RegisterMetricsCallback(&test_subscriber);
    }

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "LinearSequential should signal completion";
    
    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetEvents();
        size_t produced_count = 0;
        size_t consumed_count = 0;
        size_t transfer_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            } else if (event.event_type == "message_transfer") {
                transfer_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(transfer_count, 0) << "Expected InteriorTestNode to transfer metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
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

    // Setup metrics
    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    test::TestMetricsSubscriber test_subscriber;
    
    if (metrics_cap) {
        metrics_cap->RegisterMetricsCallback(&test_subscriber);
    }

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MergeSimple should signal completion";
    
    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetEvents();
        size_t produced_count = 0;
        size_t merged_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_merged") {
                merged_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode(s) to produce metrics";
        EXPECT_GT(merged_count, 0) << "Expected MergeTestNode to merge metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
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

    // Setup metrics
    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    test::TestMetricsSubscriber test_subscriber;
    
    if (metrics_cap) {
        metrics_cap->RegisterMetricsCallback(&test_subscriber);
    }

    AssertInitializationSuccess(executor->Init());

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "SplitSimple should signal completion";
    
    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetEvents();
        size_t produced_count = 0;
        size_t split_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_split") {
                split_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(split_count, 0) << "Expected SplitTestNode to split metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode(s) to consume metrics";
    }
}

}  // namespace
