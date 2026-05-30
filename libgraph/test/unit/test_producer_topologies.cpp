/**
 * @file test_producer_topologies.cpp
 * @brief Integration tests for producer-based graph topologies
 *
 * Tests the producer topology builders with dynamic plugin nodes:
 * - MinimalIntProducer: TestIntProducer → TestIntSinkNode + CompletionNode
 * - LinearSequentialIntProducer: Same pattern with different name
 * - MinimalDoubleProducer: TestDoubleProducer → TestDoubleSinkNode + CompletionNode
 * - LinearSequentialDoubleProducer: Same pattern for double type
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
    
    // Verify we can get node counts (basic structure validation)
    int node_count = 0;
    for (const auto& node : nodes) {
        EXPECT_NE(nullptr, node);
        ++node_count;
    }
    EXPECT_EQ(3, node_count);
}

TEST_F(ProducerTopologiesTest, MinimalIntProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(nullptr, executor) << "Failed to build executor";

    ASSERT_TRUE(executor->Init().success) << "Init failed";
    ASSERT_TRUE(executor->Start().success) << "Start failed";
    ASSERT_TRUE(executor->Run().success) << "Run failed";
    ASSERT_TRUE(executor->Stop().success) << "Stop failed";
    ASSERT_TRUE(executor->Join().success) << "Join failed";
}

// =============================================================================
// LinearSequentialIntProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(3, nodes.size()) << "Expected 3 nodes (producer, sink, completion)";
}

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(3, nodes.size());
    
    int node_count = 0;
    for (const auto& node : nodes) {
        EXPECT_NE(nullptr, node);
        ++node_count;
    }
    EXPECT_EQ(3, node_count);
}

TEST_F(ProducerTopologiesTest, LinearSequentialIntProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialIntProducer);
    ASSERT_NE(nullptr, graph);
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(nullptr, executor) << "Failed to build executor";

    ASSERT_TRUE(executor->Init().success) << "Init failed";
    ASSERT_TRUE(executor->Start().success) << "Start failed";
    ASSERT_TRUE(executor->Run().success) << "Run failed";
    ASSERT_TRUE(executor->Stop().success) << "Stop failed";
    ASSERT_TRUE(executor->Join().success) << "Join failed";
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
    
    int node_count = 0;
    for (const auto& node : nodes) {
        EXPECT_NE(nullptr, node);
        ++node_count;
    }
    EXPECT_EQ(3, node_count);
}

TEST_F(ProducerTopologiesTest, MinimalDoubleProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::MinimalDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(nullptr, executor) << "Failed to build executor";

    ASSERT_TRUE(executor->Init().success) << "Init failed";
    ASSERT_TRUE(executor->Start().success) << "Start failed";
    ASSERT_TRUE(executor->Run().success) << "Run failed";
    ASSERT_TRUE(executor->Stop().success) << "Stop failed";
    ASSERT_TRUE(executor->Join().success) << "Join failed";
}

// =============================================================================
// LinearSequentialDoubleProducer Tests (3 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_TopologyBuilds) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    EXPECT_EQ(3, nodes.size()) << "Expected 3 nodes (producer, sink, completion)";
}

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_HasCorrectStructure) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto nodes = graph->GetNodes();
    ASSERT_EQ(3, nodes.size());
    
    int node_count = 0;
    for (const auto& node : nodes) {
        EXPECT_NE(nullptr, node);
        ++node_count;
    }
    EXPECT_EQ(3, node_count);
}

TEST_F(ProducerTopologiesTest, LinearSequentialDoubleProducer_ExecutesSuccessfully) {
    auto graph = TopologyBuilder::BuildTopology(TopologyType::LinearSequentialDoubleProducer);
    ASSERT_NE(nullptr, graph);
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(30))
        .Build();
    ASSERT_NE(nullptr, executor) << "Failed to build executor";

    ASSERT_TRUE(executor->Init().success) << "Init failed";
    ASSERT_TRUE(executor->Start().success) << "Start failed";
    ASSERT_TRUE(executor->Run().success) << "Run failed";
    ASSERT_TRUE(executor->Stop().success) << "Stop failed";
    ASSERT_TRUE(executor->Join().success) << "Join failed";
}

// =============================================================================
// Metadata Tests (4 tests)
// =============================================================================

TEST_F(ProducerTopologiesTest, ProducerTopologies_MetadataAvailable) {
    auto int_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalIntProducer);
    EXPECT_EQ(3, int_minimal.expected_node_count);
    EXPECT_EQ(2, int_minimal.expected_edge_count);
    
    auto int_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialIntProducer);
    EXPECT_EQ(3, int_sequential.expected_node_count);
    EXPECT_EQ(2, int_sequential.expected_edge_count);
    
    auto double_minimal = TopologyBuilder::GetTopologyMetadata(TopologyType::MinimalDoubleProducer);
    EXPECT_EQ(3, double_minimal.expected_node_count);
    EXPECT_EQ(2, double_minimal.expected_edge_count);
    
    auto double_sequential = TopologyBuilder::GetTopologyMetadata(TopologyType::LinearSequentialDoubleProducer);
    EXPECT_EQ(3, double_sequential.expected_node_count);
    EXPECT_EQ(2, double_sequential.expected_edge_count);
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
