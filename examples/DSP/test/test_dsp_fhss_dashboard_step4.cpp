// SPDX-License-Identifier: MIT

#include "FHSSDashboardConfigurationPolicy.hpp"
#include "FHSSObservationService.hpp"

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot open " + path.string());
  return nlohmann::json::parse(input);
}

bool ContainsKeyRecursively(const nlohmann::json &value, std::string_view key) {
  if (value.is_object()) {
    if (value.contains(key))
      return true;
    for (const auto &[unused, child] : value.items()) {
      (void)unused;
      if (ContainsKeyRecursively(child, key))
        return true;
    }
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (ContainsKeyRecursively(child, key))
        return true;
  }
  return false;
}

std::shared_ptr<dsp::fhss::dashboard::FHSSObservationService> MakeService() {
  auto configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  return std::make_shared<dsp::fhss::dashboard::FHSSObservationService>(
      std::move(configuration),
      std::make_shared<graph::dashboard::GraphRuntimeSession>());
}

std::shared_ptr<dsp::fhss::dashboard::FHSSObservationService>
MakeService(nlohmann::json configuration,
            std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime) {
  auto service = std::make_shared<graph::dashboard::GraphConfigurationService>(
      std::move(configuration),
      std::make_shared<
          dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  return std::make_shared<dsp::fhss::dashboard::FHSSObservationService>(
      std::move(service), std::move(runtime));
}

class TestReceiverCaptureSource final
    : public graph::NamedSinkNode<TestReceiverCaptureSource,
                                  dsp::fhss::FHSSAssembledMessageToken>,
      public dsp::fhss::IFHSSReceiverObservationSource {
public:
  explicit TestReceiverCaptureSource(
      dsp::fhss::FHSSReceiverSampleCapture capture)
      : capture_(std::move(capture)) {}

  bool Consume(const dsp::fhss::FHSSAssembledMessageToken &,
               std::integral_constant<std::size_t, 0>) override {
    return true;
  }

  [[nodiscard]] std::shared_ptr<
      const dsp::fhss::FHSSReceiverNodeObservationSnapshot>
  SnapshotReceiverObservation() const override {
    auto snapshot =
        std::make_shared<dsp::fhss::FHSSReceiverNodeObservationSnapshot>();
    snapshot->source_schema = "graphx.test.receiver_capture.v1";
    snapshot->source_kind = "test_receiver_capture";
    snapshot->sample_captures.push_back(capture_);
    snapshot->decoded_pulses.push_back(
        {.logical_frequency_index = capture_.logical_frequency_index,
         .physical_channel_index = capture_.physical_channel_index});
    return snapshot;
  }

  [[nodiscard]] graph::JsonView GetDiagnostics() const override {
    thread_local nlohmann::json diagnostics{
        {"schema", "graphx.test.receiver_capture.diagnostics.v1"}};
    return graph::JsonView(diagnostics);
  }

private:
  dsp::fhss::FHSSReceiverSampleCapture capture_;
};

class IdentityRuntimeOwner final : public graph::dashboard::IGraphRuntimeOwner {
public:
  Result Rebuild(std::uint64_t, const BuildSnapshot &) override {
    return {200, "rebuilt", "identity test generation",
            std::make_shared<graph::GraphManager>()};
  }
  Result Start(std::uint64_t, std::uint64_t) override {
    return {.status_code = 202,
            .code = "started",
            .message = "identity test started",
            .graph_manager = nullptr,
            .cleanup_failed = false};
  }
  Result Stop(std::uint64_t) override {
    return {.status_code = 200,
            .code = "stopped",
            .message = "identity test stopped",
            .graph_manager = nullptr,
            .cleanup_failed = false};
  }
  Result Shutdown(std::uint64_t) override {
    return {.status_code = 200,
            .code = "shutdown",
            .message = "identity test shutdown",
            .graph_manager = nullptr,
            .cleanup_failed = false};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    completion_ = std::move(callback);
  }

private:
  CompletionCallback completion_;
};

TEST(FhssDashboardObservationTest,
     ExpectedTruthUsesNormativePulseAndGapTimingWithoutRuntime) {
  const auto truth = MakeService()->ExpectedTruth().document;
  ASSERT_GE(truth.at("pulses").size(), 2u);
  const auto first =
      truth.at("pulses").at(0).at("global_start_sample").get<std::uint64_t>();
  const auto second =
      truth.at("pulses").at(1).at("global_start_sample").get<std::uint64_t>();
  EXPECT_EQ(second - first, 3'200u + 3'300u);
  EXPECT_EQ(truth.at("timing_basis").at("pulse_duration_samples"), 3'200u);
  EXPECT_EQ(truth.at("timing_basis").at("inter_pulse_gap_samples"), 3'300u);
  EXPECT_EQ(truth.at("timing_basis").at("derivation"),
            "dsp::fhss::DeriveTimingModel");
  EXPECT_EQ(truth.at("bounds").at("returned_pulse_count"),
            truth.at("pulses").size());
}

TEST(FhssDashboardObservationTest,
     ExpectedTruthBoundsMessagesAndPulsesBeforeMovingDocuments) {
  auto configuration = LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH);
  auto &source =
      *std::ranges::find_if(configuration.at("nodes"), [](auto &node) {
        return node.value("id", std::string{}) == "source";
      });
  const auto original = source.at("node_config").at("messages").at(0);
  source["node_config"]["messages"] = nlohmann::json::array();
  for (std::size_t index = 0; index < 80; ++index) {
    auto message = original;
    message["message_id"] = index + 1;
    message["transmit_start_sample"] = index * 200'000;
    source["node_config"]["messages"].push_back(std::move(message));
  }
  const auto truth =
      MakeService(std::move(configuration),
                  std::make_shared<graph::dashboard::GraphRuntimeSession>())
          ->ExpectedTruth()
          .document;
  EXPECT_EQ(truth.at("messages").size(), 64u);
  EXPECT_EQ(truth.at("pulses").size(), 512u);
  EXPECT_TRUE(truth.at("bounds").at("messages_truncated"));
  EXPECT_TRUE(truth.at("bounds").at("pulses_truncated"));
  EXPECT_EQ(truth.at("bounds").at("returned_message_count"), 64u);
  EXPECT_EQ(truth.at("bounds").at("returned_pulse_count"), 512u);
  EXPECT_EQ(truth.at("expected_receiver_message").at("decoded_pulse_count"),
            512u);
  EXPECT_EQ(truth.at("expected_receiver_message").at("decoded_pulse_count"),
            truth.at("bounds").at("returned_pulse_count"));
}

TEST(FhssDashboardObservationTest,
     MissingReceiverSourceStaysUnavailableAndNeverFallsBackToTruth) {
  const auto observation = MakeService()->ReceiverObservation().document;
  EXPECT_EQ(observation.at("semantic_class"), "observed");
  EXPECT_EQ(observation.at("availability").at("state"), "unavailable");
  EXPECT_EQ(observation.at("availability").at("reason"),
            "generation_not_available");
  EXPECT_EQ(observation.at("count_availability").at("state"), "unavailable");
  EXPECT_EQ(observation.at("sample_rate").at("availability").at("state"),
            "unavailable");
  EXPECT_TRUE(observation.at("detected_count").is_null());
  EXPECT_TRUE(observation.at("rejected_count").is_null());
  EXPECT_EQ(observation.at("count_semantics").at("detected"), "unavailable");
  EXPECT_EQ(observation.at("count_semantics").at("rejected"), "unavailable");
  for (const auto field : {"messages", "expected", "truth_sha256",
                           "transmitted_word", "synthetic_impairments"})
    EXPECT_FALSE(ContainsKeyRecursively(observation, field)) << field;
}

TEST(
    FhssDashboardObservationTest,
    ActiveGenerationWithoutTypedReceiverSourceIsUnavailableWithoutTruthFallback) {
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime->SetActiveGraphManager(std::make_shared<graph::GraphManager>());
  const auto observation =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime)
          ->ReceiverObservation()
          .document;
  EXPECT_EQ(observation.at("availability").at("state"), "unavailable");
  EXPECT_EQ(observation.at("availability").at("reason"),
            "source_not_diagnosable");
  EXPECT_EQ(observation.at("count_availability").at("reason"),
            "source_not_diagnosable");
  EXPECT_TRUE(observation.at("detected_count").is_null());
  EXPECT_TRUE(observation.at("rejected_count").is_null());
  EXPECT_TRUE(observation.at("observed_pulses").empty());
  for (const auto field : {"messages", "expected", "truth_sha256",
                           "transmitted_word", "synthetic_impairments"})
    EXPECT_FALSE(ContainsKeyRecursively(observation, field)) << field;
}

TEST(FhssDashboardObservationTest,
     ProductionPluginWrappersExposeKnownTypedReceiverObservationSources) {
  auto receiver = LoadJson(DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH);
  const auto directory = std::filesystem::temp_directory_path() /
                         "graphx_phase4_wrapper_observation_test";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  std::filesystem::create_directories(directory);
  const auto iq_path = directory / "empty.cf32";
  {
    std::ofstream iq(iq_path, std::ios::binary);
    const std::array<float, 2> sample{0.0F, 0.0F};
    iq.write(reinterpret_cast<const char *>(sample.data()), sizeof(sample));
  }
  for (auto &node : receiver.at("nodes"))
    if (node.value("id", std::string{}) == "source")
      node.at("node_config")["file_path"] = iq_path.string();
  const auto graph_path = directory / "receiver.json";
  {
    std::ofstream graph(graph_path);
    graph << receiver.dump(2);
  }
  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(graph_path.string())
                      .WithPluginDirectory(DSP_PLUGIN_OUTPUT_DIRECTORY)
                      .Build();
  ASSERT_NE(executor, nullptr);
  const auto initialized = executor->Init();
  ASSERT_TRUE(initialized.success) << initialized.message;
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime->SetActiveGraphManager(executor->GetGraphManager());
  const auto observation =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime)
          ->ReceiverObservation()
          .document;
  EXPECT_EQ(observation.at("availability").at("state"), "available");
  std::set<std::string> source_kinds;
  for (const auto &source : observation.at("sources"))
    source_kinds.insert(source.at("source_kind").get<std::string>());
  EXPECT_TRUE(source_kinds.contains("production_channelizer"));
  EXPECT_TRUE(source_kinds.contains("acquisition_detector"));
  EXPECT_TRUE(source_kinds.contains("message_sink"));
  EXPECT_GE(observation.at("sources").size(), 66u);
  std::filesystem::remove_all(directory, error);
}

