// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "../dashboard/FHSSGraphRuntimeOwner.hpp"
#include "FHSSDashboardConfigurationPolicy.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "graph/DynamicEdge.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/PortFunction.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

struct HttpResponse {
  int status_code = 0;
  std::string body;
};

std::filesystem::path MakeTempAssetDirectory(const std::string &name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);

  std::ofstream index(dir / "index.html", std::ios::trunc);
  index << "<html><body>GraphX Dashboard Test</body></html>";
  return dir;
}

nlohmann::json LoadJsonFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

HttpResponse HttpRequest(std::uint16_t port, const std::string &method,
                         const std::string &target,
                         const std::string &body = {}) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return {};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {};
  }

  std::ostringstream request;
  request << method << ' ' << target << " HTTP/1.1\r\n";
  request << "Host: localhost\r\n";
  request << "Connection: close\r\n";
  request << "Content-Type: application/json\r\n";
  request << "Content-Length: " << body.size() << "\r\n\r\n";
  request << body;
  const auto wire = request.str();
  ::send(fd, wire.c_str(), wire.size(), 0);

  std::string response;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto read = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (read <= 0) {
      break;
    }
    response.append(buffer.data(), static_cast<std::size_t>(read));
  }
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);

  HttpResponse parsed;
  const auto first_line_end = response.find("\r\n");
  if (first_line_end == std::string::npos) {
    return parsed;
  }

  {
    std::istringstream status_line(response.substr(0, first_line_end));
    std::string http;
    status_line >> http >> parsed.status_code;
  }

  const auto body_pos = response.find("\r\n\r\n");
  if (body_pos != std::string::npos) {
    parsed.body = response.substr(body_pos + 4);
  }
  return parsed;
}

nlohmann::json RebuildRequestJson(const std::string &command_id,
                                  std::uint64_t expected_revision = 1) {
  return nlohmann::json{{"schema", "graphx.dashboard.config_rebuild.v1"},
                        {"command_id", command_id},
                        {"expected_revision", expected_revision}};
}

struct ProductionReceiverFixture {
  std::filesystem::path directory;
  std::filesystem::path iq_path;
  graph::dashboard::IGraphRuntimeOwner::BuildSnapshot snapshot;

  ProductionReceiverFixture() = default;
  ProductionReceiverFixture(const ProductionReceiverFixture &) = delete;
  ProductionReceiverFixture &
  operator=(const ProductionReceiverFixture &) = delete;
  ProductionReceiverFixture(ProductionReceiverFixture &&other) noexcept =
      default;
  ProductionReceiverFixture &
  operator=(ProductionReceiverFixture &&other) noexcept = default;

  ~ProductionReceiverFixture() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};

ProductionReceiverFixture MakeProductionReceiverFixture() {
  ProductionReceiverFixture result;
  result.directory = std::filesystem::temp_directory_path() /
                     ("graphx_dashboard_production_owner_fixture_" +
                      std::to_string(::getpid()));
  std::error_code error;
  std::filesystem::remove_all(result.directory, error);
  std::filesystem::create_directories(result.directory, error);
  if (error)
    throw std::runtime_error("failed to create production owner fixture");
  result.iq_path = result.directory / "receiver.cf32";

  auto canonical =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  auto source =
      std::find_if(canonical.at("nodes").begin(), canonical.at("nodes").end(),
                   [](const auto &node) {
                     return node.value("id", std::string{}) == "source";
                   });
  if (source == canonical.at("nodes").end())
    throw std::runtime_error("canonical FHSS source is missing");
  // A single complete canonical message is sufficient to exercise genuine
  // receiver traffic and completion.  Start it one pulse slot into the
  // capture so the production FIR has the same causal warm-up used by the
  // dashboard Step path.
  auto &messages = source->at("node_config").at("messages");
  messages = nlohmann::json::array({messages.front()});
  messages.front()["transmit_start_sample"] = 6'500u;
  const auto generator_config =
      dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
          graph::JsonView(source->at("node_config")));
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(generator_config);
  if (!fixture)
    throw std::runtime_error("canonical synthetic IQ generation failed");

  // Exercise the complete canonical one-message capture exactly once.  The
  // concurrency tests need real graph traffic and natural completion, not a
  // second synthetic repetition that makes their lifecycle deadline depend
  // on accumulated Debug-build CPU load from earlier tests.
  const std::size_t replay_samples = fixture->samples.size();
  std::ofstream output(result.iq_path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("failed to create production receiver IQ");
  for (const auto &sample : fixture->samples) {
    const std::array<float, 2> encoded{static_cast<float>(sample.real()),
                                       static_cast<float>(sample.imag())};
    output.write(reinterpret_cast<const char *>(encoded.data()),
                 sizeof(encoded));
  }
  output.close();

  source->at("node_config")["receiver_input"] = {
      {"file_path", result.iq_path.string()},
      {"sample_format", "cf32_le"},
      {"first_complex_sample", 0},
      {"max_complex_samples", replay_samples},
      {"max_read_complex_samples", replay_samples}};
  auto service = graph::dashboard::GraphConfigurationService(
      canonical, std::make_shared<
                     dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  const auto response = service.GetReceiverGraphResponse();
  result.snapshot = {.receiver_graph = response.at("graph"),
                     .config_revision =
                         response.at("config_revision").get<std::uint64_t>(),
                     .config_etag = response.at("etag").get<std::string>()};
  return result;
}

class ControlledRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  Result Rebuild(std::uint64_t, const BuildSnapshot &) override {
    if (next_rebuild) {
      auto result = *next_rebuild;
      next_rebuild.reset();
      return result;
    }
    return {200, "rebuild_succeeded", "built"};
  }
  Result Start(std::uint64_t, std::uint64_t) override {
    return {202, "start_accepted", "started"};
  }
  Result Stop(std::uint64_t) override {
    return {200, "stop_completed", "stopped"};
  }
  Result Shutdown(std::uint64_t) override {
    return {200, "shutdown_complete", "shutdown"};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion = std::move(callback);
  }
  std::optional<Result> next_rebuild;
  CompletionCallback completion;
};

class FhssDashboardRebuildControlTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step3_assets");
    auto config =
        LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            config,
            std::make_shared<
                dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
    owner_ = std::make_shared<ControlledRuntimeOwner>();
    runtime_session_ =
        std::make_shared<graph::dashboard::GraphRuntimeSession>(owner_);
    snapshot_collector_ =
        std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.enable_mutating_routes = true;
    options.port = 0;
    options.asset_directory = assets_;

    server_ = std::make_unique<graph::dashboard::EmbeddedDashboardServer>(
        options, configuration_service_, runtime_session_, snapshot_collector_);
    ASSERT_TRUE(server_->Start()) << server_->LastError();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  void TearDown() override {
    if (server_) {
      server_->Stop();
    }
    std::error_code error;
    std::filesystem::remove_all(assets_, error);
  }

  std::filesystem::path assets_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<ControlledRuntimeOwner> owner_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardRebuildControlTest, RebuildAcceptedRejectedStateMatrix) {
  const std::array<graph::dashboard::GraphRuntimeSession::State, 4>
      accepted_states = {
          graph::dashboard::GraphRuntimeSession::State::not_built,
          graph::dashboard::GraphRuntimeSession::State::stopped,
          graph::dashboard::GraphRuntimeSession::State::completed,
          graph::dashboard::GraphRuntimeSession::State::failed};

  for (std::size_t i = 0; i < accepted_states.size(); ++i) {
    runtime_session_->SetStateForTesting(accepted_states[i]);
    const auto response = HttpRequest(
        server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
        RebuildRequestJson("cmd-matrix-accept-" + std::to_string(i)).dump());
    EXPECT_EQ(response.status_code, 200) << response.body;
  }

  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::running);
  const auto running_response =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-matrix-reject-running").dump());
  EXPECT_EQ(running_response.status_code, 409) << running_response.body;
  const auto running_json = nlohmann::json::parse(running_response.body);
  EXPECT_EQ(running_json.at("code").get<std::string>(), "invalid_state");

  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::rebuilding);
  const auto rebuilding_response =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-matrix-reject-rebuilding").dump());
  EXPECT_EQ(rebuilding_response.status_code, 409) << rebuilding_response.body;
  const auto rebuilding_json = nlohmann::json::parse(rebuilding_response.body);
  EXPECT_EQ(rebuilding_json.at("code").get<std::string>(), "invalid_state");
}

