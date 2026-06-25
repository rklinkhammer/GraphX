// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_DEMO_EXECUTABLE_PATH
#define DSP_FHSS_DEMO_EXECUTABLE_PATH "./graphx-dsp-fhss-demo"
#endif

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                           \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

struct CommandResult {
  int exit_code{};
  std::string output;
};

struct HttpResponse {
  int status_code = 0;
  std::string body;
};

std::string ShellQuote(const std::filesystem::path &path) {
  std::string raw = path.string();
  std::string quoted{"'"};
  for (const char ch : raw) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

CommandResult RunCommand(const std::string &command) {
  std::array<char, 512> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return CommandResult{.exit_code = -1, .output = "popen failed"};
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  return CommandResult{.exit_code = pclose(pipe), .output = output};
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

std::filesystem::path TempOutputDir(const std::string &name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(path, error);
  std::filesystem::create_directories(path, error);
  return path;
}

std::filesystem::path MakeTempAssetDirectory(const std::string &name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);

  std::ofstream index(dir / "index.html", std::ios::trunc);
  index << "<html><body>GraphX Dashboard Step4 Test</body></html>";
  return dir;
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

nlohmann::json FindDiagnosticsByType(const nlohmann::json &diagnostics,
                                     const std::string &type) {
  for (const auto &node : diagnostics.at("nodes")) {
    if (node.value("type", std::string{}) == type) {
      return node.at("diagnostics");
    }
  }
  return nlohmann::json::object();
}

std::set<std::string> ObjectKeys(const nlohmann::json &value) {
  std::set<std::string> keys;
  for (auto it = value.begin(); it != value.end(); ++it) {
    keys.insert(it.key());
  }
  return keys;
}

TEST(DashboardServerStep4Test, PopulatesRuntimeMetricsAndMatchesCliSummary) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  ASSERT_TRUE(std::filesystem::exists(executable)) << executable;

  const auto output_dir = TempOutputDir("graphx_dsp_fhss_dashboard_step4");
  const auto summary_path = output_dir / "summary.json";
  const std::string command =
      ShellQuote(executable) + " --graph-config " +
      ShellQuote(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)) +
      " --plugin-dir " +
      ShellQuote(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY)) +
      " --summary-json " + ShellQuote(summary_path) +
      " --decoded-pulse-limit 3 --executor-timeout-s 12 2>&1";

  const auto command_result = RunCommand(command);
  ASSERT_EQ(command_result.exit_code, 0) << command_result.output;
  ASSERT_TRUE(std::filesystem::exists(summary_path)) << command_result.output;
  const auto cli_summary = LoadJson(summary_path);

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH).string())
                      .WithPluginDirectory(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY).string())
                      .WithExecutorTimeout(std::chrono::seconds(12))
                      .Build();
  ASSERT_TRUE(executor);

  auto manager = executor->GetGraphManager();
  ASSERT_TRUE(manager);
  manager->EnableMetrics(true);
  const auto execution_result = executor->Execute();
  ASSERT_TRUE(execution_result.success);
  ASSERT_TRUE(executor->IsCompletionSignaled());

  auto runtime_session = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime_session->SetActiveGraphManager(manager);
  runtime_session->SetLifecycleState(graph::dashboard::GraphRuntimeSession::State::completed);

  auto snapshot_collector = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  snapshot_collector->BindRuntimeSession(runtime_session);
  auto configuration_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)));

  const auto assets = MakeTempAssetDirectory("graphx_dashboard_step4_assets");
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.port = 0;
  options.asset_directory = assets;

  graph::dashboard::EmbeddedDashboardServer server(
      options, configuration_service, runtime_session, snapshot_collector);
  ASSERT_TRUE(server.Start()) << server.LastError();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  const auto metrics = HttpGet(server.BoundPort(), "/api/v1/metrics");
  ASSERT_EQ(metrics.status_code, 200) << metrics.body;
  const auto metrics_json = nlohmann::json::parse(metrics.body);
  const auto &manager_metrics = manager->GetMetrics();
  EXPECT_EQ(metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_FALSE(metrics_json.at("nodes").empty());
  EXPECT_FALSE(metrics_json.at("edges").empty());
  EXPECT_GT(metrics_json.at("graph").at("graph_total_enqueued").get<std::uint64_t>(), 0u);
  EXPECT_EQ(metrics_json.at("graph").at("graph_total_enqueued").get<std::uint64_t>(),
            manager_metrics.graph_total_enqueued.load());
  EXPECT_EQ(metrics_json.at("graph").at("graph_total_dequeued").get<std::uint64_t>(),
            manager_metrics.graph_total_dequeued.load());
  EXPECT_EQ(metrics_json.at("graph").at("total_items_processed").get<std::uint64_t>(),
            manager_metrics.total_items_processed.load());
  EXPECT_EQ(metrics_json.at("nodes").size(), manager->GetNodes().size());
  EXPECT_EQ(metrics_json.at("edges").size(), manager->GetEdges().size());
  EXPECT_EQ(metrics_json.at("graph"), cli_summary.at("graph_metrics"));
  EXPECT_EQ(metrics_json.at("nodes"), cli_summary.at("topology_activity").at("nodes"));
  EXPECT_EQ(metrics_json.at("edges"), cli_summary.at("topology_activity").at("edges"));

  const auto edge_metrics = HttpGet(server.BoundPort(), "/api/v1/metrics/edges");
  ASSERT_EQ(edge_metrics.status_code, 200) << edge_metrics.body;
  const auto edge_metrics_json = nlohmann::json::parse(edge_metrics.body);
  EXPECT_EQ(edge_metrics_json.at("edges"), metrics_json.at("edges"));
  ASSERT_FALSE(edge_metrics_json.at("edges").empty());
  EXPECT_EQ(edge_metrics_json.at("edges").at(0).at("source_node_index").get<std::size_t>(),
            manager->GetEdgeMetadata(0)->source_node_id);
  EXPECT_EQ(edge_metrics_json.at("edges").at(0).at("destination_node_index").get<std::size_t>(),
            manager->GetEdgeMetadata(0)->dest_node_id);

  const auto diagnostics = HttpGet(server.BoundPort(), "/api/v1/diagnostics");
  ASSERT_EQ(diagnostics.status_code, 200) << diagnostics.body;
  const auto diagnostics_json = nlohmann::json::parse(diagnostics.body);
  EXPECT_EQ(diagnostics_json.at("schema").get<std::string>(),
            "graphx.dashboard.diagnostics.v1");
  const auto sink_diagnostics =
      FindDiagnosticsByType(diagnostics_json, "FHSSMessageSinkNode");
  ASSERT_FALSE(sink_diagnostics.empty());
  EXPECT_EQ(sink_diagnostics.at("pulse_count"),
            cli_summary.at("fhss_diagnostics").at("pulse_count"));
  EXPECT_EQ(sink_diagnostics.at("truth_mismatch_count"),
            cli_summary.at("fhss_diagnostics").at("truth_mismatch_count"));
  EXPECT_EQ(sink_diagnostics.at("preamble_lock"),
            cli_summary.at("fhss_diagnostics").at("preamble_lock"));

  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

TEST(DashboardServerStep4Test, MetricsSchemaRemainsStableWhenRuntimePopulatesValues) {
  auto default_collector = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  const auto default_metrics = default_collector->GetMetricsSnapshot();
  const auto default_edge_metrics = default_collector->GetEdgeMetricsSnapshot();

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH).string())
                      .WithPluginDirectory(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY).string())
                      .WithExecutorTimeout(std::chrono::seconds(12))
                      .Build();
  ASSERT_TRUE(executor);

  auto manager = executor->GetGraphManager();
  ASSERT_TRUE(manager);
  manager->EnableMetrics(true);
  const auto execution_result = executor->Execute();
  ASSERT_TRUE(execution_result.success);

  auto runtime_session = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime_session->SetActiveGraphManager(manager);
  runtime_session->SetLifecycleState(graph::dashboard::GraphRuntimeSession::State::completed);

  auto collector = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  collector->BindRuntimeSession(runtime_session);
  const auto populated_metrics = collector->GetMetricsSnapshot();
  const auto populated_edge_metrics = collector->GetEdgeMetricsSnapshot();

  EXPECT_EQ(ObjectKeys(default_metrics), ObjectKeys(populated_metrics));
  EXPECT_EQ(ObjectKeys(default_metrics.at("graph")),
            ObjectKeys(populated_metrics.at("graph")));
  EXPECT_EQ(ObjectKeys(default_edge_metrics), ObjectKeys(populated_edge_metrics));
  EXPECT_TRUE(default_metrics.at("nodes").empty());
  EXPECT_TRUE(default_metrics.at("edges").empty());
  EXPECT_FALSE(populated_metrics.at("nodes").empty());
  EXPECT_FALSE(populated_metrics.at("edges").empty());
}

