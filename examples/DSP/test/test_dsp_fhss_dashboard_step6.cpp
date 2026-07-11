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
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH \
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
  index << "<html><body>GraphX Dashboard Event Replay Test</body></html>";
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

class FhssDashboardEventReplayTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step6_assets");
    config_ = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(config_);
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

  nlohmann::json PollEvents(const std::string &client_id,
                            std::optional<std::uint64_t> last_sequence = std::nullopt,
                            bool disconnect = false) {
    std::ostringstream target;
    target << "/api/v1/events?client_id=" << client_id;
    if (last_sequence.has_value()) {
      target << "&last_sequence=" << *last_sequence;
    }
    if (disconnect) {
      target << "&disconnect=1";
    }
    const auto response = HttpGet(server_->BoundPort(), target.str());
    EXPECT_EQ(response.status_code, 200) << response.body;
    return nlohmann::json::parse(response.body);
  }

  std::filesystem::path assets_;
  nlohmann::json config_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardEventReplayTest, EventsUseMonotonicSequenceContract) {
  server_->PublishEventForTesting("status", nlohmann::json{{"state", "ready"}});
  server_->PublishEventForTesting("metrics", nlohmann::json{{"graph_total_enqueued", 1}});
  server_->PublishEventForTesting("diagnostics", nlohmann::json{{"ok", true}});

  const auto batch = PollEvents("seq-client", 0);
  ASSERT_TRUE(batch.contains("events"));
  ASSERT_GE(batch.at("events").size(), 3u);
  EXPECT_FALSE(batch.at("resync_required").get<bool>());

  std::uint64_t previous = 0;
  for (const auto &event : batch.at("events")) {
    const auto sequence = event.at("sequence").get<std::uint64_t>();
    EXPECT_GT(sequence, previous);
    previous = sequence;
  }
}

TEST_F(FhssDashboardEventReplayTest, ReplayResumeRequiresContiguousRetainedRangeOnly) {
  server_->PublishEventForTesting("command", nlohmann::json{{"code", "a"}});
  server_->PublishEventForTesting("command", nlohmann::json{{"code", "b"}});

  const auto first = PollEvents("resume-client", 0);
  ASSERT_FALSE(first.at("events").empty());
  const auto last_seen = first.at("events").back().at("sequence").get<std::uint64_t>();

  server_->PublishEventForTesting("fhss_progress", nlohmann::json{{"step", 1}});
  server_->PublishEventForTesting("fhss_progress", nlohmann::json{{"step", 2}});

  (void)PollEvents("resume-client", std::nullopt, true);
  const auto resumed = PollEvents("resume-client", last_seen);
  EXPECT_FALSE(resumed.at("resync_required").get<bool>());
  ASSERT_EQ(resumed.at("events").size(), 2u);
  EXPECT_EQ(resumed.at("events").at(0).at("sequence").get<std::uint64_t>(), last_seen + 1);
  EXPECT_EQ(resumed.at("events").at(1).at("sequence").get<std::uint64_t>(), last_seen + 2);
}

TEST_F(FhssDashboardEventReplayTest, MissingOrExpiredRangeForcesResyncRequired) {
  server_->SetEventRetentionForTesting(std::chrono::milliseconds(1));
  server_->PublishEventForTesting("status", nlohmann::json{{"phase", "initial"}});
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server_->PublishEventForTesting("status", nlohmann::json{{"phase", "after-expire"}});

  const auto reconnect = PollEvents("gap-client", 0);
  EXPECT_TRUE(reconnect.at("resync_required").get<bool>());
  EXPECT_TRUE(reconnect.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, SlowClientBackpressureDoesNotBlockPublishers) {
  server_->SetEventQueueDepthForTesting(8);
  (void)PollEvents("slow-client");

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 400; ++i) {
    server_->PublishEventForTesting("metrics", nlohmann::json{{"sample", i}});
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  const auto slow_batch = PollEvents("slow-client");
  EXPECT_TRUE(slow_batch.at("resync_required").get<bool>());
  EXPECT_TRUE(slow_batch.at("events").empty());
  EXPECT_LT(elapsed.count(), 250);
}

TEST_F(FhssDashboardEventReplayTest, FailureInjectionDisconnectReconnectUnderLoadMaintainsReplay) {
  server_->SetEventQueueDepthForTesting(1024);

  server_->PublishEventForTesting("status", nlohmann::json{{"seed", 0}});
  const auto first = PollEvents("load-client", 0);
  ASSERT_FALSE(first.at("events").empty());
  const auto seen = first.at("events").back().at("sequence").get<std::uint64_t>();

  std::thread producer([this]() {
    for (int i = 0; i < 300; ++i) {
      server_->PublishEventForTesting("metrics", nlohmann::json{{"burst", i}});
    }
  });

  (void)PollEvents("load-client", std::nullopt, true);
  producer.join();

  const auto resumed = PollEvents("load-client", seen);
  EXPECT_FALSE(resumed.at("resync_required").get<bool>());
  EXPECT_FALSE(resumed.at("events").empty());
}

TEST_F(FhssDashboardEventReplayTest, FailureInjectionRetentionGapDuringReconnectForcesResync) {
  server_->SetEventRetentionForTesting(std::chrono::milliseconds(1));
  server_->PublishEventForTesting("command", nlohmann::json{{"state", "pre-gap"}});
  const auto initial = PollEvents("retention-gap-client", 0);
  ASSERT_FALSE(initial.at("events").empty());

  (void)PollEvents("retention-gap-client", std::nullopt, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server_->PublishEventForTesting("command", nlohmann::json{{"state", "post-gap"}});

  const auto reconnect = PollEvents("retention-gap-client", 0);
  EXPECT_TRUE(reconnect.at("resync_required").get<bool>());
  EXPECT_TRUE(reconnect.at("events").empty());
}

} // namespace