TEST_F(FhssDashboardRebuildControlTest,
       InvalidOrFailedRebuildHasNoRuntimeGenerationSideEffects) {
  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::running);
  const auto before_invalid = runtime_session_->SnapshotStatus();
  const auto invalid =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-invalid-running").dump());
  EXPECT_EQ(invalid.status_code, 409) << invalid.body;
  const auto after_invalid = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_invalid.state, before_invalid.state);
  EXPECT_EQ(after_invalid.active_generation, before_invalid.active_generation);
  EXPECT_EQ(after_invalid.rebuild_attempts, before_invalid.rebuild_attempts);

  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      500, "executor_construction_failed", "construction failed"};
  const auto before_failure = runtime_session_->SnapshotStatus();
  const auto failure =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-fail-construction").dump());
  EXPECT_EQ(failure.status_code, 500) << failure.body;
  const auto failure_json = nlohmann::json::parse(failure.body);
  EXPECT_EQ(failure_json.at("code").get<std::string>(),
            "executor_construction_failed");
  const auto after_failure = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_failure.state, before_failure.state);
  EXPECT_EQ(after_failure.active_generation, before_failure.active_generation);
  EXPECT_EQ(after_failure.successful_rebuilds,
            before_failure.successful_rebuilds);
}

TEST_F(FhssDashboardRebuildControlTest,
       ActivationOccursOnlyAfterSuccessfulConstructionAndControlsWork) {
  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::not_built);
  const auto before = runtime_session_->SnapshotStatus();

  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      500, "executor_construction_failed", "construction failed"};
  const auto failed_rebuild =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-activation-fail").dump());
  EXPECT_EQ(failed_rebuild.status_code, 500) << failed_rebuild.body;
  const auto after_failed = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_failed.active_generation, before.active_generation);

  const auto successful_rebuild =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-activation-pass").dump());
  EXPECT_EQ(successful_rebuild.status_code, 200) << successful_rebuild.body;
  const auto after_success = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_success.state,
            graph::dashboard::GraphRuntimeSession::State::stopped);
  EXPECT_EQ(after_success.active_generation, before.active_generation + 1);

  const auto start = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/fhss/commands/start", "{}");
  EXPECT_EQ(start.status_code, 202) << start.body;
  const auto running_status =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/status");
  EXPECT_EQ(running_status.status_code, 200) << running_status.body;
  const auto running_json = nlohmann::json::parse(running_status.body);
  EXPECT_EQ(running_json.at("lifecycle_state").get<std::string>(), "running");

  const auto stop = HttpRequest(server_->BoundPort(), "POST",
                                "/api/v1/fhss/commands/stop", "{}");
  EXPECT_EQ(stop.status_code, 200) << stop.body;
  const auto stopped_status =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/status");
  EXPECT_EQ(stopped_status.status_code, 200) << stopped_status.body;
  const auto stopped_json = nlohmann::json::parse(stopped_status.body);
  EXPECT_EQ(stopped_json.at("lifecycle_state").get<std::string>(), "stopped");
}

TEST_F(FhssDashboardRebuildControlTest,
       StartCommandTransitionsStateWithoutRebuildPrecondition) {
  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::not_built);

  const auto default_metrics =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/metrics");
  ASSERT_EQ(default_metrics.status_code, 200) << default_metrics.body;
  const auto default_metrics_json = nlohmann::json::parse(default_metrics.body);
  EXPECT_EQ(default_metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_EQ(default_metrics_json.at("graph").at("availability"),
            "unavailable");
  EXPECT_EQ(default_metrics_json.at("graph").at("unavailable_reason"),
            "no active runtime generation");
  EXPECT_TRUE(
      default_metrics_json.at("graph").at("graph_total_enqueued").is_null());
  EXPECT_TRUE(default_metrics_json.at("nodes").empty());
  EXPECT_TRUE(default_metrics_json.at("edges").empty());

  const auto start = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/fhss/commands/start", "{}");
  ASSERT_EQ(start.status_code, 409) << start.body;

  const auto status_after_start =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/status");
  ASSERT_EQ(status_after_start.status_code, 200) << status_after_start.body;
  const auto status_after_start_json =
      nlohmann::json::parse(status_after_start.body);
  EXPECT_EQ(status_after_start_json.at("lifecycle_state").get<std::string>(),
            "not_built");

  const auto populated_metrics =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/metrics");
  ASSERT_EQ(populated_metrics.status_code, 200) << populated_metrics.body;
  const auto populated_metrics_json =
      nlohmann::json::parse(populated_metrics.body);
  EXPECT_EQ(populated_metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_EQ(populated_metrics_json.at("graph").at("availability"),
            "unavailable");
  EXPECT_EQ(populated_metrics_json.at("graph").at("unavailable_reason"),
            "no active runtime generation");
  EXPECT_TRUE(
      populated_metrics_json.at("graph").at("graph_total_enqueued").is_null());
  EXPECT_TRUE(populated_metrics_json.at("nodes").empty());
  EXPECT_TRUE(populated_metrics_json.at("edges").empty());
}

TEST_F(FhssDashboardRebuildControlTest,
       CleanupFailedStateBlocksFurtherRebuilds) {
  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      200, "cleanup_failed", "cleanup failed", {}, true};

  const auto first =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-cleanup-fail").dump());
  EXPECT_EQ(first.status_code, 200) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  EXPECT_EQ(first_json.at("status").get<std::string>(),
            "succeeded_with_cleanup_failed");

  const auto status =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/status");
  EXPECT_EQ(status.status_code, 200) << status.body;
  const auto status_json = nlohmann::json::parse(status.body);
  EXPECT_EQ(status_json.at("lifecycle_state").get<std::string>(),
            "cleanup_failed");
  EXPECT_TRUE(status_json.at("rebuild_blocked").get<bool>());

  const auto blocked =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-cleanup-blocked").dump());
  EXPECT_EQ(blocked.status_code, 409) << blocked.body;
  const auto blocked_json = nlohmann::json::parse(blocked.body);
  EXPECT_EQ(blocked_json.at("code").get<std::string>(), "cleanup_failed");
}

TEST_F(FhssDashboardRebuildControlTest,
       FailureInjectionChecksAreHandledSafely) {
  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      500, "queue_disable_failed", "queue disable failed"};
  const auto queue_disable =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-queue-disable").dump());
  EXPECT_EQ(queue_disable.status_code, 500) << queue_disable.body;
  const auto queue_disable_json = nlohmann::json::parse(queue_disable.body);
  EXPECT_EQ(queue_disable_json.at("code").get<std::string>(),
            "queue_disable_failed");

  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      503, "shutdown_in_progress", "shutdown in progress"};
  const auto shutdown =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-sigterm").dump());
  EXPECT_EQ(shutdown.status_code, 503) << shutdown.body;
  const auto shutdown_json = nlohmann::json::parse(shutdown.body);
  EXPECT_EQ(shutdown_json.at("code").get<std::string>(),
            "shutdown_in_progress");
  const auto readyz = HttpRequest(server_->BoundPort(), "GET", "/readyz");
  EXPECT_EQ(readyz.status_code, 200) << readyz.body;

  runtime_session_->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  owner_->next_rebuild = ControlledRuntimeOwner::Result{
      503, "thread_interrupted", "thread interrupted"};
  const auto interrupted =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/rebuild",
                  RebuildRequestJson("cmd-thread-interrupt").dump());
  EXPECT_EQ(interrupted.status_code, 503) << interrupted.body;
  const auto interrupted_json = nlohmann::json::parse(interrupted.body);
  EXPECT_EQ(interrupted_json.at("code").get<std::string>(),
            "thread_interrupted");
}

class IsolatedRuntimeOwner final : public graph::dashboard::IGraphRuntimeOwner {
public:
  Result Rebuild(std::uint64_t generation, const BuildSnapshot &) override {
    generations.push_back(generation);
    if (observe)
      observe();
    return {200, "rebuilt", "built"};
  }
  Result Start(std::uint64_t generation, std::uint64_t run_epoch) override {
    generations.push_back(generation);
    if (completion)
      completion(generation, run_epoch, true, "isolated execution completed");
    return {202, "started", "started"};
  }
  Result Stop(std::uint64_t generation) override {
    generations.push_back(generation);
    return {202, "stopped", "stopped"};
  }
  Result Shutdown(std::uint64_t generation) override {
    generations.push_back(generation);
    return {200, "shutdown", "shutdown"};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion = std::move(callback);
  }
  std::function<void()> observe;
  std::vector<std::uint64_t> generations;
  CompletionCallback completion;
};

