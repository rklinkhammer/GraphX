// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "capabilities/CommandCapability.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "graph/GraphCoordinator.hpp"
#include "graph/GraphCli.hpp"
#include "graph/CapabilityContext.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphHttpServer.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "policies/MetricsPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graph {
class GraphHttpServerMetricsCallbackProbe {
public:
    static GraphHttpServer::MetricsCallbackObservation Run(
        GraphHttpServer& server) {
        return server.ProbeMetricsCallbackBoundariesForTesting();
    }
};
}  // namespace graph

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

json WaitForMetrics(
    const HttpHarness& harness,
    const std::function<bool(const json&)>& ready) {
    json data;
    for (std::size_t attempt = 0; attempt < 200U; ++attempt) {
        data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
        if (ready(data)) {
            return data;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ADD_FAILURE() << "timed out waiting for metrics: " << data.dump(2);
    return data;
}

void StartMetricsRuntime(HttpHarness& harness) {
    ASSERT_TRUE(harness.executor->Init().success);
    ASSERT_TRUE(harness.executor->Start().success);
}

app::metrics::NodeMetricsSchema MakeMetricSchema(
    app::metrics::MetricTarget target, const std::uint64_t generation,
    std::string metric_id = "activity", std::string scalar_type = "unsigned",
    std::string unit = "events", std::string semantics = "gauge",
    std::string aggregation = "sum") {
    return {.node_name = "diagnostic-only",
            .node_type = "RepeatedType",
            .metrics_schema = json::object(),
            .event_types = {},
            .display_hints = json::object(),
            .target = std::move(target),
            .graph_generation = generation,
            .descriptors = {{.metric_id = std::move(metric_id),
                             .scalar_type = std::move(scalar_type),
                             .unit = std::move(unit),
                             .semantics = std::move(semantics),
                             .aggregation = std::move(aggregation),
                             .availability_rule = "latest_sample"}}};
}

app::metrics::MetricsEvent MakeMetricEvent(
    const app::metrics::NodeMetricsSchema& schema,
    const std::chrono::system_clock::time_point timestamp,
    app::metrics::MetricScalar value = std::uint64_t{1},
    const std::uint64_t epoch = 0U) {
    const auto& descriptor = schema.descriptors.front();
    app::metrics::MetricsEvent event;
    event.timestamp = timestamp;
    event.source = "diagnostic-only";
    event.event_type = "sample";
    event.target = schema.target;
    event.graph_generation = schema.graph_generation;
    event.samples = {{.metric_id = descriptor.metric_id,
                      .scalar_type = descriptor.scalar_type,
                      .unit = descriptor.unit,
                      .semantics = descriptor.semantics,
                      .aggregation = descriptor.aggregation,
                      .availability_rule = descriptor.availability_rule,
                      .value = std::move(value),
                      .counter_epoch = epoch}};
    return event;
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

TEST(GraphHttpServerPhase4Test, GraphResourceCarriesOneAtomicExportIdentity) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    const auto expected = harness.coordinator->Snapshot();
    const auto response = RequestJson(harness, "GET", "/api/v1/graph");
    ASSERT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["data"], expected.Document());
    EXPECT_EQ(response["snapshot"]["coordinator_revision"],
              expected.Revision());
    EXPECT_EQ(response["snapshot"]["content_identity"],
              expected.ContentIdentity());
}

TEST(GraphHttpServerPhase4Test,
     ConcurrentGraphExportsNeverMixDocumentRevisionAndContentIdentity) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (std::uint64_t revision = 1; revision <= 50; ++revision) {
            const auto before_noop = harness.coordinator->Snapshot();
            const auto& current_config =
                before_noop.Document()["nodes"][0]["node_config"];
            EXPECT_TRUE(harness.coordinator->UpdateNodeConfig(
                "source_1", current_config));
            const auto after_noop = harness.coordinator->Snapshot();
            EXPECT_EQ(after_noop.Revision(), before_noop.Revision());
            EXPECT_EQ(after_noop.ContentIdentity(),
                      before_noop.ContentIdentity());
            EXPECT_EQ(after_noop.Document(), before_noop.Document());
            EXPECT_FALSE(harness.coordinator->UpdateNodeConfig(
                "missing-node", json{{"message_count", revision}}));
            const auto after_failed = harness.coordinator->Snapshot();
            EXPECT_EQ(after_failed.Revision(), before_noop.Revision());
            EXPECT_EQ(after_failed.ContentIdentity(),
                      before_noop.ContentIdentity());
            EXPECT_EQ(after_failed.Document(), before_noop.Document());
            EXPECT_TRUE(harness.coordinator->UpdateNodeConfig(
                "source_1", json{{"message_count", revision}}));
        }
        done.store(true, std::memory_order_release);
    });
    do {
        const auto response = RequestJson(harness, "GET", "/api/v1/graph");
        ASSERT_TRUE(response["success"].get<bool>());
        const auto revision = response["snapshot"]["coordinator_revision"]
                                  .get<std::uint64_t>();
        const graph::GraphConfigurationSnapshot reconstructed(
            response["data"], revision);
        EXPECT_EQ(response["snapshot"]["content_identity"],
                  reconstructed.ContentIdentity());
        if (revision != 0U) {
            EXPECT_EQ(response["data"]["nodes"][0]["node_config"]
                              ["message_count"],
                      revision);
        }
    } while (!done.load(std::memory_order_acquire));
    writer.join();
}

TEST(GraphHttpServerPhase4Test,
     ConcurrentMetricPublicationProducesOnlyInternallyCoherentSnapshots) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    const auto generation = harness.metrics->GetGraphGeneration();
    auto schema = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        generation, "left", "unsigned", "items", "gauge", "sum");
    auto right = schema.descriptors.front();
    right.metric_id = "right";
    schema.descriptors.push_back(right);
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());

    std::barrier start{2};
    std::atomic<bool> reader_complete{false};
    std::atomic<std::uint64_t> published{0U};
    std::thread publisher([&] {
        start.arrive_and_wait();
        while (!reader_complete.load(std::memory_order_acquire)) {
            const auto next = published.fetch_add(1U, std::memory_order_acq_rel) + 1U;
            auto event = MakeMetricEvent(
                schema, std::chrono::system_clock::now(), next);
            auto right_sample = event.samples.front();
            right_sample.metric_id = "right";
            event.samples.push_back(std::move(right_sample));
            EXPECT_TRUE(harness.metrics->InvokeSubscribers(event));
        }
    });
    start.arrive_and_wait();
    std::uint64_t previous_sequence = 0U;
    for (std::size_t read = 0U; read < 50U; ++read) {
        const auto data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
        ASSERT_EQ(data["graph_generation"], generation);
        ASSERT_EQ(data["schemas"].size(), 2U);
        ASSERT_EQ(data["values"].size(), 2U);
        const auto sequence = data["snapshot_sequence"].get<std::uint64_t>();
        EXPECT_GE(sequence, previous_sequence);
        previous_sequence = sequence;
        const auto& left_value = data["values"][0];
        const auto& right_value = data["values"][1];
        EXPECT_EQ(left_value["metric_id"], "left");
        EXPECT_EQ(right_value["metric_id"], "right");
        EXPECT_EQ(left_value["graph_generation"], generation);
        EXPECT_EQ(right_value["graph_generation"], generation);
        EXPECT_EQ(left_value["availability"], right_value["availability"]);
        EXPECT_EQ(left_value["value"], right_value["value"]);
        EXPECT_EQ(left_value["sample_time"], right_value["sample_time"]);
    }
    reader_complete.store(true, std::memory_order_release);
    publisher.join();
    EXPECT_GT(published.load(std::memory_order_acquire), 0U);
}

TEST(GraphHttpServerPhase4Test,
     TypedAndCompatibilityInitRoutesPublishEquivalentLifecycleResults) {
    HttpHarness typed;
    HttpHarness compatibility;
    ASSERT_TRUE(typed.server.Start());
    ASSERT_TRUE(compatibility.server.Start());
    const auto typed_result = RequestJson(
        typed, "POST", "/api/v1/execution/commands/init", "{}");
    const auto compatibility_result = RequestJson(
        compatibility, "POST", "/api/v1/execution/init");
    ASSERT_TRUE(typed_result["success"].get<bool>());
    ASSERT_TRUE(compatibility_result["success"].get<bool>());
    for (const auto* field : {"command", "status", "state",
                              "coordinator_revision", "configured_revision",
                              "active_revision", "graph_generation",
                              "configuration_dirty"}) {
        EXPECT_EQ(typed_result["data"][field],
                  compatibility_result["data"][field]) << field;
    }
}

