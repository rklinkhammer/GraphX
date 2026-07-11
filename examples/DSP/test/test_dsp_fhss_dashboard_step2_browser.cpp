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

class FhssDashboardConfigurationConcurrencyTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step2_browser_assets");
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

TEST_F(FhssDashboardConfigurationConcurrencyTest, TwoBrowserSessionsSeeDeterministicOptimisticConcurrency) {
  const auto browser_a = HttpRequest(server_->BoundPort(), "GET", "/api/v1/config");
  const auto browser_b = HttpRequest(server_->BoundPort(), "GET", "/api/v1/config");
  EXPECT_EQ(browser_a.status_code, 200);
  EXPECT_EQ(browser_b.status_code, 200);

  const auto browser_a_json = nlohmann::json::parse(browser_a.body);
  const auto browser_b_json = nlohmann::json::parse(browser_b.body);
  ASSERT_EQ(browser_a_json.at("config_revision").get<std::uint64_t>(), 1u);
  ASSERT_EQ(browser_b_json.at("config_revision").get<std::uint64_t>(), 1u);

  const auto staged_patch = nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                                           {"command_id", "browser-a-stage"},
                                           {"expected_revision", 1},
                                           {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                                           {"value", 1240000001.0},
                                           {"apply", "staged"}};
  const auto browser_a_patch = HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/config", staged_patch.dump());
  EXPECT_EQ(browser_a_patch.status_code, 200) << browser_a_patch.body;
  const auto browser_a_patch_json = nlohmann::json::parse(browser_a_patch.body);
  ASSERT_EQ(browser_a_patch_json.at("new_revision").get<std::uint64_t>(), 2u);

  const auto stale_browser_b_patch = nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                                                    {"command_id", "browser-b-stale"},
                                                    {"expected_revision", 1},
                                                    {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                                                    {"value", 1240000002.0},
                                                    {"apply", "staged"}};
  const auto browser_b_stale = HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/config",
                                           stale_browser_b_patch.dump());
  EXPECT_EQ(browser_b_stale.status_code, 409) << browser_b_stale.body;
  const auto browser_b_stale_json = nlohmann::json::parse(browser_b_stale.body);
  EXPECT_EQ(browser_b_stale_json.at("code").get<std::string>(), "stale_revision_conflict");

  const auto refreshed_browser_b = HttpRequest(server_->BoundPort(), "GET", "/api/v1/config");
  EXPECT_EQ(refreshed_browser_b.status_code, 200);
  const auto refreshed_browser_b_json = nlohmann::json::parse(refreshed_browser_b.body);
  EXPECT_EQ(refreshed_browser_b_json.at("config_revision").get<std::uint64_t>(), 2u);

  const auto browser_b_retry_patch = nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                                                    {"command_id", "browser-b-retry"},
                                                    {"expected_revision", 2},
                                                    {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                                                    {"value", 1240000003.0},
                                                    {"apply", "staged"}};
  const auto browser_b_retry = HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/config",
                                           browser_b_retry_patch.dump());
  EXPECT_EQ(browser_b_retry.status_code, 200) << browser_b_retry.body;
  const auto browser_b_retry_json = nlohmann::json::parse(browser_b_retry.body);
  EXPECT_EQ(browser_b_retry_json.at("new_revision").get<std::uint64_t>(), 3u);
  EXPECT_TRUE(browser_b_retry_json.at("validation").at("valid").get<bool>());
}

} // namespace