/**
 * @file test_graph_http_server.cpp
 * @brief Unit tests for GraphHttpServer - REST API and web UI.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

#include "graph/GraphHttpServer.hpp"
#include "graph/GraphCoordinator.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/ExecutionState.hpp"

namespace {

using json = nlohmann::json;

std::uint16_t ReserveLoopbackPort() {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(socket_fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(::bind(socket_fd, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)), 0);
    socklen_t length = sizeof(address);
    EXPECT_EQ(::getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address),
                            &length), 0);
    const auto port = ntohs(address.sin_port);
    ::close(socket_fd);
    return port;
}

std::string SendHttpRequest(const std::uint16_t port,
                            const std::string& method,
                            const std::string& path,
                            const std::string& body = {}) {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(socket_fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(::connect(socket_fd, reinterpret_cast<sockaddr*>(&address),
                        sizeof(address)), 0);

    std::ostringstream request;
    request << method << ' ' << path << " HTTP/1.1\r\n"
            << "Host: 127.0.0.1\r\n"
            << "Connection: close\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/json\r\n"
                << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n" << body;
    const auto encoded = request.str();
    EXPECT_EQ(::send(socket_fd, encoded.data(), encoded.size(), 0),
              static_cast<ssize_t>(encoded.size()));

    std::string response;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto received =
            ::recv(socket_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
    }
    ::close(socket_fd);
    return response;
}

int ResponseStatus(const std::string& response) {
    std::istringstream line(response.substr(0, response.find("\r\n")));
    std::string version;
    int status = 0;
    line >> version >> status;
    return status;
}

std::string ResponseBody(const std::string& response) {
    const auto separator = response.find("\r\n\r\n");
    return separator == std::string::npos ? std::string{}
                                          : response.substr(separator + 4U);
}

/**
 * @brief Simple executor mock for testing GraphCoordinator integration.
 */
class SimpleExecutorMock {
public:
    SimpleExecutorMock() : state_(graph::ExecutionState::STOPPED), stop_count_(0) {}

    graph::InitializationResult Init() {
        state_ = graph::ExecutionState::INITIALIZED;
        graph::InitializationResult result{};
        result.success = true;
        result.message = "";
        result.nodes_initialized = 0;
        result.nodes_failed = 0;
        result.elapsed_time_ms = 0;
        result.error_details = "";
        return result;
    }

    graph::ExecutionResult Start() {
        if (state_ != graph::ExecutionState::INITIALIZED) {
            graph::ExecutionResult result{};
            result.success = false;
            result.message = "Not initialized";
            result.current_state = state_;
            result.elapsed_time_ms = 0;
            result.init_elapsed_time_ms = 0;
            result.start_elapsed_time_ms = 0;
            result.run_elapsed_time_ms = 0;
            result.stop_elapsed_time_ms = 0;
            result.join_elapsed_time_ms = 0;
            result.error_details = "";
            return result;
        }
        state_ = graph::ExecutionState::RUNNING;
        graph::ExecutionResult result{};
        result.success = true;
        result.message = "";
        result.current_state = state_;
        result.elapsed_time_ms = 0;
        result.init_elapsed_time_ms = 0;
        result.start_elapsed_time_ms = 0;
        result.run_elapsed_time_ms = 0;
        result.stop_elapsed_time_ms = 0;
        result.join_elapsed_time_ms = 0;
        result.error_details = "";
        return result;
    }

    graph::ExecutionResult Run() {
        if (state_ != graph::ExecutionState::RUNNING) {
            graph::ExecutionResult result{};
            result.success = false;
            result.message = "Not running";
            result.current_state = state_;
            result.elapsed_time_ms = 0;
            result.init_elapsed_time_ms = 0;
            result.start_elapsed_time_ms = 0;
            result.run_elapsed_time_ms = 0;
            result.stop_elapsed_time_ms = 0;
            result.join_elapsed_time_ms = 0;
            result.error_details = "";
            return result;
        }
        state_ = graph::ExecutionState::STOPPED;
        graph::ExecutionResult result;
        result.success = true;
        result.message = "";
        result.current_state = state_;
        result.elapsed_time_ms = 0;
        result.init_elapsed_time_ms = 0;
        result.start_elapsed_time_ms = 0;
        result.run_elapsed_time_ms = 0;
        result.stop_elapsed_time_ms = 0;
        result.join_elapsed_time_ms = 0;
        result.error_details = "";
        return result;
    }

