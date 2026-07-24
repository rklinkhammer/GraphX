// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "FHSSDashboardConfigurationPolicy.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/dashboard/EmbeddedDashboardServer.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif
#ifndef DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH
#define DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH                                  \
  "libdsp/config/fhss_phase2_binary_iq_receiver.json"
#endif
#ifndef DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH
#define DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH "./graphx-dsp-fhss-iq-generator"
#endif
#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

struct HttpResponse {
  int status_code = 0;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
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
                         const std::string &body = {},
                         const std::string &content_type = "application/json",
                         const std::string &if_match = {}) {
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
  request << "Content-Type: " << content_type << "\r\n";
  if (!if_match.empty())
    request << "If-Match: " << if_match << "\r\n";
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
  std::size_t header_offset = first_line_end + 2;
  while (header_offset < body_pos) {
    const auto end = response.find("\r\n", header_offset);
    if (end == std::string::npos || end > body_pos)
      break;
    const auto colon = response.find(':', header_offset);
    if (colon != std::string::npos && colon < end) {
      auto name = response.substr(header_offset, colon - header_offset);
      std::transform(
          name.begin(), name.end(), name.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      auto value = response.substr(colon + 1, end - colon - 1);
      while (!value.empty() && value.front() == ' ')
        value.erase(value.begin());
      parsed.headers[name] = value;
    }
    header_offset = end + 2;
  }
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
        std::make_shared<graph::dashboard::GraphConfigurationService>(
            config,
            std::make_shared<
                dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
    runtime_session_ =
        std::make_shared<graph::dashboard::GraphRuntimeSession>();
    snapshot_collector_ =
        std::make_shared<graph::dashboard::GraphSnapshotCollector>();

    graph::dashboard::EmbeddedDashboardServer::Options options;
    options.enable_configuration_mutation_routes = true;
    options.enable_runtime_control_routes = false;
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
  const auto generated =
      HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/fhss/config",
                  generated_patch.dump());
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
       CanonicalJsonPatchUsesStrongEtagsAndAtomicPreconditions) {
  const auto initial =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/config");
  ASSERT_EQ(initial.status_code, 200);
  ASSERT_TRUE(initial.headers.contains("etag"));
  const auto etag = initial.headers.at("etag");
  EXPECT_EQ(etag, "\"graphx-config-1\"");

  const auto patch =
      nlohmann::json::array({{{"op", "replace"},
                              {"path", "/iq_center_frequency_hz"},
                              {"value", 1240000001.0}}});
  const auto missing =
      HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/fhss/config",
                  patch.dump(), "application/json-patch+json");
  EXPECT_EQ(missing.status_code, 428) << missing.body;

  const auto validation =
      HttpRequest(server_->BoundPort(), "POST", "/api/v1/fhss/config/validate",
                  patch.dump(), "application/json-patch+json", etag);
  EXPECT_EQ(validation.status_code, 200) << validation.body;
  EXPECT_EQ(validation.headers.at("etag"), etag);
  const auto after_validation =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/config");
  EXPECT_EQ(after_validation.body, initial.body);

  const auto applied =
      HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/fhss/config",
                  patch.dump(), "application/json-patch+json", etag);
  EXPECT_EQ(applied.status_code, 200) << applied.body;
  EXPECT_EQ(applied.headers.at("etag"), "\"graphx-config-2\"");

  const auto stale =
      HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/fhss/config",
                  patch.dump(), "application/json-patch+json", etag);
  EXPECT_EQ(stale.status_code, 412) << stale.body;

  const auto before_atomic =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/config");
  const auto failing =
      nlohmann::json::array({{{"op", "replace"},
                              {"path", "/iq_center_frequency_hz"},
                              {"value", 1240000123.0}},
                             {{"op", "remove"}, {"path", "/does-not-exist"}}});
  const auto rejected = HttpRequest(
      server_->BoundPort(), "PATCH", "/api/v1/fhss/config", failing.dump(),
      "application/json-patch+json", "\"graphx-config-2\"");
  EXPECT_EQ(rejected.status_code, 400) << rejected.body;
  const auto after_atomic =
      HttpRequest(server_->BoundPort(), "GET", "/api/v1/fhss/config");
  EXPECT_EQ(after_atomic.body, before_atomic.body);
  EXPECT_EQ(after_atomic.headers.at("etag"), "\"graphx-config-2\"");
}

