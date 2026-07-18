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
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
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

std::filesystem::path MakeTempArtifactDirectory(const std::string &name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);
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

class FhssDashboardConfigurationTest : public ::testing::Test {
protected:
  void SetUp() override {
    assets_ = MakeTempAssetDirectory("graphx_dashboard_step2_assets");
    artifacts_ = MakeTempArtifactDirectory("graphx_dashboard_step2_artifacts");

    auto config =
        LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
    configuration_service_ =
        std::make_shared<graph::dashboard::GraphConfigurationService>(config);
    runtime_session_ =
        std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ =
        std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.enable_mutating_routes = true;
    options.port = 0;
    options.asset_directory = assets_;
    options.artifact_root = artifacts_;

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
    std::filesystem::remove_all(artifacts_, error);
  }

  std::filesystem::path assets_;
  std::filesystem::path artifacts_;
  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<graph::dashboard::GraphSnapshotCollector> snapshot_collector_;
  std::unique_ptr<graph::dashboard::EmbeddedDashboardServer> server_;
};

TEST_F(FhssDashboardConfigurationTest,
       PatchRejectsGeneratedFieldsAndStaleRevisions) {
  const auto valid_patch =
      nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                     {"command_id", "cmd-step2-patch-1"},
                     {"expected_revision", 1},
                     {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                     {"value", 1240000001.0},
                     {"apply", "staged"}};

  const auto first = HttpRequest(server_->BoundPort(), "PATCH",
                                 "/api/v1/fhss/config", valid_patch.dump());
  EXPECT_EQ(first.status_code, 200) << first.body;
  const auto first_json = nlohmann::json::parse(first.body);
  EXPECT_EQ(first_json.at("schema").get<std::string>(),
            "graphx.dashboard.config_result.v1");
  EXPECT_EQ(first_json.at("old_revision").get<std::uint64_t>(), 1u);
  EXPECT_EQ(first_json.at("new_revision").get<std::uint64_t>(), 2u);
  EXPECT_TRUE(first_json.at("validation").at("valid").get<bool>());
  EXPECT_FALSE(first_json.contains("operation_id"));

  const auto stale_patch =
      nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                     {"command_id", "cmd-step2-patch-2"},
                     {"expected_revision", 1},
                     {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                     {"value", 1240000002.0},
                     {"apply", "staged"}};
  const auto stale = HttpRequest(server_->BoundPort(), "PATCH",
                                 "/api/v1/fhss/config", stale_patch.dump());
  EXPECT_EQ(stale.status_code, 409) << stale.body;
  const auto stale_json = nlohmann::json::parse(stale.body);
  EXPECT_EQ(stale_json.at("code").get<std::string>(),
            "stale_revision_conflict");
  EXPECT_EQ(stale_json.at("revision").get<std::uint64_t>(), 2u);

  const auto generated_patch =
      nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                     {"command_id", "cmd-step2-patch-3"},
                     {"expected_revision", 2},
                     {"pointer", "/fhss/scenario/active_frequency_indices"},
                     {"value", nlohmann::json::array({24, 28, 32, 36})},
                     {"apply", "staged"}};
  const auto generated = HttpRequest(server_->BoundPort(), "PATCH",
                                     "/api/v1/fhss/config", generated_patch.dump());
  EXPECT_EQ(generated.status_code, 409) << generated.body;
  const auto generated_json = nlohmann::json::parse(generated.body);
  EXPECT_EQ(generated_json.at("code").get<std::string>(),
            "derived_field_read_only");
  EXPECT_EQ(generated_json.at("pointer").get<std::string>(),
            "/fhss/scenario/active_frequency_indices");
  EXPECT_EQ(generated_json.at("details")
                .at("authoritative_pointer")
                .get<std::string>(),
            "/fhss/scenario");
}