TEST(GraphHttpServerPhase4Test,
     DeprecatedCliAndHttpUseEquivalentTypedResultsForEveryLifecycleCommand) {
    const auto graph_path = std::filesystem::current_path() /
        "phase4-cli-http-equivalence.json";
    {
        std::ofstream output(graph_path);
        output << LoadMinimalGraph();
    }
    graph::GraphCli cli;
    ASSERT_TRUE(cli.LoadGraph(graph_path.string()));
    cli.SetPluginDirectory(PLUGIN_OUTPUT_DIRECTORY);
    HttpHarness http;
    ASSERT_TRUE(http.server.Start());

    const auto await_http = [&](HttpHarness& harness,
                                const std::string& command,
                                const bool await_terminal = true) {
        auto envelope = RequestJson(
            harness, "POST", "/api/v1/execution/commands/" + command, "{}");
        auto result = envelope["data"];
        for (std::size_t attempt = 0U; await_terminal &&
             (result["status"] == "accepted" || result["status"] == "running") &&
             attempt < 500U;
             ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            result = RequestJson(
                harness, "GET", "/api/v1/execution/operations/" +
                    result["operation_id"].get<std::string>())["data"];
        }
        return result;
    };
    const auto compare = [&](const json& http_result) {
        ASSERT_TRUE(cli.GetLastCommandResult().has_value());
        const auto& cli_result = *cli.GetLastCommandResult();
        EXPECT_EQ(http_result["command"],
                  capabilities::ToString(cli_result.command));
        EXPECT_EQ(http_result["status"],
                  capabilities::ToString(cli_result.status));
        EXPECT_EQ(http_result["state"],
                  graph::GetExecutionStateName(cli_result.executor_state));
        EXPECT_EQ(http_result["coordinator_revision"],
                  cli_result.coordinator_revision);
        EXPECT_EQ(http_result["configured_revision"],
                  cli_result.configured_revision
                      ? json(*cli_result.configured_revision) : json(nullptr));
        EXPECT_EQ(http_result["active_revision"],
                  cli_result.active_revision
                      ? json(*cli_result.active_revision) : json(nullptr));
        EXPECT_EQ(http_result["graph_generation"],
                  cli_result.graph_generation);
        EXPECT_EQ(http_result["configuration_dirty"],
                  cli_result.configuration_dirty);
    };

    ASSERT_TRUE(cli.Configure());
    compare(await_http(http, "configure"));
    ASSERT_TRUE(cli.Init());
    compare(await_http(http, "init"));
    ASSERT_TRUE(cli.Start());
    compare(await_http(http, "start"));
    ASSERT_TRUE(cli.Stop());
    compare(await_http(http, "stop"));
    ASSERT_TRUE(cli.Join());
    compare(await_http(http, "join"));

    // Run is blocking in the deprecated terminal adapter but asynchronous over
    // HTTP. Compare their terminal typed results on fresh equivalent runtimes.
    ASSERT_TRUE(cli.LoadGraph(graph_path.string()));
    cli.SetPluginDirectory(PLUGIN_OUTPUT_DIRECTORY);
    HttpHarness run_http;
    ASSERT_TRUE(run_http.server.Start());
    ASSERT_TRUE(cli.Configure());
    (void)await_http(run_http, "configure");
    ASSERT_TRUE(cli.Init());
    (void)await_http(run_http, "init");
    ASSERT_TRUE(cli.Start());
    (void)await_http(run_http, "start");
    ASSERT_TRUE(cli.Run());
    compare(await_http(run_http, "run"));
    std::filesystem::remove(graph_path);
}

TEST(GraphHttpServerPhase4Test,
     SubscriberRegistrationIsIdempotentAcrossEveryServerLifecyclePath) {
    HttpHarness harness;
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 0U);
    ASSERT_TRUE(harness.server.Start());
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 1U);
    EXPECT_FALSE(harness.server.Start());
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 1U);
    EXPECT_TRUE(harness.server.Stop());
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 0U);
    EXPECT_TRUE(harness.server.Stop());
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 0U);

    HttpHarness invalid(0);
    EXPECT_FALSE(invalid.server.Start());
    EXPECT_EQ(invalid.metrics->GetCallbackCount(), 0U);
}