TEST_F(FhssDashboardConfigurationTest,
       PhaseTwoCapabilityDoesNotExposeRuntimeLifecycleRoutes) {
  for (const auto &target :
       {"/api/v1/fhss/config/rebuild", "/api/v1/fhss/commands/start",
        "/api/v1/fhss/commands/stop", "/api/v1/fhss/operations/op-1"}) {
    const auto response =
        HttpRequest(server_->BoundPort(), "POST", target, "{}");
    EXPECT_TRUE(response.status_code == 404 || response.status_code == 405)
        << target << ": " << response.body;
  }
}

TEST(FhssDashboardConfigurationPolicyTest,
     FullJsonPatchPointerSemanticsAndReceiverTruthSeparation) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<
                  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  const auto patch = nlohmann::json::array(
      {{{"op", "add"},
        {"path", "/operator~1notes"},
        {"value", {{"til~de", nlohmann::json::array({1, nullptr})}}}},
       {{"op", "add"}, {"path", "/operator~1notes/til~0de/-"}, {"value", 3}},
       {{"op", "copy"},
        {"from", "/operator~1notes/til~0de/0"},
        {"path", "/operator~1notes/copied"}},
       {{"op", "move"},
        {"from", "/operator~1notes/til~0de/2"},
        {"path", "/operator~1notes/moved"}},
       {{"op", "test"}, {"path", "/operator~1notes/moved"}, {"value", 3}}});
  const auto result = service.ApplyJsonPatch(patch, service.ETag(), false);
  ASSERT_EQ(result.value("status", std::string{}), "applied") << result.dump();
  const auto authoritative = service.GetScenarioResponse().at("scenario");
  EXPECT_EQ(authoritative.at("operator/notes").at("til~de").size(), 2u);
  EXPECT_EQ(authoritative.at("operator/notes").at("copied"), 1);
  EXPECT_EQ(authoritative.at("operator/notes").at("moved"), 3);

  const auto receiver = service.GetReceiverGraphResponse().at("graph");
  const auto serialized = receiver.dump();
  EXPECT_EQ(serialized.find("\"messages\""), std::string::npos);
  EXPECT_EQ(serialized.find("truth_from_fixture"), std::string::npos);
  EXPECT_EQ(serialized.find("generator_metadata"), std::string::npos);
  ASSERT_TRUE(receiver.contains("nodes"));
  const auto source =
      std::find_if(receiver.at("nodes").begin(), receiver.at("nodes").end(),
                   [](const auto &node) {
                     return node.value("id", std::string{}) == "source";
                   });
  ASSERT_NE(source, receiver.at("nodes").end());
  EXPECT_EQ(source->at("type"), "FHSSBinaryIqFileSourceNode");
}