TEST(FhssDashboardObservationTest,
     ReceiverCompatibleExplicitIqMapAndCausalGuardDecodeCleanGoldenFixture) {
  auto scenario_graph = LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH);
  auto receiver_graph = LoadJson(DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH);
  auto &scenario_source =
      *std::ranges::find_if(scenario_graph.at("nodes"), [](auto &node) {
        return node.value("id", std::string{}) == "source";
      });
  const auto &receiver_channelizer =
      *std::ranges::find_if(receiver_graph.at("nodes"), [](auto &node) {
        return node.value("id", std::string{}) == "channelizer";
      });
  auto &schedule = scenario_source.at("node_config");
  schedule["iq_offsets"] =
      receiver_channelizer.at("node_config").at("iq_offsets");
  for (auto &message : schedule.at("messages"))
    message["transmit_start_sample"] =
        message.at("transmit_start_sample").get<std::uint64_t>() + 6'500u;
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(
      dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
          graph::JsonView(schedule)));
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  const auto directory = std::filesystem::temp_directory_path() /
                         "graphx_phase4_clean_golden_receiver_test";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  std::filesystem::create_directories(directory);
  const auto iq_path = directory / "clean.cf32";
  {
    std::ofstream iq(iq_path, std::ios::binary);
    for (const auto &sample : fixture->samples) {
      const std::array<float, 2> encoded{static_cast<float>(sample.real()),
                                         static_cast<float>(sample.imag())};
      iq.write(reinterpret_cast<const char *>(encoded.data()), sizeof(encoded));
    }
  }
  for (auto &node : receiver_graph.at("nodes"))
    if (node.value("id", std::string{}) == "source")
      node.at("node_config")["file_path"] = iq_path.string();
  const auto graph_path = directory / "receiver.json";
  {
    std::ofstream graph(graph_path);
    graph << receiver_graph.dump(2);
  }
  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(graph_path.string())
                      .WithPluginDirectory(DSP_PLUGIN_OUTPUT_DIRECTORY)
                      // The complete receiver graph is deliberately exercised
                      // under ASan/UBSan as well as an ordinary C++26 build;
                      // instrumentation can make the 72-pulse replay exceed
                      // the old 12-second debug-only guard.
                      .WithExecutorTimeout(std::chrono::seconds(120))
                      .Build();
  ASSERT_NE(executor, nullptr);
  const auto execution = executor->Execute();
  ASSERT_TRUE(execution.success) << execution.message;
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  runtime->SetActiveGraphManager(executor->GetGraphManager());
  const auto service = MakeService(std::move(scenario_graph), runtime);
  const auto observed = service->ReceiverObservation().document;
  const auto expected = service->ExpectedTruth().document;
  auto observed_for_comparison = observed;
  observed_for_comparison["config_revision"] = expected.at("config_revision");
  observed_for_comparison["config_etag"] = expected.at("config_etag");
  observed_for_comparison["terminal_result"] = {
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"code", "execution_completed"},
      {"message", "independent executor completed"},
      {"terminal_at", "2026-01-01T00:00:00Z"}};
  const auto comparison =
      dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(
          expected, observed_for_comparison)
          .document;
  ASSERT_EQ(expected.at("pulses").size(), 72u);
  EXPECT_EQ(observed.at("observed_pulses").size(), 72u);
  EXPECT_TRUE(observed.at("observed_pulses")
                  .at(0)
                  .contains("viterbi_second_best_path_metric"));
  EXPECT_GE(observed.at("observed_pulses")
                .at(0)
                .at("viterbi_second_best_path_metric")
                .get<double>(),
            observed.at("observed_pulses")
                .at(0)
                .at("viterbi_path_metric")
                .get<double>());
  EXPECT_EQ(observed.at("detected_count"), 72u);
  EXPECT_EQ(observed.at("rejected_count"), 0u);
  EXPECT_TRUE(observed.at("preamble").at("locked"));
  EXPECT_EQ(observed.at("sample_rate").at("availability").at("state"),
            "available");
  EXPECT_DOUBLE_EQ(observed.at("sample_rate").at("global_input_sample_rate_hz"),
                   500'000'000.0);
  EXPECT_DOUBLE_EQ(
      observed.at("sample_rate").at("receiver_capture_sample_rate_hz"),
      50'000'000.0);
  EXPECT_EQ(observed.at("sample_rate").at("input_samples_per_capture_sample"),
            10);
  EXPECT_DOUBLE_EQ(observed.at("sample_rate")
                           .at("receiver_capture_sample_rate_hz")
                           .get<double>() *
                       observed.at("sample_rate")
                           .at("input_samples_per_capture_sample")
                           .get<double>(),
                   observed.at("sample_rate")
                       .at("global_input_sample_rate_hz")
                       .get<double>());
  const auto pulse_provenance =
      std::ranges::find_if(observed.at("provenance"), [](const auto &record) {
        return record.at("packet_field") ==
               "observed_pulses[].global_start_sample";
      });
  ASSERT_NE(pulse_provenance, observed.at("provenance").end());
  EXPECT_EQ(pulse_provenance->at("sample_interval"),
            "per pulse: [global_start_sample, global_start_sample + "
            "duration_samples) input samples");
  EXPECT_TRUE(pulse_provenance->at("capture_time").is_null());
  EXPECT_EQ(pulse_provenance->at("capture_time_availability").at("reason"),
            "not_carried_by_receiver_product");
  EXPECT_EQ(pulse_provenance->at("sample_interval_availability").at("state"),
            "available");
  EXPECT_EQ(observed.at("assembler").at("availability").at("state"),
            "available");
  EXPECT_EQ(comparison.at("matches").size(), 72u) << comparison.dump(2);
  EXPECT_TRUE(comparison.at("missed_expected_indices").empty());
  EXPECT_TRUE(comparison.at("unexpected_observed_indices").empty());
  EXPECT_TRUE(comparison.at("ambiguous").empty());
  EXPECT_TRUE(
      std::ranges::all_of(comparison.at("matches"), [](const auto &match) {
        return match.at("decoded_value_agrees").template get<bool>();
      }));
  std::filesystem::remove_all(directory, error);
}