TEST_F(FhssDashboardConfigurationTest,
       ValidationErrorsHaveStableLevelsAndShape) {
  const auto invalid_patch = nlohmann::json{
      {"schema", "graphx.dashboard.config_update.v1"},
      {"command_id", "cmd-step2-invalid"},
      {"expected_revision", 1},
      {"pointer", "/fhss/scenario/messages/0/pulses/0/frequency_index"},
      {"value", 0},
      {"apply", "validate"}};

  const auto response = HttpRequest(server_->BoundPort(), "PATCH",
                                    "/api/v1/fhss/config", invalid_patch.dump());
  EXPECT_EQ(response.status_code, 200) << response.body;
  const auto json = nlohmann::json::parse(response.body);
  EXPECT_EQ(json.at("schema").get<std::string>(),
            "graphx.dashboard.config_validation.v1");
  EXPECT_FALSE(json.at("validation").at("valid").get<bool>());
  ASSERT_FALSE(json.at("validation").at("errors").empty());
  const auto &error = json.at("validation").at("errors").at(0);
  EXPECT_EQ(error.at("level").get<std::string>(), "semantic");
  EXPECT_EQ(error.at("pointer").get<std::string>(),
            "/fhss/scenario/messages/0/pulses/0/frequency_index");
  EXPECT_FALSE(error.at("code").get<std::string>().empty());
  EXPECT_FALSE(error.at("message").get<std::string>().empty());
}