TEST(GraphRuntimeSessionOwnerTest,
     CoordinatesInjectedOwnerWithoutHoldingSessionLock) {
  auto owner = std::make_shared<IsolatedRuntimeOwner>();
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  owner->observe = [&] {
    EXPECT_EQ(session.GetState(),
              graph::dashboard::GraphRuntimeSession::State::rebuilding);
  };
  EXPECT_EQ(session
                .Rebuild({.receiver_graph = nlohmann::json::object(),
                          .config_revision = 7,
                          .config_etag = "\"config-7\""})
                .status_code,
            200);
  EXPECT_EQ(session.SnapshotStatus().active_generation, 1u);
  EXPECT_EQ(session.SnapshotStatus().active_config_revision, 7u);
  EXPECT_EQ(session.SnapshotStatus().active_config_etag, "\"config-7\"");
  EXPECT_EQ(session.Start().status_code, 202);
  EXPECT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::completed);
  EXPECT_EQ(session.SnapshotStatus().terminal_generation, 1u);
  EXPECT_EQ(session.SnapshotStatus().terminal_result_code,
            "execution_completed");
  EXPECT_EQ(session.Stop().status_code, 200);
  session.MarkShuttingDown();
  EXPECT_EQ(owner->generations, (std::vector<std::uint64_t>{1, 1, 1}));
}

class ImmediateCompletionRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  ImmediateCompletionRuntimeOwner() {
    for (const std::uint64_t value : {11u, 22u, 33u, 44u}) {
      auto manager = std::make_shared<graph::GraphManager>();
      manager->EnableMetrics(true);
      const_cast<graph::GraphMetrics &>(manager->GetMetrics())
          .peak_active_threads.store(value);
      managers_.push_back(std::move(manager));
    }
  }

  Result Rebuild(std::uint64_t generation, const BuildSnapshot &) override {
    (void)generation;
    return {200, "rebuilt", "immediate owner rebuilt", managers_.front()};
  }

  Result Start(std::uint64_t generation, std::uint64_t run_epoch) override {
    std::size_t invocation;
    {
      const std::lock_guard lock(mutex_);
      invocation = start_invocations_++;
    }
    if (completion_)
      completion_(generation, run_epoch, true, "immediate execution completed");
    if (invocation == 2) {
      std::unique_lock lock(mutex_);
      stale_start_waiting_ = true;
      cv_.notify_all();
      cv_.wait(lock, [this] { return release_stale_start_; });
    }
    return {202, "start_accepted", "immediate execution started",
            managers_.at(invocation)};
  }

  Result Stop(std::uint64_t) override {
    return {200, "stop_completed", "immediate execution stopped"};
  }
  Result Shutdown(std::uint64_t) override {
    return {200, "shutdown_complete", "immediate owner shut down"};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion_ = std::move(callback);
  }

  void WaitForStaleStart() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return stale_start_waiting_; });
  }
  void ReleaseStaleStart() {
    {
      const std::lock_guard lock(mutex_);
      release_stale_start_ = true;
    }
    cv_.notify_all();
  }

  std::shared_ptr<graph::GraphManager> Manager(std::size_t index) const {
    return managers_.at(index);
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<graph::GraphManager>> managers_;
  CompletionCallback completion_;
  std::size_t start_invocations_ = 0;
  bool stale_start_waiting_ = false;
  bool release_stale_start_ = false;
};

TEST(GraphRuntimeSessionOwnerTest,
     ImmediateCompletionPublishesReplacementManagerAndRejectsStaleResult) {
  auto owner = std::make_shared<ImmediateCompletionRuntimeOwner>();
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  auto collector = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  collector->BindRuntimeSession(session);
  session->MarkReady();
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = nlohmann::json::object(),
                           .config_revision = 77,
                           .config_etag = "\"immediate-77\""})
                .status_code,
            200);

  ASSERT_EQ(session->Start().status_code, 202);
  const auto run1 = session->SnapshotGeneration();
  ASSERT_EQ(run1.graph_manager, owner->Manager(0));
  ASSERT_EQ(session->GetState(),
            graph::dashboard::GraphRuntimeSession::State::completed);

  // Completion is delivered synchronously inside owner Start, before Start
  // returns manager2. The lifecycle must remain completed while manager2 is
  // nevertheless published to the generation snapshot and collector.
  ASSERT_EQ(session->Start().status_code, 202);
  const auto run2 = session->SnapshotGeneration();
  EXPECT_NE(run2.graph_manager, run1.graph_manager);
  EXPECT_EQ(run2.graph_manager, owner->Manager(1));
  EXPECT_EQ(run2.generation, run1.generation);
  EXPECT_EQ(run2.config_revision, run1.config_revision);
  EXPECT_EQ(run2.config_etag, run1.config_etag);
  const auto completed = session->SnapshotStatus();
  EXPECT_EQ(completed.state,
            graph::dashboard::GraphRuntimeSession::State::completed);
  EXPECT_EQ(completed.terminal_result_code, "execution_completed");
  EXPECT_EQ(completed.terminal_generation, run2.generation);
  auto collected = collector->GetMetricsSnapshot();
  EXPECT_EQ(collected.at("active_generation"), run2.generation);
  EXPECT_EQ(collected.at("active_run_epoch"), run2.run_epoch);
  EXPECT_EQ(collected.at("active_config_revision"), run2.config_revision);
  EXPECT_EQ(collected.at("active_config_etag"), run2.config_etag);
  EXPECT_EQ(collected.at("identity_availability").at("state"), "unavailable");
  EXPECT_EQ(collected.at("graph").at("availability"), "unavailable");
  EXPECT_TRUE(collected.at("graph").at("peak_active_threads").is_null());
  EXPECT_EQ(owner->Manager(1)->GetMetrics().peak_active_threads.load(), 22u);
  EXPECT_NE(owner->Manager(0)->GetMetrics().peak_active_threads.load(), 22u);
  auto diagnostics = collector->GetDiagnosticsSnapshot();
  EXPECT_EQ(diagnostics.at("active_generation"), run2.generation);
  EXPECT_EQ(diagnostics.at("active_run_epoch"), run2.run_epoch);
  EXPECT_EQ(diagnostics.at("active_config_revision"), run2.config_revision);
  EXPECT_EQ(diagnostics.at("active_config_etag"), run2.config_etag);

  // A third Start completes immediately but delays its return. A fourth run
  // supersedes it and publishes manager4. The late manager3 result must not
  // replace the newer run because its run_epoch is stale.
  graph::dashboard::GraphRuntimeSession::CommandResult stale_result;
  std::jthread stale_start([&] { stale_result = session->Start(); });
  owner->WaitForStaleStart();
  ASSERT_EQ(session->GetState(),
            graph::dashboard::GraphRuntimeSession::State::completed);
  ASSERT_EQ(session->Start().status_code, 202);
  ASSERT_EQ(session->SnapshotGeneration().graph_manager, owner->Manager(3));
  owner->ReleaseStaleStart();
  stale_start.join();
  EXPECT_EQ(stale_result.status_code, 202);
  const auto current = session->SnapshotGeneration();
  EXPECT_EQ(current.graph_manager, owner->Manager(3));
  EXPECT_NE(current.graph_manager, owner->Manager(2));
  collected = collector->GetMetricsSnapshot();
  EXPECT_EQ(collected.at("identity_availability").at("state"), "unavailable");
  EXPECT_TRUE(collected.at("graph").at("peak_active_threads").is_null());
  EXPECT_EQ(owner->Manager(3)->GetMetrics().peak_active_threads.load(), 44u);
  diagnostics = collector->GetDiagnosticsSnapshot();
  EXPECT_EQ(diagnostics.at("active_generation"), current.generation);
  EXPECT_EQ(diagnostics.at("active_run_epoch"), current.run_epoch);
  EXPECT_NE(diagnostics.at("active_run_epoch"), run2.run_epoch);
  EXPECT_EQ(diagnostics.at("active_config_revision"), current.config_revision);
  EXPECT_EQ(diagnostics.at("active_config_etag"), current.config_etag);
}

