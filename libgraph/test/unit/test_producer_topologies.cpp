// SPDX-License-Identifier: MIT

/**
 * @file test_producer_topologies.cpp
 * @brief Integration tests for producer-based graph topologies
 *
 * Tests the producer topology builders with dynamic plugin nodes:
 * - MinimalIntProducer: TestIntProducer → TestIntSinkNode + CompletionNode
 * - LinearSequentialIntProducer: TestIntProducer -> InteriorTestNode -> TestIntSinkNode + CompletionNode
 * - MinimalDoubleProducer: TestDoubleProducer → TestDoubleSinkNode + CompletionNode
 * - LinearSequentialDoubleProducer: TestDoubleProducer -> InteriorTestNode -> TestDoubleSinkNode + CompletionNode
 *
 * Validates:
 * - Two-port producer architecture (Port 0: data, Port 1: completion)
 * - Message encapsulation and unpacking for int and double types
 * - CompletionSignal delivery to dedicated completion node
 * - Full data flow from producer through sink nodes
 *
 * @author Test Suite
 * @date May 29, 2026
 */

#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <string>
#include <vector>
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "test/TestGraphTopologies.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "test/ProducerTestNodes.hpp"

namespace {

using namespace test;
using namespace graph;

// =============================================================================
// Test Fixture
// =============================================================================

/**
 * @class ProducerTopologiesTest
 * @brief Producer topologies test implementation for GraphX.
 */
class ProducerTopologiesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

/**
 * @brief Execute successfully.
 * @param graph Parameter for execute successfully.
 */
    static void ExecuteSuccessfully(const std::shared_ptr<GraphManager>& graph) {
        auto executor = GraphExecutorBuilder()
            .WithGraphManager(graph)
            .WithExecutorTimeout(std::chrono::seconds(5))
            .Build();
        ASSERT_NE(nullptr, executor) << "Failed to build executor";

        auto init_result = executor->Init();
        ASSERT_TRUE(init_result.success) << "Init failed: " << init_result.message;

        auto start_result = executor->Start();
        ASSERT_TRUE(start_result.success) << "Start failed: " << start_result.message;

        auto run_result = executor->Run();
        ASSERT_TRUE(run_result.success) << "Run failed: " << run_result.message;
        EXPECT_LT(run_result.elapsed_time_ms, 5000u) << "Run reached executor timeout instead of completion signal";

        auto stop_result = executor->Stop();
        ASSERT_TRUE(stop_result.success) << "Stop failed: " << stop_result.message;

        auto join_result = executor->Join();
        ASSERT_TRUE(join_result.success) << "Join failed: " << join_result.message;
    }

/**
 * @brief Expect node types.
 * @param graph Parameter for expect node types.
 * @param types Parameter for expect node types.
 */
    static void ExpectNodeTypes(const std::shared_ptr<GraphManager>& graph, const std::vector<std::string>& types) {
        auto nodes = graph->GetNodes();
        ASSERT_EQ(types.size(), nodes.size());

        for (size_t i = 0; i < types.size(); ++i) {
            auto wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(nodes[i]);
            ASSERT_NE(nullptr, wrapper) << "Node " << i << " is not a plugin wrapper";
            EXPECT_EQ(types[i], wrapper->GetType()) << "Unexpected type for node " << i;
        }
    }

    static void ExpectEdge(const std::shared_ptr<GraphManager>& graph,
                           size_t edge_index,
                           size_t source_node_id,
                           size_t source_port_id,
                           size_t dest_node_id,
                           size_t dest_port_id) {
        const auto* edge = graph->GetEdgeMetadata(edge_index);
        ASSERT_NE(nullptr, edge) << "Missing metadata for edge " << edge_index;
        EXPECT_EQ(source_node_id, edge->source_node_id) << "Unexpected source node for edge " << edge_index;
        EXPECT_EQ(source_port_id, edge->source_port_id) << "Unexpected source port for edge " << edge_index;
        EXPECT_EQ(dest_node_id, edge->dest_node_id) << "Unexpected destination node for edge " << edge_index;
        EXPECT_EQ(dest_port_id, edge->dest_port_id) << "Unexpected destination port for edge " << edge_index;
    }

    static std::shared_ptr<TestIntSinkNode> GetIntSink(const std::shared_ptr<GraphManager>& graph,
                                                       size_t sink_node_index) {
        auto wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(graph->GetNodes()[sink_node_index]);
        if (!wrapper) {
            return nullptr;
        }
        return wrapper->GetNode<TestIntSinkNode>();
    }

    static std::shared_ptr<TestDoubleSinkNode> GetDoubleSink(const std::shared_ptr<GraphManager>& graph,
                                                             size_t sink_node_index) {
        auto wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(graph->GetNodes()[sink_node_index]);
        if (!wrapper) {
            return nullptr;
        }
        return wrapper->GetNode<TestDoubleSinkNode>();
    }