TEST(FhssDashboardObservationTest,
     UnusedMessageSinkDoesNotOverwriteDetectorCountsOrClaimDecoderState) {
  dsp::fhss::FHSSMessageSinkNode sink;
  const auto snapshot = sink.SnapshotReceiverObservation();
  ASSERT_TRUE(snapshot);
  EXPECT_FALSE(snapshot->detected_pulse_count.has_value());
  EXPECT_FALSE(snapshot->rejected_count.has_value());
  EXPECT_FALSE(snapshot->preamble_lock.has_value());
  EXPECT_FALSE(snapshot->assembler_status.has_value());
  EXPECT_TRUE(snapshot->decoded_pulses.empty());
}

TEST(FhssDashboardObservationTest,
     TerminalOnlyNegativeSinkPreservesAssemblerRejectionWithoutAcceptance) {
  dsp::fhss::FHSSMessageSinkNode sink;
  dsp::fhss::FHSSAssembledMessageToken token{};
  token.edge_control = graph::EdgeEndOfStream{};
  token.sidecar.status = dsp::fhss::FHSSGraphXDecodeStatus::InvalidEvidence;
  token.sidecar.status_message =
      "FHSS PR7 message does not contain the 16-pulse preamble";
  ASSERT_TRUE(sink.Consume(token, std::integral_constant<std::size_t, 0>{}));
  const auto snapshot = sink.SnapshotReceiverObservation();
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->assembler_status, token.sidecar.status_message);
  EXPECT_EQ(snapshot->receiver_message_status, token.sidecar.status_message);
  ASSERT_TRUE(snapshot->receiver_message_accepted.has_value());
  EXPECT_FALSE(*snapshot->receiver_message_accepted);
  EXPECT_TRUE(snapshot->decoded_pulses.empty());
}