TEST(GraphRuntimeSessionIdentityTest,
     CapturesCanonicalIdsAndExactPortsIndependentlyOfRuntimeNames) {
  auto owner = std::make_shared<ImmediateCompletionRuntimeOwner>();
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  const nlohmann::json graph = {
      {"nodes",
       {{{"id", "source"}, {"type", "Source"}, {"name", "display source"}},
        {{"id", "sink"}, {"type", "Sink"}, {"name", "display sink"}}}},
      {"edges",
       {{{"source_node_id", "source"},
         {"source_port", 24u},
         {"target_node_id", "sink"},
         {"target_port", 25u}}}}};
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = graph,
                           .config_revision = 91,
                           .config_etag = "\"identity-91\""})
                .status_code,
            200);
  const auto captured = session->SnapshotGeneration();
  EXPECT_TRUE(captured.identity_error.empty());
  EXPECT_EQ(captured.canonical_node_ids,
            (std::vector<std::string>{"source", "sink"}));
  ASSERT_EQ(captured.canonical_edges.size(), 1u);
  EXPECT_EQ(captured.canonical_edges.front().edge_id,
            "source:24->sink:25");
  EXPECT_EQ(captured.canonical_edges.front().source_node_id, "source");
  EXPECT_EQ(captured.canonical_edges.front().source_port, 24u);
  EXPECT_EQ(captured.canonical_edges.front().destination_node_id, "sink");
  EXPECT_EQ(captured.canonical_edges.front().destination_port, 25u);
}

TEST(GraphRuntimeSessionIdentityTest,
     CanonicalEdgeFormattingIsSharedAndExactlyPortAware) {
  EXPECT_EQ(graph::dashboard::CanonicalEdgeId("source", 24, "sink", 25),
            "source:24->sink:25");
  EXPECT_NE(graph::dashboard::CanonicalEdgeId("source", 24, "sink", 25),
            graph::dashboard::CanonicalEdgeId("source", 24, "sink", 26));
}

TEST(GraphRuntimeSessionIdentityTest,
     PublishedRuntimeIdentitiesStopAtJavascriptSafeIntegerBeforeMutation) {
  constexpr std::uint64_t kMaximumJavascriptSafeInteger =
      9'007'199'254'740'991ULL;
  const nlohmann::json graph = {
      {"nodes", {{{"id", "source"}, {"type", "Source"}}}},
      {"edges", nlohmann::json::array()}};

  auto generation_owner = std::make_shared<ControlledRuntimeOwner>();
  graph::dashboard::GraphRuntimeSession generation_session(generation_owner);
  generation_session.MarkReady();
  generation_session.SetIdentityCountersForTesting(
      kMaximumJavascriptSafeInteger - 1, 0, 0, 0, 0);
  ASSERT_EQ(generation_session
                .Rebuild({.receiver_graph = graph,
                          .config_revision = kMaximumJavascriptSafeInteger,
                          .config_etag = "\"maximum-safe\""})
                .status_code,
            200);
  const auto maximum_generation = generation_session.SnapshotStatus();
  EXPECT_EQ(maximum_generation.active_generation,
            kMaximumJavascriptSafeInteger);
  const auto generation_exhausted = generation_session.Rebuild(
      {.receiver_graph = graph,
       .config_revision = kMaximumJavascriptSafeInteger,
       .config_etag = "\"maximum-safe\""});
  EXPECT_EQ(generation_exhausted.code, "identity_space_exhausted");
  const auto generation_after = generation_session.SnapshotStatus();
  EXPECT_EQ(generation_after.active_generation,
            maximum_generation.active_generation);
  EXPECT_EQ(generation_after.rebuild_attempts,
            maximum_generation.rebuild_attempts);
  EXPECT_EQ(generation_after.successful_rebuilds,
            maximum_generation.successful_rebuilds);
  EXPECT_EQ(generation_after.state, maximum_generation.state);

  auto run_owner = std::make_shared<ControlledRuntimeOwner>();
  graph::dashboard::GraphRuntimeSession run_session(run_owner);
  run_session.MarkReady();
  ASSERT_EQ(run_session
                .Rebuild({.receiver_graph = graph,
                          .config_revision = 1,
                          .config_etag = "\"config-1\""})
                .status_code,
            200);
  run_session.SetIdentityCountersForTesting(
      1, kMaximumJavascriptSafeInteger, 1, 1, 1);
  const auto before_run = run_session.SnapshotStatus();
  const auto run_exhausted = run_session.Start();
  EXPECT_EQ(run_exhausted.code, "identity_space_exhausted");
  const auto after_run = run_session.SnapshotStatus();
  EXPECT_EQ(after_run.active_run_epoch, before_run.active_run_epoch);
  EXPECT_EQ(after_run.state, before_run.state);

  for (const auto counters : std::array{
           std::array<std::uint64_t, 3>{
               kMaximumJavascriptSafeInteger, 0, 0},
           std::array<std::uint64_t, 3>{
               0, kMaximumJavascriptSafeInteger, 0},
           std::array<std::uint64_t, 3>{
               0, 0, kMaximumJavascriptSafeInteger}}) {
    auto owner = std::make_shared<ControlledRuntimeOwner>();
    graph::dashboard::GraphRuntimeSession session(owner);
    session.MarkReady();
    session.SetIdentityCountersForTesting(
        0, 0, counters[0], counters[1], counters[2]);
    const auto before = session.SnapshotStatus();
    const auto result =
        session.Rebuild({.receiver_graph = graph,
                         .config_revision = 1,
                         .config_etag = "\"config-1\""});
    EXPECT_EQ(result.code, "identity_space_exhausted");
    const auto after = session.SnapshotStatus();
    EXPECT_EQ(after.active_generation, before.active_generation);
    EXPECT_EQ(after.rebuild_attempts, before.rebuild_attempts);
    EXPECT_EQ(after.successful_rebuilds, before.successful_rebuilds);
    EXPECT_EQ(after.state, before.state);
  }
}

class MetricContractNode final : public graph::INode {
public:
  graph::LifecycleState GetLifecycleState() const override {
    return graph::LifecycleState::Uninitialized;
  }
  bool Init() override { return true; }
  bool Start() override { return true; }
  void Join() override {}
  bool JoinWithTimeout(std::chrono::milliseconds) override { return true; }
  void Stop() override {}
};

class FixedManagerRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  explicit FixedManagerRuntimeOwner(
      std::shared_ptr<graph::GraphManager> manager)
      : manager_(std::move(manager)) {}
  Result Rebuild(std::uint64_t, const BuildSnapshot &) override {
    return {200, "rebuilt", "fixed manager rebuilt", manager_};
  }
  Result Start(std::uint64_t, std::uint64_t) override {
    return {202, "started", "fixed manager started", manager_};
  }
  Result Stop(std::uint64_t) override {
    return {200, "stopped", "fixed manager stopped"};
  }
  Result Shutdown(std::uint64_t) override {
    return {200, "shutdown", "fixed manager shutdown"};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion_ = std::move(callback);
  }

private:
  std::shared_ptr<graph::GraphManager> manager_;
  CompletionCallback completion_;
};

TEST(GraphSnapshotCollectorContractTest,
     CanonicalEdgeIdentitySurvivesRuntimeNodeReordering) {
  auto manager = std::make_shared<graph::GraphManager>();
  manager->EnableMetrics(true);
  manager->AddNode(std::make_shared<MetricContractNode>(), "sink");
  manager->AddNode(std::make_shared<MetricContractNode>(), "source");
  using MetricPort = graph::Port<int, 0>;
  graph::PortFunction<MetricPort> output(graph::PortDirection::Output);
  graph::PortFunction<MetricPort> input(graph::PortDirection::Input);
  ASSERT_TRUE(manager->AddDynamicEdgeExpected(
      {.source = {.node_index = 1,
                  .descriptor = {.id = 0,
                                 .name = "output",
                                 .direction = graph::PortDirection::Output,
                                 .payload_type =
                                     std::string(output.GetTypeName()),
                                 .transport_type = std::string(
                                     output.GetTransportTypeName())},
                  .port = &output},
       .destination = {.node_index = 0,
                       .descriptor = {.id = 0,
                                      .name = "input",
                                      .direction =
                                          graph::PortDirection::Input,
                                      .payload_type =
                                          std::string(input.GetTypeName()),
                                      .transport_type = std::string(
                                          input.GetTransportTypeName())},
                       .port = &input},
       .capacity = 4}));
  auto owner = std::make_shared<FixedManagerRuntimeOwner>(manager);
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = {
                               {"nodes",
                                {{{"id", "source"}, {"type", "Source"}},
                                 {{"id", "sink"}, {"type", "Sink"}}}},
                               {"edges",
                                {{{"source_node_id", "source"},
                                  {"source_port", 0u},
                                  {"target_node_id", "sink"},
                                  {"target_port", 0u}}}}},
                           .config_revision = 95,
                           .config_etag = "\"identity-95\""})
                .status_code,
            200);
  graph::dashboard::GraphSnapshotCollector collector;
  collector.BindRuntimeSession(session);
  const auto snapshot = collector.GetMetricsSnapshot();
  EXPECT_EQ(snapshot.at("identity_availability").at("state"), "available");
  ASSERT_EQ(snapshot.at("edges").size(), 1u);
  EXPECT_EQ(snapshot.at("edges").front().at("edge_id"),
            "source:0->sink:0");
  EXPECT_EQ(snapshot.at("edges").front().at("source_node_id"), "source");
  EXPECT_EQ(snapshot.at("edges").front().at("destination_node_id"), "sink");
}

