/**
 * @file test_graph_coordinator.cpp
 * @brief Unit tests for GraphCoordinator - generic graph parameter editor.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

#include "graph/GraphCoordinator.hpp"

namespace {

using json = nlohmann::json;

/**
 * @brief Test fixture for GraphCoordinator tests.
 */
class GraphCoordinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a valid test graph
        graph_ = json::object();
        graph_["name"] = "test_graph";
        graph_["nodes"] = json::array();

        // Add test nodes
        json node1 = json::object();
        node1["id"] = "node_1";
        node1["type"] = "SourceNode";
        node1["node_config"] = json::object({{"param1", 10}, {"param2", "test"}});
        graph_["nodes"].push_back(node1);

        json node2 = json::object();
        node2["id"] = "node_2";
        node2["type"] = "ProcessNode";
        node2["node_config"] = json::object({{"param1", 20}});
        graph_["nodes"].push_back(node2);

        json node3 = json::object();
        node3["id"] = "node_3";
        node3["type"] = "SourceNode";
        node3["node_config"] = json::object({{"param1", 30}});
        graph_["nodes"].push_back(node3);
    }

    json graph_;
};

// ========== GetNode Tests ==========

TEST_F(GraphCoordinatorTest, GetNodeFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto node = coordinator.GetNode("node_1");

    EXPECT_TRUE(node.is_object());
    EXPECT_EQ(node["id"], "node_1");
    EXPECT_EQ(node["type"], "SourceNode");
    EXPECT_EQ(node["node_config"]["param1"], 10);
}

TEST_F(GraphCoordinatorTest, GetNodeNotFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto node = coordinator.GetNode("nonexistent");

    EXPECT_TRUE(node.is_null());
}

TEST_F(GraphCoordinatorTest, GetNodeReturnsCorpyNotReference) {
    graph::GraphCoordinator coordinator(graph_);
    auto node = coordinator.GetNode("node_1");

    // Modify the returned copy
    node["node_config"]["param1"] = 999;

    // Original should be unchanged
    auto original = coordinator.GetNode("node_1");
    EXPECT_EQ(original["node_config"]["param1"], 10);
}

// ========== GetNodeConfig Tests ==========

TEST_F(GraphCoordinatorTest, GetNodeConfigFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto config = coordinator.GetNodeConfig("node_1");

    EXPECT_TRUE(config.is_object());
    EXPECT_EQ(config["param1"], 10);
    EXPECT_EQ(config["param2"], "test");
}

TEST_F(GraphCoordinatorTest, GetNodeConfigNotFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto config = coordinator.GetNodeConfig("nonexistent");

    EXPECT_TRUE(config.is_null());
}

// ========== GetNodesByType Tests ==========

TEST_F(GraphCoordinatorTest, GetNodesByTypeMatching) {
    graph::GraphCoordinator coordinator(graph_);
    auto nodes = coordinator.GetNodesByType("SourceNode");

    EXPECT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[0]["id"], "node_1");
    EXPECT_EQ(nodes[1]["id"], "node_3");
}

TEST_F(GraphCoordinatorTest, GetNodesByTypeSingleMatch) {
    graph::GraphCoordinator coordinator(graph_);
    auto nodes = coordinator.GetNodesByType("ProcessNode");

    EXPECT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0]["id"], "node_2");
}

TEST_F(GraphCoordinatorTest, GetNodesByTypeNoMatches) {
    graph::GraphCoordinator coordinator(graph_);
    auto nodes = coordinator.GetNodesByType("NonexistentType");

    EXPECT_EQ(nodes.size(), 0);
}

TEST_F(GraphCoordinatorTest, GetNodesByTypeReturnscopies) {
    graph::GraphCoordinator coordinator(graph_);
    auto nodes = coordinator.GetNodesByType("SourceNode");

    // Modify a returned copy
    nodes[0]["node_config"]["param1"] = 999;

    // Original should be unchanged
    auto original_nodes = coordinator.GetNodesByType("SourceNode");
    EXPECT_EQ(original_nodes[0]["node_config"]["param1"], 10);
}

// ========== GetNodeIds Tests ==========

TEST_F(GraphCoordinatorTest, GetNodeIdsSingleNode) {
    json graph = json::object();
    graph["nodes"] = json::array();
    json node = json::object({{"id", "single"}, {"type", "Test"}});
    graph["nodes"].push_back(node);

    graph::GraphCoordinator coordinator(graph);
    auto ids = coordinator.GetNodeIds();

    EXPECT_EQ(ids.size(), 1);
    EXPECT_EQ(ids[0], "single");
}

TEST_F(GraphCoordinatorTest, GetNodeIdsMultipleNodes) {
    graph::GraphCoordinator coordinator(graph_);
    auto ids = coordinator.GetNodeIds();

    EXPECT_EQ(ids.size(), 3);
    EXPECT_EQ(ids[0], "node_1");
    EXPECT_EQ(ids[1], "node_2");
    EXPECT_EQ(ids[2], "node_3");
}