TEST(FhssDashboardObservationTest,
     TerminalSinkCountsSupersedeUpstreamKindsAndAllFieldsHaveProvenance) {
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto manager = std::make_shared<graph::GraphManager>();
  auto sink = manager->AddNode<dsp::fhss::FHSSMessageSinkNode>();
  dsp::fhss::FHSSAssembledMessageToken token{};
  token.sidecar.diagnostics.pulse_count = 17;
  token.sidecar.diagnostics.rejected_count = 2;
  token.sidecar.diagnostics.preamble_lock = true;
  token.sidecar.diagnostics.unsupported_overlap_rejected = true;
  token.sidecar.diagnostics.unsupported_impairments_rejected = false;
  token.sidecar.active_frequency_indices = {24, 28, 32, 36};
  token.sidecar.preamble_lock = true;
  token.sidecar.ordered_pulses.push_back({});
  token.sidecar.status_message = "assembled";
  ASSERT_TRUE(sink->Consume(token, std::integral_constant<std::size_t, 0>{}));
  runtime->SetActiveGraphManager(manager);
  const auto observation =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime)
          ->ReceiverObservation()
          .document;
  EXPECT_EQ(observation.at("detected_count"), 17);
  EXPECT_EQ(observation.at("rejected_count"), 2);
  EXPECT_EQ(observation.at("count_semantics").at("detected"),
            "terminal_message_sink_count");
  EXPECT_TRUE(observation.at("receiver_message_result").at("accepted"));
  EXPECT_EQ(observation.at("receiver_message_result").at("decoded_pulse_count"),
            1u);
  EXPECT_EQ(observation.at("rejection_reason_codes"),
            nlohmann::json::array({"unsupported_overlap"}));
  std::set<std::string> fields;
  for (const auto &record : observation.at("provenance"))
    fields.insert(record.at("packet_field").get<std::string>());
  for (const auto required :
       {"detected_count", "rejected_count", "rejection_reason_codes[]",
        "preamble.locked", "receiver_derived_active_frequencies[]",
        "assembler.status", "receiver_message_result.status",
        "receiver_message_result.accepted",
        "receiver_message_result.decoded_pulse_count", "sources[]"})
    EXPECT_TRUE(fields.contains(required)) << required;
}

TEST(FhssDashboardObservationTest,
     EvaluatorMatchesOneToOneAndReportsMissedAndUnexpected) {
  nlohmann::json expected{{"semantic_class", "expected"},
                          {"truth_sha256", std::string(64, 'a')},
                          {"config_revision", 3},
                          {"config_etag", "etag-3"},
                          {"expected_receiver_message",
                           {{"accepted", true}, {"decoded_pulse_count", 2}}},
                          {"pulses",
                           {{{"global_start_sample", 100},
                             {"logical_frequency_index", 1},
                             {"transmitted_word", 7}},
                            {{"global_start_sample", 200},
                             {"logical_frequency_index", 2},
                             {"transmitted_word", 8}}}}};
  nlohmann::json observed{
      {"semantic_class", "observed"},
      {"observation_sha256", std::string(64, 'b')},
      {"generation", 4},
      {"run_epoch", 2},
      {"config_revision", 3},
      {"config_etag", "etag-3"},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"terminal_result", {{"code", "execution_completed"}}},
      {"receiver_message_result",
       {{"availability", {{"state", "available"}}},
        {"accepted", true},
        {"decoded_pulse_count", 2}}},
      {"observed_pulses",
       {{{"global_start_sample", 110},
         {"logical_frequency_index", 1},
         {"decoded_value", 7}},
        {{"global_start_sample", 300},
         {"logical_frequency_index", 3},
         {"decoded_value", 9}}}}};
  const auto result =
      dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(expected,
                                                                     observed)
          .document;
  ASSERT_EQ(result.at("matches").size(), 1u);
  EXPECT_EQ(result.at("matches").at(0).at("timing_delta_samples"), 10);
  EXPECT_TRUE(result.at("matches").at(0).at("decoded_value_agrees"));
  EXPECT_EQ(result.at("missed_expected_indices"), nlohmann::json::array({1}));
  EXPECT_EQ(result.at("unexpected_observed_indices"),
            nlohmann::json::array({1}));
}