TEST(GraphSnapshotCollectorContractTest,
     MultiEdgeAggregateOverflowIsUnavailableAndCannotPublishWrappedRates) {
  auto manager = std::make_shared<graph::GraphManager>();
  manager->EnableMetrics(true);
  manager->AddNode(std::make_shared<MetricContractNode>(), "source");
  manager->AddNode(std::make_shared<MetricContractNode>(), "sink-a");
  manager->AddNode(std::make_shared<MetricContractNode>(), "sink-b");
  using MetricPort = graph::Port<int, 0>;
  graph::PortFunction<MetricPort> output_a(graph::PortDirection::Output);
  graph::PortFunction<MetricPort> input_a(graph::PortDirection::Input);
  graph::PortFunction<MetricPort> output_b(graph::PortDirection::Output);
  graph::PortFunction<MetricPort> input_b(graph::PortDirection::Input);
  const auto add_edge = [&](graph::PortFunction<MetricPort> &output,
                            graph::PortFunction<MetricPort> &input,
                            std::size_t destination) {
    return manager->AddDynamicEdgeExpected(
        {.source = {.node_index = 0,
                    .descriptor = {.id = destination - 1,
                                   .name = "output",
                                   .direction =
                                       graph::PortDirection::Output,
                                   .payload_type =
                                       std::string(output.GetTypeName()),
                                   .transport_type = std::string(
                                       output.GetTransportTypeName())},
                    .port = &output},
         .destination = {.node_index = destination,
                         .descriptor = {.id = 0,
                                        .name = "input",
                                        .direction =
                                            graph::PortDirection::Input,
                                        .payload_type =
                                            std::string(input.GetTypeName()),
                                        .transport_type = std::string(
                                            input.GetTransportTypeName())},
                         .port = &input},
         .capacity = 4});
  };
  ASSERT_TRUE(add_edge(output_a, input_a, 1));
  ASSERT_TRUE(add_edge(output_b, input_b, 2));
  auto metrics_a = manager->GetEdgeMetrics(0);
  auto metrics_b = manager->GetEdgeMetrics(1);
  ASSERT_NE(metrics_a, nullptr);
  ASSERT_NE(metrics_b, nullptr);
  const_cast<graph::EdgeMetrics &>(*metrics_a)
      .messages_enqueued.store(std::numeric_limits<std::uint64_t>::max());
  const_cast<graph::EdgeMetrics &>(*metrics_b).messages_enqueued.store(1);

  auto owner = std::make_shared<FixedManagerRuntimeOwner>(manager);
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = {
                               {"nodes",
                                {{{"id", "source"}, {"type", "Source"}},
                                 {{"id", "sink-a"}, {"type", "Sink"}},
                                 {{"id", "sink-b"}, {"type", "Sink"}}}},
                               {"edges",
                                {{{"source_node_id", "source"},
                                  {"source_port", 0u},
                                  {"target_node_id", "sink-a"},
                                  {"target_port", 0u}},
                                 {{"source_node_id", "source"},
                                  {"source_port", 1u},
                                  {"target_node_id", "sink-b"},
                                  {"target_port", 0u}}}}},
                           .config_revision = 96,
                           .config_etag = "\"identity-96\""})
                .status_code,
            200);
  graph::dashboard::GraphSnapshotCollector collector;
  collector.BindRuntimeSession(session);
  const auto first = collector.GetMetricsSnapshot();
  EXPECT_TRUE(manager->GetMetrics().aggregation_overflow.load());
  EXPECT_EQ(first.at("graph").at("availability"), "unavailable");
  EXPECT_TRUE(first.at("graph").at("graph_total_enqueued").is_null());
  EXPECT_EQ(first.at("rate_availability").at("state"), "unavailable");
  EXPECT_TRUE(first.at("qualified_rates").empty());
  EXPECT_NE(first.at("graph")
                .at("unavailable_reason")
                .get<std::string>()
                .find("overflow"),
            std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  const auto second = collector.GetMetricsSnapshot();
  EXPECT_EQ(second.at("graph").at("availability"), "unavailable");
  EXPECT_EQ(second.at("rate_availability").at("state"), "unavailable");
  EXPECT_TRUE(second.at("qualified_rates").empty());
}

TEST(GraphSnapshotCollectorContractTest,
     RatesRequireCompatibleSamplesCollectionDoesNotMutateAndUnsafeCountersAreNull) {
  auto owner = std::make_shared<ImmediateCompletionRuntimeOwner>();
  auto manager = owner->Manager(0);
  manager->AddNode(std::make_shared<MetricContractNode>(), "source");
  manager->AddNode(std::make_shared<MetricContractNode>(), "sink");
  using MetricPort = graph::Port<int, 0>;
  graph::PortFunction<MetricPort> output(graph::PortDirection::Output);
  graph::PortFunction<MetricPort> input(graph::PortDirection::Input);
  graph::DynamicEdgeConfig edge_config{
      .source = {.node_index = 0,
                 .descriptor = {.id = 0,
                                .name = "output",
                                .direction = graph::PortDirection::Output,
                                .payload_type =
                                    std::string(output.GetTypeName()),
                                .transport_type =
                                    std::string(
                                        output.GetTransportTypeName())},
                 .port = &output},
      .destination = {.node_index = 1,
                      .descriptor = {.id = 0,
                                     .name = "input",
                                     .direction =
                                         graph::PortDirection::Input,
                                     .payload_type =
                                         std::string(input.GetTypeName()),
                                     .transport_type =
                                         std::string(
                                             input.GetTransportTypeName())},
                      .port = &input},
      .capacity = 4};
  ASSERT_TRUE(manager->AddDynamicEdgeExpected(edge_config));

  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = {
                               {"nodes",
                                {{{"id", "source"}, {"type", "Source"}},
                                 {{"id", "sink"}, {"type", "Sink"}}}},
                               {"edges",
                                {{{"source_node_id", "source"},
                                  {"source_port", 0u},
                                  {"target_node_id", "sink"},
                                  {"target_port", 0u}}}}},
                           .config_revision = 94,
                           .config_etag = "\"identity-94\""})
                .status_code,
            200);
  ASSERT_EQ(session->SnapshotGeneration().graph_manager, manager);
  auto edge_metrics = manager->GetEdgeMetrics(0);
  ASSERT_NE(edge_metrics, nullptr);
  auto &mutable_edge_metrics = const_cast<graph::EdgeMetrics &>(*edge_metrics);

  graph::dashboard::GraphSnapshotCollector collector;
  collector.BindRuntimeSession(session);
  const auto initial = collector.GetMetricsSnapshot();
  ASSERT_EQ(initial.at("edges").size(), 1u);
  EXPECT_EQ(initial.at("edges").front().at("edge_id"),
            "source:0->sink:0");
  EXPECT_EQ(initial.at("edges").front().at("current_queue_depth"), 0);

  auto &runtime_edge = manager->GetEdges().front();
  ASSERT_TRUE(runtime_edge->Init());
  ASSERT_TRUE(runtime_edge->Start());
  for (int value = 0; value < 4; ++value) {
    ASSERT_TRUE(output.GetQueue().Enqueue(value));
    for (int attempt = 0;
         attempt < 200 &&
         input.GetQueue().Size() != static_cast<std::size_t>(value + 1);
         ++attempt)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(input.GetQueue().Size(), static_cast<std::size_t>(value + 1));
  }
  ASSERT_TRUE(output.GetQueue().Enqueue(4));
  for (int attempt = 0;
       attempt < 200 &&
       (input.GetQueue().Size() != 4 ||
        edge_metrics->backpressure_events.load() == 0);
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_EQ(input.GetQueue().Size(), 4u);
  ASSERT_GT(edge_metrics->backpressure_events.load(), 0u);
  const auto full = collector.GetMetricsSnapshot();
  ASSERT_EQ(full.at("edges").size(), 1u);
  EXPECT_EQ(full.at("edges").front().at("current_queue_depth"), 4);
  EXPECT_GE(full.at("edges").front().at("peak_queue_depth"), 4);
  EXPECT_GT(full.at("edges").front().at("backpressure_events"), 0);

  runtime_edge->Stop();
  ASSERT_TRUE(runtime_edge->JoinWithTimeout(std::chrono::seconds(1)));
  int drained_value = 0;
  std::size_t drained_count = 0;
  while (input.GetQueue().DequeueNonBlocking(drained_value))
    ++drained_count;
  EXPECT_EQ(drained_count, 4u);
  const auto drained = collector.GetMetricsSnapshot();
  EXPECT_EQ(drained.at("edges").front().at("current_queue_depth"), 0);
  EXPECT_GE(drained.at("edges").front().at("peak_queue_depth"), 4);
  EXPECT_FALSE(drained.at("edges").front().at("thread_active"));
  session->SetStateForTesting(
      graph::dashboard::GraphRuntimeSession::State::stopped);
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = {
                               {"nodes",
                                {{{"id", "source"}, {"type", "Source"}},
                                 {{"id", "sink"}, {"type", "Sink"}}}},
                               {"edges",
                                {{{"source_node_id", "source"},
                                  {"source_port", 0u},
                                  {"target_node_id", "sink"},
                                  {"target_port", 0u}}}}},
                           .config_revision = 95,
                           .config_etag = "\"identity-95-rebuilt\""})
                .status_code,
            200);
  const auto rebuilt = collector.GetMetricsSnapshot();
  ASSERT_EQ(rebuilt.at("edges").size(), 1u);
  EXPECT_EQ(rebuilt.at("edges").front().at("edge_id"),
            "source:0->sink:0");
  EXPECT_EQ(rebuilt.at("edges").front().at("current_queue_depth"), 0);

  // Reset only the counters used by the deterministic interval assertions
  // below; collection itself must never reset producer-owned state.
  mutable_edge_metrics.messages_enqueued.store(5);
  mutable_edge_metrics.messages_dequeued.store(2);

  const auto first = collector.GetMetricsSnapshot();
  EXPECT_EQ(first.at("rate_availability").at("state"), "unavailable");
  EXPECT_TRUE(first.at("qualified_rates").empty());
  EXPECT_EQ(edge_metrics->messages_enqueued.load(), 5u);
  EXPECT_EQ(edge_metrics->messages_dequeued.load(), 2u);

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  mutable_edge_metrics.messages_enqueued.store(8);
  mutable_edge_metrics.messages_dequeued.store(4);
  const auto second = collector.GetMetricsSnapshot();
  EXPECT_EQ(second.at("rate_availability").at("state"), "available");
  EXPECT_EQ(second.at("collection_interval").at("clock"), "steady_clock");
  ASSERT_EQ(second.at("qualified_rates").size(), 2u);
  EXPECT_EQ(edge_metrics->messages_enqueued.load(), 8u);
  EXPECT_EQ(edge_metrics->messages_dequeued.load(), 4u);

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  mutable_edge_metrics.messages_enqueued.store(7);
  const auto regressed = collector.GetMetricsSnapshot();
  EXPECT_EQ(regressed.at("rate_availability").at("state"), "unavailable");
  EXPECT_TRUE(regressed.at("qualified_rates").empty());

  ASSERT_EQ(session->Start().status_code, 202);
  const auto incompatible = collector.GetMetricsSnapshot();
  EXPECT_EQ(incompatible.at("rate_availability").at("state"),
            "unavailable");
  EXPECT_TRUE(incompatible.at("qualified_rates").empty());

  constexpr std::uint64_t kFirstUnsafeJavascriptInteger =
      9'007'199'254'740'992ULL;
  mutable_edge_metrics.messages_enqueued.store(
      kFirstUnsafeJavascriptInteger);
  const auto unsafe = collector.GetMetricsSnapshot();
  EXPECT_EQ(unsafe.at("graph").at("availability"), "unavailable");
  EXPECT_TRUE(unsafe.at("graph").at("graph_total_enqueued").is_null());
  EXPECT_FALSE(
      unsafe.at("graph").at("unavailable_reason").get<std::string>().empty());
}