TEST(GraphHttpServerPhase4Test,
    MetricsSnapshotUsesExactIdentityGenerationTypedValueAndReset) {
    HttpHarness harness;
    ASSERT_TRUE(harness.executor->Init().success);
    const auto generation = harness.metrics->GetGraphGeneration();
    ASSERT_GT(generation, 0U);
    app::metrics::NodeMetricsSchema schema{
        .node_name = "a duplicated diagnostic label",
        .node_type = "DuplicatedType",
        .metrics_schema = json::object(),
        .event_types = {},
        .display_hints = json::object(),
        .target = {.kind = app::metrics::MetricTarget::Kind::Node,
                   .node_id = "source_1"},
        .graph_generation = generation,
        .descriptors = {{.metric_id = "queue_depth",
                         .scalar_type = "unsigned",
                         .unit = "messages",
                         .semantics = "gauge",
                         .aggregation = "sum",
                         .availability_rule = "latest_sample"}}};
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());

    auto event = MakeMetricEvent(
        schema, std::chrono::system_clock::now(), std::uint64_t{7});
    event.source = "not-an-authoritative-id";
    event.event_type = "snapshot";
    harness.server.OnMetricsEvent(event);

    const auto data = WaitForMetrics(
        harness, [](const json& data) {
            return !data["values"].empty() &&
                   data["values"][0]["value"] == "7";
        });
    SCOPED_TRACE(data.dump(2));
    EXPECT_EQ(data["schema_version"], 1);
    EXPECT_EQ(data["graph_generation"], generation);
    ASSERT_EQ(data["schemas"].size(), 1U);
    ASSERT_EQ(data["values"].size(), 1U);
    EXPECT_EQ(data["values"][0]["target"]["node_id"], "source_1");
    EXPECT_EQ(data["values"][0]["metric_id"], "queue_depth");
    EXPECT_EQ(data["values"][0]["value"], "7");
    EXPECT_EQ(data["values"][0]["availability"], "available");
    const auto rejected_before_paths =
        data["diagnostics"]["rejected"].get<std::uint64_t>();
    const auto sample_before_paths = data["diagnostics"]
        ["rejection_categories"]["sample_contract"].get<std::uint64_t>();
    const auto authority_before_paths = data["diagnostics"]
        ["rejection_categories"]["authority_mismatch"].get<std::uint64_t>();

    graph::GraphHttpServer::MetricsCallbackObservation observation;
    harness.server.SetMetricsCallbackObserverForTesting(
        [&observation](const auto& current) { observation = current; });
    auto huge_target = event;
    huge_target.target = {
        .kind = app::metrics::MetricTarget::Kind::Edge,
        .source_node_id = "source_1",
        .source_port_kind = "name",
        .source_port = std::string(1U << 20U, 'x'),
        .target_node_id = "sink_1",
        .target_port_kind = "index",
        .target_port = std::uint64_t{0}};
    EXPECT_NO_THROW(harness.server.OnMetricsEvent(huge_target));
    EXPECT_EQ(observation.validations, 1U);
    EXPECT_EQ(observation.target_key_constructions, 0U);
    EXPECT_EQ(observation.samples_examined, 0U);
    EXPECT_EQ(observation.mutex_acquisitions, 0U);
    EXPECT_EQ(observation.socket_operations, 0U);
    EXPECT_EQ(observation.http_responses, 0U);
    EXPECT_EQ(observation.json_serializations, 0U);
    EXPECT_EQ(observation.capability_reentries, 0U);
    const auto forbidden_boundaries =
        graph::GraphHttpServerMetricsCallbackProbe::Run(harness.server);
    EXPECT_EQ(forbidden_boundaries.socket_operations, 1U);
    EXPECT_EQ(forbidden_boundaries.http_responses, 1U);
    EXPECT_EQ(forbidden_boundaries.json_serializations, 1U);
    EXPECT_EQ(forbidden_boundaries.capability_reentries, 1U);

    auto wrong_authority = event;
    wrong_authority.target.node_id = "unknown-node";
    harness.server.OnMetricsEvent(wrong_authority);
    EXPECT_EQ(observation.target_key_constructions, 1U);
    EXPECT_EQ(observation.mutex_acquisitions, 1U);
    EXPECT_EQ(observation.samples_examined, 0U);

    auto over_bound = event;
    over_bound.samples.resize(65U, event.samples.front());
    const auto rejected_before = data["diagnostics"]["rejected"]
                                     .get<std::uint64_t>();
    harness.server.OnMetricsEvent(over_bound);
    const auto bounded = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_GE(bounded["diagnostics"]["rejected"].get<std::uint64_t>(),
              rejected_before + 1U);
    EXPECT_EQ(bounded["values"][0]["value"], "7");
    const auto categorized =
        RequestJson(harness, "GET", "/api/v1/metrics")["data"]["diagnostics"];
    EXPECT_EQ(categorized["rejection_categories"]["sample_contract"],
              sample_before_paths + 2U);
    EXPECT_EQ(categorized["rejection_categories"]["authority_mismatch"],
              authority_before_paths + 1U);
    EXPECT_EQ(categorized["rejected"], rejected_before_paths + 3U);
    const auto category_sum =
        categorized["rejection_categories"]["schema_contract"].get<std::uint64_t>() +
        categorized["rejection_categories"]["sample_contract"].get<std::uint64_t>() +
        categorized["rejection_categories"]["authority_mismatch"].get<std::uint64_t>() +
        categorized["rejection_categories"]["subscriber_failure"].get<std::uint64_t>() +
        categorized["rejection_categories"]["internal"].get<std::uint64_t>();
    EXPECT_EQ(category_sum, categorized["rejected"].get<std::uint64_t>());
    EXPECT_EQ(categorized["dropped_queue_full"], 0U);

    auto recovery = event;
    recovery.timestamp += std::chrono::milliseconds(1);
    recovery.samples.front().value = std::uint64_t{9};
    harness.server.OnMetricsEvent(recovery);
    const auto recovered = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(recovered["values"][0]["value"], "9");
    EXPECT_EQ(observation.samples_examined, 1U);
    EXPECT_EQ(observation.samples_retained, 1U);
    EXPECT_EQ(observation.mutex_acquisitions, 1U);
    EXPECT_EQ(observation.socket_operations, 0U);
    EXPECT_EQ(observation.http_responses, 0U);
    EXPECT_EQ(observation.json_serializations, 0U);
    EXPECT_EQ(observation.capability_reentries, 0U);

    ASSERT_TRUE(harness.executor->Start().success);
    ASSERT_TRUE(harness.executor->Stop().success);
    ASSERT_TRUE(harness.executor->Join().success);
    const auto stopped = RequestJson(harness, "GET", "/api/v1/metrics");
    EXPECT_EQ(stopped["data"]["availability"]["reason"], "execution_stopped");
    EXPECT_EQ(stopped["data"]["values"][0]["availability"], "unavailable");
    EXPECT_TRUE(stopped["data"]["values"][0]["value"].is_null());
    const auto stopped_diagnostics = stopped["data"]["diagnostics"];

    harness.metrics->ResetGeneration(generation + 1U);
    const auto reset = RequestJson(harness, "GET", "/api/v1/metrics");
    EXPECT_EQ(reset["data"]["graph_generation"], generation + 1U);
    EXPECT_TRUE(reset["data"]["values"].empty());
    EXPECT_EQ(reset["data"]["availability"]["state"], "unavailable");
    // Generation reset clears retained values, but lifetime diagnostics are
    // intentionally cumulative so operators can still diagnose prior loss.
    EXPECT_EQ(reset["data"]["diagnostics"], stopped_diagnostics);
}

TEST(GraphHttpServerPhase4Test,
     ConcurrentRejectionDiagnosticsRemainCoherentAndMonotonic) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    app::metrics::MetricsEvent invalid_event;
    invalid_event.graph_generation = 0U;

    std::atomic<bool> begin{false};
    std::atomic<bool> complete{false};
    std::thread writer([&] {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t index = 0U; index < 250U; ++index) {
            harness.server.OnMetricsEvent(invalid_event);
            harness.metrics->RecordRejected(
                capabilities::MetricsRejectionCategory::SchemaContract);
            if ((index % 8U) == 0U) {
                std::this_thread::yield();
            }
        }
        complete.store(true, std::memory_order_release);
    });

    std::array<std::uint64_t, 5U> previous{};
    std::size_t observations = 0U;
    begin.store(true, std::memory_order_release);
    do {
        const auto diagnostics = RequestJson(
            harness, "GET", "/api/v1/metrics")["data"]["diagnostics"];
        const auto& categories = diagnostics["rejection_categories"];
        const std::array<std::uint64_t, 5U> current = {
            categories["schema_contract"].get<std::uint64_t>(),
            categories["sample_contract"].get<std::uint64_t>(),
            categories["authority_mismatch"].get<std::uint64_t>(),
            categories["subscriber_failure"].get<std::uint64_t>(),
            categories["internal"].get<std::uint64_t>()};
        const auto sum = std::accumulate(current.begin(), current.end(), 0ULL);
        EXPECT_EQ(diagnostics["rejected"].get<std::uint64_t>(), sum);
        for (std::size_t index = 0U; index < current.size(); ++index) {
            EXPECT_GE(current[index], previous[index]);
        }
        previous = current;
        ++observations;
    } while (!complete.load(std::memory_order_acquire) || observations < 4U);
    writer.join();

    const auto final_diagnostics = RequestJson(
        harness, "GET", "/api/v1/metrics")["data"]["diagnostics"];
    EXPECT_EQ(final_diagnostics["rejection_categories"]["schema_contract"],
              250U);
    EXPECT_EQ(final_diagnostics["rejection_categories"]["sample_contract"],
              250U);
    EXPECT_EQ(final_diagnostics["rejected"], 500U);
}