TEST(GraphConfigurationJsonPatchTest,
     RejectsMalformedPointersAndExercisesRootNullAndArrayRules) {
  graph::dashboard::GraphConfigurationService service(
      nlohmann::json{{"array", nlohmann::json::array({"zero", nullptr})},
                     {"present_null", nullptr}});

  const auto root_test = service.ApplyJsonPatch(
      nlohmann::json::array({{{"op", "test"},
                               {"path", ""},
                               {"value", service.GetScenarioResponse().at("scenario")}}}),
      service.ETag(), true);
  EXPECT_EQ(root_test.value("status", std::string{}), "validated")
      << root_test.dump();

  for (const auto &patch : std::array{
           nlohmann::json::array({{{"op", "add"}, {"path", "/bad~2"}, {"value", 1}}}),
           nlohmann::json::array({{{"op", "remove"}, {"path", "/array/01"}}}),
           nlohmann::json::array({{{"op", "remove"}, {"path", "/array/-"}}}),
           nlohmann::json::array({{{"op", "replace"}, {"path", "/array/-"}, {"value", 1}}}),
           nlohmann::json::array({{{"op", "test"}, {"path", "/absent"}, {"value", nullptr}}})}) {
    const auto result = service.ApplyJsonPatch(patch, service.ETag(), true);
    EXPECT_NE(result.value("code", std::string{}), "") << result.dump();
  }

  const auto root_replace = service.ApplyJsonPatch(
      nlohmann::json::array({{{"op", "replace"},
                               {"path", ""},
                               {"value", nlohmann::json{{"replacement", true}}}}}),
      service.ETag(), false);
  ASSERT_TRUE(root_replace.contains("status") &&
              root_replace.at("status").is_string());
  EXPECT_EQ(root_replace.at("status"), "applied") << root_replace.dump();
}

TEST(GraphConfigurationJsonPatchTest, FailureCategoriesAreIndividuallyAtomic) {
  const auto config = LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  const auto assert_rollback = [&](const nlohmann::json &patch) {
    const auto before = service.GetScenarioResponse().dump();
    const auto revision = service.ConfigRevision();
    const auto etag = service.ETag();
    const auto result = service.ApplyJsonPatch(patch, etag, false);
    EXPECT_TRUE(result.contains("code") ||
                (result.contains("validation") && !result["validation"]["valid"].get<bool>()));
    EXPECT_EQ(service.GetScenarioResponse().dump(), before);
    EXPECT_EQ(service.ConfigRevision(), revision);
    EXPECT_EQ(service.ETag(), etag);
  };
  assert_rollback(nlohmann::json::array({{{"op","increment"},{"path","/iq_center_frequency_hz"},{"value",1}}}));
  assert_rollback(nlohmann::json::array({{{"op","remove"},{"path",""}}}));
  assert_rollback(nlohmann::json::array({{{"op","test"},{"path","/iq_center_frequency_hz"},{"value",0}}}));
  assert_rollback(nlohmann::json::array({{{"op","replace"},{"path","/sample_rate_hz"},{"value",1}}}));

  graph::dashboard::GraphConfigurationService arrays(
      nlohmann::json{{"values", nlohmann::json::array({1, 3})}});
  auto result = arrays.ApplyJsonPatch(nlohmann::json::array({{{"op","add"},{"path","/values/1"},{"value",2}}}), arrays.ETag(), false);
  ASSERT_EQ(result.at("status"), "applied");
  EXPECT_EQ(arrays.GetScenarioResponse()["scenario"]["values"], nlohmann::json::array({1,2,3}));
  result = arrays.ApplyJsonPatch(nlohmann::json::array({{{"op","remove"},{"path","/values/0"}}}), arrays.ETag(), false);
  ASSERT_EQ(result.at("status"), "applied");
  EXPECT_EQ(arrays.GetScenarioResponse()["scenario"]["values"], nlohmann::json::array({2,3}));
  result = arrays.ApplyJsonPatch(nlohmann::json::array({{{"op","add"},{"path",""},{"value",nlohmann::json{{"root",true}}}}}), arrays.ETag(), false);
  EXPECT_EQ(result.at("status"), "applied");
  EXPECT_TRUE(arrays.GetScenarioResponse()["scenario"]["root"]);
}

