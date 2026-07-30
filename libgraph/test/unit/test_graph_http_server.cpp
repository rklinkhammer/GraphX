// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "capabilities/CommandCapability.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "graph/GraphCoordinator.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphHttpServer.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

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

json LoadMinimalGraph() {
    std::ifstream input(
        std::string{GRAPHX_SOURCE_ROOT} +
        "/libgraph/test/config/topologies/minimal_graph.json");
    json document;
    input >> document;
    return document;
}

struct HttpBlockingJoinState {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
};

class HttpBlockingJoinPolicy final : public graph::IExecutionPolicy {
public:
    explicit HttpBlockingJoinPolicy(
        std::shared_ptr<HttpBlockingJoinState> state)
        : state_(std::move(state)) {}

    bool OnInit(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnStart(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnRun(capabilities::GraphCapability& context) override {
        context.SetStopped();
        return true;
    }

    void OnJoin(capabilities::GraphCapability&) override {
        std::unique_lock lock(state_->mutex);
        state_->entered = true;
        state_->condition.notify_all();
        state_->condition.wait(lock, [this] { return state_->released; });
    }

private:
    std::shared_ptr<HttpBlockingJoinState> state_;
};

class HttpStopAwareRunPolicy final : public graph::IExecutionPolicy {
public:
    bool OnInit(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnStart(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnRun(capabilities::GraphCapability& context) override {
        while (!context.IsStopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

class HttpFailingRunPolicy final : public graph::IExecutionPolicy {
public:
    bool OnInit(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnStart(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnRun(capabilities::GraphCapability&) override {
        return false;
    }
};

struct PolicyExecutorBundle {
    std::shared_ptr<graph::GraphExecutor> executor;
    std::shared_ptr<capabilities::CommandCapability> commands;
    std::shared_ptr<capabilities::MetricsCapability> metrics;
};

PolicyExecutorBundle BuildPolicyExecutor(
    std::unique_ptr<graph::IExecutionPolicy> policy) {
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto graph_capability =
        std::make_shared<capabilities::GraphCapability>();
    graph_capability->SetGraphManager(graph_manager);
    auto metrics =
        std::make_shared<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::MetricsCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::CommandCapability>(commands);
    auto policies = std::make_unique<graph::ExecutionPolicyChain>(
        std::move(policy), nullptr);
    auto executor = std::make_shared<graph::GraphExecutor>(
        std::move(policies), graph_capability);
    commands->BindExecutor(executor);
    return {.executor = std::move(executor),
            .commands = std::move(commands),
            .metrics = std::move(metrics)};
}

struct HttpHarness {
    explicit HttpHarness(const int requested_port = ReserveLoopbackPort())
        : port(static_cast<std::uint16_t>(requested_port)),
          coordinator(std::make_shared<graph::GraphCoordinator>(
              LoadMinimalGraph())),
          executor(graph::GraphExecutorBuilder()
                       .WithGraphSnapshot(coordinator->Snapshot())
                       .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                       .Build()),
          commands(executor->GetCapability<
                   capabilities::CommandCapability>()),
          metrics(executor->GetCapability<
                  capabilities::MetricsCapability>()),
          server(coordinator, commands, metrics, requested_port) {}

    std::uint16_t port;
    std::shared_ptr<graph::GraphCoordinator> coordinator;
    std::shared_ptr<graph::GraphExecutor> executor;
    std::shared_ptr<capabilities::CommandCapability> commands;
    std::shared_ptr<capabilities::MetricsCapability> metrics;
    graph::GraphHttpServer server;
};

json RequestJson(const HttpHarness& harness, const std::string& method,
                 const std::string& path, const std::string& body = {}) {
    return json::parse(ResponseBody(
        SendHttpRequest(harness.port, method, path, body)));
}

}  // namespace

TEST(GraphHttpServerPhase0Test, InvalidPortsFailWithoutBinding) {
    HttpHarness zero(0);
    HttpHarness oversized(65536);
    EXPECT_FALSE(zero.server.Start());
    EXPECT_FALSE(oversized.server.Start());
}

TEST(GraphHttpServerPhase0Test, PageAndGraphLoadWithoutInitialization) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    const auto page = SendHttpRequest(harness.port, "GET", "/");
    EXPECT_EQ(ResponseStatus(page), 200);
    EXPECT_NE(ResponseBody(page).find("GraphX Management Dashboard"),
              std::string::npos);

    const auto response =
        SendHttpRequest(harness.port, "GET", "/api/v1/graph");
    EXPECT_EQ(ResponseStatus(response), 200);
    EXPECT_EQ(json::parse(ResponseBody(response))["data"]["nodes"].size(), 2);
    EXPECT_EQ(harness.executor->GetExecutionState(),
              graph::ExecutionState::CONFIGURED);
    EXPECT_EQ(harness.executor->GetGraphManager(), nullptr);
}

TEST(GraphHttpServerPhase0Test, CommandDiscoveryAndUnknownResources) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    const auto discovery =
        SendHttpRequest(harness.port, "GET",
                        "/api/v1/execution/commands");
    ASSERT_EQ(ResponseStatus(discovery), 200);
    const auto commands = json::parse(ResponseBody(discovery))["data"];
    ASSERT_EQ(commands.size(), 6);
    EXPECT_EQ(commands[0]["name"], "configure");
    EXPECT_TRUE(commands[3]["asynchronous"]);

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST",
                  "/api/v1/execution/commands/unknown")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET",
                  "/api/v1/execution/operations/op-missing")),
              404);
}

TEST(GraphHttpServerPhase0Test,
     KnownRoutesRejectWrongMethodsAndUnknownRoutesRemainNotFound) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    const std::array<std::pair<std::string, std::string>, 6>
        wrong_method_requests{{
            {"POST", "/api/v1/graph"},
            {"GET", "/api/v1/execution/init"},
            {"ET", "/api/v1/graph"},
            {"OST", "/api/v1/execution/init"},
            {"DELETE", "/api/v1/nodes/source_1"},
            {"POST", "/api/v1/execution/operations/op-missing"},
        }};
    for (const auto& [method, path] : wrong_method_requests) {
        EXPECT_EQ(ResponseStatus(
                      SendHttpRequest(harness.port, method, path)),
                  405)
            << method << " " << path;
    }
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/api/v1/not-a-resource")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST",
                  "/api/v1/execution/not-a-command")),
              404);
}

