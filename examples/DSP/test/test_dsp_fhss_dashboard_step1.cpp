// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

struct HttpResponse {
  int status_code = 0;
  std::map<std::string, std::string> headers;
  std::string body;
};

std::filesystem::path MakeTempAssetDirectory(const std::string &name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);

  std::ofstream index(dir / "index.html", std::ios::trunc);
  index << "<html><body>GraphX Dashboard Test</body></html>";
  for (const auto *name : {"bundle.js", "bundle.mjs", "bundle.css",
                           "bundle.js.map", "font.woff", "font.woff2",
                           "font.ttf", "font.otf"}) {
    std::ofstream asset(dir / name, std::ios::binary | std::ios::trunc);
    asset << "fixture";
  }
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
    std::istringstream header_stream(
        response.substr(first_line_end + 2, body_pos - first_line_end - 2));
    std::string line;
    while (std::getline(header_stream, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        auto name = line.substr(0, colon);
        std::transform(
            name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ')
          value.erase(value.begin());
        parsed.headers.emplace(std::move(name), std::move(value));
      }
    }
    parsed.body = response.substr(body_pos + 4);
  }
  return parsed;
}

HttpResponse HttpRaw(std::uint16_t port, const std::string &wire) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return {};
#if defined(SO_NOSIGPIPE)
  const int no_sigpipe = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
  const timeval socket_timeout{.tv_sec = 2, .tv_usec = 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout,
               sizeof(socket_timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {};
  }
  ::send(fd, wire.data(), wire.size(), 0);
  std::string response;
  std::array<char, 2048> buffer{};
  while (const auto count = ::recv(fd, buffer.data(), buffer.size(), 0)) {
    if (count < 0)
      break;
    response.append(buffer.data(), static_cast<std::size_t>(count));
  }
  ::close(fd);
  HttpResponse parsed;
  const auto first_line_end = response.find("\r\n");
  if (first_line_end == std::string::npos)
    return parsed;
  std::istringstream status_line(response.substr(0, first_line_end));
  std::string http;
  status_line >> http >> parsed.status_code;
  const auto body_pos = response.find("\r\n\r\n");
  if (body_pos != std::string::npos) {
    std::istringstream headers(
        response.substr(first_line_end + 2, body_pos - first_line_end - 2));
    std::string line;
    while (std::getline(headers, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const auto colon = line.find(':');
      if (colon == std::string::npos)
        continue;
      auto name = line.substr(0, colon);
      std::transform(
          name.begin(), name.end(), name.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      auto value = line.substr(colon + 1);
      if (!value.empty() && value.front() == ' ')
        value.erase(value.begin());
      parsed.headers.emplace(std::move(name), std::move(value));
    }
    parsed.body = response.substr(body_pos + 4);
  }
  return parsed;
}

struct TimedResponse {
  HttpResponse response;
  std::chrono::steady_clock::duration elapsed{};
};

TimedResponse HttpSlowProgress(std::uint16_t port,
                               std::chrono::milliseconds interval,
                               int progress_writes) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return {};
#if defined(SO_NOSIGPIPE)
  const int no_sigpipe = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
  const timeval socket_timeout{.tv_sec = 2, .tv_usec = 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout,
               sizeof(socket_timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {};
  }
  const auto started = std::chrono::steady_clock::now();
  const std::string partial =
      "GET /healthz HTTP/1.1\r\nHost: localhost\r\nX-Slow: ";
  ::send(fd, partial.data(), partial.size(), 0);
  for (int index = 0; index < progress_writes; ++index) {
    std::this_thread::sleep_for(interval);
    const char progress = 'x';
    if (::send(fd, &progress, 1, 0) != 1)
      break;
  }
  std::string wire_response;
  std::array<char, 2048> receive_buffer{};
  while (const auto count =
             ::recv(fd, receive_buffer.data(), receive_buffer.size(), 0)) {
    if (count < 0)
      break;
    wire_response.append(receive_buffer.data(),
                         static_cast<std::size_t>(count));
  }
  ::close(fd);
  TimedResponse result;
  result.elapsed = std::chrono::steady_clock::now() - started;
  const auto first_line_end = wire_response.find("\r\n");
  if (first_line_end == std::string::npos)
    return result;
  std::istringstream status_line(wire_response.substr(0, first_line_end));
  std::string http;
  status_line >> http >> result.response.status_code;
  const auto body_pos = wire_response.find("\r\n\r\n");
  if (body_pos != std::string::npos)
    result.response.body = wire_response.substr(body_pos + 4);
  return result;
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
  EXPECT_EQ(server_->BoundHost(), "127.0.0.1");
  const auto root = HttpGet(server_->BoundPort(), "/");
  EXPECT_EQ(root.status_code, 200);
  EXPECT_NE(root.body.find("GraphX Dashboard Test"), std::string::npos);
}

TEST_F(FhssDashboardServerContractTest,
       ServesOneRootDashboardAndRejectsAlternateEntrypoints) {
  const auto root = HttpGet(server_->BoundPort(), "/");
  ASSERT_EQ(root.status_code, 200);
  EXPECT_NE(root.body.find("GraphX Dashboard Test"), std::string::npos);
  for (const auto *path : {"/api/v2", "/api/v2/fhss", "/legacy",
                           "/legacy/index.html", "/v2", "/v2/index.html",
                           "/dashboard-v2", "/alternate-dashboard"}) {
    SCOPED_TRACE(path);
    EXPECT_EQ(HttpGet(server_->BoundPort(), path).status_code, 404);
  }
}

TEST_F(FhssDashboardServerContractTest,
       ContentTypesAllowAndDefaultMutationAreExact) {
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/").headers.at("content-type"),
            "text/html; charset=utf-8");
  EXPECT_EQ(
      HttpGet(server_->BoundPort(), "/healthz").headers.at("content-type"),
      "application/json");
  const auto method = HttpRaw(
      server_->BoundPort(),
      "PUT /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
  EXPECT_EQ(method.status_code, 405);
  EXPECT_EQ(method.headers.at("allow"), "GET");
  EXPECT_EQ(method.headers.at("content-type"), "application/problem+json");
  const auto mutation =
      HttpRaw(server_->BoundPort(),
              "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
              "localhost\r\nContent-Length: 2\r\n"
              "Content-Type: application/json\r\nConnection: close\r\n\r\n{}");
  EXPECT_EQ(mutation.status_code, 404);
  EXPECT_EQ(mutation.headers.at("content-type"), "application/problem+json");
}

TEST_F(FhssDashboardServerContractTest, FrontendAssetContentTypesAreExact) {
  const std::map<std::string, std::string> expected{
      {"/bundle.js", "text/javascript; charset=utf-8"},
      {"/bundle.mjs", "text/javascript; charset=utf-8"},
      {"/bundle.css", "text/css; charset=utf-8"},
      {"/bundle.js.map", "application/json"},
      {"/font.woff", "font/woff"},
      {"/font.woff2", "font/woff2"},
      {"/font.ttf", "font/ttf"},
      {"/font.otf", "font/otf"}};
  for (const auto &[path, content_type] : expected) {
    SCOPED_TRACE(path);
    const auto response = HttpGet(server_->BoundPort(), path);
    EXPECT_EQ(response.status_code, 200);
    EXPECT_EQ(response.headers.at("content-type"), content_type);
  }
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
  const auto graph = HttpGet(server_->BoundPort(), "/api/v1/fhss/graph");
  EXPECT_EQ(graph.status_code, 200);
  const auto graph_json = nlohmann::json::parse(graph.body);
  EXPECT_EQ(graph_json.at("schema").get<std::string>(),
            "graphx.dashboard.graph.v1");
  EXPECT_TRUE(graph_json.contains("config_revision"));
  EXPECT_TRUE(graph_json.at("graph").contains("nodes"));
  EXPECT_TRUE(graph_json.at("graph").contains("edges"));

  const auto config = HttpGet(server_->BoundPort(), "/api/v1/fhss/config");
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
  const auto metrics = HttpGet(server_->BoundPort(), "/api/v1/fhss/metrics");
  EXPECT_EQ(metrics.status_code, 200);
  const auto metrics_json = nlohmann::json::parse(metrics.body);
  EXPECT_EQ(metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_TRUE(metrics_json.at("nodes").is_array());
  EXPECT_TRUE(metrics_json.at("edges").is_array());
  EXPECT_EQ(metrics_json.at("nodes").size(), 0u);
  EXPECT_EQ(metrics_json.at("edges").size(), 0u);
  EXPECT_EQ(metrics_json.at("active_run_epoch"), 0u);
  ASSERT_EQ(metrics_json.at("metric_definitions").size(), 19u);
  const std::map<std::pair<std::string, std::string>, std::string>
      expected_units{
          {{"graph", "total_items_processed"}, "item"},
          {{"graph", "total_items_rejected"}, "item"},
          {{"graph", "total_messages_processed"}, "message"},
          {{"graph", "graph_total_enqueued"}, "message"},
          {{"graph", "graph_total_dequeued"}, "message"},
          {{"graph", "backpressure_events"}, "event"},
          {{"graph", "peak_queue_depth"}, "message"},
          {{"graph", "peak_active_threads"}, "thread"},
          {{"node", "inbound_messages"}, "message"},
          {{"node", "outbound_messages"}, "message"},
          {{"node", "rejected_messages"}, "message"},
          {{"node", "backpressure_events"}, "event"},
          {{"node", "peak_queue_depth"}, "message"},
          {{"edge", "messages_enqueued"}, "message"},
          {{"edge", "messages_dequeued"}, "message"},
          {{"edge", "messages_rejected"}, "message"},
          {{"edge", "backpressure_events"}, "event"},
          {{"edge", "current_queue_depth"}, "message"},
          {{"edge", "peak_queue_depth"}, "message"},
      };
  for (const auto &definition : metrics_json.at("metric_definitions")) {
    EXPECT_TRUE(definition.at("kind") == "counter" ||
                definition.at("kind") == "gauge");
    EXPECT_EQ(definition.at("reset"), "new_graph_generation");
    const auto key = std::pair{definition.at("scope").get<std::string>(),
                               definition.at("name").get<std::string>()};
    ASSERT_TRUE(expected_units.contains(key));
    EXPECT_EQ(definition.at("unit"), expected_units.at(key));
    EXPECT_TRUE(definition.contains("monotonic"));
  }

  const auto edge_metrics =
      HttpGet(server_->BoundPort(), "/api/v1/fhss/metrics/edges");
  EXPECT_EQ(edge_metrics.status_code, 200);
  const auto edge_metrics_json = nlohmann::json::parse(edge_metrics.body);
  EXPECT_EQ(edge_metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.edge_metrics.v1");
  EXPECT_TRUE(edge_metrics_json.at("edges").is_array());
  EXPECT_EQ(edge_metrics_json.at("edges").size(), 0u);
  EXPECT_EQ(edge_metrics_json.at("active_run_epoch"), 0u);

  const auto diagnostics =
      HttpGet(server_->BoundPort(), "/api/v1/fhss/diagnostics");
  EXPECT_EQ(diagnostics.status_code, 200);
  const auto diagnostics_json = nlohmann::json::parse(diagnostics.body);
  EXPECT_EQ(diagnostics_json.at("schema"), "graphx.dashboard.diagnostics.v1");
  EXPECT_EQ(diagnostics_json.at("active_generation"), 0u);
  EXPECT_EQ(diagnostics_json.at("active_run_epoch"), 0u);
  EXPECT_EQ(diagnostics_json.at("active_config_revision"), 0u);
  EXPECT_EQ(diagnostics_json.at("active_config_etag"), "");
}

TEST_F(FhssDashboardServerContractTest, CleanShutdownStopsServer) {
  ASSERT_TRUE(server_->IsRunning());
  server_->Stop();
  EXPECT_FALSE(server_->IsRunning());

  const auto after_shutdown = HttpGet(server_->BoundPort(), "/healthz");
  EXPECT_EQ(after_shutdown.status_code, 0);
}

TEST_F(FhssDashboardServerContractTest, AddsDefensiveBrowserHeaders) {
  const auto response = HttpGet(server_->BoundPort(), "/");
  EXPECT_EQ(response.headers.at("x-content-type-options"), "nosniff");
  EXPECT_EQ(response.headers.at("x-frame-options"), "DENY");
  EXPECT_EQ(response.headers.at("referrer-policy"), "no-referrer");
  EXPECT_NE(response.headers.at("content-security-policy")
                .find("frame-ancestors 'none'"),
            std::string::npos);
}

TEST_F(FhssDashboardServerContractTest, RejectsEncodedAndPlainTraversal) {
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/../outside").status_code, 404);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/%2e%2e/outside").status_code, 404);
}

TEST_F(FhssDashboardServerContractTest, SafeMetricsGetDoesNotPublishAnEvent) {
  ASSERT_EQ(
      HttpGet(server_->BoundPort(), "/api/v1/fhss/events?client_id=safe-get")
          .status_code,
      200);
  ASSERT_EQ(HttpGet(server_->BoundPort(), "/api/v1/fhss/metrics").status_code,
            200);
  const auto events =
      HttpGet(server_->BoundPort(), "/api/v1/fhss/events?client_id=safe-get");
  ASSERT_EQ(events.status_code, 200);
  EXPECT_TRUE(nlohmann::json::parse(events.body).at("events").empty());
}

TEST_F(FhssDashboardServerContractTest,
       RejectsAmbiguousFramingAndUnsupportedMethods) {
  const auto ambiguous =
      HttpRaw(server_->BoundPort(),
              "POST /api/v1/fhss/config HTTP/1.1\r\nHost: localhost\r\n"
              "Content-Length: 2\r\nTransfer-Encoding: chunked\r\nConnection: "
              "close\r\n\r\n{}");
  EXPECT_EQ(ambiguous.status_code, 400);
  const auto unsupported = HttpRaw(
      server_->BoundPort(),
      "PUT /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
  ASSERT_EQ(unsupported.status_code, 405);
  EXPECT_TRUE(unsupported.headers.contains("allow"));
  EXPECT_EQ(unsupported.headers.at("content-type"), "application/problem+json");
  EXPECT_EQ(nlohmann::json::parse(unsupported.body).at("status"), 405);
  const auto unsupported_bad_body = HttpRaw(
      server_->BoundPort(),
      "PUT /healthz HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\n"
      "Connection: close\r\n\r\nx");
  ASSERT_EQ(unsupported_bad_body.status_code, 405);
  EXPECT_EQ(unsupported_bad_body.headers.at("allow"), "GET");
  EXPECT_EQ(nlohmann::json::parse(unsupported_bad_body.body).at("status"), 405);
  const auto missing_host =
      HttpRaw(server_->BoundPort(),
              "GET /healthz HTTP/1.1\r\nConnection: close\r\n\r\n");
  ASSERT_EQ(missing_host.status_code, 400);
  EXPECT_EQ(nlohmann::json::parse(missing_host.body).at("status"), 400);
  const auto duplicate_host =
      HttpRaw(server_->BoundPort(),
              "GET /healthz HTTP/1.1\r\nHost: first\r\nHost: second\r\n"
              "Connection: close\r\n\r\n");
  ASSERT_EQ(duplicate_host.status_code, 400);
  EXPECT_EQ(nlohmann::json::parse(duplicate_host.body).at("status"), 400);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
}

TEST_F(FhssDashboardServerContractTest,
       PartialClientDoesNotBlockConcurrentHealthRequest) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server_->BoundPort());
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)),
            0);
  const std::string partial = "GET /healthz HTTP/1.1\r\nHost: localhost\r\n";
  ASSERT_EQ(::send(fd, partial.data(), partial.size(), 0),
            static_cast<ssize_t>(partial.size()));
  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/healthz").status_code, 200);
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::seconds(1));
  ::close(fd);
}

