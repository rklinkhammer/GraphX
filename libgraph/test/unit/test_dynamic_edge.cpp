// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "graph/DynamicEdge.hpp"
#include "graph/GraphManager.hpp"
#include "graph/PortFunction.hpp"
#include "test/AdvancedTestNodes.hpp"

namespace {

using OutputIntPort = graph::Port<int, 0>;
using InputIntPort = graph::Port<int, 0>;
using InputFloatPort = graph::Port<float, 0>;

graph::RuntimePortHandle MakeOutputHandle(graph::IPortFunction* port, std::size_t node_index) {
    return graph::RuntimePortHandle{
        .node_index = node_index,
        .descriptor = graph::RuntimePortDescriptor{
            .id = 0,
            .name = "Output0",
            .direction = graph::PortDirection::Output,
            .payload_type = "int",
            .transport_type = std::string(port ? port->GetTransportTypeName() : "ActiveQueue<int>"),
        },
        .port = port,
    };
}

graph::RuntimePortHandle MakeOutputHandleWithTransport(
    graph::IPortFunction* port,
    std::size_t node_index,
    std::string transport_type) {
    auto handle = MakeOutputHandle(port, node_index);
    handle.descriptor.transport_type = std::move(transport_type);
    return handle;
}

graph::RuntimePortHandle MakeInputHandle(graph::IPortFunction* port, std::size_t node_index) {
    return graph::RuntimePortHandle{
        .node_index = node_index,
        .descriptor = graph::RuntimePortDescriptor{
            .id = 0,
            .name = "Input0",
            .direction = graph::PortDirection::Input,
            .payload_type = "int",
            .transport_type = std::string(port ? port->GetTransportTypeName() : "ActiveQueue<int>"),
        },
        .port = port,
    };
}

graph::RuntimePortHandle MakeInputHandleWithTransport(
    graph::IPortFunction* port,
    std::size_t node_index,
    std::string transport_type) {
    auto handle = MakeInputHandle(port, node_index);
    handle.descriptor.transport_type = std::move(transport_type);
    return handle;
}

TEST(DynamicEdgeTest, ValidateDynamicEdgeCompatibilityAcceptsMatchingHandles) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    auto result = graph::ValidateDynamicEdgeCompatibility(
        MakeOutputHandle(&output, 0),
        MakeInputHandle(&input, 1),
        8);

    EXPECT_TRUE(result);
}

TEST(DynamicEdgeTest, ValidateDynamicEdgeCompatibilityRejectsTransportMismatch) {
    graph::RuntimePortHandle source = {
        .node_index = 0,
        .descriptor = graph::RuntimePortDescriptor{0, "Output0", graph::PortDirection::Output, "int", "queue_a"},
        .port = nullptr,
    };
    graph::RuntimePortHandle destination = {
        .node_index = 1,
        .descriptor = graph::RuntimePortDescriptor{0, "Input0", graph::PortDirection::Input, "int", "queue_b"},
        .port = nullptr,
    };

    auto result = graph::ValidateDynamicEdgeCompatibility(source, destination, 8);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::RuntimePortConnectError::TransportTypeMismatch);
}

TEST(DynamicEdgeTest, DynamicEdgeInitAndStartTrackState) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);
    auto metrics = std::make_shared<graph::EdgeMetrics>();

    graph::DynamicEdge edge(MakeOutputHandle(&output, 0), MakeInputHandle(&input, 1), 8, metrics);

    EXPECT_TRUE(edge.Init());
    EXPECT_TRUE(edge.IsInitialized());
    EXPECT_TRUE(edge.Start());
    EXPECT_TRUE(edge.IsRunning());
    edge.Stop();
    EXPECT_FALSE(edge.IsRunning());
}

TEST(DynamicEdgeTest, DynamicEdgeInitRejectsDescriptorOnlyFallbackPorts) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdge edge(
        MakeOutputHandleWithTransport(&output, 0, "runtime.descriptor"),
        MakeInputHandleWithTransport(&input, 1, "runtime.descriptor"),
        8);

    EXPECT_FALSE(edge.Init());
}

TEST(DynamicEdgeTest, DynamicEdgeJoinWithTimeoutRespectsDeadline) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdge edge(MakeOutputHandle(&output, 0), MakeInputHandle(&input, 1), 8);
    ASSERT_TRUE(edge.Init());
    ASSERT_TRUE(edge.Start());

    EXPECT_FALSE(edge.JoinWithTimeout(std::chrono::milliseconds(2)));

    edge.Stop();
    EXPECT_TRUE(edge.JoinWithTimeout(std::chrono::milliseconds(250)));
}

TEST(DynamicEdgeTest, DynamicEdgeTransfersPayloadAtRuntime) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdge edge(MakeOutputHandle(&output, 0), MakeInputHandle(&input, 1), 8);
    ASSERT_TRUE(edge.Init());
    ASSERT_TRUE(output.GetQueue().Enqueue(99));

    ASSERT_TRUE(edge.Start());

    bool transferred = false;
    for (int i = 0; i < 50; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            EXPECT_EQ(value, 99);
            transferred = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    edge.Stop();
    edge.Join();

    EXPECT_TRUE(transferred);
}

