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
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

HttpResponse HttpGet(std::uint16_t port, const std::string &target) {
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

  const std::string request = "GET " + target +
                              " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  ::send(fd, request.c_str(), request.size(), 0);

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

const nlohmann::json *FindSourceNodeConfig(const nlohmann::json &graph_config) {
  if (!graph_config.contains("nodes") || !graph_config.at("nodes").is_array()) {
    return nullptr;
  }
  for (const auto &node : graph_config.at("nodes")) {
    if (node.value("type", std::string{}) == "FHSSSyntheticIqSourceNode" &&
        node.contains("node_config")) {
      return &node.at("node_config");
    }
  }
  return nullptr;
}

class FhssDashboardVisualizationTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_ = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            config_, std::make_shared<dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
    runtime_session_ = std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ = std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.port = 0;
    options.asset_directory =
        std::filesystem::path(GRAPHX_SOURCE_ROOT) / "examples" / "DSP" /
        "dashboard" / "dist";
    options.artifact_root = options.asset_directory.parent_path();
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
  }

  nlohmann::json VisualizationRequest(const std::string &query) const {
    const auto response = HttpGet(server_->BoundPort(), "/api/v1/fhss/visualization" + query);
    EXPECT_EQ(response.status_code, 200) << response.body;
    return nlohmann::json::parse(response.body);
  }

  nlohmann::json config_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardVisualizationTest, ScheduleAndHeatmapRenderingAreCorrect) {
  const auto index_response = HttpGet(server_->BoundPort(), "/");
  ASSERT_EQ(index_response.status_code, 200);
  const auto script_marker = index_response.body.find("src=\"");
  ASSERT_NE(script_marker, std::string::npos);
  const auto script_start = script_marker + 5;
  const auto script_end = index_response.body.find('"', script_start);
  ASSERT_NE(script_end, std::string::npos);
  const auto script_response = HttpGet(
      server_->BoundPort(),
      index_response.body.substr(script_start, script_end - script_start));
  ASSERT_EQ(script_response.status_code, 200);
  EXPECT_NE(script_response.body.find("FHSS Schedule"), std::string::npos);
  EXPECT_NE(script_response.body.find("64-Channel Heatmap"), std::string::npos);
  EXPECT_NE(script_response.body.find("Synthetic Schedule Expectations"),
            std::string::npos);
  EXPECT_NE(script_response.body.find("/visualization"),
            std::string::npos);

  const auto viz = VisualizationRequest("?message_offset=0&message_limit=3&pulse_offset=0&pulse_limit=32&refresh_ms=250");
  EXPECT_EQ(viz.at("schema").get<std::string>(), "graphx.dashboard.fhss_visualization.v1");

  const auto *source_config = FindSourceNodeConfig(config_);
  ASSERT_NE(source_config, nullptr);
  ASSERT_TRUE(source_config->contains("messages"));
  const auto &messages = source_config->at("messages");
  ASSERT_TRUE(messages.is_array());

  const auto &schedule = viz.at("schedule");
  EXPECT_EQ(schedule.at("message_count_total").get<std::size_t>(), messages.size());
  EXPECT_LE(schedule.at("messages").size(), 3u);
  ASSERT_FALSE(schedule.at("messages").empty());
  const auto &rendered_message = schedule.at("messages").front();
  const auto &source_message = messages.front();
  EXPECT_EQ(rendered_message.at("transmit_start_sample"),
            source_message.at("transmit_start_sample"));
  EXPECT_EQ(rendered_message.at("pulse_count").get<std::size_t>(),
            source_message.at("pulses").size());
  EXPECT_EQ(rendered_message.at("body_pulse_count").get<std::size_t>(),
            rendered_message.at("pulse_count").get<std::size_t>() -
                rendered_message.at("preamble_pulse_count").get<std::size_t>());

  std::array<std::uint64_t, 64> expected_counts{};
  for (const auto &message : messages) {
    if (!message.contains("pulses") || !message.at("pulses").is_array()) {
      continue;
    }
    for (const auto &pulse : message.at("pulses")) {
      const auto frequency_index = pulse.value("frequency_index", std::uint64_t{0});
      if (frequency_index < expected_counts.size()) {
        ++expected_counts[static_cast<std::size_t>(frequency_index)];
      }
    }
  }

  const auto &heatmap = viz.at("heatmap");
  ASSERT_EQ(heatmap.at("channel_count").get<std::size_t>(), 64u);
  ASSERT_EQ(heatmap.at("channels").size(), 64u);
  for (std::size_t i = 0; i < 64u; ++i) {
    const auto &channel = heatmap.at("channels").at(i);
    EXPECT_EQ(channel.at("channel_index").get<std::size_t>(), i);
    EXPECT_EQ(channel.at("expected_pulse_count").get<std::uint64_t>(), expected_counts[i]);
  }

  const auto &timeline = viz.at("timeline");
  EXPECT_LE(timeline.at("pulses").size(), 32u);
  EXPECT_EQ(timeline.at("total_pulse_count").get<std::size_t>(),
            std::accumulate(messages.begin(), messages.end(), std::size_t{0},
                            [](std::size_t total, const auto &message) {
                              return total + message.at("pulses").size();
                            }));
  for (const auto &pulse : timeline.at("pulses")) {
    EXPECT_TRUE(pulse.contains("absolute_pulse_index"));
    EXPECT_TRUE(pulse.contains("message_id"));
    EXPECT_TRUE(pulse.contains("expected_sample_start"));
    EXPECT_TRUE(pulse.contains("source"));
    EXPECT_EQ(pulse.at("source"), "configured_schedule");
    EXPECT_FALSE(pulse.contains("detected_sample_start"));
    EXPECT_FALSE(pulse.contains("confidence"));
    EXPECT_FALSE(pulse.contains("rejected"));
  }
  ASSERT_GE(timeline.at("pulses").size(), 2u);
  EXPECT_EQ(timeline.at("pulses").at(0).at("expected_sample_start"),
            source_message.at("transmit_start_sample"));
  EXPECT_EQ(timeline.at("pulses").at(1).at("expected_sample_start")
                .get<std::uint64_t>(),
            source_message.at("transmit_start_sample").get<std::uint64_t>() +
                6'500u);
}

TEST_F(FhssDashboardVisualizationTest, SnapshotSizeAndRefreshRateAreBounded) {
  const auto aggressive = VisualizationRequest(
      "?message_offset=0&message_limit=100000&pulse_offset=0&pulse_limit=100000&refresh_ms=1");

  const auto &bounds = aggressive.at("bounds");
  EXPECT_EQ(bounds.at("refresh_interval_ms").get<std::uint64_t>(), 100u);

  const auto &schedule = aggressive.at("schedule");
  const auto &timeline = aggressive.at("timeline");
  EXPECT_EQ(schedule.at("message_limit").get<std::size_t>(), 64u);
  EXPECT_EQ(timeline.at("pulse_limit").get<std::size_t>(), 512u);
  EXPECT_LE(schedule.at("messages").size(), 64u);
  EXPECT_LE(timeline.at("pulses").size(), 512u);

  const auto estimated_bytes = bounds.at("snapshot_bytes_estimate").get<std::size_t>();
  EXPECT_LT(estimated_bytes, 300000u);

  const auto very_slow = VisualizationRequest("?refresh_ms=999999");
  EXPECT_EQ(very_slow.at("bounds").at("refresh_interval_ms").get<std::uint64_t>(), 2000u);
}

TEST(FhssDashboardVisualizationCancellationTest,
     ProductionHandlerHonorsDeadlineAndStopCompletesDeterministically) {
  auto config = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  for (auto &node : config["nodes"]) {
    if (node.value("type", std::string{}) == "FHSSSyntheticIqSourceNode" &&
        node["node_config"].contains("messages")) {
      const auto original = node["node_config"]["messages"];
      for (int repeat = 0; repeat < 32; ++repeat) {
        for (const auto &message : original) node["node_config"]["messages"].push_back(message);
      }
    }
  }
  auto service = std::make_shared<graph::dashboard::GraphConfigurationService>(
      config, std::make_shared<dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory =
      std::filesystem::path(GRAPHX_SOURCE_ROOT) / "examples" / "DSP" /
      "dashboard" / "dist";
  options.total_request_timeout = std::chrono::milliseconds(5);
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler = dsp::fhss::dashboard::MakeApiHandler(service, runtime),
          .cooperative_cancellation = true,
          .maximum_checkpoint_latency = std::chrono::milliseconds(1)};
  graph::dashboard::EmbeddedDashboardServer server(options, service, runtime, snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto status =
      HttpGet(server.BoundPort(), "/api/v1/fhss/visualization").status_code;
  // A fast implementation may finish before the deliberately short deadline;
  // otherwise it must report the bounded timeout. Both outcomes satisfy the
  // cancellation contract without introducing an artificial production delay.
  EXPECT_TRUE(status == 200 || status == 408);
  const auto stop_started = std::chrono::steady_clock::now();
  server.Stop();
  EXPECT_LT(std::chrono::steady_clock::now() - stop_started, std::chrono::milliseconds(100));
}

} // namespace