TEST_F(FhssDashboardServerContractTest, RejectsSymlinkAndNonRegularAssets) {
  const auto outside = assets_.parent_path() / "graphx_dashboard_outside.txt";
  {
    std::ofstream stream(outside);
    stream << "secret";
  }
  std::error_code error;
  std::filesystem::create_symlink(outside, assets_ / "escape.txt", error);
  if (!error)
    EXPECT_EQ(HttpGet(server_->BoundPort(), "/escape.txt").status_code, 404);
  std::filesystem::create_directory(assets_ / "directory", error);
  EXPECT_EQ(HttpGet(server_->BoundPort(), "/directory").status_code, 404);
  std::filesystem::remove(outside, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     SlowApplicationHandlerIsCancelledAtAbsoluteDeadlineAndStopDrainsPool) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_slow_handler_assets");
  auto config_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          MinimalGraphConfig());
  auto runtime_session =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshot_collector =
      std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  auto cancellation_observed = std::make_shared<std::atomic<bool>>(false);
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.total_request_timeout = std::chrono::milliseconds(120);
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler =
              [cancellation_observed](
                  const graph::dashboard::EmbeddedDashboardServer::ApiRequest &,
                  const graph::dashboard::EmbeddedDashboardServer::ApiContext
                      &context)
              -> std::optional<
                  graph::dashboard::EmbeddedDashboardServer::ApiResponse> {
            while (!context.stop_token.stop_requested()) {
              std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            cancellation_observed->store(context.stop_token.stop_requested());
            return graph::dashboard::EmbeddedDashboardServer::ApiResponse{
                .status_code = 408,
                .content_type = "application/problem+json",
                .body = "{}"};
          },
          .cooperative_cancellation = true,
          .maximum_checkpoint_latency = std::chrono::milliseconds(5)};
  graph::dashboard::EmbeddedDashboardServer server(
      options, config_service, runtime_session, snapshot_collector);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto started = std::chrono::steady_clock::now();
  const auto response = HttpGet(server.BoundPort(), "/api/v1/fhss/slow-test");
  EXPECT_EQ(response.status_code, 408);
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::milliseconds(500));
  server.Stop();
  EXPECT_TRUE(cancellation_observed->load());
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     NormalizesApplicationProblemStatusToWireStatus) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_problem_status_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler = [](const auto &request, const auto &)
              -> std::optional<
                  graph::dashboard::EmbeddedDashboardServer::ApiResponse> {
            if (request.path != "/api/v1/fhss/problem-status-test")
              return std::nullopt;
            return graph::dashboard::EmbeddedDashboardServer::ApiResponse{
                .status_code = 422,
                .content_type = "application/problem+json",
                .body = nlohmann::json{{"type", "urn:test:problem"},
                                       {"title", "test_problem"},
                                       {"status", 500},
                                       {"detail", "safe detail"},
                                       {"extension", "preserved"}}
                            .dump()};
          },
          .cooperative_cancellation = true,
          .maximum_checkpoint_latency = std::chrono::milliseconds(1)};
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto response =
      HttpGet(server.BoundPort(), "/api/v1/fhss/problem-status-test");
  ASSERT_EQ(response.status_code, 422);
  EXPECT_EQ(response.headers.at("content-type"), "application/problem+json");
  const auto problem = nlohmann::json::parse(response.body);
  EXPECT_EQ(problem.at("status"), 422);
  EXPECT_EQ(problem.at("type"), "urn:test:problem");
  EXPECT_EQ(problem.at("title"), "test_problem");
  EXPECT_EQ(problem.at("detail"), "safe detail");
  EXPECT_EQ(problem.at("extension"), "preserved");
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     SlowReaderCannotBlockConcurrentHealthOrShutdown) {
  constexpr std::size_t kLargeBodyBytes = 32U * 1024U * 1024U;
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_slow_reader_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.max_response_bytes = kLargeBodyBytes + 1024;
  options.max_concurrent_connections = 2;
  options.write_timeout = std::chrono::milliseconds(100);
  options.total_request_timeout = std::chrono::seconds(2);
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler = [](const auto &request, const auto &)
              -> std::optional<
                  graph::dashboard::EmbeddedDashboardServer::ApiResponse> {
            if (request.path != "/api/v1/fhss/large-response-test")
              return std::nullopt;
            return graph::dashboard::EmbeddedDashboardServer::ApiResponse{
                .status_code = 200,
                .content_type = "application/octet-stream",
                .body = std::string(kLargeBodyBytes, 'x')};
          },
          .cooperative_cancellation = true,
          .maximum_checkpoint_latency = std::chrono::milliseconds(1)};
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();

  const int slow_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(slow_fd, 0);
  const int receive_bytes = 1024;
  ASSERT_EQ(::setsockopt(slow_fd, SOL_SOCKET, SO_RCVBUF, &receive_bytes,
                         sizeof(receive_bytes)),
            0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(server.BoundPort());
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  ASSERT_EQ(::connect(slow_fd, reinterpret_cast<sockaddr *>(&address),
                      sizeof(address)),
            0);
  const std::string request =
      "GET /api/v1/fhss/large-response-test HTTP/1.1\r\n"
      "Host: localhost\r\nConnection: close\r\n\r\n";
  ASSERT_EQ(::send(slow_fd, request.data(), request.size(), 0),
            static_cast<ssize_t>(request.size()));

  const auto health_started = std::chrono::steady_clock::now();
  EXPECT_EQ(HttpGet(server.BoundPort(), "/healthz").status_code, 200);
  EXPECT_LT(std::chrono::steady_clock::now() - health_started,
            std::chrono::seconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const auto stop_started = std::chrono::steady_clock::now();
  server.Stop();
  EXPECT_LT(std::chrono::steady_clock::now() - stop_started,
            std::chrono::milliseconds(500));
  ::close(slow_fd);
  std::error_code error;
  std::filesystem::remove_all(assets, error);
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

TEST(FhssDashboardServerFailureTest,
     RejectsHandlerWithoutCooperativeBoundContract) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_unbounded_handler_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.application_api_handler =
      graph::dashboard::EmbeddedDashboardServer::ApiHandlerRegistration{
          .handler = [](const auto &, const auto &)
              -> std::optional<
                  graph::dashboard::EmbeddedDashboardServer::ApiResponse> {
            return std::nullopt;
          },
          .cooperative_cancellation = false,
          .maximum_checkpoint_latency = std::chrono::milliseconds(1)};
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  EXPECT_FALSE(server.Start());
  EXPECT_NE(server.LastError().find("cooperative cancellation"),
            std::string::npos);
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest, PortCanBeReusedAfterStop) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_port_reuse_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options first_options;
  first_options.asset_directory = assets;
  auto first_runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  graph::dashboard::EmbeddedDashboardServer first(first_options, config,
                                                  first_runtime, snapshots);
  ASSERT_TRUE(first.Start()) << first.LastError();
  const auto port = first.BoundPort();
  first.Stop();
  graph::dashboard::EmbeddedDashboardServer::Options second_options;
  second_options.asset_directory = assets;
  second_options.port = port;
  auto second_runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  graph::dashboard::EmbeddedDashboardServer second(second_options, config,
                                                   second_runtime, snapshots);
  EXPECT_TRUE(second.Start()) << second.LastError();
  second.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest, IdleRequestTimesOutWith408) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_idle_timeout_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.idle_timeout = std::chrono::milliseconds(60);
  options.read_timeout = std::chrono::milliseconds(400);
  options.total_request_timeout = std::chrono::milliseconds(500);
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto response = HttpRaw(server.BoundPort(),
                                "GET /healthz HTTP/1.1\r\nHost: localhost\r\n");
  EXPECT_EQ(response.status_code, 408);
  EXPECT_EQ(nlohmann::json::parse(response.body).at("code"), "idle_timeout");
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     ContinuousProgressSurvivesIdleAndEndsAtAbsoluteReadDeadline) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_read_deadline_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.idle_timeout = std::chrono::milliseconds(90);
  options.read_timeout = std::chrono::milliseconds(260);
  options.total_request_timeout = std::chrono::milliseconds(600);
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto result =
      HttpSlowProgress(server.BoundPort(), std::chrono::milliseconds(40), 5);
  EXPECT_EQ(result.response.status_code, 408);
  EXPECT_EQ(nlohmann::json::parse(result.response.body).at("code"),
            "read_timeout");
  EXPECT_GE(result.elapsed, std::chrono::milliseconds(240));
  EXPECT_LT(result.elapsed, std::chrono::milliseconds(450));
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     TotalDeadlineWinsWhenShorterThanAbsoluteReadDeadline) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_total_deadline_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.idle_timeout = std::chrono::milliseconds(100);
  options.read_timeout = std::chrono::milliseconds(500);
  options.total_request_timeout = std::chrono::milliseconds(180);
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto result =
      HttpSlowProgress(server.BoundPort(), std::chrono::milliseconds(40), 4);
  EXPECT_EQ(result.response.status_code, 408);
  EXPECT_EQ(nlohmann::json::parse(result.response.body).at("code"),
            "total_request_timeout");
  EXPECT_GE(result.elapsed, std::chrono::milliseconds(165));
  EXPECT_LT(result.elapsed, std::chrono::milliseconds(350));
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     ResponseLimitAndSiblingEscapeAreRejected) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_response_limit_assets");
  {
    std::ofstream large(assets / "large.txt");
    large << std::string(2048, 'x');
  }
  const auto sibling =
      assets.parent_path() / "graphx_dashboard_response_limit_assets_evil";
  std::filesystem::create_directories(sibling);
  {
    std::ofstream secret(sibling / "secret.txt");
    secret << "secret";
  }
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.max_response_bytes = 1024;
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  EXPECT_EQ(HttpGet(server.BoundPort(), "/large.txt").status_code, 413);
  EXPECT_EQ(
      HttpGet(server.BoundPort(),
              "/%2e%2e/graphx_dashboard_response_limit_assets_evil/secret.txt")
          .status_code,
      404);
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
  std::filesystem::remove_all(sibling, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     ValidWrongTypeJsonReturns4xxAndServerSurvives) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_wrong_type_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.enable_mutating_routes = true;
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();
  const auto disallowed =
      HttpRaw(server.BoundPort(),
              "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
              "localhost\r\nContent-Length: 2\r\n"
              "Content-Type: application/json\r\nConnection: close\r\n\r\n{}");
  EXPECT_EQ(disallowed.status_code, 405);
  EXPECT_EQ(disallowed.headers.at("allow"), "GET, PATCH");
  const auto response =
      HttpRaw(server.BoundPort(),
              "PATCH /api/v1/fhss/config HTTP/1.1\r\nHost: "
              "localhost\r\nContent-Length: 2\r\n"
              "Content-Type: application/json\r\nConnection: close\r\n\r\n[]");
  EXPECT_GE(response.status_code, 400);
  EXPECT_LT(response.status_code, 500);
  EXPECT_EQ(HttpGet(server.BoundPort(), "/healthz").status_code, 200);
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
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

