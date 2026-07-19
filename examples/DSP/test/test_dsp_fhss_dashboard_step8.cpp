// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "FHSSDashboardApi.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"
#include "FHSSDashboardConfigurationPolicy.hpp"

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
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

namespace {

struct HttpResponse {
  int status_code = 0;
  std::string body;
};

nlohmann::json LoadJsonFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

HttpResponse HttpRequest(std::uint16_t port,
                         const std::string &method,
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

class FhssDashboardArtifactExportTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    artifact_root_ = std::filesystem::temp_directory_path() /
                     ("graphx_step8_artifacts_" + std::to_string(timestamp));

    config_ = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            config_, std::make_shared<dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
    runtime_session_ = std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ = std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.enable_mutating_routes = true;
    options.port = 0;
    options.asset_directory =
        std::filesystem::path(GRAPHX_SOURCE_ROOT) / "examples" / "DSP" / "dashboard";
    options.artifact_root = artifact_root_;
    options.application_api_handler =
        graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
            .handler = dsp::fhss::dashboard::MakeApiHandler(configuration_service_,
                                                             runtime_session_),
            .cooperative_cancellation = true,
            .maximum_checkpoint_latency = std::chrono::milliseconds(5)};

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
    std::filesystem::remove_all(artifact_root_, error);
  }

  nlohmann::json VisualizationRequest(const std::string &query) const {
    const auto response =
        HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/visualization" + query);
    EXPECT_EQ(response.status_code, 200) << response.body;
    return nlohmann::json::parse(response.body);
  }

  nlohmann::json PostJson(const std::string &target,
                          const nlohmann::json &payload,
                          int *status_code) const {
    const auto response =
        HttpRequest(server_->BoundPort(), "POST", target, payload.dump());
    if (status_code) {
      *status_code = response.status_code;
    }
    return nlohmann::json::parse(response.body);
  }

  nlohmann::json config_;
  std::filesystem::path artifact_root_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardArtifactExportTest, VisualizationDoesNotFabricateReceiverObservations) {
  const auto viz = VisualizationRequest(
      "?message_offset=0&message_limit=3&pulse_offset=0&pulse_limit=64&refresh_ms=250");

  EXPECT_FALSE(viz.contains("decoder"));
  EXPECT_FALSE(viz.contains("selected_channel_preview"));
  EXPECT_FALSE(viz.contains("spectrum_bins"));
  ASSERT_TRUE(viz.contains("timeline"));
  for (const auto &entry : viz.at("timeline").at("pulses")) {
    EXPECT_EQ(entry.at("source"), "configured_schedule");
    EXPECT_FALSE(entry.contains("detected_sample_start"));
    EXPECT_FALSE(entry.contains("confidence"));
    EXPECT_FALSE(entry.contains("viterbi"));
  }

  ASSERT_TRUE(viz.contains("fixture_label"));
  EXPECT_NE(viz.at("fixture_label").get<std::string>().find("Deterministic GraphX CPU FHSS fixture"),
            std::string::npos);
}

TEST_F(FhssDashboardArtifactExportTest, ArtifactBundleExportHonorsContainmentAndSigmfOptIn) {
  const auto output_path = artifact_root_ / "bundle" / "dashboard_bundle.json";
  int status_code = 0;
  const auto result = PostJson(
      "/api/v1/fhss/artifacts/bundle",
      nlohmann::json{{"output_path", output_path.string()},
                     {"include_sigmf_capture", true}},
      &status_code);

  EXPECT_EQ(status_code, 202) << result;
  EXPECT_EQ(result.at("schema").get<std::string>(),
            "graphx.dashboard.fhss_artifact_bundle_result.v1");
  EXPECT_EQ(result.at("status").get<std::string>(), "succeeded");
  ASSERT_TRUE(result.at("files").is_array());
  EXPECT_EQ(result.at("files").size(), 2u);

  EXPECT_TRUE(std::filesystem::exists(output_path));
  const auto bundle_json = LoadJsonFile(output_path);
  EXPECT_EQ(bundle_json.at("schema").get<std::string>(),
            "graphx.dashboard.fhss_artifact_bundle.v1");
  EXPECT_TRUE(bundle_json.at("sigmf_capture").at("enabled").get<bool>());
  EXPECT_FALSE(bundle_json.at("sigmf_capture").at("contains_raw_iq").get<bool>());
}

TEST_F(FhssDashboardArtifactExportTest, ArtifactBundleRejectsPathContainmentViolation) {
  const auto outside_root = std::filesystem::temp_directory_path() /
                            "graphx_step8_forbidden" / "bundle.json";
  int status_code = 0;
  const auto result = PostJson(
      "/api/v1/fhss/artifacts/bundle",
      nlohmann::json{{"output_path", outside_root.string()},
                     {"include_sigmf_capture", false}},
      &status_code);

  EXPECT_EQ(status_code, 400) << result;
  EXPECT_EQ(result.at("code").get<std::string>(), "artifact_path_not_allowed");
}

TEST_F(FhssDashboardArtifactExportTest, FailureInjectionReportsArtifactWriteFailure) {
  const auto output_path = artifact_root_ / "bundle" / "fail_bundle.json";
  int status_code = 0;
  const auto result = PostJson(
      "/api/v1/fhss/artifacts/bundle",
      nlohmann::json{{"output_path", output_path.string()},
                     {"include_sigmf_capture", true},
                     {"failure_injection", "enospc"}},
      &status_code);

  EXPECT_EQ(status_code, 500) << result;
  EXPECT_EQ(result.at("code").get<std::string>(), "artifact_write_failed");
}

} // namespace