TEST(GraphHttpServerPhase0Test,
     ExpiredExecutorIsUnavailableWhileDiscoveryRemainsAvailable) {
    HttpHarness harness;
    harness.executor.reset();
    ASSERT_TRUE(harness.server.Start());

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET",
                  "/api/v1/execution/commands")),
              200);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/api/v1/execution/state")),
              503);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/init")),
              503);
}

TEST(GraphHttpServerPhase0Test, StateReportsAllConfigurationIdentities) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    const auto state =
        RequestJson(harness, "GET", "/api/v1/execution/state")["data"];
    EXPECT_EQ(state["state"], "CONFIGURED");
    EXPECT_EQ(state["coordinator_revision"], 0);
    EXPECT_EQ(state["configured_revision"], 0);
    EXPECT_TRUE(state["active_revision"].is_null());
    EXPECT_EQ(state["graph_generation"], 1);
    EXPECT_FALSE(state["configuration_dirty"]);
}

TEST(GraphHttpServerPhase0Test, InvalidTransitionReturnsConflict) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/start")),
              409);
}

TEST(GraphHttpServerPhase0Test,
     PatchBeforeConfigureBecomesTheInitializedRevision) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "PATCH", "/api/v1/nodes/source_1",
                  R"({"node_config":{"message_count":3}})")),
              200);
    auto dirty =
        RequestJson(harness, "GET", "/api/v1/execution/state")["data"];
    EXPECT_EQ(dirty["coordinator_revision"], 1);
    EXPECT_EQ(dirty["configured_revision"], 0);
    EXPECT_TRUE(dirty["configuration_dirty"]);

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/configure")),
              200);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/init")),
              200);

    const auto state =
        RequestJson(harness, "GET", "/api/v1/execution/state")["data"];
    EXPECT_EQ(state["configured_revision"], 1);
    EXPECT_EQ(state["active_revision"], 1);
    EXPECT_EQ(state["graph_generation"], 2);
    EXPECT_FALSE(state["configuration_dirty"]);
    EXPECT_NE(harness.executor->GetGraphManager(), nullptr);
}