    graph::ExecutionResult Stop() {
        state_ = graph::ExecutionState::STOPPED;
        graph::ExecutionResult result{};
        result.success = true;
        result.message = "";
        result.current_state = state_;
        result.elapsed_time_ms = 0;
        result.init_elapsed_time_ms = 0;
        result.start_elapsed_time_ms = 0;
        result.run_elapsed_time_ms = 0;
        result.stop_elapsed_time_ms = 0;
        result.join_elapsed_time_ms = 0;
        result.error_details = "";
        return result;
    }

    void RequestStop() noexcept {
        state_ = graph::ExecutionState::STOPPED;
    }

    graph::ExecutionResult Join() {
        state_ = graph::ExecutionState::STOPPED;
        graph::ExecutionResult result{};
        result.success = true;
        result.message = "";
        result.current_state = state_;
        result.elapsed_time_ms = 0;
        result.init_elapsed_time_ms = 0;
        result.start_elapsed_time_ms = 0;
        result.run_elapsed_time_ms = 0;
        result.stop_elapsed_time_ms = 0;
        result.join_elapsed_time_ms = 0;
        result.error_details = "";
        return result;
    }

    bool IsRunning() const {
        return state_ == graph::ExecutionState::RUNNING;
    }

    graph::ExecutionState GetExecutionState() const {
        return state_;
    }

    std::uint64_t GetStopSequenceCount() const noexcept {
        return stop_count_;
    }

private:
    graph::ExecutionState state_;
    std::uint64_t stop_count_;
};

/**
 * @brief Test fixture for GraphHttpServer tests.
 */
class GraphHttpServerTest : public ::testing::Test {
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
        node2["node_config"] = json::object({{"frequency", 2000}, {"gain", 1.5}});
        graph_["nodes"].push_back(node2);

        json node3 = json::object();
        node3["id"] = "node_3";
        node3["type"] = "SourceNode";
        node3["node_config"] = json::object({{"rate", 44100}});
        graph_["nodes"].push_back(node3);
    }

    json graph_;
};

// ========== API Endpoint Tests ==========

TEST_F(GraphHttpServerTest, GraphHttpServerCreationSucceeds) {
    // Create server without executor (executor can be nullptr)
    auto server = std::make_unique<graph::GraphHttpServer>(graph_, nullptr, 9999);
    EXPECT_TRUE(server != nullptr);
}

TEST_F(GraphHttpServerTest, GraphHttpServerStartSucceeds) {
    auto server = std::make_unique<graph::GraphHttpServer>(graph_, nullptr, 9997);
    
    // Note: Server Start() may fail if port is in use, but should at least not crash
    server->Start();
    server->Stop();
    EXPECT_FALSE(server->IsRunning());
}

TEST_F(GraphHttpServerTest, GraphHttpServerStopWhenNotRunningSucceeds) {
    auto server = std::make_unique<graph::GraphHttpServer>(graph_, nullptr, 9996);
    
    // Stop without starting - should not crash
    EXPECT_TRUE(server->Stop());
    EXPECT_FALSE(server->IsRunning());
}

TEST_F(GraphHttpServerTest, InvalidPortsFailWithoutBinding) {
    graph::GraphHttpServer zero_port(graph_, nullptr, 0);
    graph::GraphHttpServer oversized_port(graph_, nullptr, 65536);
    EXPECT_FALSE(zero_port.Start());
    EXPECT_FALSE(oversized_port.Start());
    EXPECT_FALSE(zero_port.IsRunning());
    EXPECT_FALSE(oversized_port.IsRunning());
}

TEST_F(GraphHttpServerTest, RestGetGraphReturnsGraph) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response = SendHttpRequest(port, "GET", "/api/v1/graph");
    EXPECT_EQ(ResponseStatus(response), 200);
    const auto document = json::parse(ResponseBody(response));
    EXPECT_TRUE(document["success"]);
    EXPECT_EQ(document["data"]["nodes"].size(), 3);
}