TEST(GraphConfigurationJsonPatchTest,
     RevisionStopsAtJavascriptSafeIntegerWithoutMutatingConfiguration) {
  constexpr std::uint64_t kMaximumJavascriptSafeInteger =
      9'007'199'254'740'991ULL;
  graph::dashboard::GraphConfigurationService service(
      nlohmann::json{{"value", 1}});
  service.SetRevisionForTesting(kMaximumJavascriptSafeInteger - 1);

  const auto first = service.ApplyJsonPatch(
      nlohmann::json::array(
          {{{"op", "replace"}, {"path", "/value"}, {"value", 2}}}),
      service.ETag(), false);
  ASSERT_EQ(first.value("status", std::string{}), "applied") << first.dump();
  EXPECT_EQ(service.ConfigRevision(), kMaximumJavascriptSafeInteger);
  EXPECT_EQ(service.GetScenarioResponse().at("scenario").at("value"), 2);

  const auto before = service.GetScenarioResponse();
  const auto exhausted = service.ApplyJsonPatch(
      nlohmann::json::array(
          {{{"op", "replace"}, {"path", "/value"}, {"value", 3}}}),
      service.ETag(), false);
  EXPECT_EQ(exhausted.value("code", std::string{}),
            "revision_space_exhausted")
      << exhausted.dump();
  EXPECT_EQ(service.ConfigRevision(), kMaximumJavascriptSafeInteger);
  EXPECT_EQ(service.GetScenarioResponse(), before);

  const auto undo = service.UndoLastEdit();
  EXPECT_EQ(undo.value("code", std::string{}), "revision_space_exhausted")
      << undo.dump();
  EXPECT_EQ(service.ConfigRevision(), kMaximumJavascriptSafeInteger);
  EXPECT_EQ(service.GetScenarioResponse(), before);
}

TEST(FhssDashboardConfigurationPolicyTest,
     AliasGeneratedPathsAndMoveCopySourcesCannotBypassReadOnlyPolicy) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<
                  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  for (const auto &path : {"/active_frequency_indices",
                           "/fhss/scenario/active_frequency_indices",
                           "/nodes/source/node_config/active_frequency_indices"}) {
    const auto patch = nlohmann::json::array(
        {{{"op", "replace"}, {"path", path}, {"value", nlohmann::json::array()}}});
    const auto result = service.ApplyJsonPatch(patch, service.ETag(), false);
    EXPECT_EQ(result.value("code", std::string{}), "derived_field_read_only")
        << path << ": " << result.dump();
  }
  for (const auto op : {"move", "copy"}) {
    const auto patch = nlohmann::json::array(
        {{{"op", op},
          {"from", "/nodes/source/node_config/preamble_pulses"},
          {"path", "/operator_copy"}}});
    const auto result = service.ApplyJsonPatch(patch, service.ETag(), false);
    EXPECT_EQ(result.value("code", std::string{}), "derived_field_read_only")
        << result.dump();
  }
}

TEST(FhssDashboardAtomicCasTest,
     SimultaneousSameEtagWritersHaveSingleCasWinner) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<
                  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  const auto etag = service.ETag();
  const auto patch = nlohmann::json::array(
      {{{"op", "replace"},
        {"path", "/iq_center_frequency_hz"},
        {"value", 1240000001.0}}});
  std::barrier start(3);
  std::array<nlohmann::json, 2> results;
  std::array<std::jthread, 2> writers{
      std::jthread([&] { start.arrive_and_wait(); results[0] = service.ApplyJsonPatch(patch, etag, false); }),
      std::jthread([&] { start.arrive_and_wait(); results[1] = service.ApplyJsonPatch(patch, etag, false); })};
  start.arrive_and_wait();
  for (auto &writer : writers)
    writer.join();
  const auto wins = std::count_if(results.begin(), results.end(), [](const auto &r) {
    return r.contains("status") && r.at("status").is_string() &&
           r.at("status") == "applied";
  });
  const auto stale = std::count_if(results.begin(), results.end(), [](const auto &r) {
    return r.value("code", std::string{}) == "etag_precondition_failed";
  });
  EXPECT_EQ(wins, 1);
  EXPECT_EQ(stale, 1);
  EXPECT_EQ(service.ConfigRevision(), 2u);
}