TEST(GraphHttpServerPhase4Test,
     CounterRatesRequireCompatibleOrderedSamplesAndRejectBadTime) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    const auto generation = harness.metrics->GetGraphGeneration();
    auto schema = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        generation, "completed", "unsigned", "items",
        "monotonic_counter", "rate");
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());
    const auto base = std::chrono::system_clock::now() -
                      std::chrono::milliseconds(500);
    auto publish = [&](const std::int64_t offset_ms, const std::uint64_t value,
                       const std::uint64_t epoch = 1U,
                       const bool accepted = true) {
        const auto before = RequestJson(
            harness, "GET", "/api/v1/metrics")["data"];
        const auto prior_sequence = before["snapshot_sequence"].get<std::uint64_t>();
        const auto prior_rejected = before["diagnostics"]["rejected"].get<std::uint64_t>();
        harness.metrics->InvokeSubscribers(MakeMetricEvent(
            schema, base + std::chrono::milliseconds(offset_ms), value, epoch));
        return WaitForMetrics(harness, [&](const json& data) {
            return accepted
                ? data["snapshot_sequence"].get<std::uint64_t>() > prior_sequence
                : data["diagnostics"]["rejected"].get<std::uint64_t>() >
                      prior_rejected;
        });
    };
    auto data = publish(0, 10U);
    SCOPED_TRACE(data.dump(2));
    EXPECT_TRUE(data["values"][0]["rate"].is_null());
    EXPECT_EQ(data["values"][0]["rate_reason"], "not_enough_samples");
    data = publish(100, 20U);
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 100.0, 0.01);
    data = publish(200, 30U, 2U);
    EXPECT_TRUE(data["values"][0]["rate"].is_null());
    EXPECT_EQ(data["values"][0]["rate_reason"], "counter_epoch_changed");
    data = publish(300, 5U, 2U);
    EXPECT_TRUE(data["values"][0]["rate"].is_null());
    EXPECT_EQ(data["values"][0]["rate_reason"], "counter_not_increasing");

    const auto rejected_before = data["diagnostics"]["rejected"]
                                     .get<std::uint64_t>();
    const auto sequence_before_bad_time =
        data["snapshot_sequence"].get<std::uint64_t>();
    data = publish(300, 99U, 2U, false);
    EXPECT_GT(data["snapshot_sequence"].get<std::uint64_t>(),
              sequence_before_bad_time);
    EXPECT_EQ(data["values"][0]["value"], "5");
    EXPECT_TRUE(data["values"][0]["rate"].is_null());
    EXPECT_EQ(data["values"][0]["rate_reason"],
              "non_positive_sample_interval");
    auto future = MakeMetricEvent(schema, std::chrono::system_clock::now() +
                                  std::chrono::seconds(2), std::uint64_t{100}, 2U);
    harness.metrics->InvokeSubscribers(future);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(data["values"][0]["value"], "5");
    EXPECT_GE(data["diagnostics"]["rejected"].get<std::uint64_t>(),
              rejected_before + 2U);

    auto incompatible = MakeMetricEvent(
        schema, base + std::chrono::milliseconds(400), std::uint64_t{6}, 2U);
    incompatible.samples[0].unit = "bytes";
    harness.metrics->InvokeSubscribers(incompatible);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(data["values"][0]["availability"], "available");
    EXPECT_EQ(data["values"][0]["value"], "5");
    EXPECT_EQ(data["values"][0]["rate_reason"],
              "non_positive_sample_interval");
    data = publish(500, 7U, 2U);
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 10.0, 0.01);

    incompatible = MakeMetricEvent(
        schema, base + std::chrono::milliseconds(600), std::int64_t{8}, 2U);
    incompatible.samples[0].scalar_type = "integer";
    harness.metrics->InvokeSubscribers(incompatible);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(data["values"][0]["value"], "7");
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 10.0, 0.01);

    incompatible = MakeMetricEvent(
        schema, base + std::chrono::milliseconds(700), std::uint64_t{9}, 2U);
    incompatible.samples[0].semantics = "gauge";
    harness.metrics->InvokeSubscribers(incompatible);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(data["values"][0]["value"], "7");
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 10.0, 0.01);

    auto unavailable = MakeMetricEvent(
        schema, base + std::chrono::milliseconds(800), std::uint64_t{10}, 2U);
    unavailable.samples[0].available = false;
    unavailable.samples[0].unavailable_reason = "publisher_offline";
    harness.metrics->InvokeSubscribers(unavailable);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(data["values"][0]["availability"], "unavailable");
    EXPECT_EQ(data["values"][0]["reason"], "publisher_offline");
    EXPECT_TRUE(data["values"][0]["sample_time"].is_string());

    data = publish(1000, 9007199254740993ULL, 3U);
    EXPECT_EQ(data["values"][0]["rate_reason"],
              "incompatible_previous_sample");
    data = publish(1100, 9007199254740994ULL, 3U);
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 10.0, 0.01);
    data = publish(1200, std::numeric_limits<std::uint64_t>::max(), 3U);
    EXPECT_EQ(data["values"][0]["value"], "18446744073709551615");

    auto old = MakeMetricEvent(schema, base + std::chrono::milliseconds(900),
                               std::uint64_t{11}, 2U);
    old.graph_generation = generation - 1U;
    harness.metrics->InvokeSubscribers(old);
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_GE(data["diagnostics"]["rejected"].get<std::uint64_t>(),
              rejected_before + 3U);
}

TEST(GraphHttpServerPhase4Test,
     SignedCounterDeltaIsOverflowFreeAcrossTheFullInt64Range) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    const auto generation = harness.metrics->GetGraphGeneration();
    auto schema = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node, .node_id = "source_1"},
        generation, "signed_completed", "integer", "items",
        "monotonic_counter", "rate");
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());
    const auto base = std::chrono::system_clock::now() -
                      std::chrono::milliseconds(500);
    auto publish = [&](const std::int64_t offset, const std::int64_t value) {
        const auto sequence = RequestJson(
            harness, "GET", "/api/v1/metrics")["data"]["snapshot_sequence"]
                .get<std::uint64_t>();
        harness.server.OnMetricsEvent(MakeMetricEvent(
            schema, base + std::chrono::milliseconds(offset), value, 1U));
        return WaitForMetrics(harness, [&](const json& data) {
            return data["snapshot_sequence"].get<std::uint64_t>() > sequence;
        });
    };
    auto data = publish(0, std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(data["values"][0]["value"], "-9223372036854775808");
    data = publish(100, std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(data["values"][0]["value"], "9223372036854775807");
    EXPECT_DOUBLE_EQ(data["values"][0]["rate"].get<double>(),
                     static_cast<double>(std::numeric_limits<std::uint64_t>::max()) /
                         0.1);
    data = publish(200, -10);
    EXPECT_EQ(data["values"][0]["rate_reason"], "counter_not_increasing");
    data = publish(300, 10);
    EXPECT_NEAR(data["values"][0]["rate"].get<double>(), 200.0, 0.01);
    data = publish(400, 10);
    EXPECT_EQ(data["values"][0]["rate_reason"], "counter_not_increasing");
    data = publish(500, 9);
    EXPECT_EQ(data["values"][0]["rate_reason"], "counter_not_increasing");
}

TEST(GraphHttpServerPhase4Test,
     ExactEdgeTuplesRemainUnboundUntilTheirOwnPublisherProducesData) {
    HttpHarness numeric;
    StartMetricsRuntime(numeric);
    const auto generation = numeric.metrics->GetGraphGeneration();
    app::metrics::MetricTarget target{
        .kind = app::metrics::MetricTarget::Kind::Edge,
        .source_node_id = "source_1",
        .source_port_kind = "index",
        .source_port = std::uint64_t{0},
        .target_node_id = "sink_1",
        .target_port_kind = "index",
        .target_port = std::uint64_t{0}};
    auto schema = MakeMetricSchema(target, generation);
    numeric.metrics->SetNodeMetricsSchemas({schema, schema});
    ASSERT_TRUE(numeric.server.Start());
    auto data = RequestJson(numeric, "GET", "/api/v1/metrics")["data"];
    SCOPED_TRACE(data.dump(2));
    ASSERT_EQ(data["schemas"].size(), 1U);
    EXPECT_EQ(data["values"][0]["reason"], "unbound_edge_identity");
    EXPECT_TRUE(numeric.metrics->PublishExactEdgeMetrics(MakeMetricEvent(
        schema, std::chrono::system_clock::now(), std::uint64_t{1})));
    data = WaitForMetrics(numeric, [](const json& snapshot) {
        return !snapshot["values"].empty() &&
               snapshot["values"][0]["availability"] == "available";
    });
    EXPECT_EQ(data["values"][0]["availability"], "available");
    EXPECT_EQ(data["values"][0]["target"]["source_port"]["kind"], "index");
    EXPECT_EQ(data["values"][0]["target"]["target_port"]["value"], 0U);
}

TEST(GraphHttpServerPhase4Test, NamedEdgePortsUseTheCompleteExactTuple) {
    std::ifstream input(
        std::string{GRAPHX_SOURCE_ROOT} +
        "/libgraph/test/config/topologies/generic_grouped_split_merge.json");
    json document;
    input >> document;
    HttpHarness harness(ReserveLoopbackPort(), {}, std::move(document));
    StartMetricsRuntime(harness);
    app::metrics::MetricTarget target{
        .kind = app::metrics::MetricTarget::Kind::Edge,
        .source_node_id = "source_1",
        .source_port_kind = "name",
        .source_port = std::string{"Data"},
        .target_node_id = "merge_1",
        .target_port_kind = "name",
        .target_port = std::string{"In0"}};
    auto schema = MakeMetricSchema(
        target, harness.metrics->GetGraphGeneration());
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());
    EXPECT_TRUE(harness.metrics->PublishExactEdgeMetrics(MakeMetricEvent(
        schema, std::chrono::system_clock::now(), std::uint64_t{1})));
    const auto data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    ASSERT_EQ(data["values"].size(), 1U);
    EXPECT_EQ(data["values"][0]["target"]["source_port"]["kind"], "name");
    EXPECT_EQ(data["values"][0]["target"]["source_port"]["value"], "Data");
    EXPECT_EQ(data["values"][0]["target"]["target_port"]["value"], "In0");
}