TEST(GraphRuntimeSessionIdentityTest,
     DuplicateCanonicalIdentityIsUnavailableRatherThanAmbiguous) {
  auto owner = std::make_shared<ImmediateCompletionRuntimeOwner>();
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  const nlohmann::json graph = {
      {"nodes",
       {{{"id", "same"}, {"type", "A"}},
        {{"id", "same"}, {"type", "B"}}}},
      {"edges", nlohmann::json::array()}};
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = graph,
                           .config_revision = 92,
                           .config_etag = "\"identity-92\""})
                .status_code,
            200);
  const auto captured = session->SnapshotGeneration();
  EXPECT_FALSE(captured.identity_error.empty());
  EXPECT_TRUE(captured.canonical_node_ids.empty());
}

class IdentityTestNode final : public graph::INode {
public:
  graph::LifecycleState GetLifecycleState() const override {
    return graph::LifecycleState::Uninitialized;
  }
  bool Init() override { return true; }
  bool Start() override { return true; }
  void Join() override {}
  bool JoinWithTimeout(std::chrono::milliseconds) override { return true; }
  void Stop() override {}
};

class CanonicalIdentityRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  explicit CanonicalIdentityRuntimeOwner(
      std::shared_ptr<graph::GraphManager> manager)
      : manager_(std::move(manager)) {}

  Result Rebuild(std::uint64_t, const BuildSnapshot &) override {
    return {200, "rebuilt", "canonical identity owner rebuilt", manager_};
  }
  Result Start(std::uint64_t, std::uint64_t) override {
    return {202, "started", "canonical identity owner started", manager_};
  }
  Result Stop(std::uint64_t) override {
    return {200, "stopped", "canonical identity owner stopped", manager_};
  }
  Result Shutdown(std::uint64_t) override {
    return {200, "shutdown", "canonical identity owner shut down", manager_};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion_ = std::move(callback);
  }

private:
  std::shared_ptr<graph::GraphManager> manager_;
  CompletionCallback completion_;
};

TEST(GraphRuntimeSessionIdentityTest,
     CollectorUsesRuntimeCanonicalIdsWhenRuntimeNodeOrderChanges) {
  auto manager = std::make_shared<graph::GraphManager>();
  manager->AddNode(std::make_shared<IdentityTestNode>(), "sink");
  manager->AddNode(std::make_shared<IdentityTestNode>(), "source");
  manager->EnableMetrics(true);
  auto owner = std::make_shared<CanonicalIdentityRuntimeOwner>(manager);
  auto session = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  session->MarkReady();
  const nlohmann::json graph = {
      {"nodes",
       {{{"id", "source"}, {"type", "Source"}},
        {{"id", "sink"}, {"type", "Sink"}}}},
      {"edges", nlohmann::json::array()}};
  ASSERT_EQ(session
                ->Rebuild({.receiver_graph = graph,
                           .config_revision = 93,
                           .config_etag = "\"identity-93\""})
                .status_code,
            200);

  graph::dashboard::GraphSnapshotCollector collector;
  collector.BindRuntimeSession(session);
  const auto metrics = collector.GetMetricsSnapshot();
  ASSERT_EQ(metrics.at("identity_availability").at("state"), "available");
  ASSERT_EQ(metrics.at("nodes").size(), 2u);
  EXPECT_EQ(metrics.at("nodes").at(0).at("node_id"), "sink");
  EXPECT_EQ(metrics.at("nodes").at(1).at("node_id"), "source");
  const auto diagnostics = collector.GetDiagnosticsSnapshot();
  ASSERT_EQ(diagnostics.at("nodes").size(), 2u);
  EXPECT_EQ(diagnostics.at("nodes").at(0).at("node_id"), "sink");
  EXPECT_EQ(diagnostics.at("nodes").at(1).at("node_id"), "source");
}

