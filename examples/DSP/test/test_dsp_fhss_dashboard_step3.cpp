// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "../dashboard/FHSSGraphRuntimeOwner.hpp"
#include "FHSSDashboardConfigurationPolicy.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
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
  const auto generator_config =
      dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
          graph::JsonView(source->at("node_config")));
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(generator_config);
  if (!fixture)
    throw std::runtime_error("canonical synthetic IQ generation failed");

  constexpr std::size_t kReplaySamples = 1'048'576;
  std::ofstream output(result.iq_path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("failed to create production receiver IQ");
  for (std::size_t index = 0; index < kReplaySamples; ++index) {
    const auto sample = fixture->samples[index % fixture->samples.size()];
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
      {"max_complex_samples", kReplaySamples},
      {"max_read_complex_samples", kReplaySamples}};
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
  EXPECT_EQ(default_metrics_json.at("graph")
                .at("graph_total_enqueued")
                .get<std::uint64_t>(),
            0u);
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
  EXPECT_EQ(populated_metrics_json.at("graph")
                .at("graph_total_enqueued")
                .get<std::uint64_t>(),
            0u);
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
  EXPECT_EQ(collected.at("graph").at("peak_active_threads"), 22u);
  EXPECT_NE(collected.at("graph").at("peak_active_threads"), 11u);
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
  EXPECT_EQ(collected.at("graph").at("peak_active_threads"), 44u);
  diagnostics = collector->GetDiagnosticsSnapshot();
  EXPECT_EQ(diagnostics.at("active_generation"), current.generation);
  EXPECT_EQ(diagnostics.at("active_run_epoch"), current.run_epoch);
  EXPECT_NE(diagnostics.at("active_run_epoch"), run2.run_epoch);
  EXPECT_EQ(diagnostics.at("active_config_revision"), current.config_revision);
  EXPECT_EQ(diagnostics.at("active_config_etag"), current.config_etag);
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
  owner->SetExecutorTimeoutForTesting(std::chrono::seconds(1));
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