TEST(GraphHttpServerPhase4Test,
     SchemaReplacementPurgesRetainedValuesAndAdmitsFirstNewSampleWithoutGet) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    const auto generation = harness.metrics->GetGraphGeneration();
    auto first = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        generation, "first_metric");
    harness.metrics->SetNodeMetricsSchemas({first});
    ASSERT_TRUE(harness.server.Start());
    ASSERT_TRUE(harness.metrics->InvokeSubscribers(MakeMetricEvent(
        first, std::chrono::system_clock::now(), std::uint64_t{1})));
    auto data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    ASSERT_EQ(data["values"].size(), 1U);
    EXPECT_EQ(data["values"][0]["metric_id"], "first_metric");
    EXPECT_EQ(data["values"][0]["value"], "1");

    auto replacement = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        generation, "replacement_metric");
    harness.metrics->SetNodeMetricsSchemas({replacement});
    // Deliberately publish before a metrics GET. The subscriber must refresh
    // schema authority itself and must not retain/account the removed value.
    ASSERT_TRUE(harness.metrics->InvokeSubscribers(MakeMetricEvent(
        replacement, std::chrono::system_clock::now(), std::uint64_t{2})));
    data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    ASSERT_EQ(data["schemas"].size(), 1U);
    ASSERT_EQ(data["values"].size(), 1U);
    EXPECT_EQ(data["values"][0]["metric_id"], "replacement_metric");
    EXPECT_EQ(data["values"][0]["value"], "2");
}

TEST(GraphHttpServerPhase4Test,
     InvalidUtf8MetricValueIsRejectedBeforeItCanPoisonJsonSerialization) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    auto schema = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        harness.metrics->GetGraphGeneration(), "status", "string", "",
        "state", "none");
    harness.metrics->SetNodeMetricsSchemas({schema});
    ASSERT_TRUE(harness.server.Start());
    auto event = MakeMetricEvent(
        schema, std::chrono::system_clock::now(), std::string{"\xFF"});
    harness.server.OnMetricsEvent(event);
    const auto response = SendHttpRequest(harness.port, "GET", "/api/v1/metrics");
    ASSERT_EQ(ResponseStatus(response), 200);
    const auto data = json::parse(ResponseBody(response))["data"];
    ASSERT_EQ(data["values"].size(), 1U);
    EXPECT_EQ(data["values"][0]["availability"], "unavailable");
    EXPECT_GE(data["diagnostics"]["rejected"].get<std::uint64_t>(), 1U);
    EXPECT_LT(ResponseBody(response).size(), 1024U * 1024U);
}

TEST(GraphHttpServerPhase4Test,
     ScalarAndRetainedValueByteBoundsKeepResponseBelowOneMiB) {
    HttpHarness harness;
    StartMetricsRuntime(harness);
    const auto generation = harness.metrics->GetGraphGeneration();
    auto first = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "source_1"},
        generation, "string_000", "string", "", "gauge", "none");
    first.descriptors.clear();
    auto second = first;
    for (std::size_t index = 0U; index < 80U; ++index) {
        auto descriptor = MakeMetricSchema(
            first.target, generation,
            "string_" + std::to_string(100U + index), "string", "",
            "gauge", "none").descriptors.front();
        (index < 64U ? first.descriptors : second.descriptors)
            .push_back(std::move(descriptor));
    }
    harness.metrics->SetNodeMetricsSchemas({first, second});
    ASSERT_TRUE(harness.server.Start());
    const auto schemas = harness.metrics->GetNodeMetricsSchemas();
    ASSERT_EQ(schemas.size(), 2U);
    auto combined = schemas.front();
    combined.descriptors.assign(schemas.front().descriptors.begin(),
                                schemas.front().descriptors.begin() + 3);
    auto combined_event = MakeMetricEvent(
        combined, std::chrono::system_clock::now(), std::string(1024U, 'a'));
    combined_event.samples.clear();
    for (const auto& descriptor : combined.descriptors) {
        auto single = combined;
        single.descriptors = {descriptor};
        combined_event.samples.push_back(MakeMetricEvent(
            single, combined_event.timestamp, std::string(1024U, 'a'))
            .samples.front());
    }
    ASSERT_TRUE(harness.metrics->InvokeSubscribers(combined_event));
    const auto combined_data = RequestJson(
        harness, "GET", "/api/v1/metrics")["data"];
    EXPECT_EQ(std::ranges::count_if(
        combined_data["values"], [](const auto& value) {
            return value["availability"] == "available";
        }), 3);
    const auto rejected_before = harness.metrics->RejectedCount();
    for (const auto& schema : schemas) {
        for (const auto& descriptor : schema.descriptors) {
            auto single = schema;
            single.descriptors = {descriptor};
            harness.server.OnMetricsEvent(MakeMetricEvent(
                single, std::chrono::system_clock::now(),
                std::string(1024U, 'x')));
        }
    }
    auto too_large = schemas.front();
    too_large.descriptors = {too_large.descriptors.front()};
    harness.server.OnMetricsEvent(MakeMetricEvent(
        too_large, std::chrono::system_clock::now(),
        std::string(1025U, 'x')));
    const auto response = SendHttpRequest(harness.port, "GET", "/api/v1/metrics");
    ASSERT_EQ(ResponseStatus(response), 200);
    EXPECT_LT(ResponseBody(response).size(), 1024U * 1024U);
    const auto data = json::parse(ResponseBody(response))["data"];
    ASSERT_EQ(data["values"].size(), 80U);
    const auto available = std::ranges::count_if(
        data["values"], [](const auto& value) {
            return value["availability"] == "available";
        });
    EXPECT_GT(available, 0);
    EXPECT_LT(available, 80);
    EXPECT_GT(data["diagnostics"]["rejected"].get<std::uint64_t>(),
              rejected_before);
}

TEST(GraphHttpServerPhase4Test,
     MetricsRouteRejectsWrongMethodAndReturnsBoundedSnapshotUnavailable) {
    HttpHarness harness;
    ASSERT_TRUE(harness.server.Start());
    const auto wrong_method = SendHttpRequest(
        harness.port, "POST", "/api/v1/metrics", "{}");
    ASSERT_EQ(ResponseStatus(wrong_method), 405);
    EXPECT_EQ(ResponseHeader(wrong_method, "Allow"), "GET, OPTIONS");

    harness.server.SetMetricsBodyLimitForTesting(128U);
    const auto unavailable = SendHttpRequest(
        harness.port, "GET", "/api/v1/metrics");
    ASSERT_EQ(ResponseStatus(unavailable), 503);
    ASSERT_LT(ResponseBody(unavailable).size(), 1024U * 1024U);
    const auto error = json::parse(ResponseBody(unavailable));
    EXPECT_FALSE(error["success"].get<bool>());
    EXPECT_EQ(error["error"], "snapshot_unavailable");
    harness.server.SetMetricsBodyLimitForTesting(1024U * 1024U);
    const auto recovered = SendHttpRequest(
        harness.port, "GET", "/api/v1/metrics");
    ASSERT_EQ(ResponseStatus(recovered), 200);
    EXPECT_TRUE(json::parse(ResponseBody(recovered))["success"].get<bool>());
}