TEST(FhssDashboardAtomicCasTest,
     SimultaneousLegacyRevisionWritersHaveSingleCasWinner) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<
                  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  std::barrier start(3);
  std::array<nlohmann::json, 2> results;
  std::array<std::jthread, 2> writers{
      std::jthread([&] {
        start.arrive_and_wait();
        results[0] = service.PatchConfig(
            {{"schema", "graphx.dashboard.config_update.v1"},
             {"command_id", "legacy-writer-1"}, {"expected_revision", 1},
             {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
             {"value", 1240000001.0}, {"apply", "staged"}});
      }),
      std::jthread([&] {
        start.arrive_and_wait();
        results[1] = service.PatchConfig(
            {{"schema", "graphx.dashboard.config_update.v1"},
             {"command_id", "legacy-writer-2"}, {"expected_revision", 1},
             {"pointer", "/fhss/scenario/iq_center_frequency_hz"},
             {"value", 1240000002.0}, {"apply", "staged"}});
      })};
  start.arrive_and_wait();
  for (auto &writer : writers)
    writer.join();
  EXPECT_EQ(std::count_if(results.begin(), results.end(), [](const auto &r) {
              return r.contains("status") && r.at("status").is_string() &&
                     r.at("status") == "staged";
            }),
            1);
  EXPECT_EQ(std::count_if(results.begin(), results.end(), [](const auto &r) {
              return r.value("code", std::string{}) == "stale_revision_conflict";
            }),
            1);
  EXPECT_EQ(service.ConfigRevision(), 2u);
}

TEST_F(FhssDashboardConfigurationTest,
       ReceiverFacingGetsRecursivelyExcludeGeneratorTruth) {
  for (const auto target : {"/api/v1/fhss/config", "/api/v1/fhss/config/effective",
                            "/api/v1/fhss/graph", "/api/v1/fhss/graph/receiver-minimal",
                            "/api/v1/fhss/nodes/source",
                            "/api/v1/fhss/nodes/source/parameters"}) {
    const auto response = HttpRequest(server_->BoundPort(), "GET", target);
    ASSERT_EQ(response.status_code, 200) << target << ": " << response.body;
    for (const auto forbidden : {"\"messages\"", "truth_from_fixture",
                                 "generator_metadata", "transmitted_active_frequency_indices",
                                 "transmitted_pulse_frequency_indices"}) {
      EXPECT_EQ(response.body.find(forbidden), std::string::npos)
          << target << " leaked " << forbidden;
    }
  }
  const auto authoritative = HttpRequest(
      server_->BoundPort(), "GET", "/api/v1/fhss/config/authoritative");
  ASSERT_EQ(authoritative.status_code, 200);
  EXPECT_NE(authoritative.body.find("\"messages\""), std::string::npos);
}

TEST_F(FhssDashboardConfigurationTest, ExactMediaTypesRejectSuffixBypasses) {
  const auto patch = nlohmann::json::array(
      {{{"op", "test"}, {"path", "/iq_center_frequency_hz"},
        {"value", 1240000000.0}}});
  for (const auto media : {"application/json-patch+json-bogus",
                           "application/json-bogus", "application/json; broken"}) {
    const auto response = HttpRequest(
        server_->BoundPort(), "PATCH", "/api/v1/fhss/config", patch.dump(),
        media, configuration_service_->ETag());
    EXPECT_EQ(response.status_code, 415) << media << ": " << response.body;
  }
  const auto validate = HttpRequest(
      server_->BoundPort(), "POST", "/api/v1/fhss/config/validate",
      patch.dump(), "application/json-patch+json-bogus",
      configuration_service_->ETag());
  EXPECT_EQ(validate.status_code, 415) << validate.body;
}