TEST(DynamicEdgeTest, DynamicEdgeQueueSizeReflectsDestinationDepth) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdge edge(MakeOutputHandle(&output, 0), MakeInputHandle(&input, 1), 8);
    ASSERT_TRUE(edge.Init());
    ASSERT_TRUE(output.GetQueue().Enqueue(77));
    ASSERT_TRUE(edge.Start());

    bool observed_non_zero_depth = false;
    for (int i = 0; i < 60; ++i) {
        if (edge.GetQueueSize() >= 1u) {
            observed_non_zero_depth = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    edge.Stop();
    edge.Join();

    EXPECT_TRUE(observed_non_zero_depth);
}

TEST(DynamicEdgeTest, DynamicEdgeTransientTransferFailureDoesNotStopEdge) {
    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdge edge(MakeOutputHandle(&output, 0), MakeInputHandle(&input, 1), 1);
    ASSERT_TRUE(edge.Init());
    ASSERT_TRUE(edge.Start());

    ASSERT_TRUE(output.GetQueue().Enqueue(1));
    bool first_arrived = false;
    for (int i = 0; i < 60; ++i) {
        if (edge.GetQueueSize() >= 1u) {
            first_arrived = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(first_arrived);

    // Queue is still full, so this can trigger a transient transfer failure.
    ASSERT_TRUE(output.GetQueue().Enqueue(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(edge.IsRunning());

    int value = 0;
    ASSERT_TRUE(input.GetQueue().DequeueNonBlocking(value));
    ASSERT_EQ(value, 1);

    ASSERT_TRUE(output.GetQueue().Enqueue(3));
    bool next_arrived = false;
    for (int i = 0; i < 100; ++i) {
        if (input.GetQueue().DequeueNonBlocking(value)) {
            next_arrived = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    edge.Stop();
    edge.Join();

    EXPECT_TRUE(next_arrived);
}

TEST(DynamicEdgeTest, GraphManagerAddDynamicEdgeExpectedStoresDynamicEdgeMetadata) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());
    graph.AddNode(std::make_shared<test::SinkTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 1),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);

    ASSERT_TRUE(added);
    ASSERT_NE(*added, nullptr);
    EXPECT_EQ(graph.GetEdges().size(), 1u);
    EXPECT_EQ((*added)->GetSourceNodeId(), 0u);
    EXPECT_EQ((*added)->GetDestNodeId(), 1u);
    EXPECT_EQ((*added)->GetMessageTypeName(), "int");
}

TEST(DynamicEdgeTest, GraphManagerAddDynamicEdgeExpectedRejectsInvalidNodeIndex) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 9),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);

    ASSERT_FALSE(added);
    EXPECT_EQ(added.error().code, app::error::GraphExecutionError::MissingNode);
}

TEST(DynamicEdgeTest, GraphManagerDynamicEdgeMetricsTrackRuntimeTransfer) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());
    graph.AddNode(std::make_shared<test::SinkTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 1),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);
    ASSERT_TRUE(added);
    ASSERT_EQ(graph.GetEdges().size(), 1u);

    auto* edge = graph.GetEdges().front().get();
    ASSERT_NE(edge, nullptr);
    ASSERT_TRUE(edge->Init());
    ASSERT_TRUE(output.GetQueue().Enqueue(7));
    ASSERT_TRUE(output.GetQueue().Enqueue(8));
    ASSERT_TRUE(edge->Start());

    int consumed = 0;
    for (int i = 0; i < 100 && consumed < 2; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            ++consumed;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    edge->Stop();
    edge->Join();

    EXPECT_EQ(consumed, 2);

    auto edge_metrics = graph.GetEdgeMetrics(0);
    ASSERT_NE(edge_metrics, nullptr);
    EXPECT_GE(edge_metrics->messages_enqueued.load(std::memory_order_relaxed), 2u);
    EXPECT_GE(edge_metrics->messages_dequeued.load(std::memory_order_relaxed), 2u);
    EXPECT_GE(edge_metrics->peak_queue_depth.load(std::memory_order_relaxed), 1u);
}