TEST(GraphHttpServerPhase4Test,
     DuplicateNameAndTypePolicySchemasReachHttpByCanonicalNodeIdOnly) {
    auto document = LoadMinimalGraph();
    auto second = document["nodes"][0];
    document["nodes"][0]["id"] = "source_a";
    document["nodes"][0]["name"] = "same diagnostic label";
    second["id"] = "source_b";
    second["name"] = "same diagnostic label";
    document["nodes"] = json::array({document["nodes"][0], second});
    document["edges"] = json::array();
    HttpHarness harness(ReserveLoopbackPort(), {}, std::move(document));
    ASSERT_TRUE(harness.server.Start());
    ASSERT_TRUE(harness.executor->Init().success);
    const auto schemas = harness.metrics->GetNodeMetricsSchemas();
    ASSERT_EQ(schemas.size(), 2U);
    EXPECT_EQ(schemas[0].node_name, schemas[1].node_name);
    EXPECT_EQ(schemas[0].node_type, schemas[1].node_type);
    std::set<std::string> canonical_ids;
    for (const auto& schema : schemas) {
        canonical_ids.insert(schema.target.node_id);
        ASSERT_TRUE(harness.metrics->InvokeSubscribers(MakeMetricEvent(
            schema, std::chrono::system_clock::now(), std::uint64_t{3}, 1U)));
    }
    EXPECT_EQ(canonical_ids,
              (std::set<std::string>{"source_a", "source_b"}));
    const auto data = RequestJson(harness, "GET", "/api/v1/metrics")["data"];
    ASSERT_EQ(data["values"].size(), 4U);
    std::set<std::string> value_targets;
    for (const auto& value : data["values"]) {
        value_targets.insert(value["target"]["node_id"].get<std::string>());
    }
    EXPECT_EQ(value_targets, canonical_ids);
}

TEST(MetricsCapabilityPhase4Test,
     RejectsOverBoundAndMalformedSchemasWithoutPoisoningLaterValidSchema) {
    {
        capabilities::MetricsCapability generation_zero;
        auto zero_schema = MakeMetricSchema(
            {.kind = app::metrics::MetricTarget::Kind::Node,
             .node_id = "zero"}, 0U);
        generation_zero.SetNodeMetricsSchemas({zero_schema});
        EXPECT_TRUE(generation_zero.GetNodeMetricsSchemas().empty());
        EXPECT_FALSE(generation_zero.InvokeSubscribers(MakeMetricEvent(
            zero_schema, std::chrono::system_clock::now(),
            std::uint64_t{1})));
    }
    capabilities::MetricsCapability capability;
    capability.ResetGeneration(9U);
    auto invalid = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = std::string(257U, 'x')}, 9U);
    auto valid = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "exact-node"}, 9U);
    capability.SetNodeMetricsSchemas({invalid, valid, valid});
    const auto accepted = capability.GetNodeMetricsSchemas();
    ASSERT_EQ(accepted.size(), 1U);
    EXPECT_EQ(accepted[0].target.node_id, "exact-node");
    EXPECT_GE(capability.RejectedCount(), 1U);
    EXPECT_GE(capability.RejectionCount(
                  capabilities::MetricsRejectionCategory::SchemaContract),
              1U);
    auto malformed_sample = MakeMetricEvent(
        valid, std::chrono::system_clock::now(), std::uint64_t{1});
    malformed_sample.samples.front().scalar_type = "invalid";
    EXPECT_FALSE(capability.InvokeSubscribers(malformed_sample));
    EXPECT_GE(capability.RejectionCount(
                  capabilities::MetricsRejectionCategory::SampleContract),
              1U);

    auto too_many_descriptors = valid;
    too_many_descriptors.descriptors.resize(
        100000U, too_many_descriptors.descriptors.front());
    const auto rejected_before_large_schema = capability.RejectedCount();
    capability.SetNodeMetricsSchemas({std::move(too_many_descriptors), valid});
    const auto after_large_schema = capability.GetNodeMetricsSchemas();
    ASSERT_EQ(after_large_schema.size(), 1U);
    EXPECT_EQ(after_large_schema.front().target.node_id, "exact-node");
    EXPECT_EQ(capability.RejectedCount(), rejected_before_large_schema + 1U);

    auto huge_descriptor_field = valid;
    huge_descriptor_field.descriptors.front().metric_id =
        std::string(1000000U, 'm');
    capability.SetNodeMetricsSchemas({std::move(huge_descriptor_field), valid});
    ASSERT_EQ(capability.GetNodeMetricsSchemas().size(), 1U);
    EXPECT_EQ(capability.GetNodeMetricsSchemas().front().target.node_id,
              "exact-node");
    auto huge_event_type = valid;
    huge_event_type.event_types = {std::string(1000000U, 'e')};
    capability.SetNodeMetricsSchemas({std::move(huge_event_type), valid});
    ASSERT_EQ(capability.GetNodeMetricsSchemas().size(), 1U);

    const std::string invalid_schema_utf8{"\xC3\x28", 2U};
    auto invalid_node_name = valid;
    invalid_node_name.node_name = invalid_schema_utf8;
    auto invalid_node_type = valid;
    invalid_node_type.node_type = invalid_schema_utf8;
    auto invalid_event_type = valid;
    invalid_event_type.event_types = {invalid_schema_utf8};
    auto invalid_target_utf8 = valid;
    invalid_target_utf8.target.node_id = invalid_schema_utf8;
    auto invalid_descriptor_utf8 = valid;
    invalid_descriptor_utf8.descriptors.front().metric_id = invalid_schema_utf8;
    const auto rejected_before_utf8 = capability.RejectedCount();
    capability.SetNodeMetricsSchemas({
        invalid_node_name, invalid_node_type, invalid_event_type,
        invalid_target_utf8, invalid_descriptor_utf8, valid});
    const auto after_invalid_utf8 = capability.GetNodeMetricsSchemas();
    ASSERT_EQ(after_invalid_utf8.size(), 1U);
    EXPECT_EQ(after_invalid_utf8.front().target.node_id, "exact-node");
    EXPECT_EQ(capability.RejectedCount(), rejected_before_utf8 + 5U);

    auto deeply_nested = valid;
    deeply_nested.metrics_schema = nlohmann::json::object();
    auto* cursor = &deeply_nested.metrics_schema;
    for (std::size_t depth = 0; depth < 1000U; ++depth) {
        (*cursor)["nested"] = nlohmann::json::object();
        cursor = &(*cursor)["nested"];
    }
    capability.SetNodeMetricsSchemas({std::move(deeply_nested), valid});
    ASSERT_EQ(capability.GetNodeMetricsSchemas().size(), 1U);

    std::vector<app::metrics::NodeMetricsSchema> too_many_schemas(
        2049U, valid);
    capability.SetNodeMetricsSchemas(std::move(too_many_schemas));
    EXPECT_TRUE(capability.GetNodeMetricsSchemas().empty());

    auto hidden_irrelevant = valid;
    hidden_irrelevant.target.source_node_id = std::string(100000U, 'x');
    capability.SetNodeMetricsSchemas({hidden_irrelevant, valid});
    ASSERT_EQ(capability.GetNodeMetricsSchemas().size(), 1U);
    EXPECT_EQ(capability.GetNodeMetricsSchemas().front().target.node_id,
              "exact-node");

    auto same_target_first = MakeMetricSchema(valid.target, 9U, "first");
    auto same_target_second = MakeMetricSchema(valid.target, 9U, "second");
    capability.SetNodeMetricsSchemas({same_target_first, same_target_second});
    class SameTargetSubscriber final : public app::metrics::IMetricsSubscriber {
    public:
        void OnMetricsEvent(const app::metrics::MetricsEvent&) override {
            ++calls;
        }
        std::size_t calls{0U};
    } same_target_subscriber;
    capability.RegisterMetricsCallback(&same_target_subscriber);
    EXPECT_TRUE(capability.InvokeSubscribers(MakeMetricEvent(
        same_target_second, std::chrono::system_clock::now(),
        std::uint64_t{1})));
    EXPECT_EQ(same_target_subscriber.calls, 1U);
    capability.UnregisterMetricsCallback(&same_target_subscriber);

    auto delimiter_a = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "a|metric:b"}, 9U, "c");
    auto delimiter_b = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Node,
         .node_id = "a"}, 9U, "b|metric:c");
    capability.SetNodeMetricsSchemas({delimiter_a, delimiter_b});
    EXPECT_EQ(capability.GetNodeMetricsSchemas().size(), 2U);

    class AuthoritySubscriber final : public app::metrics::IMetricsSubscriber {
    public:
        void OnMetricsEvent(const app::metrics::MetricsEvent&) override {
            ++calls;
        }
        std::size_t calls{0U};
    } authority_subscriber;
    capability.RegisterMetricsCallback(&authority_subscriber);
    auto authoritative = MakeMetricEvent(
        delimiter_a, std::chrono::system_clock::now(), std::uint64_t{1});
    capability.InvokeSubscribers(authoritative);
    EXPECT_EQ(authority_subscriber.calls, 1U);
    const auto authority_rejected_before = capability.RejectedCount();
    authoritative.samples[0].metric_id = "unknown";
    EXPECT_FALSE(capability.InvokeSubscribers(authoritative));
    EXPECT_EQ(authority_subscriber.calls, 1U);
    EXPECT_EQ(capability.RejectedCount(), authority_rejected_before + 1U);
    authoritative.samples[0] = MakeMetricEvent(
        delimiter_a, std::chrono::system_clock::now(), std::uint64_t{1})
        .samples[0];
    authoritative.samples[0].unit = "mismatch";
    EXPECT_FALSE(capability.InvokeSubscribers(authoritative));
    EXPECT_EQ(authority_subscriber.calls, 1U);
    EXPECT_EQ(capability.RejectedCount(), authority_rejected_before + 2U);
    capability.UnregisterMetricsCallback(&authority_subscriber);

    auto incomplete_edge = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Edge,
         .source_node_id = "source",
         .source_port_kind = "index",
         .source_port = std::uint64_t{0},
         .target_node_id = "",
         .target_port_kind = "index",
         .target_port = std::uint64_t{0}}, 9U);
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(MakeMetricEvent(
        incomplete_edge, std::chrono::system_clock::now())));
    auto wrong_generation = incomplete_edge;
    wrong_generation.target.target_node_id = "sink";
    wrong_generation.graph_generation = 8U;
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(MakeMetricEvent(
        wrong_generation, std::chrono::system_clock::now())));

    auto unregistered_edge = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Edge,
         .source_node_id = "source",
         .source_port_kind = "index",
         .source_port = std::uint64_t{0},
         .target_node_id = "sink",
         .target_port_kind = "index",
         .target_port = std::uint64_t{0}}, 9U, "not_registered");
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(MakeMetricEvent(
        unregistered_edge, std::chrono::system_clock::now())));

    class CountingSubscriber final : public app::metrics::IMetricsSubscriber {
    public:
        void OnMetricsEvent(const app::metrics::MetricsEvent&) override {
            ++calls;
        }
        std::size_t calls{0U};
    } subscriber;
    capability.RegisterMetricsCallback(&subscriber);
    auto over_bound = MakeMetricEvent(delimiter_a,
        std::chrono::system_clock::now());
    over_bound.samples.resize(100000U, over_bound.samples.front());
    const auto rejected_before = capability.RejectedCount();
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(over_bound));
    EXPECT_EQ(subscriber.calls, 0U);
    EXPECT_EQ(capability.RejectedCount(), rejected_before + 1U);
    const auto direct_rejected_before = capability.RejectedCount();
    EXPECT_FALSE(capability.InvokeSubscribers(over_bound));
    EXPECT_EQ(capability.RejectedCount(), direct_rejected_before + 1U);

    auto bounded_edge = MakeMetricSchema(
        {.kind = app::metrics::MetricTarget::Kind::Edge,
         .source_node_id = "source",
         .source_port_kind = "index",
         .source_port = std::uint64_t{0},
         .target_node_id = "sink",
         .target_port_kind = "index",
         .target_port = std::uint64_t{0}},
        9U, "string_0", "string", "", "gauge", "none");
    bounded_edge.descriptors.clear();
    for (std::size_t index = 0U; index < 20U; ++index) {
        bounded_edge.descriptors.push_back(MakeMetricSchema(
            bounded_edge.target, 9U, "string_" + std::to_string(index),
            "string", "", "gauge", "none").descriptors.front());
    }
    capability.SetNodeMetricsSchemas({bounded_edge});
    auto bytes_over_limit = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::now(), std::string(1024U, 'x'));
    bytes_over_limit.samples.clear();
    for (const auto& descriptor : bounded_edge.descriptors) {
        auto single = bounded_edge;
        single.descriptors = {descriptor};
        bytes_over_limit.samples.push_back(MakeMetricEvent(
            single, bytes_over_limit.timestamp, std::string(1024U, 'x'))
            .samples.front());
    }
    EXPECT_LE(bytes_over_limit.samples.size(), 64U);
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(bytes_over_limit));
    EXPECT_FALSE(capability.InvokeSubscribers(bytes_over_limit));
    EXPECT_EQ(subscriber.calls, 0U);

    auto invalid_utf8 = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::now(), std::string{"\xFF"});
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(invalid_utf8));
    EXPECT_FALSE(capability.InvokeSubscribers(invalid_utf8));
    auto future = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::now() +
                          std::chrono::seconds(2), std::string{"valid"});
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(future));
    EXPECT_FALSE(capability.InvokeSubscribers(future));
    auto ancient = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::time_point::min(),
        std::string{"valid"});
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(ancient));
    EXPECT_FALSE(capability.InvokeSubscribers(ancient));
    auto legacy_fields = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::now(), std::string{"valid"});
    for (std::size_t index = 0U; index < 65U; ++index) {
        legacy_fields.data.emplace("field_" + std::to_string(index), "1");
    }
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(legacy_fields));
    EXPECT_FALSE(capability.InvokeSubscribers(legacy_fields));
    legacy_fields.data.clear();
    legacy_fields.data.emplace("field", std::string(20000U, 'x'));
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(legacy_fields));
    EXPECT_FALSE(capability.InvokeSubscribers(legacy_fields));
    EXPECT_EQ(subscriber.calls, 0U);

    auto escaped_under = MakeMetricEvent(
        bounded_edge, std::chrono::system_clock::now(), std::string(1024U, 'x'));
    escaped_under.samples.resize(1U);
    escaped_under.data = {{"a", std::string(1024U, '\x01')},
                          {"b", std::string(1024U, '\x01')}};
    auto escaped_over = escaped_under;
    escaped_over.data["c"] = std::string(1024U, '\x01');
    const auto independent_data_bytes = [](const auto& event) {
        return nlohmann::json(event.data).dump().size();
    };
    EXPECT_LT(independent_data_bytes(escaped_under), 16384U);
    EXPECT_GT(independent_data_bytes(escaped_over), 16384U);
    EXPECT_TRUE(capabilities::MetricsCapability::ValidateEventContract(
        escaped_under));
    EXPECT_FALSE(capabilities::MetricsCapability::ValidateEventContract(
        escaped_over));
    EXPECT_TRUE(capability.PublishExactEdgeMetrics(escaped_under));
    EXPECT_FALSE(capability.PublishExactEdgeMetrics(escaped_over));
    EXPECT_EQ(subscriber.calls, 1U);
    EXPECT_TRUE(capability.InvokeSubscribers(escaped_under));
    EXPECT_EQ(subscriber.calls, 2U);
    capability.UnregisterMetricsCallback(&subscriber);
}