TEST(FhssDashboardObservationTest,
     EvaluatorReportsCompleteThreeWayEqualBestSetAsAmbiguous) {
  nlohmann::json expected{{"semantic_class", "expected"},
                          {"truth_sha256", std::string(64, 'a')},
                          {"config_revision", 1},
                          {"config_etag", "etag-1"},
                          {"expected_receiver_message",
                           {{"accepted", true}, {"decoded_pulse_count", 3}}},
                          {"pulses",
                           {{{"global_start_sample", 100},
                             {"logical_frequency_index", 1},
                             {"transmitted_word", 7}},
                            {{"global_start_sample", 100},
                             {"logical_frequency_index", 1},
                             {"transmitted_word", 7}},
                            {{"global_start_sample", 100},
                             {"logical_frequency_index", 1},
                             {"transmitted_word", 7}}}}};
  nlohmann::json observed{
      {"semantic_class", "observed"},
      {"observation_sha256", std::string(64, 'b')},
      {"generation", 1},
      {"run_epoch", 1},
      {"config_revision", 1},
      {"config_etag", "etag-1"},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"terminal_result", {{"code", "execution_completed"}}},
      {"receiver_message_result",
       {{"availability", {{"state", "available"}}},
        {"accepted", true},
        {"decoded_pulse_count", 1}}},
      {"observed_pulses",
       {{{"global_start_sample", 100},
         {"logical_frequency_index", 1},
         {"decoded_value", 7}}}}};
  const auto result =
      dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(expected,
                                                                     observed)
          .document;
  EXPECT_TRUE(result.at("matches").empty());
  ASSERT_EQ(result.at("ambiguous").size(), 1u);
  EXPECT_EQ(result.at("ambiguous").at(0).at("candidate_expected_indices"),
            nlohmann::json::array({0, 1, 2}));
  EXPECT_EQ(result.at("missed_expected_indices").size(), 3u);
  EXPECT_FALSE(result.at("terminal_result_agrees"));
}

TEST(
    FhssDashboardObservationTest,
    EvaluatorReturnsIndeterminateForMalformedUnavailableTerminalAndStaleInputs) {
  nlohmann::json expected{{"semantic_class", "expected"},
                          {"truth_sha256", std::string(64, 'a')},
                          {"config_revision", 2},
                          {"config_etag", "etag-2"},
                          {"expected_receiver_message",
                           {{"accepted", false}, {"decoded_pulse_count", 0}}},
                          {"pulses", nlohmann::json::array()}};
  nlohmann::json observed{
      {"semantic_class", "observed"},
      {"observation_sha256", std::string(64, 'b')},
      {"generation", 1},
      {"run_epoch", 1},
      {"config_revision", 2},
      {"config_etag", "etag-2"},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"terminal_result", {{"code", "execution_completed"}}},
      {"receiver_message_result",
       {{"availability", {{"state", "available"}}},
        {"accepted", false},
        {"decoded_pulse_count", 0}}},
      {"observed_pulses", nlohmann::json::array()}};
  const auto compare = [&](const nlohmann::json &lhs,
                           const nlohmann::json &rhs) {
    return dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(lhs,
                                                                          rhs)
        .document;
  };
  EXPECT_EQ(compare(expected, observed).at("evaluation_state"), "evaluated");
  auto stale = observed;
  stale["config_revision"] = 1;
  EXPECT_EQ(compare(expected, stale).at("availability").at("reason"),
            "configuration_identity_mismatch");
  auto failed = observed;
  failed["terminal_result"]["code"] = "execution_failed";
  EXPECT_EQ(compare(expected, failed).at("availability").at("reason"),
            "receiver_execution_not_completed");
  auto unavailable = observed;
  unavailable["availability"] = {{"state", "unavailable"},
                                 {"reason", "no_receiver_samples"}};
  EXPECT_EQ(compare(expected, unavailable).at("availability").at("reason"),
            "receiver_observation_unavailable");
  auto malformed = observed;
  malformed["observed_pulses"] = "not-an-array";
  EXPECT_EQ(compare(expected, malformed).at("availability").at("reason"),
            "observed_document_malformed");
  EXPECT_TRUE(compare(expected, malformed).at("matches").empty());
}