TEST(FhssDashboardConfigurationPolicyTest,
     ArchitectureGoldenUses6500SampleWindowsAndCheckedArithmetic) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy policy;
  auto authoritative = policy.ExtractAuthoritative(config);
  ASSERT_GE(authoritative.at("messages").size(), 2u);
  authoritative["messages"].erase(authoritative["messages"].begin() + 2,
                                  authoritative["messages"].end());
  const auto first_start =
      authoritative.at("messages")
          .at(0)
          .value("transmit_start_sample", std::uint64_t{0});
  const auto first_count =
      authoritative.at("messages").at(0).at("pulses").size();
  const auto independent_end =
      first_start +
      static_cast<std::uint64_t>(first_count) * std::uint64_t{6500};

  authoritative["messages"][1]["transmit_start_sample"] = independent_end;
  auto exact_boundary = policy.Validate(authoritative);
  EXPECT_TRUE(exact_boundary.at("valid").get<bool>()) << exact_boundary.dump();

  authoritative["messages"][1]["transmit_start_sample"] = independent_end - 1;
  const auto overlap = policy.Validate(authoritative);
  EXPECT_FALSE(overlap.at("valid").get<bool>());
  EXPECT_NE(overlap.dump().find("scheduled_messages_overlap"),
            std::string::npos);

  authoritative["allow_overlap"] = true;
  EXPECT_TRUE(policy.Validate(authoritative).at("valid").get<bool>());

  authoritative["allow_overlap"] = false;
  authoritative["messages"][1]["transmit_start_sample"] =
      std::numeric_limits<std::uint64_t>::max();
  const auto overflow = policy.Validate(authoritative);
  EXPECT_FALSE(overflow.at("valid").get<bool>());
  EXPECT_NE(overflow.dump().find("message_window_overflow"), std::string::npos);

  const auto effective =
      policy.DeriveEffective(config, policy.ExtractAuthoritative(config));
  EXPECT_EQ(effective.at("dashboard_derived").at("active_frequency_indices"),
            nlohmann::json::array({24, 28, 32, 36}));
  EXPECT_EQ(effective.at("dashboard_derived").at("timing").at("pulse_samples"),
            3200);
  EXPECT_EQ(effective.at("dashboard_derived").at("timing").at("gap_samples"),
            3300);
  EXPECT_EQ(
      effective.at("dashboard_derived").at("timing").at("pulse_period_samples"),
      6500);
}

TEST(FhssDashboardConfigurationPolicyTest,
     ProvenancePointersResolveAndCoverEveryGeneratedTarget) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy policy;
  const auto authoritative = policy.ExtractAuthoritative(config);
  const auto effective = policy.DeriveEffective(config, authoritative);
  const auto targets = policy.GeneratedPaths();
  const auto provenance = policy.Provenance();
  ASSERT_EQ(provenance.size(), targets.size());
  std::set<std::string> rule_ids;
  for (const auto &record : provenance) {
    rule_ids.insert(record.at("rule_id").get<std::string>());
    EXPECT_TRUE(record.at("units").is_string());
    EXPECT_EQ(record.at("classification").at("target"), "generated");
    EXPECT_EQ(record.at("classification").at("mutability"), "read-only");
    const auto source_classification =
        record.at("classification").at("source").get<std::string>();
    EXPECT_TRUE(source_classification == "authoritative" ||
                source_classification == "architecture");
    const auto target = record.at("target_pointer").get<std::string>();
    EXPECT_NO_THROW((void)effective.at(nlohmann::json::json_pointer(target)))
        << target;
    ASSERT_TRUE(record.at("source_pointers").is_array());
    for (const auto &source : record.at("source_pointers")) {
      const auto pointer = source.get<std::string>();
      EXPECT_EQ(pointer.find('*'), std::string::npos);
      EXPECT_EQ(pointer.find(".."), std::string::npos);
      EXPECT_NO_THROW(
          (void)authoritative.at(nlohmann::json::json_pointer(pointer)))
          << pointer;
    }
    if (source_classification == "architecture")
      EXPECT_TRUE(record.at("source_pointers").empty());
  }
  EXPECT_EQ(rule_ids.size(), provenance.size());
  const auto active_record = std::find_if(
      provenance.begin(), provenance.end(), [](const auto &record) {
        return record.at("target_pointer") ==
               "/dashboard_derived/active_frequency_indices";
      });
  ASSERT_NE(active_record, provenance.end());
  EXPECT_EQ(active_record->at("source_pointers").size(), 16u);
  for (const auto &source : active_record->at("source_pointers"))
    EXPECT_NE(source.get<std::string>().find("/frequency_index"),
              std::string::npos);
}