TEST(GraphHttpServerPhase0Test,
     PatchAfterInitMarksDirtyWithoutMutatingActiveGraph) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/init")),
              200);
    const auto active_manager = harness.executor->GetGraphManager();

    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "PATCH", "/api/v1/nodes/source_1",
                  R"({"node_config":{"message_count":100}})")),
              200);
    const auto state =
        RequestJson(harness, "GET", "/api/v1/execution/state")["data"];
    EXPECT_TRUE(state["configuration_dirty"]);
    EXPECT_EQ(state["active_revision"], 0);
    EXPECT_EQ(harness.executor->GetGraphManager(), active_manager);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/start")),
              409);
}

TEST(GraphHttpServerPhase0Test,
     AsyncRunReturnsLocationAndCompletesSingleTeardown) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/init")),
              200);
    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/start")),
              200);

    const auto accepted =
        SendHttpRequest(harness.port, "POST", "/api/v1/execution/run");
    ASSERT_EQ(ResponseStatus(accepted), 202);
    EXPECT_NE(accepted.find("\r\nLocation: /api/v1/execution/operations/"),
              std::string::npos);
    const auto operation_id =
        json::parse(ResponseBody(accepted))["data"]["operation_id"]
            .get<std::string>();

    json operation;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        operation = RequestJson(
            harness, "GET",
            "/api/v1/execution/operations/" + operation_id)["data"];
        if (operation["status"] == "completed" ||
            operation["status"] == "failed" ||
            operation["status"] == "cancelled") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);

    EXPECT_EQ(operation["status"], "completed");
    EXPECT_EQ(operation["state"], "STOPPED");
    EXPECT_EQ(harness.executor->GetStopSequenceCount(), 1);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/execution/join")),
              200);
}

