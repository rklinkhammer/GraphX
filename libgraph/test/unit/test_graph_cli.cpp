#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "graph/GraphCli.hpp"

namespace fs = std::filesystem;

class GraphCliTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test data directory
        test_dir_ = "/tmp/graphx_cli_test_" + std::to_string(std::time(nullptr));
        fs::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // Clean up test files
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }
    
    /// Create a simple test graph file
    std::string CreateTestGraphFile(const std::string& filename) {
        nlohmann::json graph;
        graph["nodes"] = nlohmann::json::array();
        
        nlohmann::json node1;
        node1["id"] = "node_1";
        node1["type"] = "AudioFilter";
        node1["node_config"]["frequency"] = 1000;
        node1["node_config"]["gain"] = 0.5;
        graph["nodes"].push_back(node1);
        
        nlohmann::json node2;
        node2["id"] = "node_2";
        node2["type"] = "AudioEffect";
        node2["node_config"]["effect_type"] = "reverb";
        node2["node_config"]["wet"] = 0.3;
        graph["nodes"].push_back(node2);
        
        std::string filepath = test_dir_ + "/" + filename;
        std::ofstream file(filepath);
        file << std::setw(2) << graph << std::endl;
        file.close();
        
        return filepath;
    }
    
    std::string test_dir_;
    graph::GraphCli cli_;
};

// Test 1: Load valid graph file
TEST_F(GraphCliTest, LoadValidGraphFile) {
    std::string filepath = CreateTestGraphFile("valid_graph.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    EXPECT_TRUE(cli_.IsGraphLoaded());
    
    const auto& graph = cli_.GetGraphJson();
    EXPECT_TRUE(graph.contains("nodes"));
    EXPECT_EQ(graph["nodes"].size(), 2);
    EXPECT_EQ(graph["nodes"][0]["id"], "node_1");
}

// Test 2: Load non-existent file returns error
TEST_F(GraphCliTest, LoadNonExistentFileReturnsError) {
    EXPECT_FALSE(cli_.LoadGraph("/nonexistent/path/graph.json"));
    EXPECT_FALSE(cli_.IsGraphLoaded());
}

// Test 3: Save graph to file
TEST_F(GraphCliTest, SaveGraphToFile) {
    std::string load_path = CreateTestGraphFile("original_graph.json");
    std::string save_path = test_dir_ + "/saved_graph.json";
    
    EXPECT_TRUE(cli_.LoadGraph(load_path));
    EXPECT_TRUE(cli_.SaveGraph(save_path));
    EXPECT_TRUE(fs::exists(save_path));
    
    // Verify saved file contains valid JSON
    std::ifstream file(save_path);
    nlohmann::json saved_graph;
    file >> saved_graph;
    
    EXPECT_TRUE(saved_graph.contains("nodes"));
    EXPECT_EQ(saved_graph["nodes"].size(), 2);
}

// Test 4: List all nodes
TEST_F(GraphCliTest, ListAllNodes) {
    std::string filepath = CreateTestGraphFile("graph_for_list.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    std::string result = cli_.ListNodes("", "table");
    // Verify we got a non-empty result
    EXPECT_FALSE(result.empty());
    // Verify no error message
    EXPECT_EQ(result.find("Error"), std::string::npos);
    // Verify we have node count info
    EXPECT_NE(result.find("Nodes"), std::string::npos);
}

// Test 5: Get single node
TEST_F(GraphCliTest, GetSingleNode) {
    std::string filepath = CreateTestGraphFile("graph_for_get.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    std::string result = cli_.GetNode("node_1", "table");
    EXPECT_NE(result.find("node_1"), std::string::npos);
    EXPECT_NE(result.find("AudioFilter"), std::string::npos);
    EXPECT_NE(result.find("1000"), std::string::npos);
}

// Test 6: Get node not found
TEST_F(GraphCliTest, GetNodeNotFound) {
    std::string filepath = CreateTestGraphFile("graph_for_get_notfound.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    std::string result = cli_.GetNode("nonexistent_node", "table");
    EXPECT_NE(result.find("Error"), std::string::npos);
    EXPECT_NE(result.find("not found"), std::string::npos);
}

// Test 7: Update node configuration
TEST_F(GraphCliTest, UpdateNodeConfiguration) {
    std::string filepath = CreateTestGraphFile("graph_for_update.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    nlohmann::json new_config;
    new_config["frequency"] = 2000;
    new_config["gain"] = 0.75;
    
    EXPECT_TRUE(cli_.UpdateNode("node_1", new_config));
    
    // Verify the update persisted
    std::string result = cli_.GetNode("node_1", "json");
    EXPECT_NE(result.find("2000"), std::string::npos);
    EXPECT_NE(result.find("0.75"), std::string::npos);
}

// Test 8: Show graph as table
TEST_F(GraphCliTest, ShowGraphAsTable) {
    std::string filepath = CreateTestGraphFile("graph_for_show_table.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    std::string result = cli_.ShowGraph("table");
    EXPECT_NE(result.find("Nodes"), std::string::npos);
    EXPECT_NE(result.find("ID"), std::string::npos);
    EXPECT_NE(result.find("Type"), std::string::npos);
    EXPECT_NE(result.find("node_1"), std::string::npos);
}

// Test 9: Show graph as JSON
TEST_F(GraphCliTest, ShowGraphAsJson) {
    std::string filepath = CreateTestGraphFile("graph_for_show_json.json");
    
    EXPECT_TRUE(cli_.LoadGraph(filepath));
    
    std::string result = cli_.ShowGraph("json");
    try {
        nlohmann::json parsed = nlohmann::json::parse(result);
        EXPECT_TRUE(parsed.contains("nodes"));
    } catch (const std::exception& e) {
        FAIL() << "Failed to parse JSON: " << e.what();
    }
}

// Test 10: Save graph persists edits
TEST_F(GraphCliTest, SaveGraphPersistsEdits) {
    std::string load_path = CreateTestGraphFile("graph_for_persist.json");
    std::string save_path = test_dir_ + "/edited_graph.json";
    
    // Load, update, and save
    EXPECT_TRUE(cli_.LoadGraph(load_path));
    
    nlohmann::json new_config;
    new_config["frequency"] = 5000;
    EXPECT_TRUE(cli_.UpdateNode("node_1", new_config));
    
    EXPECT_TRUE(cli_.SaveGraph(save_path));
    
    // Reload and verify edit persisted
    graph::GraphCli cli2;
    EXPECT_TRUE(cli2.LoadGraph(save_path));
    
    std::string node_result = cli2.GetNode("node_1", "json");
    nlohmann::json node = nlohmann::json::parse(node_result);
    EXPECT_EQ(node["node_config"]["frequency"], 5000);
}