TEST_F(GraphCoordinatorTest, GetNodeIdsEmpty) {
    json empty_graph = json::object();
    empty_graph["nodes"] = json::array();

    graph::GraphCoordinator coordinator(empty_graph);
    auto ids = coordinator.GetNodeIds();

    EXPECT_EQ(ids.size(), 0);
}

TEST_F(GraphCoordinatorTest, GetNodeIdsNoNodesField) {
    json graph = json::object();
    // Intentionally no "nodes" field

    graph::GraphCoordinator coordinator(graph);
    auto ids = coordinator.GetNodeIds();

    EXPECT_EQ(ids.size(), 0);
}

// ========== GetNodeCount Tests ==========

TEST_F(GraphCoordinatorTest, GetNodeCountMultiple) {
    graph::GraphCoordinator coordinator(graph_);
    EXPECT_EQ(coordinator.GetNodeCount(), 3);
}

TEST_F(GraphCoordinatorTest, GetNodeCountEmpty) {
    json empty_graph = json::object();
    empty_graph["nodes"] = json::array();

    graph::GraphCoordinator coordinator(empty_graph);
    EXPECT_EQ(coordinator.GetNodeCount(), 0);
}

TEST_F(GraphCoordinatorTest, GetNodeCountNoNodesField) {
    json graph = json::object();

    graph::GraphCoordinator coordinator(graph);
    EXPECT_EQ(coordinator.GetNodeCount(), 0);
}

TEST_F(GraphCoordinatorTest, GetNodeCountNodesNotArray) {
    json graph = json::object();
    graph["nodes"] = json::object();  // Not an array

    graph::GraphCoordinator coordinator(graph);
    EXPECT_EQ(coordinator.GetNodeCount(), 0);
}

// ========== HasNode Tests ==========

TEST_F(GraphCoordinatorTest, HasNodeTrue) {
    graph::GraphCoordinator coordinator(graph_);
    EXPECT_TRUE(coordinator.HasNode("node_1"));
}

TEST_F(GraphCoordinatorTest, HasNodeFalse) {
    graph::GraphCoordinator coordinator(graph_);
    EXPECT_FALSE(coordinator.HasNode("nonexistent"));
}

TEST_F(GraphCoordinatorTest, HasNodeEmpty) {
    json empty_graph = json::object();
    empty_graph["nodes"] = json::array();

    graph::GraphCoordinator coordinator(empty_graph);
    EXPECT_FALSE(coordinator.HasNode("any_id"));
}

// ========== GetGraphJson Tests ==========

TEST_F(GraphCoordinatorTest, GetGraphJsonStructurePreserved) {
    graph::GraphCoordinator coordinator(graph_);
    auto graph_copy = coordinator.GetGraphJson();

    EXPECT_EQ(graph_copy["name"], "test_graph");
    EXPECT_EQ(graph_copy["nodes"].size(), 3);
    EXPECT_EQ(graph_copy["nodes"][0]["id"], "node_1");
}

TEST_F(GraphCoordinatorTest, GetGraphJsonReturnscopy) {
    graph::GraphCoordinator coordinator(graph_);
    auto graph_copy = coordinator.GetGraphJson();

    // Modify the copy
    graph_copy["name"] = "modified";

    // Original should be unchanged
    auto original = coordinator.GetGraphJson();
    EXPECT_EQ(original["name"], "test_graph");
}

// ========== UpdateNodeConfig Tests ==========

TEST_F(GraphCoordinatorTest, UpdateNodeConfigSuccessful) {
    graph::GraphCoordinator coordinator(graph_);

    json new_config = json::object({{"param1", 100}, {"param2", "updated"}});
    bool result = coordinator.UpdateNodeConfig("node_1", new_config);

    EXPECT_TRUE(result);

    auto updated = coordinator.GetNodeConfig("node_1");
    EXPECT_EQ(updated["param1"], 100);
    EXPECT_EQ(updated["param2"], "updated");
}

TEST_F(GraphCoordinatorTest, UpdateNodeConfigNotFound) {
    graph::GraphCoordinator coordinator(graph_);

    json new_config = json::object({{"param1", 100}});
    bool result = coordinator.UpdateNodeConfig("nonexistent", new_config);

    EXPECT_FALSE(result);
}

TEST_F(GraphCoordinatorTest, UpdateNodeConfigModifiesReferencedGraph) {
    graph::GraphCoordinator coordinator(graph_);

    json new_config = json::object({{"new_param", 555}});
    coordinator.UpdateNodeConfig("node_2", new_config);

    // Check that underlying graph was modified
    EXPECT_EQ(graph_["nodes"][1]["node_config"]["new_param"], 555);
}

TEST_F(GraphCoordinatorTest, UpdateNodeConfigPreservesOtherNodes) {
    graph::GraphCoordinator coordinator(graph_);

    json new_config = json::object({{"param1", 100}});
    coordinator.UpdateNodeConfig("node_1", new_config);

    // node_2 should be unchanged
    auto node2_config = coordinator.GetNodeConfig("node_2");
    EXPECT_EQ(node2_config["param1"], 20);
}