class BlockingMetricsSubscriber final : public app::metrics::IMetricsSubscriber {
public:
    void OnMetricsEvent(const app::metrics::MetricsEvent&) override {
        std::unique_lock lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [this] { return release; });
    }
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool release{false};
};

TEST(MetricsCapabilityPhase4Test,
     UnregisterWaitsForInFlightCallbackAndDuplicateRegistrationIsIgnored) {
    capabilities::MetricsCapability capability;
    BlockingMetricsSubscriber subscriber;
    capability.RegisterMetricsCallback(&subscriber);
    capability.RegisterMetricsCallback(&subscriber);
    ASSERT_EQ(capability.GetCallbackCount(), 1U);
    std::thread publisher([&] {
        app::metrics::MetricsEvent event;
        event.target.kind = app::metrics::MetricTarget::Kind::Node;
        event.target.node_id = "subscriber-test";
        capability.InvokeSubscribers(event);
    });
    {
        std::unique_lock lock(subscriber.mutex);
        subscriber.condition.wait(lock, [&] { return subscriber.entered; });
    }
    auto removal = std::async(std::launch::async, [&] {
        capability.UnregisterMetricsCallback(&subscriber);
    });
    EXPECT_EQ(removal.wait_for(std::chrono::milliseconds(20)),
              std::future_status::timeout);
    {
        std::scoped_lock lock(subscriber.mutex);
        subscriber.release = true;
    }
    subscriber.condition.notify_all();
    publisher.join();
    EXPECT_EQ(removal.wait_for(std::chrono::seconds(1)),
              std::future_status::ready);
    EXPECT_EQ(capability.GetCallbackCount(), 0U);
}