TEST(DynamicEdgeTest, GraphManagerMetricsAggregateDynamicEdgeCounters) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());
    graph.AddNode(std::make_shared<test::SinkTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 1),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);
    ASSERT_TRUE(added);
    ASSERT_EQ(graph.GetEdges().size(), 1u);

    auto* edge = graph.GetEdges().front().get();
    ASSERT_NE(edge, nullptr);
    ASSERT_TRUE(edge->Init());
    ASSERT_TRUE(output.GetQueue().Enqueue(11));
    ASSERT_TRUE(output.GetQueue().Enqueue(12));
    ASSERT_TRUE(edge->Start());

    int consumed = 0;
    for (int i = 0; i < 100 && consumed < 2; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            ++consumed;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    edge->Stop();
    edge->Join();

    ASSERT_EQ(consumed, 2);

    const auto& metrics = graph.GetMetrics();
    EXPECT_GE(metrics.graph_total_enqueued.load(std::memory_order_relaxed), 2u);
    EXPECT_GE(metrics.graph_total_dequeued.load(std::memory_order_relaxed), 2u);
    EXPECT_GE(metrics.total_messages_processed.load(std::memory_order_relaxed), 2u);
    EXPECT_GE(metrics.total_queue_time_ns.load(std::memory_order_relaxed), 1u);
}

TEST(DynamicEdgeTest, GraphManagerLifecycleTimingMetricsTrackRuntimeExecution) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());
    graph.AddNode(std::make_shared<test::SinkTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 1),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);
    ASSERT_TRUE(added);

    ASSERT_TRUE(graph.Init());
    ASSERT_TRUE(graph.Start());
    ASSERT_TRUE(output.GetQueue().Enqueue(21));

    bool consumed = false;
    for (int i = 0; i < 100; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            EXPECT_EQ(value, 21);
            consumed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    graph.Stop();
    graph.Join();

    ASSERT_TRUE(consumed);

    const auto& metrics = graph.GetMetrics();
    EXPECT_GE(metrics.init_time_ns.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.start_time_ns.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.execution_time_ns.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.total_queue_time_ns.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.GetThroughputItemsPerSec(), 0.0);
    EXPECT_GE(metrics.GetAverageLatencyUs(), 0.0);
}

TEST(DynamicEdgeTest, GraphManagerAggregatesDynamicEdgeThreadTimingMetrics) {
    graph::GraphManager graph;
    graph.AddNode(std::make_shared<test::SourceTestNode>());
    graph.AddNode(std::make_shared<test::SinkTestNode>());

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);

    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 1),
        .capacity = 8,
    };

    auto added = graph.AddDynamicEdgeExpected(config);
    ASSERT_TRUE(added);

    ASSERT_TRUE(graph.Init());
    ASSERT_TRUE(graph.Start());

    bool observed_active_peak = false;
    for (int i = 0; i < 50; ++i) {
        const auto& live_metrics = graph.GetMetrics();
        if (live_metrics.peak_active_threads.load(std::memory_order_relaxed) >= 1u) {
            observed_active_peak = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Allow edge thread to enter idle/wait state before first transfer.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(output.GetQueue().Enqueue(31));

    bool consumed = false;
    for (int i = 0; i < 100; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            EXPECT_EQ(value, 31);
            consumed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    graph.Stop();
    graph.Join();

    ASSERT_TRUE(consumed);
    ASSERT_TRUE(observed_active_peak);

    const auto& metrics = graph.GetMetrics();
    EXPECT_GE(metrics.total_process_time_ns.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.total_thread_time_ns.load(std::memory_order_relaxed),
              metrics.total_process_time_ns.load(std::memory_order_relaxed));
}

TEST(DynamicEdgeTest, MixedTypedAndDynamicEdgesRunThroughLifecycle) {
    graph::GraphManager graph;
    auto source_node = std::make_shared<test::SourceTestNode>();
    auto typed_sink = std::make_shared<test::SinkTestNode>();
    auto dynamic_sink = std::make_shared<test::SinkTestNode>();

    graph.AddNode(source_node);
    graph.AddNode(typed_sink);
    graph.AddNode(dynamic_sink);
    graph.AddEdge<test::SourceTestNode, 0, test::SinkTestNode, 0>(source_node, typed_sink, 8);

    graph::PortFunction<OutputIntPort> output(graph::PortDirection::Output);
    graph::PortFunction<InputIntPort> input(graph::PortDirection::Input);
    graph::DynamicEdgeConfig config{
        .source = MakeOutputHandle(&output, 0),
        .destination = MakeInputHandle(&input, 2),
        .capacity = 8,
    };

    auto added_dynamic = graph.AddDynamicEdgeExpected(config);
    ASSERT_TRUE(added_dynamic);

    ASSERT_TRUE(graph.Init());
    ASSERT_TRUE(graph.Start());
    ASSERT_TRUE(output.GetQueue().Enqueue(42));

    bool transferred = false;
    for (int i = 0; i < 100; ++i) {
        int value = 0;
        if (input.GetQueue().DequeueNonBlocking(value)) {
            EXPECT_EQ(value, 42);
            transferred = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    graph.Stop();
    graph.Join();

    ASSERT_TRUE(transferred);
    const auto& metrics = graph.GetMetrics();
    EXPECT_GE(metrics.graph_total_enqueued.load(std::memory_order_relaxed), 1u);
    EXPECT_GE(metrics.graph_total_dequeued.load(std::memory_order_relaxed), 1u);
}

}  // namespace