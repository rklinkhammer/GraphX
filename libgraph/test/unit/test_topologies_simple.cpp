// SPDX-License-Identifier: MIT

/**
 * @file test_topologies_simple.cpp
 * @brief Test Topologies Simple Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include "test/TestGraphTopologies.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "capabilities/MetricsCapability.hpp"

namespace {

void AssertExecutionSuccess(const graph::ExecutionResult& result,
                            const char* operation) {
    ASSERT_TRUE(result.success) << operation << " failed: " << result.message;
}

/**
 * @brief Assert initialization success.
 * @param result Parameter for assert initialization success.
 */
void AssertInitializationSuccess(const graph::InitializationResult& result) {
    ASSERT_TRUE(result.success) << "Init failed: " << result.message;
}

size_t CountCompletionCallbackProviders(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t provider_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto facade_adapter = graph::GetAsNodeFacadeAdapter(node);
        if (!facade_adapter) {
            continue;
        }

        auto completion_provider =
            facade_adapter->GetCompletionCallbackProviderPtr();
        if (completion_provider) {
            ++provider_count;
        }
    }

    return provider_count;
}

size_t CountInstalledCompletionCallbacks(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t installed_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto facade_adapter = graph::GetAsNodeFacadeAdapter(node);
        if (!facade_adapter) {
            continue;
        }

        auto completion_provider = std::static_pointer_cast<
            graph::CompletionCallbackProvider>(
                facade_adapter->GetCompletionCallbackProviderPtr());
        if (completion_provider && completion_provider->HasCallbackProvider()) {
            ++installed_count;
        }
    }

    return installed_count;
}

void ExpectCompletionCallbackInstallation(
    const std::shared_ptr<graph::GraphManager>& graph,
    size_t expected_provider_count) {
    ASSERT_EQ(CountCompletionCallbackProviders(graph), expected_provider_count)
        << "Unexpected number of completion-capable nodes";
    EXPECT_EQ(CountInstalledCompletionCallbacks(graph), expected_provider_count)
        << "CompletionPolicy should install callbacks during executor Init()";
}

void ExpectTypedSplitNodeExtraction(
    const std::shared_ptr<graph::GraphManager>& graph,
    size_t expected_split_nodes) {
    size_t extracted_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto facade_adapter = graph::GetAsNodeFacadeAdapter(node);
        if (!facade_adapter || facade_adapter->GetType() != "SplitTestNode") {
            continue;
        }

        auto typed_split = facade_adapter->GetNode<test::SplitTestNode>();
        ASSERT_NE(typed_split, nullptr)
            << "SplitTestNode wrapper should allow typed extraction for edge wiring";
        ++extracted_count;
    }

    EXPECT_EQ(extracted_count, expected_split_nodes)
        << "Unexpected number of extractable SplitTestNode instances";
}

void ExpectNodeNamesDifferFromTypes(
    const std::shared_ptr<graph::GraphManager>& graph,
    const char* topology_name) {
    for (const auto& node : graph->GetNodes()) {
        auto facade_adapter = graph::GetAsNodeFacadeAdapter(node);
        ASSERT_NE(facade_adapter, nullptr)
            << topology_name << ": expected a NodeFacadeAdapter-backed node";
        EXPECT_FALSE(facade_adapter->GetName().empty())
            << topology_name << ": node name should not be empty";
        EXPECT_NE(facade_adapter->GetName(), facade_adapter->GetType())
            << topology_name << ": node name must not reuse node type";
    }
}

TEST(TopologiesSimple, TopologyNodesUseInstanceNamesNotTypes) {
    for (const auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
        auto graph = test::TopologyBuilder::BuildTopology(type);
        ASSERT_NE(graph, nullptr)
            << "Failed to build topology " << static_cast<int>(type);
        ExpectNodeNamesDifferFromTypes(graph, test::GetTopologyDocumentation(type).c_str());
    }
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
    ExpectCompletionCallbackInstallation(graph, 0);

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
        auto events = test_subscriber.GetCapturedEvents();
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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    // MinimalGraph has sink that signals completion when 10 messages received
    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MinimalGraph should signal completion";

    // Verify metrics were published
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "LinearSequential should signal completion";

    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MergeSimple should signal completion";

    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
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

    ExpectTypedSplitNodeExtraction(graph, 1);

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
    ExpectCompletionCallbackInstallation(graph, 2);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "SplitSimple should signal completion";

    // Verify metrics
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
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

/**
 * @test Topology 6: DiamondComplex
 * Source → Split → Interior + Interior → Merge → Sink
 */
TEST(TopologiesSimple, Topology6_DiamondComplex) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::DiamondComplex);
    ASSERT_NE(graph, nullptr) << "Failed to build DiamondComplex topology";

    ExpectTypedSplitNodeExtraction(graph, 1);

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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "DiamondComplex should signal completion";

    // Verify metrics: produced → split → transfer → merged → consumed
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
        size_t produced_count = 0;
        size_t split_count = 0;
        size_t transfer_count = 0;
        size_t merged_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_split") {
                split_count++;
            } else if (event.event_type == "message_transfer") {
                transfer_count++;
            } else if (event.event_type == "message_merged") {
                merged_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(split_count, 0) << "Expected SplitTestNode to split metrics";
        EXPECT_GT(transfer_count, 0) << "Expected InteriorTestNode to transfer metrics";
        EXPECT_GT(merged_count, 0) << "Expected MergeTestNode to merge metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
}

