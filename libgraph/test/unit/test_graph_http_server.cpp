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
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

int ConnectLoopbackClient(const std::uint16_t port) {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(socket_fd, 0);
    if (socket_fd < 0) {
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    EXPECT_EQ(::setsockopt(socket_fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                           static_cast<socklen_t>(sizeof(no_sigpipe))),
              0);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(::connect(socket_fd, reinterpret_cast<sockaddr*>(&address),
                        sizeof(address)),
              0);
    return socket_fd;
}

ssize_t SendWithoutSigpipe(const int socket_fd, const std::string_view bytes) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    return ::send(socket_fd, bytes.data(), bytes.size(), flags);
}

std::string SendHttpRequest(const std::uint16_t port,
                            const std::string& method,
                            const std::string& path,
                            const std::string& body = {}) {
    const int socket_fd = ConnectLoopbackClient(port);
    EXPECT_GE(socket_fd, 0);

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
    EXPECT_EQ(SendWithoutSigpipe(socket_fd, encoded),
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

std::string ResponseHeader(const std::string& response,
                           const std::string& name) {
    const auto marker = "\r\n" + name + ": ";
    const auto begin = response.find(marker);
    if (begin == std::string::npos) {
        return {};
    }
    const auto value_begin = begin + marker.size();
    const auto end = response.find("\r\n", value_begin);
    return response.substr(value_begin, end - value_begin);
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
    explicit HttpHarness(
        const int requested_port = ReserveLoopbackPort(),
        std::string index_path = {},
        json document = LoadMinimalGraph())
        : port(static_cast<std::uint16_t>(requested_port)),
          coordinator(std::make_shared<graph::GraphCoordinator>(
              std::move(document))),
          executor(graph::GraphExecutorBuilder()
                       .WithGraphSnapshot(coordinator->Snapshot())
                       .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                       .Build()),
          commands(executor->GetCapability<
                   capabilities::CommandCapability>()),
          metrics(executor->GetCapability<
                  capabilities::MetricsCapability>()),
          server(coordinator, commands, metrics, requested_port,
                 std::move(index_path)) {}

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

TEST(GraphHttpServerPhase1Test,
     ServesContainedStaticInventoryWithExactMimeAndMethodBehavior) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    const auto script = SendHttpRequest(
        harness.port, "GET", "/assets/graphx-dashboard.js");
    EXPECT_EQ(ResponseStatus(script), 200);
    EXPECT_EQ(ResponseHeader(script, "Content-Type"),
              "application/javascript; charset=utf-8");
    EXPECT_FALSE(ResponseBody(script).empty());

    const auto stylesheet = SendHttpRequest(
        harness.port, "GET", "/assets/graphx-dashboard.css");
    EXPECT_EQ(ResponseStatus(stylesheet), 200);
    EXPECT_EQ(ResponseHeader(stylesheet, "Content-Type"),
              "text/css; charset=utf-8");
    EXPECT_FALSE(ResponseBody(stylesheet).empty());

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/assets/missing.js")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/assets/graphx-dashboard.js")),
              405);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/assets/%2e%2e/index.html")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/../CMakeLists.txt")),
              404);
}

TEST(GraphHttpServerPhase1Test,
     ExplicitResourceRootNeverFallsBackPerMissingAsset) {
    const auto port = ReserveLoopbackPort();
    const auto resource_root =
        std::filesystem::path{GRAPHX_SOURCE_ROOT} / "build-ninja" /
        "ninja-debug" / "test-output" /
        ("isolated-dashboard-root-" + std::to_string(port));
    std::error_code error;
    std::filesystem::create_directories(resource_root, error);
    ASSERT_FALSE(error);
    {
        std::ofstream index(resource_root / "index.html");
        ASSERT_TRUE(index);
        index << "<!doctype html><title>isolated root</title>";
    }

    HttpHarness harness(
        port, (resource_root / "index.html").string());
    ASSERT_TRUE(harness.server.Start());
    const auto page = SendHttpRequest(port, "GET", "/");
    EXPECT_EQ(ResponseStatus(page), 200);
    EXPECT_NE(ResponseBody(page).find("isolated root"), std::string::npos);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  port, "GET", "/assets/graphx-dashboard.js")),
              404);
    EXPECT_TRUE(harness.server.Stop());

    std::filesystem::remove_all(resource_root, error);
    EXPECT_FALSE(error);
}