class LiveThreadRuntimeOwner final
    : public graph::dashboard::IGraphRuntimeOwner {
public:
  ~LiveThreadRuntimeOwner() override { (void)Shutdown(generation_); }

  Result Rebuild(std::uint64_t generation, const BuildSnapshot &) override {
    const std::lock_guard operation_lock(operation_mutex_);
    if (worker_.joinable())
      return {409, "execution_active", "live worker is active"};
    generation_ = generation;
    return {200, "rebuilt", "live owner rebuilt"};
  }

  Result Start(std::uint64_t generation, std::uint64_t run_epoch) override {
    const std::lock_guard operation_lock(operation_mutex_);
    if (generation != generation_ || worker_.joinable())
      return {409, "invalid_start", "live owner cannot start"};
    {
      std::unique_lock lock(state_mutex_);
      start_entered_ = true;
      state_cv_.notify_all();
      state_cv_.wait(lock, [this] { return start_released_; });
      finish_requested_ = false;
      natural_completion_ = false;
      worker_finished_ = false;
    }
    worker_ = std::jthread([this, generation, run_epoch](std::stop_token) {
      bool natural = false;
      {
        std::unique_lock lock(state_mutex_);
        state_cv_.wait(lock, [this] { return finish_requested_; });
        natural = natural_completion_;
      }
      if (completion_)
        completion_(generation, run_epoch, true,
                    natural ? "live execution completed"
                            : "live execution cancelled");
      {
        const std::lock_guard lock(state_mutex_);
        worker_finished_ = true;
      }
      state_cv_.notify_all();
    });
    return {202, "start_accepted", "live worker launched"};
  }

  Result Stop(std::uint64_t generation) override {
    const std::lock_guard operation_lock(operation_mutex_);
    if (generation != generation_ || !worker_.joinable())
      return {409, "invalid_stop", "live owner is not running"};
    Finish(false);
    worker_.join();
    return {200, "stop_completed", "live worker joined"};
  }

  Result Shutdown(std::uint64_t) override {
    const std::lock_guard operation_lock(operation_mutex_);
    if (worker_.joinable()) {
      Finish(false);
      worker_.join();
    }
    return {200, "shutdown_complete", "live owner shut down"};
  }

  void SetCompletionCallback(CompletionCallback callback) override {
    completion_ = std::move(callback);
  }

  void WaitForStartEntered() {
    std::unique_lock lock(state_mutex_);
    state_cv_.wait(lock, [this] { return start_entered_; });
  }
  void ReleaseStart() {
    {
      const std::lock_guard lock(state_mutex_);
      start_released_ = true;
    }
    state_cv_.notify_all();
  }
  void CompleteNaturally() { Finish(true); }
  void EmitCompletionForRun(std::uint64_t run_epoch) {
    if (completion_)
      completion_(generation_, run_epoch, true, "injected stale completion");
  }
  void WaitForWorkerFinished() {
    std::unique_lock lock(state_mutex_);
    state_cv_.wait(lock, [this] { return worker_finished_; });
  }

private:
  void Finish(bool natural) {
    {
      const std::lock_guard lock(state_mutex_);
      natural_completion_ = natural;
      finish_requested_ = true;
    }
    state_cv_.notify_all();
  }

  std::mutex operation_mutex_, state_mutex_;
  std::condition_variable state_cv_;
  std::jthread worker_;
  CompletionCallback completion_;
  std::uint64_t generation_ = 0;
  bool start_entered_ = false, start_released_ = false;
  bool finish_requested_ = false, natural_completion_ = false;
  bool worker_finished_ = false;
};

TEST(GraphRuntimeSessionOwnerTest,
     StartMilestoneAndStopIntentArbitrateLiveThreadTruthfully) {
  auto owner = std::make_shared<LiveThreadRuntimeOwner>();
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  ASSERT_EQ(session
                .Rebuild({.receiver_graph = nlohmann::json::object(),
                          .config_revision = 9,
                          .config_etag = "\"live-9\""})
                .status_code,
            200);

  graph::dashboard::GraphRuntimeSession::CommandResult start_result;
  std::jthread starter([&] { start_result = session.Start(); });
  owner->WaitForStartEntered();
  EXPECT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::starting);
  EXPECT_NE(session.LifecycleStateString(), "running");
  owner->ReleaseStart();
  starter.join();
  ASSERT_EQ(start_result.status_code, 202);
  ASSERT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::running);

  ASSERT_EQ(session.Stop().status_code, 200);
  const auto cancelled = session.SnapshotStatus();
  EXPECT_EQ(cancelled.state,
            graph::dashboard::GraphRuntimeSession::State::stopped);
  EXPECT_TRUE(cancelled.stop_requested);
  EXPECT_EQ(cancelled.terminal_generation, 1u);
  EXPECT_EQ(cancelled.terminal_result_code, "execution_cancelled");

  ASSERT_EQ(session.Start().status_code, 202);
  owner->EmitCompletionForRun(1);
  EXPECT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::running);
  owner->CompleteNaturally();
  owner->WaitForWorkerFinished();
  const auto completed = session.SnapshotStatus();
  EXPECT_EQ(completed.state,
            graph::dashboard::GraphRuntimeSession::State::completed);
  EXPECT_FALSE(completed.stop_requested);
  EXPECT_EQ(completed.terminal_result_code, "execution_completed");
}

