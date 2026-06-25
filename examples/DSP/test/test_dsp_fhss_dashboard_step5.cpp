// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSSyntheticIqSourceNode.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/FHSSScenarioController.hpp"
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
#include <future>
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
  index << "<html><body>GraphX Dashboard Step5 Test</body></html>";
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

nlohmann::json CommandRequestJson(const std::string &command_id) {
  return nlohmann::json{{"command_id", command_id}};
}

dsp::fhss::FHSSSyntheticIqGeneratorConfig LoadSourceConfig(
    const nlohmann::json &graph_config) {
  for (const auto &node : graph_config.at("nodes")) {
    if (node.at("type") == "FHSSSyntheticIqSourceNode") {
      return dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
          graph::JsonView(node.at("node_config")));
    }
  }
  throw std::runtime_error("FHSSSyntheticIqSourceNode not found");
}

nlohmann::json LoadSourceNodeConfigJson(const nlohmann::json &graph_config) {
  for (const auto &node : graph_config.at("nodes")) {
    if (node.at("type") == "FHSSSyntheticIqSourceNode") {
      return node.at("node_config");
    }
  }
  throw std::runtime_error("FHSSSyntheticIqSourceNode not found");
}

class DashboardServerStep5Test : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step5_assets");
    config_ = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(config_);
    runtime_session_ = std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ = std::make_shared<graph::dashboard::GraphSnapshotCollector>();
    source_ = std::make_shared<dsp::fhss::FHSSSyntheticIqSourceNode>(LoadSourceConfig(config_));
    controller_ = std::make_shared<graph::dashboard::FHSSScenarioController>(
        configuration_service_, runtime_session_);
    controller_->BindInjectionSource(source_);

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.port = 0;
    options.asset_directory = assets_;

    server_ = std::make_unique<graph::dashboard::EmbeddedDashboardServer>(
        options, configuration_service_, runtime_session_, snapshot_collector_,
        controller_);
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
  nlohmann::json config_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::shared_ptr<dsp::fhss::FHSSSyntheticIqSourceNode> source_;
  std::shared_ptr<graph::dashboard::FHSSScenarioController> controller_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST(DashboardStep5SourceTest, ExactlyOneMessagePerStepBlocksBetweenRequests) {
  const auto config = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  dsp::fhss::FHSSSyntheticIqSourceNode source(LoadSourceConfig(config));
  const auto source_messages = LoadSourceNodeConfigJson(config).at("messages");
  ASSERT_GE(source_messages.size(), 1u);

  auto &queue = source.GetMessageInjectionQueue();

  auto first_future = std::async(std::launch::async, [&source]() {
    return source.Produce(std::integral_constant<std::size_t, 0>{});
  });
  EXPECT_EQ(first_future.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);

  ASSERT_TRUE(queue.Enqueue(graph::dashboard::FHSSMessageInjectionRequest{
      .kind = graph::dashboard::FHSSMessageInjectionKind::ScheduledMessage,
      .correlation = {.scenario_id = "scenario-a", .message_id = 1, .release_sequence = 1},
      .scheduled_message = source_messages.at(0),
      .end_of_stream_after_produce = false}));
  const auto first_token = first_future.get();
  ASSERT_TRUE(first_token.has_value());
  EXPECT_EQ(first_token->sidecar.correlation.message_id, 1u);
  EXPECT_EQ(first_token->sidecar.correlation.release_sequence, 1u);

  auto second_future = std::async(std::launch::async, [&source]() {
    return source.Produce(std::integral_constant<std::size_t, 0>{});
  });
  EXPECT_EQ(second_future.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);

  ASSERT_TRUE(queue.Enqueue(graph::dashboard::FHSSMessageInjectionRequest{
      .kind = graph::dashboard::FHSSMessageInjectionKind::ScheduledMessage,
      .correlation = {.scenario_id = "scenario-a", .message_id = 2, .release_sequence = 2},
      .scheduled_message = source_messages.at(0),
      .end_of_stream_after_produce = true}));
  const auto second_token = second_future.get();
  ASSERT_TRUE(second_token.has_value());
  EXPECT_EQ(second_token->sidecar.correlation.message_id, 2u);
  EXPECT_EQ(second_token->sidecar.correlation.release_sequence, 2u);
}

TEST_F(DashboardServerStep5Test, RejectsDuplicateOrConcurrentStepRequests) {
  controller_->SetAutoCompleteForTesting(false);

  const auto first = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/commands/step-message",
                                 CommandRequestJson("cmd-step5-1").dump());
  EXPECT_EQ(first.status_code, 202) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  EXPECT_EQ(first_json.at("status").get<std::string>(), "running");

  const auto duplicate = HttpRequest(server_->BoundPort(), "POST",
                                     "/api/v1/commands/step-message",
                                     CommandRequestJson("cmd-step5-2").dump());
  EXPECT_EQ(duplicate.status_code, 409) << duplicate.body;
  const auto duplicate_json = nlohmann::json::parse(duplicate.body);
  EXPECT_EQ(duplicate_json.at("code").get<std::string>(), "message_in_flight");

  const auto correlation = controller_->ActiveCorrelationForTesting();
  ASSERT_TRUE(correlation.has_value());
  controller_->PublishTerminalResultForTesting(graph::dashboard::FHSSMessageTerminalResult{
      .correlation = *correlation,
      .status = graph::dashboard::FHSSMessageTerminalStatus::Completed,
      .code = "message_completed",
      .message = "completed"});

  const auto operation = HttpRequest(
      server_->BoundPort(), "GET",
      "/api/v1/operations/" + first_json.at("operation_id").get<std::string>());
  EXPECT_EQ(operation.status_code, 200) << operation.body;
  const auto operation_json = nlohmann::json::parse(operation.body);
  EXPECT_TRUE(operation_json.at("terminal").get<bool>());
  EXPECT_EQ(operation_json.at("status").get<std::string>(), "succeeded");
}