TEST(GraphManagerPhase4Test,
     DuplicateCanonicalIdPreservesNodeIdentityVectorAlignment) {
    graph::GraphManager manager;
    manager.AddNode(std::make_shared<test::SourceTestNode>(), "duplicate-id");
    const auto nodes_before = manager.GetNodes().size();
    const auto ids_before = manager.GetCanonicalNodeIds();
    EXPECT_THROW(
        manager.AddNode(std::make_shared<test::SourceTestNode>(),
                        "duplicate-id"),
        std::invalid_argument);
    EXPECT_EQ(manager.GetNodes().size(), nodes_before);
    EXPECT_EQ(manager.GetCanonicalNodeIds(), ids_before);
    manager.AddNode(std::make_shared<test::SinkTestNode>(), "sink-id");
    ASSERT_EQ(manager.GetNodes().size(), manager.GetCanonicalNodeIds().size());
    EXPECT_EQ(manager.GetCanonicalNodeIds().back(), "sink-id");
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

    struct ClientSockets {
        ~ClientSockets() {
            for (const int descriptor : descriptors) {
                ::close(descriptor);
            }
        }
        std::vector<int> descriptors;
    } clients;
    constexpr std::string_view partial_request =
        "GET /api/v1/graph HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    constexpr std::size_t overload_count = 64U;
    clients.descriptors.reserve(
        graph::GraphHttpServer::RequestWorkerLimit() +
        graph::GraphHttpServer::PendingRequestLimit() + overload_count);

    for (std::size_t index = 0;
         index < graph::GraphHttpServer::RequestWorkerLimit(); ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        clients.descriptors.push_back(socket_fd);
        ASSERT_EQ(SendWithoutSigpipe(socket_fd, partial_request),
                  static_cast<ssize_t>(partial_request.size()));
    }
    const auto active_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.ActiveRequestCount() !=
               graph::GraphHttpServer::RequestWorkerLimit() &&
           std::chrono::steady_clock::now() < active_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    for (std::size_t index = 0;
         index < graph::GraphHttpServer::PendingRequestLimit(); ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        clients.descriptors.push_back(socket_fd);
        ASSERT_EQ(SendWithoutSigpipe(socket_fd, partial_request),
                  static_cast<ssize_t>(partial_request.size()));
    }
    const auto pending_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (harness.server.PendingRequestCount() !=
               graph::GraphHttpServer::PendingRequestLimit() &&
           std::chrono::steady_clock::now() < pending_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());
    ASSERT_EQ(harness.server.PendingRequestCount(),
              graph::GraphHttpServer::PendingRequestLimit());

    for (std::size_t index = 0; index < overload_count; ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        clients.descriptors.push_back(socket_fd);
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

TEST(GraphHttpServerPhase4Test,
     ShutdownJoinsActiveMetricsRequestsWithoutRetainedWorkers) {
    HttpHarness harness;
    struct SnapshotGate {
        std::mutex mutex;
        std::condition_variable condition;
        std::size_t entered{0U};
        bool release{false};
    } gate;
    harness.server.SetMetricsSnapshotEntryHookForTesting([&] {
        std::unique_lock lock(gate.mutex);
        ++gate.entered;
        gate.condition.notify_all();
        gate.condition.wait(lock, [&] { return gate.release; });
    });
    ASSERT_TRUE(harness.server.Start());
    std::vector<int> clients;
    clients.reserve(graph::GraphHttpServer::RequestWorkerLimit());
    constexpr std::string_view metrics_request =
        "GET /api/v1/metrics HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n";
    for (std::size_t index = 0;
         index < graph::GraphHttpServer::RequestWorkerLimit(); ++index) {
        const int socket_fd = ConnectLoopbackClient(harness.port);
        ASSERT_GE(socket_fd, 0);
        clients.push_back(socket_fd);
        ASSERT_EQ(SendWithoutSigpipe(socket_fd, metrics_request),
                  static_cast<ssize_t>(metrics_request.size()));
    }
    {
        std::unique_lock lock(gate.mutex);
        ASSERT_TRUE(gate.condition.wait_for(lock, std::chrono::seconds(2), [&] {
            return gate.entered == graph::GraphHttpServer::RequestWorkerLimit();
        }));
    }
    ASSERT_EQ(harness.server.ActiveRequestCount(),
              graph::GraphHttpServer::RequestWorkerLimit());

    auto stopped = std::async(std::launch::async, [&] {
        return harness.server.Stop();
    });
    EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(20)),
              std::future_status::timeout);
    {
        std::scoped_lock lock(gate.mutex);
        gate.release = true;
    }
    gate.condition.notify_all();
    ASSERT_EQ(stopped.wait_for(std::chrono::seconds(2)),
              std::future_status::ready);
    EXPECT_TRUE(stopped.get());
    EXPECT_EQ(harness.server.ActiveRequestCount(), 0U);
    EXPECT_EQ(harness.server.PendingRequestCount(), 0U);
    EXPECT_EQ(harness.server.RetainedRequestWorkerCount(), 0U);
    EXPECT_EQ(harness.metrics->GetCallbackCount(), 0U);
    for (const int socket_fd : clients) {
        EXPECT_EQ(::close(socket_fd), 0);
    }
}

TEST(GraphHttpServerPhase4Test,
     ExecutorJoinDrainsAcquiredRealNodePublicationAndDetachesSafely) {
    auto coordinator = std::make_shared<graph::GraphCoordinator>(
        LoadMinimalGraph());
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphSnapshot(coordinator->Snapshot())
                        .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                        .Build();
    ASSERT_TRUE(executor->Init().success);
    auto graph_capability =
        executor->GetCapability<capabilities::GraphCapability>();
    ASSERT_NE(graph_capability, nullptr);
    graph::CapabilityContext capability_context{*graph_capability};
    const auto nodes = executor->GetGraphManager()->GetNodes();
    std::shared_ptr<graph::IMetricsCallbackProvider> retained_publisher;
    for (const auto& node : nodes) {
        auto publisher = capability_context.NodeCapability<
            graph::IMetricsCallbackProvider>(node);
        if (publisher) {
            retained_publisher = *publisher;
            break;
        }
    }
    ASSERT_NE(retained_publisher, nullptr);
    ASSERT_TRUE(retained_publisher->HasMetricsCallback());
    ASSERT_TRUE(executor->Start().success);

    auto acquired_callback = retained_publisher->AcquireMetricsCallback();
    ASSERT_NE(acquired_callback, nullptr);
    auto concrete_callback = std::dynamic_pointer_cast<
        policies::MetricsCapabilityCallback>(acquired_callback);
    ASSERT_NE(concrete_callback, nullptr);
    std::atomic<bool> release_publication{false};
    std::atomic<bool> publication_entered{false};
    concrete_callback->SetEntryHookForTesting([&] {
        publication_entered.store(true, std::memory_order_release);
        while (!release_publication.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    std::thread publisher([&] {
        static_cast<void>(acquired_callback->PublishAsync({}));
    });
    while (!publication_entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(executor->Stop().success);
    auto joining = std::async(std::launch::async, [&] { return executor->Join(); });
    EXPECT_EQ(joining.wait_for(std::chrono::milliseconds(25)),
              std::future_status::timeout);
    release_publication.store(true, std::memory_order_release);
    publisher.join();
    ASSERT_EQ(joining.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(joining.get().success);
    EXPECT_FALSE(retained_publisher->HasMetricsCallback());
    EXPECT_EQ(retained_publisher->GetMetricsCallback(), nullptr);
    EXPECT_EQ(retained_publisher->AcquireMetricsCallback(), nullptr);
    executor.reset();
    EXPECT_FALSE(retained_publisher->HasMetricsCallback());
    EXPECT_EQ(retained_publisher->GetMetricsCallback(), nullptr);
    EXPECT_FALSE(acquired_callback->PublishAsync({}));
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