TEST(DashboardServerStep4Test, SnapshotCollectionInterruptionReturnsStablePayloadAndResumes) {
  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH).string())
                      .WithPluginDirectory(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY).string())
                      .WithExecutorTimeout(std::chrono::seconds(12))
                      .Build();
  ASSERT_TRUE(executor);

  auto manager = executor->GetGraphManager();
  ASSERT_TRUE(manager);
  manager->EnableMetrics(true);
  const auto execution_result = executor->Execute();
  ASSERT_TRUE(execution_result.success);

  auto runtime_session = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime_session->SetActiveGraphManager(manager);
  runtime_session->SetLifecycleState(graph::dashboard::GraphRuntimeSession::State::completed);

  auto snapshot_collector = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
  snapshot_collector->BindRuntimeSession(runtime_session);
  auto configuration_service =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)));

  const auto assets = MakeTempAssetDirectory("graphx_dashboard_step4_interrupt_assets");
  graph::dashboard::EmbeddedDashboardServer::Options options;
  options.port = 0;
  options.asset_directory = assets;

  graph::dashboard::EmbeddedDashboardServer server(
      options, configuration_service, runtime_session, snapshot_collector);
  ASSERT_TRUE(server.Start()) << server.LastError();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  snapshot_collector->InjectNextCollectionInterruptionForTesting();
  const auto interrupted_metrics = HttpGet(server.BoundPort(), "/api/v1/metrics");
  ASSERT_EQ(interrupted_metrics.status_code, 200) << interrupted_metrics.body;
  const auto interrupted_metrics_json = nlohmann::json::parse(interrupted_metrics.body);
  EXPECT_EQ(interrupted_metrics_json.at("schema").get<std::string>(),
            "graphx.dashboard.metrics.v1");
  EXPECT_EQ(interrupted_metrics_json.at("graph").at("graph_total_enqueued").get<std::uint64_t>(), 0u);
  EXPECT_TRUE(interrupted_metrics_json.at("nodes").empty());
  EXPECT_TRUE(interrupted_metrics_json.at("edges").empty());

  const auto resumed_metrics = HttpGet(server.BoundPort(), "/api/v1/metrics");
  ASSERT_EQ(resumed_metrics.status_code, 200) << resumed_metrics.body;
  const auto resumed_metrics_json = nlohmann::json::parse(resumed_metrics.body);
  EXPECT_GT(resumed_metrics_json.at("graph").at("graph_total_enqueued").get<std::uint64_t>(), 0u);
  EXPECT_FALSE(resumed_metrics_json.at("nodes").empty());
  EXPECT_FALSE(resumed_metrics_json.at("edges").empty());

  snapshot_collector->InjectNextCollectionInterruptionForTesting();
  const auto interrupted_diagnostics = HttpGet(server.BoundPort(), "/api/v1/diagnostics");
  ASSERT_EQ(interrupted_diagnostics.status_code, 200) << interrupted_diagnostics.body;
  const auto interrupted_diagnostics_json = nlohmann::json::parse(interrupted_diagnostics.body);
  EXPECT_EQ(interrupted_diagnostics_json.at("schema").get<std::string>(),
            "graphx.dashboard.diagnostics.v1");
  EXPECT_TRUE(interrupted_diagnostics_json.at("nodes").empty());

  const auto resumed_diagnostics = HttpGet(server.BoundPort(), "/api/v1/diagnostics");
  ASSERT_EQ(resumed_diagnostics.status_code, 200) << resumed_diagnostics.body;
  const auto resumed_diagnostics_json = nlohmann::json::parse(resumed_diagnostics.body);
  EXPECT_FALSE(resumed_diagnostics_json.at("nodes").empty());
  EXPECT_FALSE(FindDiagnosticsByType(resumed_diagnostics_json, "FHSSMessageSinkNode").empty());

  server.Stop();
  std::error_code error;
  std::filesystem::remove_all(assets, error);
}

} // namespace