TEST(GraphHttpServerPhase1Test,
     ResetDuringLargeAssetResponseDoesNotTerminateServer) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    const int socket_fd = ConnectLoopbackClient(harness.port);
    ASSERT_GE(socket_fd, 0);
    int receive_buffer_bytes = 1024;
    ASSERT_EQ(::setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF,
                           &receive_buffer_bytes,
                           static_cast<socklen_t>(
                               sizeof(receive_buffer_bytes))),
              0);
    constexpr std::string_view request =
        "GET /assets/graphx-dashboard.js HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ASSERT_EQ(SendWithoutSigpipe(socket_fd, request),
              static_cast<ssize_t>(request.size()));

    const auto active_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.ActiveRequestCount() == 0U &&
           std::chrono::steady_clock::now() < active_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(harness.server.ActiveRequestCount(), 1U);

    linger reset_on_close{.l_onoff = 1, .l_linger = 0};
    ASSERT_EQ(::setsockopt(socket_fd, SOL_SOCKET, SO_LINGER,
                           &reset_on_close,
                           static_cast<socklen_t>(sizeof(reset_on_close))),
              0);
    ASSERT_EQ(::close(socket_fd), 0);

    const auto completed_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.ActiveRequestCount() != 0U &&
           std::chrono::steady_clock::now() < completed_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(harness.server.ActiveRequestCount(), 0U);
    EXPECT_TRUE(harness.server.IsRunning());
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/api/v1/graph")),
              200);
}

TEST(GraphHttpServerPhase1Test,
     DestructorJoinsActiveLargeStaticResponseBeforeHandlerStateDies) {
    auto harness = std::make_unique<HttpHarness>();
    ASSERT_TRUE(harness->server.Start());

    const int socket_fd = ConnectLoopbackClient(harness->port);
    ASSERT_GE(socket_fd, 0);
    int receive_buffer_bytes = 1024;
    ASSERT_EQ(::setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF,
                           &receive_buffer_bytes,
                           static_cast<socklen_t>(
                               sizeof(receive_buffer_bytes))),
              0);
    constexpr std::string_view request =
        "GET /assets/graphx-dashboard.js HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ASSERT_EQ(SendWithoutSigpipe(socket_fd, request),
              static_cast<ssize_t>(request.size()));

    const auto active_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness->server.ActiveRequestCount() == 0U &&
           std::chrono::steady_clock::now() < active_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(harness->server.ActiveRequestCount(), 1U);

    auto destroyed = std::async(
        std::launch::async,
        [owned_harness = std::move(harness)]() mutable {
            owned_harness.reset();
        });
    ASSERT_EQ(destroyed.wait_for(std::chrono::seconds(2)),
              std::future_status::ready);
    destroyed.get();

    timeval receive_timeout{.tv_sec = 0, .tv_usec = 100000};
    ASSERT_EQ(::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                           &receive_timeout,
                           static_cast<socklen_t>(sizeof(receive_timeout))),
              0);
    std::array<char, 4096> buffer{};
    bool connection_closed = false;
    const auto close_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < close_deadline) {
        const auto received =
            ::recv(socket_fd, buffer.data(), buffer.size(), 0);
        if (received == 0) {
            connection_closed = true;
            break;
        }
        if (received < 0 && errno != EINTR && errno != EAGAIN &&
            errno != EWOULDBLOCK) {
            connection_closed = true;
            break;
        }
    }
    EXPECT_TRUE(connection_closed);
    EXPECT_EQ(::close(socket_fd), 0);
}

TEST(GraphHttpServerPhase1Test,
     SequentialAndOverlappingRequestsKeepWorkerBookkeepingBounded) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    ASSERT_EQ(harness.server.RetainedRequestWorkerCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    constexpr std::size_t sequential_requests =
        graph::GraphHttpServer::RequestWorkerLimit() * 20U;
    for (std::size_t index = 0; index < sequential_requests; ++index) {
        const auto response = SendHttpRequest(
            harness.port, "GET", "/api/v1/graph");
        ASSERT_EQ(ResponseStatus(response), 200) << index;
    }
    EXPECT_LE(harness.server.RetainedRequestWorkerCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    constexpr std::size_t overlapping_requests =
        graph::GraphHttpServer::RequestWorkerLimit();
    std::vector<std::future<int>> requests;
    requests.reserve(overlapping_requests);
    for (std::size_t index = 0; index < overlapping_requests; ++index) {
        requests.push_back(std::async(std::launch::async, [&harness] {
            return ResponseStatus(SendHttpRequest(
                harness.port, "GET", "/api/v1/graph"));
        }));
    }
    for (auto& request : requests) {
        EXPECT_EQ(request.get(), 200);
    }
    EXPECT_LE(harness.server.RetainedRequestWorkerCount(),
              graph::GraphHttpServer::RequestWorkerLimit());
    EXPECT_LE(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());
}

TEST(GraphHttpServerPhase1Test,
     AdmissionQueueRejectsOverloadAndShutdownRemainsPrompt) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    std::vector<int> admitted_clients;
    constexpr std::string_view partial_request =
        "GET /api/v1/graph HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    const auto admitted_count =
        graph::GraphHttpServer::RequestWorkerLimit() +
        graph::GraphHttpServer::PendingRequestLimit();
    admitted_clients.reserve(admitted_count);
    for (std::size_t index = 0; index < admitted_count; ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        ASSERT_EQ(SendWithoutSigpipe(socket_fd, partial_request),
                  static_cast<ssize_t>(partial_request.size()));
        admitted_clients.push_back(socket_fd);
    }

    const auto saturated_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((harness.server.ActiveRequestCount() !=
                graph::GraphHttpServer::RequestWorkerLimit() ||
            harness.server.PendingRequestCount() !=
                graph::GraphHttpServer::PendingRequestLimit()) &&
           std::chrono::steady_clock::now() < saturated_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());
    ASSERT_EQ(harness.server.PendingRequestCount(),
              graph::GraphHttpServer::PendingRequestLimit());

    constexpr std::size_t overload_count = 64U;
    std::vector<int> rejected_clients;
    rejected_clients.reserve(overload_count);
    for (std::size_t index = 0; index < overload_count; ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        rejected_clients.push_back(socket_fd);
    }
    const auto rejected_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.RejectedRequestCount() < overload_count &&
           std::chrono::steady_clock::now() < rejected_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(harness.server.RejectedRequestCount(), overload_count);
    EXPECT_EQ(harness.server.PendingRequestCount(),
              graph::GraphHttpServer::PendingRequestLimit());
    EXPECT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    const auto stop_started = std::chrono::steady_clock::now();
    EXPECT_TRUE(harness.server.Stop());
    const auto stop_elapsed =
        std::chrono::steady_clock::now() - stop_started;
    EXPECT_LT(stop_elapsed, std::chrono::seconds(2));
    EXPECT_EQ(harness.server.PendingRequestCount(), 0U);
    EXPECT_EQ(harness.server.ActiveRequestCount(), 0U);
    EXPECT_EQ(harness.server.RetainedRequestWorkerCount(), 0U);

    for (const int client : admitted_clients) {
        ::close(client);
    }
    for (const int client : rejected_clients) {
        ::close(client);
    }
}