TEST(GraphHttpServerPhase0Test,
     KnownCancelledAndFailedOperationsRemainQueryable) {
    {
        const auto port = ReserveLoopbackPort();
        auto coordinator = std::make_shared<graph::GraphCoordinator>(
            LoadMinimalGraph());
        auto bundle = BuildPolicyExecutor(
            std::make_unique<HttpStopAwareRunPolicy>());
        graph::GraphHttpServer server(
            coordinator, bundle.commands, bundle.metrics, port);
        ASSERT_TRUE(server.Start());
        ASSERT_EQ(ResponseStatus(SendHttpRequest(
                      port, "POST", "/api/v1/execution/init")),
                  200);
        ASSERT_EQ(ResponseStatus(SendHttpRequest(
                      port, "POST", "/api/v1/execution/start")),
                  200);
        const auto run_response = SendHttpRequest(
            port, "POST", "/api/v1/execution/run");
        ASSERT_EQ(ResponseStatus(run_response), 202);
        const auto run_operation_id =
            json::parse(ResponseBody(run_response))["data"]["operation_id"]
                .get<std::string>();
        ASSERT_EQ(ResponseStatus(SendHttpRequest(
                      port, "POST", "/api/v1/execution/stop")),
                  202);

        std::string lookup;
        json operation;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            lookup = SendHttpRequest(
                port, "GET",
                "/api/v1/execution/operations/" +
                    run_operation_id);
            ASSERT_EQ(ResponseStatus(lookup), 200);
            operation = json::parse(ResponseBody(lookup));
            if (operation["data"]["status"] == "cancelled") {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        EXPECT_EQ(operation["data"]["status"], "cancelled");
        EXPECT_TRUE(operation["success"]);
    }

    {
        const auto port = ReserveLoopbackPort();
        auto coordinator = std::make_shared<graph::GraphCoordinator>(
            LoadMinimalGraph());
        auto bundle = BuildPolicyExecutor(
            std::make_unique<HttpFailingRunPolicy>());
        graph::GraphHttpServer server(
            coordinator, bundle.commands, bundle.metrics, port);
        ASSERT_TRUE(server.Start());
        ASSERT_EQ(ResponseStatus(SendHttpRequest(
                      port, "POST", "/api/v1/execution/init")),
                  200);
        ASSERT_EQ(ResponseStatus(SendHttpRequest(
                      port, "POST", "/api/v1/execution/start")),
                  200);
        const auto run_response = SendHttpRequest(
            port, "POST", "/api/v1/execution/run");
        ASSERT_EQ(ResponseStatus(run_response), 202);
        const auto run_operation_id =
            json::parse(ResponseBody(run_response))["data"]["operation_id"]
                .get<std::string>();

        std::string lookup;
        json operation;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            lookup = SendHttpRequest(
                port, "GET",
                "/api/v1/execution/operations/" +
                    run_operation_id);
            ASSERT_EQ(ResponseStatus(lookup), 200);
            operation = json::parse(ResponseBody(lookup));
            if (operation["data"]["status"] == "failed") {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        EXPECT_EQ(operation["data"]["status"], "failed");
        EXPECT_FALSE(operation["success"]);
        EXPECT_FALSE(operation["message"].get<std::string>().empty());
    }
}

TEST(GraphHttpServerPhase0Test,
     NaturalRunStoppingAcceptsJoinObserverWithAcceptedHttpStatus) {
    const auto port = ReserveLoopbackPort();
    auto coordinator = std::make_shared<graph::GraphCoordinator>(
        LoadMinimalGraph());
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto graph_capability =
        std::make_shared<capabilities::GraphCapability>();
    graph_capability->SetGraphManager(graph_manager);
    auto metrics =
        std::make_shared<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::MetricsCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::CommandCapability>(commands);
    auto blocking_state = std::make_shared<HttpBlockingJoinState>();
    auto policies = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<HttpBlockingJoinPolicy>(blocking_state), nullptr);
    auto executor = std::make_shared<graph::GraphExecutor>(
        std::move(policies), graph_capability);
    commands->BindExecutor(executor);
    graph::GraphHttpServer server(coordinator, commands, metrics, port);
    ASSERT_TRUE(server.Start());

    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  port, "POST", "/api/v1/execution/init")),
              200);
    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  port, "POST", "/api/v1/execution/start")),
              200);
    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  port, "POST", "/api/v1/execution/run")),
              202);
    {
        std::unique_lock lock(blocking_state->mutex);
        ASSERT_TRUE(blocking_state->condition.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return blocking_state->entered; }));
    }
    ASSERT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPING);

    const auto join_response = SendHttpRequest(
        port, "POST", "/api/v1/execution/join");
    ASSERT_EQ(ResponseStatus(join_response), 202);
    const auto join_data =
        json::parse(ResponseBody(join_response))["data"];
    EXPECT_EQ(join_data["status"], "accepted");
    EXPECT_EQ(join_data["state"], "STOPPING");
    const auto join_operation_id =
        join_data["operation_id"].get<std::string>();

    {
        std::scoped_lock lock(blocking_state->mutex);
        blocking_state->released = true;
    }
    blocking_state->condition.notify_all();

    json operation;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        operation = json::parse(ResponseBody(SendHttpRequest(
            port, "GET",
            "/api/v1/execution/operations/" +
                join_operation_id)))["data"];
        if (operation["status"] == "completed" ||
            operation["status"] == "failed") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    EXPECT_EQ(operation["status"], "completed");
    EXPECT_EQ(operation["state"], "STOPPED");
}

TEST(GraphHttpServerPhase0Test, MalformedPatchAndCommandBodiesAreRejected) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "PATCH", "/api/v1/nodes/source_1",
                  R"({"node_config":)")),
              400);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST",
                  "/api/v1/execution/commands/init", "[]")),
              400);
}