TEST_F(FhssDashboardConfigurationTest,
       PatchExportAndOperationLifecycleAreContracted) {
  const auto patch =
      nlohmann::json{{"schema", "graphx.dashboard.config_update.v1"},
                     {"command_id", "cmd-step2-export-patch"},
                     {"expected_revision", 1},
                     {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
                     {"value", 1240000001.0},
                     {"apply", "staged"}};
  const auto patch_response = HttpRequest(server_->BoundPort(), "PATCH",
                                          "/api/v1/fhss/config", patch.dump());
  EXPECT_EQ(patch_response.status_code, 200) << patch_response.body;
  const auto patch_json = nlohmann::json::parse(patch_response.body);
  EXPECT_TRUE(patch_json.at("validation").at("valid").get<bool>());
  EXPECT_FALSE(patch_json.contains("operation_id"));

  const auto export_path = artifacts_ / "export" / "effective.json";
  const auto export_request =
      nlohmann::json{{"schema", "graphx.dashboard.config_export.v1"},
                     {"command_id", "cmd-step2-export-1"},
                     {"expected_revision", 2},
                     {"output_path", export_path.string()},
                     {"resource", "effective"}};
  const auto export_response =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/export",
                  export_request.dump());
  EXPECT_EQ(export_response.status_code, 202) << export_response.body;
  const auto export_json = nlohmann::json::parse(export_response.body);
  ASSERT_TRUE(export_json.contains("operation_id"));
  EXPECT_EQ(export_json.at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(export_json.at("submitted_revision").get<std::uint64_t>(), 2u);
  ASSERT_TRUE(std::filesystem::exists(export_path)) << export_response.body;

  const auto operation_id = export_json.at("operation_id").get<std::string>();
  const auto operation = HttpRequest(server_->BoundPort(), "GET",
                                     "/api/v1/fhss/operations/" + operation_id);
  EXPECT_EQ(operation.status_code, 200) << operation.body;
  const auto operation_json = nlohmann::json::parse(operation.body);
  EXPECT_TRUE(operation_json.at("terminal").get<bool>());
  EXPECT_EQ(operation_json.at("status").get<std::string>(), "succeeded");
  EXPECT_EQ(operation_json.at("result").at("output_path").get<std::string>(),
            export_path.string());

  const auto cancel =
      HttpRequest(server_->BoundPort(), "POST",
                  "/api/v1/fhss/operations/" + operation_id + "/cancel");
  EXPECT_EQ(cancel.status_code, 409) << cancel.body;
  const auto cancel_json = nlohmann::json::parse(cancel.body);
  EXPECT_EQ(cancel_json.at("code").get<std::string>(),
            "operation_not_terminal");

  const auto deleted = HttpRequest(server_->BoundPort(), "DELETE",
                                   "/api/v1/fhss/operations/" + operation_id);
  EXPECT_EQ(deleted.status_code, 204) << deleted.body;

  const auto after_delete = HttpRequest(server_->BoundPort(), "GET",
                                        "/api/v1/fhss/operations/" + operation_id);
  EXPECT_EQ(after_delete.status_code, 404) << after_delete.body;
  const auto after_delete_json = nlohmann::json::parse(after_delete.body);
  EXPECT_EQ(after_delete_json.at("code").get<std::string>(),
            "operation_not_found_or_expired");
}

TEST(FhssDashboardConfigurationServiceTest,
     ExportReplayAndFailureInjectionBehaveDeterministically) {
  auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(config);
  service.SetArtifactRoot(
      MakeTempArtifactDirectory("graphx_dashboard_step2_service_artifacts"));

  std::vector<std::string> observed_events;
  bool connected = true;
  service.SetUpdateEventSinkForTesting([&](const nlohmann::json &event) {
    if (event.value("sequence", 0) == 1) {
      observed_events.push_back("queued");
      connected = false;
      return;
    }
    if (!connected && event.value("sequence", 0) == 2) {
      connected = true;
      return;
    }
    if (connected) {
      observed_events.push_back(event.value("status", std::string{}));
    }
  });

  const auto export_path = std::filesystem::temp_directory_path() /
                           "graphx_dashboard_step2_service_artifacts" /
                           "replay" / "effective.json";
  const auto request =
      nlohmann::json{{"schema", "graphx.dashboard.config_export.v1"},
                     {"command_id", "cmd-replay"},
                     {"expected_revision", 1},
                     {"output_path", export_path.string()},
                     {"resource", "effective"}};
  const auto first = service.ExportConfig(request);
  EXPECT_EQ(first.at("status").get<std::string>(), "succeeded");
  EXPECT_FALSE(observed_events.empty());
  EXPECT_EQ(observed_events.front(), "queued");

  const auto replay = service.ExportConfig(request);
  EXPECT_EQ(replay.at("operation_id").get<std::string>(),
            first.at("operation_id").get<std::string>());

  auto mismatched_request = request;
  mismatched_request["output_path"] =
      (export_path.parent_path() / "other.json").string();
  const auto mismatched = service.ExportConfig(mismatched_request);
  EXPECT_EQ(mismatched.at("schema").get<std::string>(),
            "graphx.dashboard.error.v1");
  EXPECT_EQ(mismatched.at("code").get<std::string>(),
            "idempotency_key_reused_with_different_payload");

  service.SetValidationInjectorForTesting(
      [](const nlohmann::json &request_body)
          -> std::optional<
              graph::dashboard::GraphConfigurationService::ValidationError> {
        if (request_body.value("command_id", std::string{}) == "cmd-enospc") {
          return graph::dashboard::GraphConfigurationService::ValidationError{
              .level = "construction",
              .node_id = "graph",
              .pointer = "/api/v1/fhss/config/export",
              .code = "enospc_during_export",
              .message = "No space left on device"};
        }
        return std::nullopt;
      });
  const auto failure_request = nlohmann::json{
      {"schema", "graphx.dashboard.config_export.v1"},
      {"command_id", "cmd-enospc"},
      {"expected_revision", 1},
      {"output_path", (export_path.parent_path() / "enospc.json").string()},
      {"resource", "effective"}};
  const auto failure = service.ExportConfig(failure_request);
  EXPECT_EQ(failure.at("status").get<std::string>(), "failed");
  EXPECT_EQ(failure.at("result").at("code").get<std::string>(),
            "enospc_during_export");

  const auto operation_id = first.at("operation_id").get<std::string>();
  service.ExpireOperationForTesting(operation_id);
  const auto expired = service.GetOperationResponse(operation_id);
  EXPECT_EQ(expired.at("schema").get<std::string>(),
            "graphx.dashboard.error.v1");
  EXPECT_EQ(expired.at("code").get<std::string>(),
            "operation_not_found_or_expired");
}

} // namespace