TEST(FhssDashboardServerFailureTest, RejectsNonLoopbackAndInvalidHosts) {
  const auto assets =
      MakeTempAssetDirectory("graphx_dashboard_host_failure_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  for (const std::string host :
       {"0.0.0.0", "192.0.2.1", "localhost", "not-an-address"}) {
    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.host = host;
    options.asset_directory = assets;
    graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                     snapshots);
    EXPECT_FALSE(server.Start()) << host;
    EXPECT_NE(server.LastError().find("loopback"), std::string::npos) << host;
  }
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     SupportsExplicitIpv6LoopbackWhenAvailable) {
  const auto assets = MakeTempAssetDirectory("graphx_dashboard_ipv6_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.host = "::1";
  options.asset_directory = assets;
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  if (!server.Start()) {
    GTEST_SKIP() << "IPv6 loopback unavailable: " << server.LastError();
  }
  EXPECT_EQ(server.BoundHost(), "::1");
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(FhssDashboardServerContractStandaloneTest,
     EnforcesHttpAndJsonStructuralLimits) {
  const auto assets = MakeTempAssetDirectory("graphx_dashboard_limit_assets");
  auto config = std::make_shared<graph::dashboard::GraphConfigurationService>(
      MinimalGraphConfig());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto snapshots = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.asset_directory = assets;
  options.max_header_bytes = 1024;
  options.max_body_bytes = 64;
  options.max_json_depth = 2;
  options.max_json_members = 4;
  options.max_json_string_bytes = 4;
  graph::dashboard::EmbeddedDashboardServer server(options, config, runtime,
                                                   snapshots);
  ASSERT_TRUE(server.Start()) << server.LastError();

  const std::string long_header(1200, 'x');
  EXPECT_EQ(HttpRaw(server.BoundPort(),
                    "GET /healthz HTTP/1.1\r\nHost: localhost\r\nX-Large: " +
                        long_header + "\r\nConnection: close\r\n\r\n")
                .status_code,
            431);
  EXPECT_EQ(HttpRaw(server.BoundPort(),
                    "POST /api/v1/fhss/config HTTP/1.1\r\nHost: localhost\r\n"
                    "Content-Length: 65\r\nConnection: close\r\n\r\n")
                .status_code,
            413);
  const auto deep = std::string{"{\"a\":{\"b\":{\"c\":1}}}"};
  EXPECT_EQ(
      HttpRaw(server.BoundPort(), "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
                                  "localhost\r\nContent-Length: " +
                                      std::to_string(deep.size()) +
                                      "\r\nConnection: close\r\n\r\n" + deep)
          .status_code,
      400);
  const auto long_string = std::string{"{\"a\":\"12345\"}"};
  EXPECT_EQ(HttpRaw(server.BoundPort(),
                    "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
                    "localhost\r\nContent-Length: " +
                        std::to_string(long_string.size()) +
                        "\r\nConnection: close\r\n\r\n" + long_string)
                .status_code,
            400);
  const auto members = std::string{"[1,2,3,4,5]"};
  EXPECT_EQ(
      HttpRaw(server.BoundPort(), "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
                                  "localhost\r\nContent-Length: " +
                                      std::to_string(members.size()) +
                                      "\r\nConnection: close\r\n\r\n" + members)
          .status_code,
      400);
  for (const std::string invalid : {"{\"a\":1,\"a\":2}", "{\"a\":1e100}"}) {
    EXPECT_EQ(HttpRaw(server.BoundPort(),
                      "POST /api/v1/fhss/config HTTP/1.1\r\nHost: "
                      "localhost\r\nContent-Length: " +
                          std::to_string(invalid.size()) +
                          "\r\nConnection: close\r\n\r\n" + invalid)
                  .status_code,
              400);
  }
  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

} // namespace
