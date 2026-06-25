// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                           \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
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

class DashboardServerStep3Test : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step3_assets");
    auto config = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ = std::make_shared<graph::dashboard::GraphConfigurationService>(config);
    runtime_session_ = std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ = std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
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
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(DashboardServerStep3Test, RebuildAcceptedRejectedStateMatrix) {
  const std::array<graph::dashboard::GraphRuntimeSession::State, 4> accepted_states = {
      graph::dashboard::GraphRuntimeSession::State::not_built,
      graph::dashboard::GraphRuntimeSession::State::stopped,
      graph::dashboard::GraphRuntimeSession::State::completed,
      graph::dashboard::GraphRuntimeSession::State::failed};

  for (std::size_t i = 0; i < accepted_states.size(); ++i) {
    runtime_session_->SetStateForTesting(accepted_states[i]);
    const auto response = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                      RebuildRequestJson("cmd-matrix-accept-" + std::to_string(i)).dump());
    EXPECT_EQ(response.status_code, 202) << response.body;
  }

  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::running);
  const auto running_response = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                            RebuildRequestJson("cmd-matrix-reject-running").dump());
  EXPECT_EQ(running_response.status_code, 409) << running_response.body;
  const auto running_json = nlohmann::json::parse(running_response.body);
  EXPECT_EQ(running_json.at("code").get<std::string>(), "invalid_state");

  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::rebuilding);
  const auto rebuilding_response = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                               RebuildRequestJson("cmd-matrix-reject-rebuilding").dump());
  EXPECT_EQ(rebuilding_response.status_code, 409) << rebuilding_response.body;
  const auto rebuilding_json = nlohmann::json::parse(rebuilding_response.body);
  EXPECT_EQ(rebuilding_json.at("code").get<std::string>(), "invalid_state");
}

TEST_F(DashboardServerStep3Test, InvalidOrFailedRebuildHasNoRuntimeGenerationSideEffects) {
  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::running);
  const auto before_invalid = runtime_session_->SnapshotStatus();
  const auto invalid = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                   RebuildRequestJson("cmd-invalid-running").dump());
  EXPECT_EQ(invalid.status_code, 409) << invalid.body;
  const auto after_invalid = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_invalid.state, before_invalid.state);
  EXPECT_EQ(after_invalid.active_generation, before_invalid.active_generation);
  EXPECT_EQ(after_invalid.rebuild_attempts, before_invalid.rebuild_attempts);

  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::stopped);
  runtime_session_->InjectNextExecutorConstructionFailureForTesting();
  const auto before_failure = runtime_session_->SnapshotStatus();
  const auto failure = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                   RebuildRequestJson("cmd-fail-construction").dump());
  EXPECT_EQ(failure.status_code, 500) << failure.body;
  const auto failure_json = nlohmann::json::parse(failure.body);
  EXPECT_EQ(failure_json.at("code").get<std::string>(), "executor_construction_failed");
  const auto after_failure = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_failure.state, before_failure.state);
  EXPECT_EQ(after_failure.active_generation, before_failure.active_generation);
  EXPECT_EQ(after_failure.successful_rebuilds, before_failure.successful_rebuilds);
}