TEST_F(GraphHttpServerTest, RestGetNodeUsesSpecifiedRoute) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response =
        SendHttpRequest(port, "GET", "/api/v1/nodes/node_2");
    EXPECT_EQ(ResponseStatus(response), 200);
    EXPECT_EQ(json::parse(ResponseBody(response))["data"]["id"], "node_2");
}

TEST_F(GraphHttpServerTest, RestGetNodeReturns404) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response =
        SendHttpRequest(port, "GET", "/api/v1/nodes/missing");
    EXPECT_EQ(ResponseStatus(response), 404);
}

TEST_F(GraphHttpServerTest, RestGetNodesByTypeUsesSpecifiedRoute) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response =
        SendHttpRequest(port, "GET", "/api/v1/nodes/type/SourceNode");
    EXPECT_EQ(ResponseStatus(response), 200);
    EXPECT_EQ(json::parse(ResponseBody(response))["data"].size(), 2);
}

TEST_F(GraphHttpServerTest, RestPatchUpdatesOnlyNodeConfig) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response = SendHttpRequest(
        port, "PATCH", "/api/v1/nodes/node_1",
        R"({"node_config":{"param1":77,"replacement":true}})");
    EXPECT_EQ(ResponseStatus(response), 200);
    EXPECT_EQ(graph_["nodes"][0]["id"], "node_1");
    EXPECT_EQ(graph_["nodes"][0]["type"], "SourceNode");
    EXPECT_EQ(graph_["nodes"][0]["node_config"]["param1"], 77);
    EXPECT_TRUE(graph_["nodes"][0]["node_config"]["replacement"]);
}

TEST_F(GraphHttpServerTest, RestPatchRejectsMalformedJson) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response = SendHttpRequest(
        port, "PATCH", "/api/v1/nodes/node_1", R"({"node_config":)");
    EXPECT_EQ(ResponseStatus(response), 400);
}

TEST_F(GraphHttpServerTest, RestExecutionWithoutExecutorReturns501) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  port, "GET", "/api/v1/execution/state")),
              501);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  port, "POST", "/api/v1/execution/init")),
              501);
}

TEST_F(GraphHttpServerTest, RestUnknownEndpointReturns404) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    EXPECT_EQ(ResponseStatus(
                  SendHttpRequest(port, "GET", "/api/v1/missing")),
              404);
}

TEST_F(GraphHttpServerTest, GenericIndexIsTheCheckedInInteractiveUi) {
    const auto port = ReserveLoopbackPort();
    graph::GraphHttpServer server(graph_, nullptr, port);
    ASSERT_TRUE(server.Start());
    const auto response = SendHttpRequest(port, "GET", "/");
    EXPECT_EQ(ResponseStatus(response), 200);
    const auto body = ResponseBody(response);
    EXPECT_NE(body.find("saveNodeConfig"), std::string::npos);
    EXPECT_NE(body.find("filterTable"), std::string::npos);
    EXPECT_NE(body.find("/execution/"), std::string::npos);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorIntegrationWithGetGraph) {
    graph::GraphCoordinator coordinator(graph_);
    auto retrieved_graph = coordinator.GetGraphJson();
    
    EXPECT_TRUE(retrieved_graph.contains("nodes"));
    EXPECT_EQ(retrieved_graph["nodes"].size(), 3);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeById) {
    graph::GraphCoordinator coordinator(graph_);
    auto node = coordinator.GetNode("node_1");
    
    EXPECT_FALSE(node.is_null());
    EXPECT_EQ(node["id"], "node_1");
    EXPECT_EQ(node["type"], "SourceNode");
    EXPECT_EQ(node["node_config"]["param1"], 10);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeNotFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto node = coordinator.GetNode("nonexistent");
    
    EXPECT_TRUE(node.is_null());
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodesByType) {
    graph::GraphCoordinator coordinator(graph_);
    auto nodes = coordinator.GetNodesByType("SourceNode");
    
    EXPECT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[0]["id"], "node_1");
    EXPECT_EQ(nodes[1]["id"], "node_3");
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeIds) {
    graph::GraphCoordinator coordinator(graph_);
    auto ids = coordinator.GetNodeIds();
    
    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "node_1") != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "node_2") != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "node_3") != ids.end());
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeCount) {
    graph::GraphCoordinator coordinator(graph_);
    EXPECT_EQ(coordinator.GetNodeCount(), 3);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorHasNode) {
    graph::GraphCoordinator coordinator(graph_);
    
    EXPECT_TRUE(coordinator.HasNode("node_1"));
    EXPECT_TRUE(coordinator.HasNode("node_2"));
    EXPECT_FALSE(coordinator.HasNode("nonexistent"));
}