TEST(FHSSGraphRuntimeOwnerConcurrencyTest,
     SameGenerationRestartWaitsForItsOwnStartupMilestone) {
  auto fixture = MakeProductionReceiverFixture();
  const auto runtime_directory = fixture.directory / "restart-runtime";
  auto owner = std::make_shared<dsp::fhss::dashboard::FHSSGraphRuntimeOwner>(
      std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY), runtime_directory);
  owner->SetExecutorTimeoutForTesting(std::chrono::seconds(60));
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  ASSERT_EQ(session.Rebuild(fixture.snapshot).status_code, 200);

  // Complete attempt one so its published startup milestone is deliberately
  // stale when the same executor and generation are started again.
  ASSERT_EQ(session.Start().status_code, 202);
  for (int attempt = 0; attempt < 60'000; ++attempt) {
    if (session.GetState() ==
        graph::dashboard::GraphRuntimeSession::State::completed)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::completed);
  ASSERT_EQ(session.SnapshotStatus().terminal_result_code,
            "execution_completed");
  const auto run1_snapshot = session.SnapshotGeneration();
  ASSERT_NE(run1_snapshot.graph_manager, nullptr);
  const auto run1_enqueued =
      run1_snapshot.graph_manager->GetMetrics().graph_total_enqueued.load();
  const auto run1_dequeued =
      run1_snapshot.graph_manager->GetMetrics().graph_total_dequeued.load();

  std::mutex gate_mutex;
  std::condition_variable gate_cv;
  bool worker_entered = false;
  bool release_worker = false;
  owner->SetBeforeExecuteHookForTesting([&] {
    std::unique_lock lock(gate_mutex);
    worker_entered = true;
    gate_cv.notify_all();
    gate_cv.wait(lock, [&] { return release_worker; });
  });
  std::atomic<bool> run2_traffic_observed{false};
  owner->SetAfterStartupHookForTesting(
      [&](const std::shared_ptr<graph::GraphManager> &manager) {
        for (int attempt = 0; attempt < 5'000; ++attempt) {
          const auto &metrics = manager->GetMetrics();
          if (metrics.graph_total_enqueued.load() != 0u ||
              metrics.graph_total_dequeued.load() != 0u) {
            run2_traffic_observed.store(true, std::memory_order_release);
            return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      });

  std::atomic<bool> start_returned{false};
  std::atomic<bool> stop_returned{false};
  graph::dashboard::GraphRuntimeSession::CommandResult start_result;
  graph::dashboard::IGraphRuntimeOwner::Result stop_result;
  std::jthread starter([&] {
    start_result = session.Start();
    start_returned.store(true, std::memory_order_release);
  });
  {
    std::unique_lock lock(gate_mutex);
    ASSERT_TRUE(gate_cv.wait_for(lock, std::chrono::seconds(5),
                                 [&] { return worker_entered; }));
  }

  // Attempt one's completed token must not let attempt two return before its
  // worker has even entered GraphExecutor::Init().
  EXPECT_FALSE(start_returned.load(std::memory_order_acquire));
  EXPECT_EQ(session.GetState(),
            graph::dashboard::GraphRuntimeSession::State::starting);
  EXPECT_NE(session.LifecycleStateString(), "running");

  // Invoke the owner concurrently to prove its operation mutex prevents Stop
  // from reaching GraphExecutor while attempt two is still before Init.
  std::jthread stopper([&] {
    stop_result = owner->Stop(1);
    stop_returned.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(stop_returned.load(std::memory_order_acquire))
      << "Stop bypassed the serialized pre-Init startup attempt";
  {
    const std::lock_guard lock(gate_mutex);
    release_worker = true;
  }
  gate_cv.notify_all();
  starter.join();
  stopper.join();

  EXPECT_EQ(start_result.status_code, 202) << start_result.code;
  EXPECT_EQ(stop_result.status_code, 200) << stop_result.code;
  const auto run2_snapshot = session.SnapshotGeneration();
  ASSERT_NE(run2_snapshot.graph_manager, nullptr);
  EXPECT_NE(run2_snapshot.graph_manager, run1_snapshot.graph_manager);
  EXPECT_EQ(run2_snapshot.generation, run1_snapshot.generation);
  EXPECT_EQ(run2_snapshot.config_revision, run1_snapshot.config_revision);
  EXPECT_EQ(run2_snapshot.config_etag, run1_snapshot.config_etag);
  EXPECT_EQ(
      run1_snapshot.graph_manager->GetMetrics().graph_total_enqueued.load(),
      run1_enqueued);
  EXPECT_EQ(
      run1_snapshot.graph_manager->GetMetrics().graph_total_dequeued.load(),
      run1_dequeued);
  EXPECT_TRUE(run2_traffic_observed.load(std::memory_order_acquire));
  EXPECT_GT(
      run2_snapshot.graph_manager->GetMetrics().graph_total_enqueued.load() +
          run2_snapshot.graph_manager->GetMetrics().graph_total_dequeued.load(),
      0u)
      << "restart traffic was not attributed to the replacement manager";
  for (int attempt = 0; attempt < 5'000; ++attempt) {
    if (session.GetState() !=
        graph::dashboard::GraphRuntimeSession::State::running)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto terminal = session.SnapshotStatus();
  EXPECT_EQ(terminal.terminal_generation, 1u);
  EXPECT_EQ(owner->Shutdown(0).status_code, 200);
}

TEST(FHSSGraphRuntimeOwnerTest,
     MalformedIqAlignmentCompletesAcceptedRunWithStableTerminalFailure) {
  auto fixture = MakeProductionReceiverFixture();
  {
    std::ofstream malformed(fixture.iq_path,
                            std::ios::binary | std::ios::trunc);
    malformed << "malformed";
  }
  auto owner = std::make_shared<dsp::fhss::dashboard::FHSSGraphRuntimeOwner>(
      std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY),
      fixture.directory / "malformed-runtime");
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  ASSERT_EQ(session.Rebuild(fixture.snapshot).status_code, 200);
  ASSERT_EQ(session.Start().status_code, 202);
  for (int attempt = 0; attempt < 5'000; ++attempt) {
    if (session.GetState() ==
        graph::dashboard::GraphRuntimeSession::State::failed)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto terminal = session.SnapshotStatus();
  EXPECT_EQ(terminal.state,
            graph::dashboard::GraphRuntimeSession::State::failed);
  EXPECT_EQ(terminal.terminal_result_code, "execution_failed");
  EXPECT_EQ(terminal.terminal_result_message,
            "receiver_input_preflight_failed: IQ byte length is not aligned "
            "to cf32_le complex samples");
  EXPECT_EQ(terminal.terminal_generation, 1u);
  EXPECT_EQ(terminal.active_run_epoch, 1u);
  EXPECT_EQ(owner->Shutdown(0).status_code, 200);
}

TEST(FHSSGraphRuntimeOwnerTest,
     ExecutorTimeoutWithoutCompletionSignalIsTerminalFailure) {
  auto fixture = MakeProductionReceiverFixture();
  auto owner = std::make_shared<dsp::fhss::dashboard::FHSSGraphRuntimeOwner>(
      std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY),
      fixture.directory / "timeout-runtime");
  // The synthetic fixture may complete in under a second in an optimized
  // container. Use a short positive timeout so this test exercises the
  // incomplete/no-completion-signal path rather than host performance.
  owner->SetExecutorTimeoutForTesting(std::chrono::milliseconds(1));
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  ASSERT_EQ(session.Rebuild(fixture.snapshot).status_code, 200);
  ASSERT_EQ(session.Start().status_code, 202);
  for (int attempt = 0; attempt < 5'000; ++attempt) {
    if (session.GetState() ==
        graph::dashboard::GraphRuntimeSession::State::failed)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto terminal = session.SnapshotStatus();
  EXPECT_EQ(terminal.state,
            graph::dashboard::GraphRuntimeSession::State::failed);
  EXPECT_EQ(terminal.terminal_result_code, "execution_failed");
  EXPECT_EQ(terminal.terminal_result_message,
            "receiver_execution_incomplete: executor stopped before graph "
            "completion signal");
  EXPECT_EQ(terminal.terminal_generation, 1u);
  EXPECT_EQ(terminal.active_run_epoch, 1u);
  EXPECT_EQ(owner->Shutdown(0).status_code, 200);
}

TEST(FHSSGraphRuntimeOwnerConcurrencyTest,
     CooperativeStopUsesOneGraphExecutorTeardownEntry) {
  auto fixture = MakeProductionReceiverFixture();
  auto owner = std::make_shared<dsp::fhss::dashboard::FHSSGraphRuntimeOwner>(
      std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY),
      fixture.directory / "cooperative-stop-runtime");
  owner->SetExecutorTimeoutForTesting(std::chrono::seconds(60));
  graph::dashboard::GraphRuntimeSession session(owner);
  session.MarkReady();
  ASSERT_EQ(session.Rebuild(fixture.snapshot).status_code, 200);
  ASSERT_EQ(session.Start().status_code, 202);

  const auto started = std::chrono::steady_clock::now();
  const auto stopped = session.Stop();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  EXPECT_EQ(stopped.status_code, 200) << stopped.code << ": "
                                      << stopped.message;
  EXPECT_LT(elapsed, std::chrono::seconds(5));
  EXPECT_EQ(owner->StopSequenceCountForTesting(), 1u)
      << "the control thread must request cancellation, not enter a second "
         "GraphExecutor teardown sequence";
  EXPECT_EQ(session.SnapshotStatus().terminal_result_code,
            "execution_cancelled");

  EXPECT_EQ(owner->Shutdown(0).status_code, 200);
  EXPECT_EQ(owner->StopSequenceCountForTesting(), 1u)
      << "Shutdown of a retired execution must remain request-only";
}

TEST(FHSSGraphRuntimeOwnerConcurrencyTest,
     CanonicalReceiverTrafficSerializesStartStopRebuildAndShutdown) {
  auto fixture = MakeProductionReceiverFixture();
  const auto runtime_directory = fixture.directory / "runtime";
  dsp::fhss::dashboard::FHSSGraphRuntimeOwner owner(
      std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY), runtime_directory);

  std::mutex callback_mutex;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> callbacks;
  owner.SetCompletionCallback([&](std::uint64_t generation,
                                  std::uint64_t run_epoch, bool, std::string) {
    const std::lock_guard lock(callback_mutex);
    callbacks.emplace_back(generation, run_epoch);
  });

  const auto built = owner.Rebuild(1, fixture.snapshot);
  ASSERT_EQ(built.status_code, 200) << built.code << ": " << built.message;
  ASSERT_NE(built.graph_manager, nullptr);
  const auto started = owner.Start(1, 1);
  ASSERT_EQ(started.status_code, 202)
      << started.code << ": " << started.message;
  ASSERT_EQ(started.graph_manager, built.graph_manager);

  bool traffic_observed = false;
  for (int attempt = 0; attempt < 30'000; ++attempt) {
    const auto &metrics = built.graph_manager->GetMetrics();
    if (metrics.graph_total_enqueued.load() != 0u ||
        metrics.graph_total_dequeued.load() != 0u) {
      traffic_observed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (!traffic_observed) {
    (void)owner.Shutdown(0);
    FAIL() << "the production receiver never published edge traffic";
  }

  // Start has crossed the production thread-launch milestone. Race all three
  // operations that can retire or replace that same live execution.
  std::barrier rendezvous(4);
  std::array<graph::dashboard::IGraphRuntimeOwner::Result, 3> results;
  std::jthread stop([&] {
    rendezvous.arrive_and_wait();
    results[0] = owner.Stop(1);
  });
  std::jthread rebuild([&] {
    rendezvous.arrive_and_wait();
    results[1] = owner.Rebuild(2, fixture.snapshot);
  });
  std::jthread shutdown([&] {
    rendezvous.arrive_and_wait();
    results[2] = owner.Shutdown(1);
  });
  rendezvous.arrive_and_wait();
  stop.join();
  rebuild.join();
  shutdown.join();

  for (const auto &result : results) {
    EXPECT_GE(result.status_code, 200);
    EXPECT_LE(result.status_code, 599);
    EXPECT_FALSE(result.code.empty());
  }
  const auto retired = owner.Shutdown(0);
  ASSERT_EQ(retired.status_code, 200)
      << retired.code << ": " << retired.message;
  std::size_t callback_count = 0;
  {
    const std::lock_guard lock(callback_mutex);
    callback_count = callbacks.size();
    ASSERT_FALSE(callbacks.empty());
    for (const auto &[generation, run_epoch] : callbacks) {
      EXPECT_EQ(generation, 1u);
      EXPECT_EQ(run_epoch, 1u);
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    const std::lock_guard lock(callback_mutex);
    EXPECT_EQ(callbacks.size(), callback_count)
        << "a callback escaped after all production threads retired";
  }
}

} // namespace