TEST(FhssDashboardConfigurationPolicyTest,
     RequiresPreambleDerivesStableDeduplicatedActiveSetAndRejectsUnsupportedIqRules) {
  const auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy policy;
  auto authoritative = policy.ExtractAuthoritative(config);
  EXPECT_FALSE(authoritative.contains("active_frequency_indices"));
  EXPECT_FALSE(authoritative.contains("preamble_pulses"));

  auto missing = authoritative;
  missing.erase("messages");
  const auto missing_result = policy.Validate(missing);
  EXPECT_FALSE(missing_result.at("valid").get<bool>());
  EXPECT_NE(missing_result.dump().find("missing_preamble"), std::string::npos);

  auto short_preamble = authoritative;
  short_preamble["messages"][0]["pulses"].erase(
      short_preamble["messages"][0]["pulses"].begin() + 15,
      short_preamble["messages"][0]["pulses"].end());
  const auto short_result = policy.Validate(short_preamble);
  EXPECT_FALSE(short_result.at("valid").get<bool>());
  EXPECT_NE(short_result.dump().find("invalid_preamble_length"),
            std::string::npos);

  auto ordered = authoritative;
  constexpr std::array<int, 4> order{36, 24, 36, 28};
  for (std::size_t index = 0; index < 16; ++index)
    ordered["messages"][0]["pulses"][index]["frequency_index"] =
        order[index % order.size()];
  EXPECT_EQ(policy.DeriveEffective(config, ordered)
                .at("dashboard_derived")
                .at("active_frequency_indices"),
            nlohmann::json::array({36, 24, 28}));

  for (const auto &[field, value] :
       std::array<std::pair<const char *, nlohmann::json>, 5>{
           {{"sample_rate_hz", 1.0}, {"bit_rate_hz", 1.0},
            {"bits_per_pulse", 31}, {"pulse_gap_seconds", 0.0},
            {"enable_multipath", true}}}) {
    auto invalid = authoritative;
    invalid[field] = value;
    EXPECT_FALSE(policy.Validate(invalid).at("valid").get<bool>()) << field;
  }
}

TEST(FhssDashboardConfigurationPolicyTest,
     ReceiverExportIsExactCanonicalPackagedTemplate) {
  const auto generator =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  const auto receiver = LoadJsonFile(std::filesystem::path(
      DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH));
  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy policy(receiver);
  const auto authoritative = policy.ExtractAuthoritative(generator);
  EXPECT_EQ(policy.ReceiverMinimalGraph(
                policy.DeriveEffective(generator, authoritative)),
            receiver);
}