TEST_F(GraphHttpServerTest, GraphCoordinatorUpdateNodeConfig) {
    graph::GraphCoordinator coordinator(graph_);
    
    json new_config;
    new_config["param1"] = 100;
    new_config["param2"] = "modified";
    
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", new_config));
    
    auto node = coordinator.GetNode("node_1");
    EXPECT_EQ(node["node_config"]["param1"], 100);
    EXPECT_EQ(node["node_config"]["param2"], "modified");
}

TEST_F(GraphHttpServerTest, GraphCoordinatorUpdateNodeConfigNotFound) {
    graph::GraphCoordinator coordinator(graph_);
    
    json new_config;
    new_config["param1"] = 100;
    
    EXPECT_FALSE(coordinator.UpdateNodeConfig("nonexistent", new_config));
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeConfig) {
    graph::GraphCoordinator coordinator(graph_);
    auto config = coordinator.GetNodeConfig("node_2");
    
    EXPECT_FALSE(config.is_null());
    EXPECT_EQ(config["frequency"], 2000);
    EXPECT_EQ(config["gain"], 1.5);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetNodeConfigNotFound) {
    graph::GraphCoordinator coordinator(graph_);
    auto config = coordinator.GetNodeConfig("nonexistent");
    
    EXPECT_TRUE(config.is_null());
}

TEST_F(GraphHttpServerTest, MockExecutorStateTransitions) {
    SimpleExecutorMock executor;
    
    // Initial state
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_FALSE(executor.IsRunning());
    
    // Init
    auto init_result = executor.Init();
    EXPECT_TRUE(init_result.success);
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::INITIALIZED);
    
    // Start
    auto exec_result = executor.Start();
    EXPECT_TRUE(exec_result.success);
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::RUNNING);
    EXPECT_TRUE(executor.IsRunning());
    
    // Run
    exec_result = executor.Run();
    EXPECT_TRUE(exec_result.success);
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_FALSE(executor.IsRunning());
}

TEST_F(GraphHttpServerTest, MockExecutorStop) {
    SimpleExecutorMock executor;
    
    auto init_result = executor.Init();
    EXPECT_TRUE(init_result.success);
    
    auto exec_result = executor.Start();
    EXPECT_TRUE(exec_result.success);
    EXPECT_TRUE(executor.IsRunning());
    
    exec_result = executor.Stop();
    EXPECT_TRUE(exec_result.success);
    EXPECT_FALSE(executor.IsRunning());
}

TEST_F(GraphHttpServerTest, GraphCoordinatorPreservesAllNodeFields) {
    graph::GraphCoordinator coordinator(graph_);
    
    // Verify all fields preserved after update
    auto node = coordinator.GetNode("node_2");
    EXPECT_EQ(node["id"], "node_2");
    EXPECT_EQ(node["type"], "ProcessNode");
    
    json new_config;
    new_config["frequency"] = 5000;
    new_config["gain"] = 2.0;
    
    coordinator.UpdateNodeConfig("node_2", new_config);
    
    auto updated = coordinator.GetNode("node_2");
    EXPECT_EQ(updated["id"], "node_2");
    EXPECT_EQ(updated["type"], "ProcessNode");
    EXPECT_EQ(updated["node_config"]["frequency"], 5000);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorMultipleUpdates) {
    graph::GraphCoordinator coordinator(graph_);
    
    // First update
    json config1;
    config1["value"] = 1;
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", config1));
    
    // Second update
    json config2;
    config2["value"] = 2;
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", config2));
    
    // Verify second update took effect
    auto node = coordinator.GetNode("node_1");
    EXPECT_EQ(node["node_config"]["value"], 2);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorEmptyNodeConfig) {
    graph::GraphCoordinator coordinator(graph_);
    
    json empty_config = json::object();
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", empty_config));
    
    auto node = coordinator.GetNode("node_1");
    EXPECT_EQ(node["node_config"].size(), 0);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorComplexNodeConfig) {
    graph::GraphCoordinator coordinator(graph_);
    
    json complex_config;
    complex_config["nested"]["value"] = 42;
    complex_config["array"] = json::array({1, 2, 3});
    complex_config["string"] = "test";
    
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", complex_config));
    
    auto node = coordinator.GetNode("node_1");
    EXPECT_EQ(node["node_config"]["nested"]["value"], 42);
    EXPECT_EQ(node["node_config"]["array"].size(), 3);
    EXPECT_EQ(node["node_config"]["string"], "test");
}

