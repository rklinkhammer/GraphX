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

class ProducerTopologiesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

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

    static void ExpectCompletion(const std::shared_ptr<GraphManager>& graph, size_t completion_node_index) {
        auto completion = GetCompletionNode(graph, completion_node_index);
        ASSERT_NE(nullptr, completion);
        EXPECT_EQ(1u, completion->GetSignalCount());
        EXPECT_TRUE(completion->HasReceivedCompletion());
        EXPECT_TRUE(completion->WasCompletionGateSatisfied())
            << "Completion callback should wait until all expected data reaches the sink";
    }
};

// =============================================================================
// MinimalIntProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, MinimalIntProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(3, nodes.size()) << "Expected 3 nodes (producer, sink, completion)";
}

TEST_F(ProducerTopologiesTest, MinimalIntProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(3, nodes.size());
    ASSERT_EQ(2, graph->GetEdges().size());
    ExpectNodeTypes(graph, {"TestIntProducer", "TestIntSinkNode", "CompletionNode"});
    ExpectEdge(graph, 0, 0, 0, 1, 0);
    ExpectEdge(graph, 1, 0, 1, 2, 0);
}

TEST_F(ProducerTopologiesTest, MinimalIntProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalIntProducer);
    ASSERT_NE(nullptr, graph);
    ConfigureCompletionGateForIntSink(graph, 1, 2, 4);
    ExecuteSuccessfully(graph);
    ExpectIntDataFlow(graph, 1, {1, 2, 3, 4});
    ExpectCompletion(graph, 2);
}

// =============================================================================
// LinearSequentialIntProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(4, nodes.size()) << "Expected 4 nodes (producer, interior, sink, completion)";
}

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(4, nodes.size());
    ASSERT_EQ(3, graph->GetEdges().size());
    ExpectNodeTypes(graph, {"TestIntProducer", "InteriorTestNode", "TestIntSinkNode", "CompletionNode"});
    ExpectEdge(graph, 0, 0, 0, 1, 0);
    ExpectEdge(graph, 1, 1, 0, 2, 0);
    ExpectEdge(graph, 2, 0, 1, 3, 0);
}

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    ConfigureCompletionGateForIntSink(graph, 2, 3, 4);
    ExecuteSuccessfully(graph);
    ExpectIntDataFlow(graph, 2, {1, 2, 3, 4});
    ExpectCompletion(graph, 3);
}

// =============================================================================
// MinimalDoubleProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, MinimalDoubleProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(3, nodes.size()) << "Expected 3 nodes (producer, sink, completion)";
}

TEST_F(ProducerTopologiesTest, MinimalDoubleProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(3, nodes.size());
    ASSERT_EQ(2, graph->GetEdges().size());
    ExpectNodeTypes(graph, {"TestDoubleProducer", "TestDoubleSinkNode", "CompletionNode"});
    ExpectEdge(graph, 0, 0, 0, 1, 0);
    ExpectEdge(graph, 1, 0, 1, 2, 0);
}

TEST_F(ProducerTopologiesTest, MinimalDoubleProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalDoubleProducer);
    ASSERT_NE(nullptr, graph);
    ConfigureCompletionGateForDoubleSink(graph, 1, 2, 10);
    ExecuteSuccessfully(graph);
    ExpectDoubleDataFlow(graph, 1, {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0});
    ExpectCompletion(graph, 2);
}

// =============================================================================
// LinearSequentialDoubleProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(4, nodes.size()) << "Expected 4 nodes (producer, interior, sink, completion)";
}

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(4, nodes.size());
    ASSERT_EQ(3, graph->GetEdges().size());
    ExpectNodeTypes(graph, {"TestDoubleProducer", "InteriorTestNode", "TestDoubleSinkNode", "CompletionNode"});
    ExpectEdge(graph, 0, 0, 0, 1, 0);
    ExpectEdge(graph, 1, 1, 0, 2, 0);
    ExpectEdge(graph, 2, 0, 1, 3, 0);
}

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    ConfigureCompletionGateForDoubleSink(graph, 2, 3, 10);
    ExecuteSuccessfully(graph);
    ExpectDoubleDataFlow(graph, 2, {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0});
    ExpectCompletion(graph, 3);
}

// =============================================================================
// Metadata Tests (4 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, ProducerTopologies_MetadataAvailable) {
    auto int_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalIntProducer);
    EXPECT_EQ(3, int_minimal.expected_node_count);
    EXPECT_EQ(2, int_minimal.expected_edge_count);
    
    auto int_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialIntProducer);
    EXPECT_EQ(4, int_sequential.expected_node_count);
    EXPECT_EQ(3, int_sequential.expected_edge_count);
    
    auto double_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalDoubleProducer);
    EXPECT_EQ(3, double_minimal.expected_node_count);
    EXPECT_EQ(2, double_minimal.expected_edge_count);
    
    auto double_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialDoubleProducer);
    EXPECT_EQ(4, double_sequential.expected_node_count);
    EXPECT_EQ(3, double_sequential.expected_edge_count);
}

TEST_F(ProducerTopologiesTest, ProducerTopologies_NamesCorrect) {
    auto int_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalIntProducer);
    EXPECT_EQ("MinimalIntProducer", int_minimal.name);
    
    auto int_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialIntProducer);
    EXPECT_EQ("LinearSequentialIntProducer", int_sequential.name);
    
    auto double_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalDoubleProducer);
    EXPECT_EQ("MinimalDoubleProducer", double_minimal.name);
    
    auto double_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialDoubleProducer);
    EXPECT_EQ("LinearSequentialDoubleProducer", double_sequential.name);
}

TEST_F(ProducerTopologiesTest, ProducerTopologies_DescriptionsNonEmpty) {
    auto int_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalIntProducer);
    EXPECT_FALSE(int_minimal.description.empty());
    
    auto int_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialIntProducer);
    EXPECT_FALSE(int_sequential.description.empty());
    
    auto double_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalDoubleProducer);
    EXPECT_FALSE(double_minimal.description.empty());
    
    auto double_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialDoubleProducer);
    EXPECT_FALSE(double_sequential.description.empty());
}

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