    static std::shared_ptr<CompletionNode> GetCompletionNode(const std::shared_ptr<GraphManager>& graph,
                                                             size_t completion_node_index) {
        auto wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(graph->GetNodes()[completion_node_index]);
        if (!wrapper) {
            return nullptr;
        }
        return wrapper->GetNode<CompletionNode>();
    }

    static void ConfigureCompletionGateForIntSink(const std::shared_ptr<GraphManager>& graph,
                                                  size_t sink_node_index,
                                                  size_t completion_node_index,
                                                  size_t expected_data_count) {
        auto sink = GetIntSink(graph, sink_node_index);
        ASSERT_NE(nullptr, sink);
        auto completion = GetCompletionNode(graph, completion_node_index);
        ASSERT_NE(nullptr, completion);

        completion->SetCompletionGate([sink, expected_data_count] {
            return sink->GetReceivedCount() >= expected_data_count;
        });
    }

    static void ConfigureCompletionGateForDoubleSink(const std::shared_ptr<GraphManager>& graph,
                                                     size_t sink_node_index,
                                                     size_t completion_node_index,
                                                     size_t expected_data_count) {
        auto sink = GetDoubleSink(graph, sink_node_index);
        ASSERT_NE(nullptr, sink);
        auto completion = GetCompletionNode(graph, completion_node_index);
        ASSERT_NE(nullptr, completion);

        completion->SetCompletionGate([sink, expected_data_count] {
            return sink->GetReceivedCount() >= expected_data_count;
        });
    }

    static void ExpectIntDataFlow(const std::shared_ptr<GraphManager>& graph,
                                  size_t sink_node_index,
                                  const std::vector<int>& expected_values) {
        auto sink = GetIntSink(graph, sink_node_index);
        ASSERT_NE(nullptr, sink);

        auto values = sink->GetReceivedValues();
        EXPECT_EQ(expected_values, values);
        EXPECT_FALSE(sink->HasDataLoss());
        EXPECT_FALSE(sink->HasDuplicates());
    }

    static void ExpectDoubleDataFlow(const std::shared_ptr<GraphManager>& graph,
                                     size_t sink_node_index,
                                     const std::vector<double>& expected_values) {
        auto sink = GetDoubleSink(graph, sink_node_index);
        ASSERT_NE(nullptr, sink);

        auto values = sink->GetReceivedValues();
        ASSERT_EQ(expected_values.size(), values.size());
        for (size_t i = 0; i < expected_values.size(); ++i) {
            EXPECT_DOUBLE_EQ(expected_values[i], values[i]) << "Unexpected double payload at index " << i;
        }
    }

/**
 * @brief Expect completion.
 * @param graph Parameter for expect completion.
 * @param completion_node_index Parameter for expect completion.
 */
    static void ExpectCompletion(const std::shared_ptr<GraphManager>& graph, size_t completion_node_index) {
        auto completion = GetCompletionNode(graph, completion_node_index);
        ASSERT_NE(nullptr, completion);
        EXPECT_EQ(1u, completion->GetSignalCount());
        EXPECT_TRUE(completion->HasReceivedCompletion());
        EXPECT_TRUE(completion->WasCompletionGateSatisfied())
            << "Completion callback should wait until all expected data reaches the sink";
    }
};

struct ProducerTopologyCase {
    TopologyType type;
    const char* name;
    std::size_t expected_nodes;
    std::size_t expected_edges;
    std::vector<std::string> node_types;
    std::vector<std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>> edges;
    enum class PayloadKind { Int, Double } payload_kind;
    std::size_t sink_node_index;
    std::size_t completion_node_index;
    std::size_t expected_data_count;
    std::vector<int> int_values;
    std::vector<double> double_values;
};

/**
 * @class ProducerTopologyParameterizedTest
 * @brief Producer topology parameterized test implementation for GraphX.
 */
class ProducerTopologyParameterizedTest
    : public ProducerTopologiesTest,
      public ::testing::WithParamInterface<ProducerTopologyCase> {};

TEST_F(ProducerTopologiesTest, GetNodeReturnsNullForMismatchedRequestedType) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalIntProducer);
    ASSERT_NE(nullptr, graph);

    auto sink_wrapper = std::dynamic_pointer_cast<NodeFacadeAdapterWrapper>(graph->GetNodes()[1]);
    ASSERT_NE(nullptr, sink_wrapper);

    auto correct_type = sink_wrapper->GetNode<TestIntSinkNode>();
    EXPECT_NE(nullptr, correct_type);

    auto mismatched_type = sink_wrapper->GetNode<TestDoubleSinkNode>();
    EXPECT_EQ(nullptr, mismatched_type);
}