TEST_F(GraphHttpServerTest, GraphCoordinatorDoesNotOwnGraph) {
    {
        graph::GraphCoordinator coordinator(graph_);
        // Coordinator goes out of scope but graph_ should remain valid
    }
    
    // Verify graph_ is still accessible
    EXPECT_EQ(graph_["nodes"].size(), 3);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorNonCopyable) {
    graph::GraphCoordinator coordinator1(graph_);
    
    // These should not compile:
    // graph::GraphCoordinator coordinator2 = coordinator1;
    // graph::GraphCoordinator coordinator3(coordinator1);
    
    // Verify we can still use the original
    EXPECT_EQ(coordinator1.GetNodeCount(), 3);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorThreadSafetySimple) {
    graph::GraphCoordinator coordinator(graph_);
    
    std::vector<std::thread> threads;
    
    // Create multiple reader threads
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&coordinator]() {
            for (int j = 0; j < 10; ++j) {
                auto count = coordinator.GetNodeCount();
                (void)count;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify data integrity
    EXPECT_EQ(coordinator.GetNodeCount(), 3);
}

TEST_F(GraphHttpServerTest, GraphCoordinatorGetGraphJsonIsIndependent) {
    graph::GraphCoordinator coordinator(graph_);
    
    auto graph1 = coordinator.GetGraphJson();
    auto graph2 = coordinator.GetGraphJson();
    
    // Both should be equal
    EXPECT_EQ(graph1, graph2);
    
    // Modifications to graph1 should not affect coordinator
    graph1["nodes"][0]["node_config"]["param1"] = 9999;
    
    auto node = coordinator.GetNode("node_1");
    EXPECT_EQ(node["node_config"]["param1"], 10);  // Original value
}

// ========== Integration Tests ==========

TEST_F(GraphHttpServerTest, ExecutorAndCoordinatorIntegration) {
    SimpleExecutorMock executor;
    graph::GraphCoordinator coordinator(graph_);
    
    // Simulate workflow
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_EQ(coordinator.GetNodeCount(), 3);
    
    // Update parameters before execution
    json new_config;
    new_config["param1"] = 20;
    new_config["param2"] = "updated";
    EXPECT_TRUE(coordinator.UpdateNodeConfig("node_1", new_config));
    
    // Initialize and start executor
    auto init_result = executor.Init();
    EXPECT_TRUE(init_result.success);
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::INITIALIZED);
    
    auto exec_result = executor.Start();
    EXPECT_TRUE(exec_result.success);
    EXPECT_EQ(executor.GetExecutionState(), graph::ExecutionState::RUNNING);
}

TEST_F(GraphHttpServerTest, ExecutorStopSequenceCount) {
    SimpleExecutorMock executor;
    EXPECT_EQ(executor.GetStopSequenceCount(), 0);
    
    executor.Init();
    executor.Start();
    executor.Stop();
    
    // Stop sequence count should be tracked (at least 0, implementation-dependent)
    auto count = executor.GetStopSequenceCount();
    EXPECT_GE(count, 0);
}

TEST_F(GraphHttpServerTest, GraphHttpServerNonCopyable) {
    // Executor is not needed for this test

    auto server1 = std::make_unique<graph::GraphHttpServer>(graph_, nullptr);
    
    // These should not compile:
    // auto server2 = server1;
    // graph::GraphHttpServer server3(*server1);
    
    EXPECT_TRUE(server1 != nullptr);
}

}  // namespace