TEST(FhssDashboardObservationTest,
     EvaluatorIsTotalForNestedNegativeRangeAndTypeErrors) {
  const nlohmann::json valid_expected{
      {"semantic_class", "expected"},
      {"truth_sha256", std::string(64, 'a')},
      {"config_revision", 2},
      {"config_etag", "etag-2"},
      {"expected_receiver_message",
       {{"accepted", true}, {"decoded_pulse_count", 1}}},
      {"pulses",
       {{{"global_start_sample", 100},
         {"logical_frequency_index", 1},
         {"transmitted_word", 7}}}}};
  const nlohmann::json valid_observed{
      {"semantic_class", "observed"},
      {"observation_sha256", std::string(64, 'b')},
      {"generation", 1},
      {"run_epoch", 1},
      {"config_revision", 2},
      {"config_etag", "etag-2"},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"terminal_result", {{"code", "execution_completed"}}},
      {"receiver_message_result",
       {{"availability", {{"state", "available"}}},
        {"accepted", true},
        {"decoded_pulse_count", 1}}},
      {"observed_pulses",
       {{{"global_start_sample", 100},
         {"logical_frequency_index", 1},
         {"decoded_value", 7}}}}};
  struct InvalidCase {
    bool mutate_expected;
    std::string pointer;
    nlohmann::json value;
    std::string reason;
  };
  const std::vector<InvalidCase> cases{
      {true, "/pulses/0/global_start_sample", -1,
       "expected_document_malformed"},
      {true, "/pulses/0/global_start_sample",
       std::numeric_limits<double>::quiet_NaN(), "expected_document_malformed"},
      {true, "/pulses/0/logical_frequency_index", -1,
       "expected_document_malformed"},
      {true, "/pulses/0/logical_frequency_index", 64,
       "expected_document_malformed"},
      {true, "/pulses/0/transmitted_word", -1, "expected_document_malformed"},
      {true, "/pulses/0/transmitted_word",
       std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1,
       "expected_document_malformed"},
      {true, "/expected_receiver_message", nullptr,
       "expected_document_malformed"},
      {true, "/expected_receiver_message/accepted", nullptr,
       "expected_document_malformed"},
      {true, "/expected_receiver_message/accepted", nlohmann::json::object(),
       "expected_document_malformed"},
      {true, "/expected_receiver_message/decoded_pulse_count", -1,
       "expected_document_malformed"},
      {true, "/expected_receiver_message/decoded_pulse_count", 513,
       "expected_document_malformed"},
      {true, "/expected_receiver_message/decoded_pulse_count", nullptr,
       "expected_document_malformed"},
      {false, "/observed_pulses/0/global_start_sample", -1,
       "observed_document_malformed"},
      {false, "/observed_pulses/0/logical_frequency_index", -1,
       "observed_document_malformed"},
      {false, "/observed_pulses/0/logical_frequency_index", 64,
       "observed_document_malformed"},
      {false, "/observed_pulses/0/decoded_value", -1,
       "observed_document_malformed"},
      {false, "/receiver_message_result", nullptr,
       "observed_document_malformed"},
      {false, "/receiver_message_result/availability", nullptr,
       "observed_document_malformed"},
      {false, "/receiver_message_result/availability/state", nullptr,
       "observed_document_malformed"},
      {false, "/receiver_message_result/accepted", nlohmann::json::object(),
       "observed_document_malformed"},
      {false, "/receiver_message_result/decoded_pulse_count", -1,
       "observed_document_malformed"},
      {false, "/receiver_message_result/decoded_pulse_count", 513,
       "observed_document_malformed"},
      {false, "/receiver_message_result/decoded_pulse_count", nullptr,
       "observed_document_malformed"},
      {false, "/terminal_result/code", nlohmann::json::object(),
       "observed_document_malformed"},
  };
  for (const auto &[mutate_expected, pointer, value, reason] : cases) {
    auto expected = valid_expected;
    auto observed = valid_observed;
    (mutate_expected ? expected
                     : observed)[nlohmann::json::json_pointer(pointer)] = value;
    nlohmann::json result;
    EXPECT_NO_THROW(
        result = dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(
                     expected, observed)
                     .document)
        << pointer;
    ASSERT_TRUE(result.is_object()) << pointer;
    EXPECT_EQ(result.at("evaluation_state"), "indeterminate") << pointer;
    EXPECT_EQ(result.at("availability").at("reason"), reason) << pointer;
    EXPECT_TRUE(result.at("matches").empty()) << pointer;
  }
}

TEST(FhssDashboardObservationTest,
     EvaluatorMatchesSafelyAcrossFullUint64SampleDomain) {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  const nlohmann::json expected{
      {"semantic_class", "expected"},
      {"truth_sha256", std::string(64, 'a')},
      {"config_revision", 2},
      {"config_etag", "etag-2"},
      {"expected_receiver_message",
       {{"accepted", true}, {"decoded_pulse_count", 1}}},
      {"pulses",
       {{{"global_start_sample", maximum},
         {"logical_frequency_index", 1},
         {"transmitted_word", 7}}}}};
  const nlohmann::json observed{
      {"semantic_class", "observed"},
      {"observation_sha256", std::string(64, 'b')},
      {"generation", 1},
      {"run_epoch", 1},
      {"config_revision", 2},
      {"config_etag", "etag-2"},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"terminal_result", {{"code", "execution_completed"}}},
      {"receiver_message_result",
       {{"availability", {{"state", "available"}}},
        {"accepted", true},
        {"decoded_pulse_count", 1}}},
      {"observed_pulses",
       {{{"global_start_sample", maximum - 1},
         {"logical_frequency_index", 1},
         {"decoded_value", 7}}}}};

  const auto comparison =
      dsp::fhss::dashboard::FHSSObservationService::CompareDocuments(expected,
                                                                     observed)
          .document;
  ASSERT_EQ(comparison.at("evaluation_state"), "evaluated");
  ASSERT_EQ(comparison.at("matches").size(), 1u);
  EXPECT_EQ(comparison.at("matches").at(0).at("timing_delta_samples"), -1);
  EXPECT_TRUE(comparison.at("terminal_result_agrees"));
}