TEST(FhssDashboardConfigurationIntegrationTest,
     ExactServiceReceiverExportExecutesWithOnlySyntheticIqAvailable) {
  const auto temp = std::filesystem::temp_directory_path() /
                    "graphx_dashboard_phase2_receiver_execution";
  std::error_code error;
  std::filesystem::remove_all(temp, error);
  std::filesystem::create_directories(temp, error);
  ASSERT_FALSE(error);
  const auto schedule_path = temp / "schedule.json";
  const auto iq_path = temp / "capture.cf32";
  const auto truth_path = temp / "truth.json";
  const auto graph_path = temp / "receiver.json";

  constexpr std::uint32_t frequencies[]{24, 28, 32, 36};
  constexpr std::uint32_t words[]{0xaaaaaaaau, 0x77777777u, 0x12121212u,
                                  0x62626262u};
  nlohmann::json pulses = nlohmann::json::array();
  for (std::size_t index = 0; index < 16; ++index) {
    pulses.push_back({{"frequency_index", frequencies[index % 4]},
                      {"value", words[index % 4]}, {"role", "preamble"}});
  }
  pulses.push_back({{"frequency_index", 24},
                    {"value", 0x01020304u}, {"role", "body"}});
  const nlohmann::json schedule = {
      {"active_frequency_indices", {24, 28, 32, 36}},
      {"iq_center_frequency_hz", 1240000000.0},
      {"messages", nlohmann::json::array({{{"message_id", 41},
                                             {"transmit_start_sample", 0},
                                             {"pulses", pulses}}})},
      {"idle_duration_samples", 0}, {"allow_overlap", false},
      {"enable_noise", false}, {"enable_doppler", false},
      {"enable_multipath", false}};
  {
    std::ofstream output(schedule_path);
    output << schedule.dump(2);
  }
  const std::string command =
      std::string{"'"} + DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH +
      "' --message-json '" + schedule_path.string() + "' --iq-output '" +
      iq_path.string() + "' --truth-output '" + truth_path.string() + "'";
  ASSERT_EQ(std::system(command.c_str()), 0);

  const auto generator =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  const auto golden = LoadJsonFile(
      std::filesystem::path(DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      generator, std::make_shared<
                     dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  auto exported = service.GetReceiverGraphResponse().at("graph");
  ASSERT_EQ(exported, golden);
  exported.at("nodes").at(0).at("node_config")["file_path"] = iq_path.string();
  auto expected = golden;
  expected.at("nodes").at(0).at("node_config")["file_path"] = iq_path.string();
  ASSERT_EQ(exported, expected)
      << "execution may parameterize only the binary IQ file path";
  {
    std::ofstream output(graph_path);
    output << exported.dump(2);
  }
  ASSERT_TRUE(std::filesystem::remove(schedule_path));
  ASSERT_TRUE(std::filesystem::remove(truth_path));

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(graph_path.string())
                      .WithPluginDirectory(DSP_PLUGIN_OUTPUT_DIRECTORY)
                      .WithExecutorTimeout(std::chrono::seconds(20))
                      .Build();
  ASSERT_NE(executor, nullptr);
  const auto execution = executor->Execute();
  EXPECT_TRUE(execution.success) << execution.message;
  EXPECT_FALSE(std::filesystem::exists(schedule_path));
  EXPECT_FALSE(std::filesystem::exists(truth_path));
  std::filesystem::remove_all(temp, error);
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

  const auto response =
      HttpRequest(server_->BoundPort(), "PATCH", "/api/v1/fhss/config",
                  invalid_patch.dump());
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
       PatchAndExportInspectionDoNotExposeOperationLifecycle) {
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
  EXPECT_EQ(operation.status_code, 404) << operation.body;

  const auto cancel =
      HttpRequest(server_->BoundPort(), "POST",
                  "/api/v1/fhss/operations/" + operation_id + "/cancel");
  EXPECT_EQ(cancel.status_code, 404) << cancel.body;

  const auto deleted = HttpRequest(server_->BoundPort(), "DELETE",
                                   "/api/v1/fhss/operations/" + operation_id);
  EXPECT_EQ(deleted.status_code, 404) << deleted.body;

  const auto after_delete = HttpRequest(
      server_->BoundPort(), "GET", "/api/v1/fhss/operations/" + operation_id);
  EXPECT_EQ(after_delete.status_code, 404) << after_delete.body;
  const auto after_delete_json = nlohmann::json::parse(after_delete.body);
  EXPECT_EQ(after_delete_json.at("code").get<std::string>(), "not_found");
}

TEST(FhssDashboardConfigurationServiceTest,
     ExportReplayAndFailureInjectionBehaveDeterministically) {
  auto config =
      LoadJsonFile(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  graph::dashboard::GraphConfigurationService service(
      config, std::make_shared<
                  dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
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