TEST(GraphHttpServerPhase1Test, StopInterruptsAndJoinsEveryActiveRequestWorker) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    std::vector<int> clients;
    constexpr std::size_t request_count =
        graph::GraphHttpServer::RequestWorkerLimit() * 2U;
    clients.reserve(request_count);
    for (std::size_t index = 0; index < request_count; ++index) {
        const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(socket_fd, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(harness.port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(::connect(socket_fd, reinterpret_cast<sockaddr*>(&address),
                            sizeof(address)), 0);
        constexpr std::string_view partial_request =
            "GET /api/v1/graph HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        ASSERT_EQ(::send(socket_fd, partial_request.data(),
                         partial_request.size(), 0),
                  static_cast<ssize_t>(partial_request.size()));
        clients.push_back(socket_fd);
    }

    const auto active_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.ActiveRequestCount() <
               graph::GraphHttpServer::RequestWorkerLimit() &&
           std::chrono::steady_clock::now() < active_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    auto stopped = std::async(std::launch::async,
                              [&harness] { return harness.server.Stop(); });
    ASSERT_EQ(stopped.wait_for(std::chrono::seconds(2)),
              std::future_status::ready);
    EXPECT_TRUE(stopped.get());
    EXPECT_EQ(harness.server.RetainedRequestWorkerCount(), 0U);
    EXPECT_EQ(harness.server.ActiveRequestCount(), 0U);
    EXPECT_TRUE(harness.server.Stop());
    EXPECT_EQ(harness.server.PendingRequestCount(), 0U);
    EXPECT_EQ(harness.server.RetainedRequestWorkerCount(), 0U);
    for (const int client : clients) {
        ::close(client);
    }
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

TEST(GraphHttpServerPhase2PresentationIsolationTest,
     PatchPreservesTopLevelPresentationAndAddsNoLocalState) {
    auto document = LoadMinimalGraph();
    document["presentation"] = {
        {"groups",
         json::array(
             {{{"id", "source-group"},
               {"label", "Source group"},
               {"members", json::array({"source_1"})},
               {"layout", "grid"},
               {"collapsed_by_default", true}}})}};
    const auto expected_presentation = document["presentation"];
    HttpHarness harness(ReserveLoopbackPort(), {}, std::move(document));
    ASSERT_TRUE(harness.server.Start());

    ASSERT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "PATCH", "/api/v1/nodes/source_1",
                  R"({"node_config":{"message_count":3}})")),
              200);
    const auto graph =
        RequestJson(harness, "GET", "/api/v1/graph")["data"];
    ASSERT_TRUE(graph.contains("presentation"));
    EXPECT_EQ(graph["presentation"], expected_presentation);
    EXPECT_FALSE(graph["presentation"].contains("collapsedGroupIds"));
    EXPECT_FALSE(graph["presentation"].contains("isolatedGroupId"));
    EXPECT_FALSE(graph["presentation"].contains("positions"));
}

TEST(GraphHttpServerPhase2PresentationIsolationTest,
     NoGroupOrTopologyMutationRouteExists) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());

    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "GET", "/api/v1/groups")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "POST", "/api/v1/groups/collapse")),
              404);
    EXPECT_EQ(ResponseStatus(SendHttpRequest(
                  harness.port, "PATCH", "/api/v1/topology",
                  R"({"nodes":[],"edges":[]})")),
              404);
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