/**
 * @test Topology 7: MultiPathSequential
 * Source → Interior → Interior → Interior → Sink
 */
TEST(TopologiesSimple, Topology7_MultiPathSequential) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::MultiPathSequential);
    ASSERT_NE(graph, nullptr) << "Failed to build MultiPathSequential topology";

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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "MultiPathSequential should signal completion";

    // Verify metrics: produced → transfers → consumed
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
        size_t produced_count = 0;
        size_t transfer_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_transfer") {
                transfer_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(transfer_count, 0) << "Expected InteriorTestNode(s) to transfer metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
}

/**
 * @test Topology 8: InteriorToMerge
 * Source → Interior → Merge → Sink
 */
TEST(TopologiesSimple, Topology8_InteriorToMerge) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::InteriorToMerge);
    ASSERT_NE(graph, nullptr) << "Failed to build InteriorToMerge topology";

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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "InteriorToMerge should signal completion";

    // Verify metrics: produced → transfer → merged → consumed
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
        size_t produced_count = 0;
        size_t transfer_count = 0;
        size_t merged_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_transfer") {
                transfer_count++;
            } else if (event.event_type == "message_merged") {
                merged_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(transfer_count, 0) << "Expected InteriorTestNode to transfer metrics";
        EXPECT_GT(merged_count, 0) << "Expected MergeTestNode to merge metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
}

/**
 * @test Topology 9: ParallelMergeWithInterior
 * Source + Source + Interior → Merge → Sink
 */
TEST(TopologiesSimple, Topology9_ParallelMergeWithInterior) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::ParallelMergeWithInterior);
    ASSERT_NE(graph, nullptr) << "Failed to build ParallelMergeWithInterior topology";

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
    ExpectCompletionCallbackInstallation(graph, 1);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "ParallelMergeWithInterior should signal completion";

    // Verify metrics: produced → transfer → merged → consumed
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
        size_t produced_count = 0;
        size_t transfer_count = 0;
        size_t merged_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_transfer") {
                transfer_count++;
            } else if (event.event_type == "message_merged") {
                merged_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode(s) to produce metrics";
        EXPECT_GT(transfer_count, 0) << "Expected InteriorTestNode to transfer metrics";
        EXPECT_GT(merged_count, 0) << "Expected MergeTestNode to merge metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
}

/**
 * @test Topology 10: ComplexNetwork
 * Complex interleaved merge/split operations
 */
TEST(TopologiesSimple, Topology10_ComplexNetwork) {
    auto graph = test::TopologyBuilder::BuildTopology(test::TopologyType::ComplexNetwork);
    ASSERT_NE(graph, nullptr) << "Failed to build ComplexNetwork topology";

    ExpectTypedSplitNodeExtraction(graph, 1);

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
    ExpectCompletionCallbackInstallation(graph, 2);

    AssertExecutionSuccess(executor->Start(), "Start");

    AssertExecutionSuccess(executor->Run(), "Run");

    AssertExecutionSuccess(executor->Stop(), "Stop");

    AssertExecutionSuccess(executor->Join(), "Join");

    bool is_signaled = executor->IsCompletionSignaled();
    EXPECT_TRUE(is_signaled) << "ComplexNetwork should signal completion";

    // Verify metrics: All event types expected in a complex topology
    if (metrics_cap) {
        auto events = test_subscriber.GetCapturedEvents();
        size_t produced_count = 0;
        size_t consumed_count = 0;
        for (const auto& event : events) {
            if (event.event_type == "message_produced") {
                produced_count++;
            } else if (event.event_type == "message_consumed") {
                consumed_count++;
            }
        }
        EXPECT_GT(produced_count, 0) << "Expected SourceTestNode to produce metrics";
        EXPECT_GT(consumed_count, 0) << "Expected SinkTestNode to consume metrics";
    }
}

}  // namespace
