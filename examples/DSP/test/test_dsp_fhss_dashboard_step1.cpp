// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

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

nlohmann::json MinimalGraphConfig() {
  return nlohmann::json{
      {"nodes", nlohmann::json::array({nlohmann::json{
                    {"id", "source"},
                    {"type", "FHSSSyntheticIqSourceNode"},
                    {"node_config", nlohmann::json::object()}}})},
      {"edges", nlohmann::json::array({nlohmann::json{{"source", "source"},
                                                      {"target", "sink"}}})}};
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

  const std::string request =
      "GET " + target +
      " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  ::send(fd, request.c_str(), request.size(), 0);

  std::string response;
  std::array<char, 2048> buffer{};
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

class FhssDashboardServerContractTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step1_assets");

    auto config_service =
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            MinimalGraphConfig());
    auto runtime_session =
        std::make_shared<graph::dashboard::GraphRuntimeSession>();
    auto snapshot_collector =
        std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.port = 0;
    options.asset_directory = assets_;

    server_ = std::make_unique<graph::dashboard::EmbeddedDashboardServer>(
        options, config_service, runtime_session, snapshot_collector);
    ASSERT_TRUE(server_->Start()) << server_->LastError();
    ASSERT_GT(server_->BoundPort(), 0u);

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
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardServerContractTest,
       SupportsEphemeralPortStartupAndAssetServing) {
  const auto root = HttpGet(server_->BoundPort(), "/");
  EXPECT_EQ(root.status_code, 200);
  EXPECT_NE(root.body.find("GraphX Dashboard Test"), std::string::npos);
}

TEST_F(FhssDashboardServerContractTest, HealthzAndReadyzStateEndpoints) {
  const auto health = HttpGet(server_->BoundPort(), "/healthz");
  EXPECT_EQ(health.status_code, 200);
  const auto health_json = nlohmann::json::parse(health.body);
  EXPECT_EQ(health_json.at("status").get<std::string>(), "ok");

  const auto ready = HttpGet(server_->BoundPort(), "/readyz");
  EXPECT_EQ(ready.status_code, 200);
  const auto ready_json = nlohmann::json::parse(ready.body);
  EXPECT_TRUE(ready_json.at("ready").get<bool>());
  EXPECT_EQ(ready_json.at("state").get<std::string>(), "ready");
}

TEST_F(FhssDashboardServerContractTest, GraphAndConfigResponseSchemas) {
  const auto graph = HttpGet(server_->BoundPort(), "/api/v1/graph");
  EXPECT_EQ(graph.status_code, 200);
  const auto graph_json = nlohmann::json::parse(graph.body);
  EXPECT_EQ(graph_json.at("schema").get<std::string>(),
            "graphx.dashboard.graph.v1");
  EXPECT_TRUE(graph_json.contains("config_revision"));
  EXPECT_TRUE(graph_json.at("graph").contains("nodes"));
  EXPECT_TRUE(graph_json.at("graph").contains("edges"));

  const auto config = HttpGet(server_->BoundPort(), "/api/v1/config");
  EXPECT_EQ(config.status_code, 200);
  const auto config_json = nlohmann::json::parse(config.body);
  EXPECT_EQ(config_json.at("schema").get<std::string>(),
            "graphx.dashboard.config.v1");
  EXPECT_TRUE(config_json.contains("owner"));
  EXPECT_TRUE(config_json.at("effective").contains("nodes"));
  EXPECT_TRUE(config_json.at("effective").contains("edges"));
}

TEST_F(FhssDashboardServerContractTest,
       MetricsSchemasAreStableWithDefaultPayloads) {
  const auto metrics = HttpGet(server_->BoundPort(), "/api/v1/metrics");
  EXPECT_EQ(metrics.status_code, 200);
  const auto metrics_json = nlohmann::json::parse(metrics.body);
  EXPECT_EQ(metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_TRUE(metrics_json.at("nodes").is_array());
  EXPECT_TRUE(metrics_json.at("edges").is_array());
  EXPECT_EQ(metrics_json.at("nodes").size(), 0u);
  EXPECT_EQ(metrics_json.at("edges").size(), 0u);

  const auto edge_metrics =
      HttpGet(server_->BoundPort(), "/api/v1/metrics/edges");
  EXPECT_EQ(edge_metrics.status_code, 200);
  const auto edge_metrics_json = nlohmann::json::parse(edge_metrics.body);
  EXPECT_EQ(edge_metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.edge_metrics.v1");
  EXPECT_TRUE(edge_metrics_json.at("edges").is_array());
  EXPECT_EQ(edge_metrics_json.at("edges").size(), 0u);
}

TEST_F(FhssDashboardServerContractTest, CleanShutdownStopsServer) {
  ASSERT_TRUE(server_->IsRunning());
  server_->Stop();
  EXPECT_FALSE(server_->IsRunning());

  const auto after_shutdown = HttpGet(server_->BoundPort(), "/healthz");
  EXPECT_EQ(after_shutdown.status_code, 0);
}

TEST(FhssDashboardServerFailureTest, StartupFailsWhenAssetDirectoryMissing) {
  auto config_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          MinimalGraphConfig());
  auto runtime_session =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshot_collector =
      std::make_shared<graph::dashboard::GraphSnapshotCollector>();

  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.port = 0;
  options.asset_directory = std::filesystem::temp_directory_path() /
                            "graphx_missing_dashboard_assets";

  graph::dashboard::EmbeddedDashboardServer server(
      options, config_service, runtime_session, snapshot_collector);
  EXPECT_FALSE(server.Start());
  EXPECT_NE(server.LastError().find("asset directory"), std::string::npos);
}

TEST(FhssDashboardServerFailureTest, StartupFailsWhenConfigurationIsInvalid) {
  auto config_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          nlohmann::json{{"nodes", nlohmann::json::array()}});
  auto runtime_session =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshot_collector =
      std::make_shared<graph::dashboard::GraphSnapshotCollector>();

  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_invalid_cfg_assets");

  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.port = 0;
  options.asset_directory = assets;

  graph::dashboard::EmbeddedDashboardServer server(
      options, config_service, runtime_session, snapshot_collector);
  EXPECT_FALSE(server.Start());
  EXPECT_NE(server.LastError().find("invalid effective graph"),
            std::string::npos);

  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerFailureTest, StartupFailsWhenPortAlreadyBound) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_bind_failure_assets");
  auto config_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          MinimalGraphConfig());
  auto runtime_session_a =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto runtime_session_b =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshot_collector =
      std::make_shared<graph::dashboard::GraphSnapshotCollector>();

  graph::dashboard::EmbeddedDashboardServer::Options options_a;
  options_a.port = 0;
  options_a.asset_directory = assets;

  graph::dashboard::EmbeddedDashboardServer server_a(
      options_a, config_service, runtime_session_a, snapshot_collector);
  ASSERT_TRUE(server_a.Start()) << server_a.LastError();
  const auto occupied_port = server_a.BoundPort();
  ASSERT_GT(occupied_port, 0u);

  graph::dashboard::EmbeddedDashboardServer::Options options_b;
  options_b.port = occupied_port;
  options_b.asset_directory = assets;

  graph::dashboard::EmbeddedDashboardServer server_b(
      options_b, config_service, runtime_session_b, snapshot_collector);
  EXPECT_FALSE(server_b.Start());
  EXPECT_NE(server_b.LastError().find("bind"), std::string::npos);

  server_a.Stop();

  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

} // namespace