TEST_F(GraphCoordinatorTest, UpdateNodeConfigReplacesEntireConfig) {
    graph::GraphCoordinator coordinator(graph_);

    // Original node_1 has param1 and param2
    auto before = coordinator.GetNodeConfig("node_1");
    EXPECT_TRUE(before.contains("param1"));
    EXPECT_TRUE(before.contains("param2"));

    // Update with only new_param
    json new_config = json::object({{"new_param", 999}});
    coordinator.UpdateNodeConfig("node_1", new_config);

    // Verify entire config was replaced
    auto after = coordinator.GetNodeConfig("node_1");
    EXPECT_TRUE(after.contains("new_param"));
    EXPECT_FALSE(after.contains("param1"));
    EXPECT_FALSE(after.contains("param2"));
}

// ========== Edge Cases ==========

TEST_F(GraphCoordinatorTest, EmptyGraphHandling) {
    json empty_graph = json::object();
    empty_graph["nodes"] = json::array();

    graph::GraphCoordinator coordinator(empty_graph);

    EXPECT_EQ(coordinator.GetNodeCount(), 0);
    EXPECT_EQ(coordinator.GetNodeIds().size(), 0);
    EXPECT_TRUE(coordinator.GetGraphJson()["nodes"].is_array());
    EXPECT_FALSE(coordinator.HasNode("any_id"));
    EXPECT_TRUE(coordinator.GetNode("any_id").is_null());
}

TEST_F(GraphCoordinatorTest, MalformedNodesMisssingId) {
    json graph = json::object();
    graph["nodes"] = json::array();

    json bad_node = json::object();
    bad_node["type"] = "SomeType";
    bad_node["node_config"] = json::object();
    // Missing "id" field
    graph["nodes"].push_back(bad_node);

    graph::GraphCoordinator coordinator(graph);

    // Should skip malformed node
    EXPECT_EQ(coordinator.GetNodeCount(), 1);  // Count still includes it
    EXPECT_EQ(coordinator.GetNodeIds().size(), 0);  // But ID extraction skips it
    EXPECT_FALSE(coordinator.HasNode(""));
}

TEST_F(GraphCoordinatorTest, NodesFieldNotArray) {
    json graph = json::object();
    graph["nodes"] = json::object();  // Not an array

    graph::GraphCoordinator coordinator(graph);

    EXPECT_EQ(coordinator.GetNodeCount(), 0);
    EXPECT_EQ(coordinator.GetNodeIds().size(), 0);
    EXPECT_TRUE(coordinator.GetGraphJson()["nodes"].is_object());
}

// ========== Thread Safety Tests ==========

TEST_F(GraphCoordinatorTest, ConcurrentReads) {
    graph::GraphCoordinator coordinator(graph_);

    std::vector<std::thread> threads;
    std::vector<size_t> results;
    std::mutex results_lock;

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&coordinator, &results, &results_lock]() {
            auto count = coordinator.GetNodeCount();
            {
                std::lock_guard<std::mutex> lock(results_lock);
                results.push_back(count);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All reads should see the same count
    for (size_t count : results) {
        EXPECT_EQ(count, 3);
    }
}

TEST_F(GraphCoordinatorTest, ConcurrentReadsAndWrite) {
    graph::GraphCoordinator coordinator(graph_);

    std::vector<std::thread> threads;
    bool write_success = false;

    // Spawn reader threads
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&coordinator]() {
            for (int j = 0; j < 10; ++j) {
                auto count = coordinator.GetNodeCount();
                EXPECT_GE(count, 3);
            }
        });
    }

    // Spawn writer thread
    threads.emplace_back([&coordinator, &write_success]() {
        json new_config = json::object({{"param", 777}});
        write_success = coordinator.UpdateNodeConfig("node_2", new_config);
    });

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_TRUE(write_success);
    auto config = coordinator.GetNodeConfig("node_2");
    EXPECT_EQ(config["param"], 777);
}

TEST_F(GraphCoordinatorTest, ConcurrentUpdates) {
    graph::GraphCoordinator coordinator(graph_);

    std::vector<std::thread> threads;

    // Each thread updates a different node
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&coordinator, i]() {
            std::string node_id = "node_" + std::to_string(i + 1);
            json new_config = json::object({{"thread_value", i}});
            coordinator.UpdateNodeConfig(node_id, new_config);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify all updates succeeded
    for (int i = 0; i < 3; ++i) {
        std::string node_id = "node_" + std::to_string(i + 1);
        auto config = coordinator.GetNodeConfig(node_id);
        EXPECT_EQ(config["thread_value"], i);
    }
}

// ========== Non-copyable, non-movable verification ==========

TEST_F(GraphCoordinatorTest, CannotCopyConstruct) {
    graph::GraphCoordinator coordinator(graph_);

    // This should not compile (verified at compile time)
    // graph::GraphCoordinator copy(coordinator);  // Compile error expected
}

TEST_F(GraphCoordinatorTest, CannotMoveConstruct) {
    graph::GraphCoordinator coordinator(graph_);

    // This should not compile (verified at compile time)
    // graph::GraphCoordinator moved(std::move(coordinator));  // Compile error expected
}

}  // namespace