TEST_P(ProducerTopologyParameterizedTest, MetadataStructureAndExecutionMatchDefinition) {
    const auto& test_case = GetParam();

    auto metadata = TopologyBuilder::GetTopologyMetadata(test_case.type);
    EXPECT_EQ(test_case.name, metadata.name);
    EXPECT_EQ(test_case.expected_nodes, metadata.expected_node_count);
    EXPECT_EQ(test_case.expected_edges, metadata.expected_edge_count);
    EXPECT_FALSE(metadata.description.empty());

    auto graph = TopologyBuilder::BuildTopology(test_case.type);
    ASSERT_NE(nullptr, graph);

    auto nodes = graph->GetNodes();
    ASSERT_EQ(test_case.expected_nodes, nodes.size());
    ASSERT_EQ(test_case.expected_edges, graph->GetEdges().size());
    ExpectNodeTypes(graph, test_case.node_types);

    for (std::size_t i = 0; i < test_case.edges.size(); ++i) {
        const auto [src_node, src_port, dst_node, dst_port] = test_case.edges[i];
        ExpectEdge(graph, i, src_node, src_port, dst_node, dst_port);
    }

    if (test_case.payload_kind == ProducerTopologyCase::PayloadKind::Int) {
        ConfigureCompletionGateForIntSink(graph, test_case.sink_node_index, test_case.completion_node_index, test_case.expected_data_count);
        ExecuteSuccessfully(graph);
        ExpectIntDataFlow(graph, test_case.sink_node_index, test_case.int_values);
    } else {
        ConfigureCompletionGateForDoubleSink(graph, test_case.sink_node_index, test_case.completion_node_index, test_case.expected_data_count);
        ExecuteSuccessfully(graph);
        ExpectDoubleDataFlow(graph, test_case.sink_node_index, test_case.double_values);
    }

    ExpectCompletion(graph, test_case.completion_node_index);
}

INSTANTIATE_TEST_SUITE_P(
    ProducerTopologyCases,
    ProducerTopologyParameterizedTest,
    ::testing::Values(
        ProducerTopologyCase{
            TopologyType::MinimalIntProducer,
            "MinimalIntProducer",
            3,
            2,
            {"TestIntProducer", "TestIntSinkNode", "CompletionNode"},
            {{0, 0, 1, 0}, {0, 1, 2, 0}},
            ProducerTopologyCase::PayloadKind::Int,
            1,
            2,
            4,
            {1, 2, 3, 4},
            {}
        },
        ProducerTopologyCase{
            TopologyType::LinearSequentialIntProducer,
            "LinearSequentialIntProducer",
            4,
            3,
            {"TestIntProducer", "InteriorTestNode", "TestIntSinkNode", "CompletionNode"},
            {{0, 0, 1, 0}, {1, 0, 2, 0}, {0, 1, 3, 0}},
            ProducerTopologyCase::PayloadKind::Int,
            2,
            3,
            4,
            {1, 2, 3, 4},
            {}
        },
        ProducerTopologyCase{
            TopologyType::MinimalDoubleProducer,
            "MinimalDoubleProducer",
            3,
            2,
            {"TestDoubleProducer", "TestDoubleSinkNode", "CompletionNode"},
            {{0, 0, 1, 0}, {0, 1, 2, 0}},
            ProducerTopologyCase::PayloadKind::Double,
            1,
            2,
            10,
            {},
            {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0}
        },
        ProducerTopologyCase{
            TopologyType::LinearSequentialDoubleProducer,
            "LinearSequentialDoubleProducer",
            4,
            3,
            {"TestDoubleProducer", "InteriorTestNode", "TestDoubleSinkNode", "CompletionNode"},
            {{0, 0, 1, 0}, {1, 0, 2, 0}, {0, 1, 3, 0}},
            ProducerTopologyCase::PayloadKind::Double,
            2,
            3,
            10,
            {},
            {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0}
        }));

TEST_F(ProducerTopologiesTest, AllTopologyTypes_IncludeProducers) {
    auto all_types = TopologyBuilder::GetAllTopologyTypes();
    
    bool found_int_minimal = false;
    bool found_int_sequential = false;
    bool found_double_minimal = false;
    bool found_double_sequential = false;
    
    for (const auto& type : all_types) {
        if (type == TopologyType::MinimalIntProducer) found_int_minimal = true;
        if (type == TopologyType::LinearSequentialIntProducer) found_int_sequential = true;
        if (type == TopologyType::MinimalDoubleProducer) found_double_minimal = true;
        if (type == TopologyType::LinearSequentialDoubleProducer) found_double_sequential = true;
    }
    
    EXPECT_TRUE(found_int_minimal) << "MinimalIntProducer not in all types";
    EXPECT_TRUE(found_int_sequential) << "LinearSequentialIntProducer not in all types";
    EXPECT_TRUE(found_double_minimal) << "MinimalDoubleProducer not in all types";
    EXPECT_TRUE(found_double_sequential) << "LinearSequentialDoubleProducer not in all types";
}

} // namespace