TEST_F(DashboardServerStep5Test, ContinueProcessesMessagesSequentiallyAndCompletes) {
  controller_->SetAutoCompleteForTesting(true);

  const auto response = HttpRequest(server_->BoundPort(), "POST",
                                    "/api/v1/commands/continue",
                                    CommandRequestJson("cmd-step5-continue").dump());
  EXPECT_EQ(response.status_code, 202) << response.body;

  const auto response_json = nlohmann::json::parse(response.body);
  EXPECT_TRUE(response_json.at("terminal").get<bool>());
  EXPECT_EQ(response_json.at("status").get<std::string>(), "succeeded");
  ASSERT_TRUE(response_json.contains("result"));
  EXPECT_EQ(response_json.at("result").at("command").get<std::string>(), "continue");
  EXPECT_GT(response_json.at("result").at("message_count").get<std::uint64_t>(), 0u);
}

TEST_F(DashboardServerStep5Test, ResetRetainsTerminalRecordsAndRestartsScenarioCursor) {
  controller_->SetAutoCompleteForTesting(true);

  const auto first = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/commands/step-message",
                                 CommandRequestJson("cmd-step5-reset-1").dump());
  EXPECT_EQ(first.status_code, 202) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  EXPECT_TRUE(first_json.at("terminal").get<bool>());
  const auto first_correlation = first_json.at("result").at("correlation");

  const auto reset = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/commands/reset",
                                 CommandRequestJson("cmd-step5-reset").dump());
  EXPECT_EQ(reset.status_code, 202) << reset.body;

  const auto retained = HttpRequest(
      server_->BoundPort(), "GET",
      "/api/v1/operations/" + first_json.at("operation_id").get<std::string>());
  EXPECT_EQ(retained.status_code, 200) << retained.body;
  const auto retained_json = nlohmann::json::parse(retained.body);
  EXPECT_TRUE(retained_json.at("terminal").get<bool>());

  const auto after_reset = HttpRequest(server_->BoundPort(), "POST",
                                       "/api/v1/commands/step-message",
                                       CommandRequestJson("cmd-step5-reset-2").dump());
  EXPECT_EQ(after_reset.status_code, 202) << after_reset.body;
  const auto after_reset_json = nlohmann::json::parse(after_reset.body);
  const auto after_reset_correlation = after_reset_json.at("result").at("correlation");
  EXPECT_EQ(after_reset_correlation.at("message_id").get<std::uint64_t>(),
            first_correlation.at("message_id").get<std::uint64_t>());
  EXPECT_NE(after_reset_correlation.at("scenario_id").get<std::string>(),
            first_correlation.at("scenario_id").get<std::string>());
}

TEST_F(DashboardServerStep5Test, FailureInjectionTimeoutRaceAndQueueDisableAreStable) {
  controller_->SetAutoCompleteForTesting(false);

  const auto first = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/commands/step-message",
                                 CommandRequestJson("cmd-step5-race").dump());
  EXPECT_EQ(first.status_code, 202) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  const auto correlation = controller_->ActiveCorrelationForTesting();
  ASSERT_TRUE(correlation.has_value());

  controller_->PublishTerminalResultForTesting(graph::dashboard::FHSSMessageTerminalResult{
      .correlation = *correlation,
      .status = graph::dashboard::FHSSMessageTerminalStatus::TimedOut,
      .code = "step_timeout",
      .message = "timed out"});
  controller_->PublishTerminalResultForTesting(graph::dashboard::FHSSMessageTerminalResult{
      .correlation = *correlation,
      .status = graph::dashboard::FHSSMessageTerminalStatus::Cancelled,
      .code = "cancelled_late",
      .message = "cancelled late"});

  const auto operation = HttpRequest(
      server_->BoundPort(), "GET",
      "/api/v1/operations/" + first_json.at("operation_id").get<std::string>());
  EXPECT_EQ(operation.status_code, 200) << operation.body;
  const auto operation_json = nlohmann::json::parse(operation.body);
  EXPECT_EQ(operation_json.at("result").at("terminal_status").get<std::string>(), "timed_out");

  const auto reset = HttpRequest(server_->BoundPort(), "POST",
                                 "/api/v1/commands/reset",
                                 CommandRequestJson("cmd-step5-race-reset").dump());
  EXPECT_EQ(reset.status_code, 202) << reset.body;

  source_->DisableMessageInjectionQueue();
  const auto disabled = HttpRequest(server_->BoundPort(), "POST",
                                    "/api/v1/commands/step-message",
                                    CommandRequestJson("cmd-step5-disabled").dump());
  EXPECT_EQ(disabled.status_code, 202) << disabled.body;
  const auto disabled_json = nlohmann::json::parse(disabled.body);
  EXPECT_TRUE(disabled_json.at("terminal").get<bool>());
  EXPECT_EQ(disabled_json.at("status").get<std::string>(), "failed");
}

} // namespace