TEST_F(DashboardServerStep3Test, ActivationOccursOnlyAfterSuccessfulConstructionAndControlsWork) {
  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::not_built);
  const auto before = runtime_session_->SnapshotStatus();

  runtime_session_->InjectNextExecutorConstructionFailureForTesting();
  const auto failed_rebuild = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                          RebuildRequestJson("cmd-activation-fail").dump());
  EXPECT_EQ(failed_rebuild.status_code, 500) << failed_rebuild.body;
  const auto after_failed = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_failed.active_generation, before.active_generation);

  const auto successful_rebuild = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                              RebuildRequestJson("cmd-activation-pass").dump());
  EXPECT_EQ(successful_rebuild.status_code, 202) << successful_rebuild.body;
  const auto after_success = runtime_session_->SnapshotStatus();
  EXPECT_EQ(after_success.state, graph::dashboard::GraphRuntimeSession::State::stopped);
  EXPECT_EQ(after_success.active_generation, before.active_generation + 1);

  const auto start = HttpRequest(server_->BoundPort(), "POST", "/api/v1/commands/start", "{}");
  EXPECT_EQ(start.status_code, 202) << start.body;
  const auto running_status = HttpRequest(server_->BoundPort(), "GET", "/api/v1/status");
  EXPECT_EQ(running_status.status_code, 200) << running_status.body;
  const auto running_json = nlohmann::json::parse(running_status.body);
  EXPECT_EQ(running_json.at("lifecycle_state").get<std::string>(), "running");

  const auto stop = HttpRequest(server_->BoundPort(), "POST", "/api/v1/commands/stop", "{}");
  EXPECT_EQ(stop.status_code, 202) << stop.body;
  const auto stopped_status = HttpRequest(server_->BoundPort(), "GET", "/api/v1/status");
  EXPECT_EQ(stopped_status.status_code, 200) << stopped_status.body;
  const auto stopped_json = nlohmann::json::parse(stopped_status.body);
  EXPECT_EQ(stopped_json.at("lifecycle_state").get<std::string>(), "stopped");
}

TEST_F(DashboardServerStep3Test, CleanupFailedStateBlocksFurtherRebuilds) {
  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::stopped);
  runtime_session_->InjectNextCleanupFailureForTesting();

  const auto first = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                 RebuildRequestJson("cmd-cleanup-fail").dump());
  EXPECT_EQ(first.status_code, 202) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  EXPECT_EQ(first_json.at("status").get<std::string>(), "succeeded_with_cleanup_failed");

  const auto status = HttpRequest(server_->BoundPort(), "GET", "/api/v1/status");
  EXPECT_EQ(status.status_code, 200) << status.body;
  const auto status_json = nlohmann::json::parse(status.body);
  EXPECT_EQ(status_json.at("lifecycle_state").get<std::string>(), "cleanup_failed");
  EXPECT_TRUE(status_json.at("rebuild_blocked").get<bool>());

  const auto blocked = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                   RebuildRequestJson("cmd-cleanup-blocked").dump());
  EXPECT_EQ(blocked.status_code, 409) << blocked.body;
  const auto blocked_json = nlohmann::json::parse(blocked.body);
  EXPECT_EQ(blocked_json.at("code").get<std::string>(), "cleanup_failed");
}

TEST_F(DashboardServerStep3Test, FailureInjectionChecksAreHandledSafely) {
  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::stopped);
  runtime_session_->InjectNextQueueDisableFailureForTesting();
  const auto queue_disable = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                         RebuildRequestJson("cmd-queue-disable").dump());
  EXPECT_EQ(queue_disable.status_code, 500) << queue_disable.body;
  const auto queue_disable_json = nlohmann::json::parse(queue_disable.body);
  EXPECT_EQ(queue_disable_json.at("code").get<std::string>(), "queue_disable_failed");

  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::stopped);
  runtime_session_->InjectShutdownDuringNextRebuildForTesting();
  const auto shutdown = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                    RebuildRequestJson("cmd-sigterm").dump());
  EXPECT_EQ(shutdown.status_code, 503) << shutdown.body;
  const auto shutdown_json = nlohmann::json::parse(shutdown.body);
  EXPECT_EQ(shutdown_json.at("code").get<std::string>(), "shutdown_in_progress");
  const auto readyz = HttpRequest(server_->BoundPort(), "GET", "/readyz");
  EXPECT_EQ(readyz.status_code, 503) << readyz.body;

  runtime_session_->SetStateForTesting(graph::dashboard::GraphRuntimeSession::State::stopped);
  runtime_session_->InjectNextThreadInterruptionForTesting();
  const auto interrupted = HttpRequest(server_->BoundPort(), "POST", "/api/v1/config/rebuild",
                                       RebuildRequestJson("cmd-thread-interrupt").dump());
  EXPECT_EQ(interrupted.status_code, 503) << interrupted.body;
  const auto interrupted_json = nlohmann::json::parse(interrupted.body);
  EXPECT_EQ(interrupted_json.at("code").get<std::string>(), "thread_interrupted");
}

} // namespace