TEST(FhssDashboardObservationTest,
     ComparisonRejectsPatchedCurrentTruthUntilMatchingGenerationIsRebuilt) {
  auto configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>(
      std::make_shared<IdentityRuntimeOwner>());
  runtime->MarkReady();
  const auto rebuild =
      runtime->Rebuild({.receiver_graph = nlohmann::json::object(),
                        .config_revision = configuration->ConfigRevision(),
                        .config_etag = configuration->ETag()});
  ASSERT_EQ(rebuild.status_code, 200);
  auto service = std::make_shared<dsp::fhss::dashboard::FHSSObservationService>(
      configuration, runtime);
  const auto patch = configuration->ApplyJsonPatch(
      nlohmann::json::array({{{"op", "replace"},
                              {"path", "/iq_center_frequency_hz"},
                              {"value", 1'240'000'001.0}}}),
      configuration->ETag(), false);
  ASSERT_EQ(patch.at("status"), "applied");
  const auto comparison = service->Comparison().document;
  EXPECT_EQ(comparison.at("evaluation_state"), "indeterminate");
  EXPECT_EQ(comparison.at("availability").at("reason"),
            "configuration_identity_mismatch");
  EXPECT_TRUE(comparison.at("matches").empty());
}

TEST(FhssDashboardObservationTest,
     PublicReceiverConfigCanDisableObservationExportForMatchingGeneration) {
  auto configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  const auto patch = configuration->ApplyJsonPatch(
      nlohmann::json::array(
          {{{"op", "add"},
            {"path", "/receiver_input"},
            {"value", {{"dashboard_observation_enabled", false}}}}}),
      configuration->ETag(), false);
  ASSERT_EQ(patch.at("status"), "applied");
  const auto effective = configuration->GetConfigResponse().at("effective");
  const auto source =
      std::ranges::find_if(effective.at("nodes"), [](const auto &node) {
        return node.value("id", std::string{}) == "source";
      });
  ASSERT_NE(source, effective.at("nodes").end());
  EXPECT_FALSE(
      source->at("node_config").contains("dashboard_observation_enabled"));
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>(
      std::make_shared<IdentityRuntimeOwner>());
  runtime->MarkReady();
  ASSERT_EQ(runtime
                ->Rebuild({.receiver_graph = effective,
                           .config_revision = configuration->ConfigRevision(),
                           .config_etag = configuration->ETag()})
                .status_code,
            200);
  const auto observation =
      std::make_shared<dsp::fhss::dashboard::FHSSObservationService>(
          configuration, runtime)
          ->ReceiverObservation()
          .document;
  EXPECT_EQ(observation.at("availability").at("reason"),
            "observation_export_disabled");
  EXPECT_TRUE(observation.at("observed_pulses").empty());
  EXPECT_TRUE(observation.at("detected_count").is_null());
  EXPECT_TRUE(observation.at("sources").empty());
}

TEST(FhssDashboardObservationTest,
     SpectrumMatchesIndependentComplexToneGoldenBinAndScale) {
  constexpr std::size_t fft_size = 64;
  constexpr int tone_bin = 5;
  constexpr double pi = 3.1415926535897932384626433832795;
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.sample_rate_hz = 64'000.0;
  for (std::size_t sample = 0; sample < fft_size; ++sample) {
    const auto phase = 2.0 * pi * static_cast<double>(tone_bin * sample) /
                       static_cast<double>(fft_size);
    capture.samples.emplace_back(std::cos(phase), std::sin(phase));
  }
  const auto spectrum =
      dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
          capture, fft_size);
  ASSERT_EQ(spectrum.at("availability").at("state"), "available");
  ASSERT_EQ(spectrum.at("bins").size(), fft_size);
  const auto peak =
      std::ranges::max_element(spectrum.at("bins"), {}, [](const auto &bin) {
        return bin.at("magnitude_linear_re_1_complex_unit")
            .template get<double>();
      });
  ASSERT_NE(peak, spectrum.at("bins").end());
  EXPECT_EQ(peak->at("bin"), tone_bin);
  EXPECT_NEAR(peak->at("baseband_frequency_hz").get<double>(), 5'000.0, 1.0e-9);
  EXPECT_NEAR(peak->at("magnitude_linear_re_1_complex_unit").get<double>(), 1.0,
              1.0e-12);
  EXPECT_EQ(spectrum.at("bins").front().at("bin"), -32);
  EXPECT_EQ(spectrum.at("bins").back().at("bin"), 31);
  EXPECT_FALSE(spectrum.at("scaling").at("calibrated_power"));
}

TEST(FhssDashboardObservationTest,
     SpectrumRejectsShortNonFiniteAndUnsupportedSizesDeterministically) {
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.sample_rate_hz = 64'000.0;
  capture.samples.assign(64, {1.0, 0.0});
  for (const auto size : {0u, 15u, 17u, 512u}) {
    const auto spectrum =
        dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
            capture, size);
    EXPECT_EQ(spectrum.at("availability").at("state"), "unavailable");
    EXPECT_TRUE(spectrum.at("bins").empty());
  }
  capture.samples.resize(15);
  EXPECT_EQ(dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
                capture, 16)
                .at("availability")
                .at("reason"),
            "invalid_capture");
  capture.samples.assign(64, {1.0, 0.0});
  capture.input_sample_interval = 0;
  EXPECT_EQ(dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
                capture, 64)
                .at("availability")
                .at("reason"),
            "invalid_capture");
  capture.input_sample_interval = 1;
  capture.samples.at(3) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
  EXPECT_EQ(dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
                capture, 64)
                .at("availability")
                .at("reason"),
            "invalid_capture");
}

TEST(FhssDashboardObservationTest,
     SpectrumRoutePreservesInvalidCaptureAvailabilityAndEmptyBins) {
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.logical_frequency_index = 7;
  capture.physical_channel_index = 7;
  capture.sample_rate_hz = 64'000.0;
  capture.input_sample_interval = 0;
  capture.samples.assign(64, {1.0, 0.0});
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto manager = std::make_shared<graph::GraphManager>();
  manager->AddNode<TestReceiverCaptureSource>(std::move(capture));
  runtime->SetActiveGraphManager(manager);
  const auto spectrum =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime)
          ->Spectrum(7, 64);
  EXPECT_EQ(spectrum.at("availability").at("state"), "unavailable");
  EXPECT_EQ(spectrum.at("availability").at("reason"), "invalid_capture");
  EXPECT_TRUE(spectrum.at("bins").empty());
  EXPECT_EQ(spectrum.at("semantic_class"), "unavailable");
}

TEST(FhssDashboardObservationTest,
     OmittedSpectrumChannelUsesObservedPhysicalChannelOrIsUnavailable) {
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.logical_frequency_index = 24;
  capture.physical_channel_index = 7;
  capture.sample_rate_hz = 64'000.0;
  capture.input_sample_interval = 1;
  capture.samples.assign(64, {1.0, 0.0});
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto manager = std::make_shared<graph::GraphManager>();
  manager->AddNode<TestReceiverCaptureSource>(std::move(capture));
  runtime->SetActiveGraphManager(manager);
  const auto service =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime);
  const auto spectrum = service->Spectrum(std::nullopt, 64);
  EXPECT_EQ(spectrum.at("availability").at("state"), "available");
  EXPECT_EQ(spectrum.at("channel_index"), 7);
  EXPECT_EQ(spectrum.at("logical_frequency_index"), 24);

  auto empty_runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  empty_runtime->SetActiveGraphManager(std::make_shared<graph::GraphManager>());
  const auto unavailable =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), empty_runtime)
          ->Spectrum(std::nullopt, 64);
  EXPECT_EQ(unavailable.at("availability").at("reason"),
            "no_candidate_detected");
  EXPECT_TRUE(unavailable.at("channel_index").is_null());
  EXPECT_TRUE(unavailable.at("bins").empty());
}

TEST(FhssDashboardObservationTest,
     SpectrumOffBinResponseIsFiniteBoundedAndRepeatable) {
  constexpr std::size_t fft_size = 64;
  constexpr double pi = 3.1415926535897932384626433832795;
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.sample_rate_hz = 64'000.0;
  for (std::size_t sample = 0; sample < fft_size; ++sample) {
    const auto phase = 2.0 * pi * 5.25 * static_cast<double>(sample) /
                       static_cast<double>(fft_size);
    capture.samples.emplace_back(std::cos(phase), std::sin(phase));
  }
  const auto first =
      dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
          capture, fft_size);
  const auto second =
      dsp::fhss::dashboard::FHSSObservationService::SpectrumFromCapture(
          capture, fft_size);
  EXPECT_EQ(first, second);
  ASSERT_EQ(first.at("bins").size(), fft_size);
  for (const auto &bin : first.at("bins")) {
    const auto linear =
        bin.at("magnitude_linear_re_1_complex_unit").get<double>();
    const auto db = bin.at("magnitude_db_re_1_complex_unit").get<double>();
    EXPECT_TRUE(std::isfinite(linear));
    EXPECT_TRUE(std::isfinite(db));
    EXPECT_GE(linear, 0.0);
    EXPECT_LE(linear, 1.0 + 1.0e-12);
  }
}

TEST(FhssDashboardObservationTest,
     ObservationHistoryIsCurrentRunOnlyBoundedAndRepeatable) {
  const auto service = MakeService();
  const auto first = service->History();
  const auto second = service->History();
  EXPECT_EQ(first, second);
  EXPECT_EQ(first.at("retention").at("max_entries"), 1);
  EXPECT_EQ(first.at("retention").at("max_bytes"), 1'048'576);
  ASSERT_EQ(first.at("entries").size(), 1u);
}

TEST(FhssDashboardObservationTest,
     TypedMessageSinkSnapshotsAreImmutableUnderConcurrentAccess) {
  dsp::fhss::FHSSMessageSinkNode sink;
  dsp::fhss::FHSSAssembledMessageToken token{};
  std::atomic<bool> done = false;
  std::thread writer([&] {
    for (int iteration = 0; iteration < 1'000; ++iteration)
      ASSERT_TRUE(
          sink.Consume(token, std::integral_constant<std::size_t, 0>{}));
    done.store(true, std::memory_order_release);
  });
  while (!done.load(std::memory_order_acquire)) {
    const auto snapshot = sink.SnapshotReceiverObservation();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->source_kind, "message_sink");
    (void)sink.GetDiagnostics();
  }
  writer.join();
}

TEST(FhssDashboardObservationTest,
     ConcurrentReadOnlyObservationQueriesAreBoundedAndStable) {
  dsp::fhss::FHSSReceiverSampleCapture capture;
  capture.logical_frequency_index = 24;
  capture.physical_channel_index = 24;
  capture.sample_rate_hz = 64'000.0;
  capture.samples.assign(256, {1.0, 0.0});
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>();
  auto manager = std::make_shared<graph::GraphManager>();
  manager->AddNode<TestReceiverCaptureSource>(std::move(capture));
  runtime->SetActiveGraphManager(manager);
  const auto service =
      MakeService(LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH), runtime);
  const auto baseline_observation = service->ReceiverObservation().document;
  std::atomic<bool> failed = false;
  std::array<std::jthread, 4> readers;
  for (auto &reader : readers) {
    reader = std::jthread([&] {
      for (int iteration = 0; iteration < 50; ++iteration) {
        const auto observation = service->ReceiverObservation().document;
        const auto provenance = service->Provenance();
        const auto history = service->History();
        const auto spectrum = service->Spectrum(std::nullopt, 128);
        if (observation != baseline_observation ||
            provenance.dump().size() > (1u << 20) ||
            history.at("entries").size() > 1u ||
            spectrum.at("bins").size() > 256u)
          failed.store(true, std::memory_order_release);
      }
    });
  }
  for (auto &reader : readers)
    reader.join();
  EXPECT_FALSE(failed.load(std::memory_order_acquire));
}

} // namespace
