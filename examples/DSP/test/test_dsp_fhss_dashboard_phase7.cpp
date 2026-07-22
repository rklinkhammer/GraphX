// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include "FHSSDashboardConfigurationPolicy.hpp"
#include "FHSSInvestigationBundleService.hpp"
#include "FHSSIqArtifactGenerator.hpp"
#include "FHSSGraphRuntimeOwner.hpp"
#include "FHSSObservationService.hpp"
#include "FHSSHash.hpp"
#include "FHSSJobController.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <span>

namespace {

nlohmann::json Phase7LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  nlohmann::json document;
  input >> document;
  return document;
}

std::filesystem::path Phase7Temp() {
  return std::filesystem::temp_directory_path() /
         ("graphx-dashboard-phase7-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
}

class Phase7Gate {
public:
  void Enter() {
    std::unique_lock lock(mutex_);
    entered_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return released_; });
  }
  void Wait() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return entered_; });
  }
  bool WaitFor(std::chrono::milliseconds bound) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, bound, [this] { return entered_; });
  }
  void Release() {
    std::lock_guard lock(mutex_);
    released_ = true;
    cv_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool entered_ = false;
  bool released_ = false;
};

struct Phase7Fixture {
  std::filesystem::path root = Phase7Temp();
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          Phase7LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>();
  std::shared_ptr<dsp::fhss::dashboard::FHSSJobController> jobs;
  Phase7Fixture() {
    runtime->MarkReady();
    jobs = std::make_shared<dsp::fhss::dashboard::FHSSJobController>(
        configuration, runtime, root);
  }
  ~Phase7Fixture() {
    jobs->Shutdown();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

struct Phase7RealOwnerFixture {
  std::filesystem::path root = Phase7Temp();
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          Phase7LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  std::shared_ptr<dsp::fhss::dashboard::FHSSGraphRuntimeOwner> owner =
      std::make_shared<dsp::fhss::dashboard::FHSSGraphRuntimeOwner>(
          std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY), root / "runtime");
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  std::shared_ptr<dsp::fhss::dashboard::FHSSJobController> jobs;

  Phase7RealOwnerFixture() {
    runtime->MarkReady();
    jobs = std::make_shared<dsp::fhss::dashboard::FHSSJobController>(
        configuration, runtime, root);
  }
  ~Phase7RealOwnerFixture() {
    jobs->Shutdown();
    (void)owner->Shutdown(0);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

void Phase7Write(const std::filesystem::path &path,
                 std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void Phase7WriteJson(const std::filesystem::path &path,
                     const nlohmann::json &document) {
  std::ofstream(path) << document.dump() << '\n';
}

struct SeededPhase7Source {
  std::string job_id = "j-000000000000000000000007";
  dsp::fhss::dashboard::FHSSJobController::InvestigationSource source;
};

constexpr std::uint64_t kPhase7TestTimeoutMs = 120'000;

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
constexpr bool kThreadSanitizerBuild = true;
#else
constexpr bool kThreadSanitizerBuild = false;
#endif
#elif defined(__SANITIZE_THREAD__)
constexpr bool kThreadSanitizerBuild = true;
#else
constexpr bool kThreadSanitizerBuild = false;
#endif

// TSan instruments every queue/edge synchronization in this real one-message
// graph. Keep the ordinary regression contract at 15 seconds while giving only
// that instrumented test build a finite overhead envelope. These are test-side
// request/wait bounds; the production operation default remains 30 seconds.
constexpr auto kRealOwnerTestEnvelope =
    kThreadSanitizerBuild ? std::chrono::seconds(45)
                          : std::chrono::seconds(15);
static_assert(
    dsp::fhss::dashboard::FHSSInvestigationBundleService::kDefaultTimeout ==
    std::chrono::seconds(30));

template <typename Fixture>
SeededPhase7Source SeedSource(Fixture &fixture,
                              std::string format = "cf32_le") {
  auto graph = Phase7LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH);
  auto generator = std::ranges::find_if(graph.at("nodes"), [](const auto &node) {
    return node.value("id", "") == "source";
  });
  auto input = generator->at("node_config");
  input["messages"] = nlohmann::json::array({input.at("messages").at(0)});
  auto artifacts = graphx::examples::fhss::GenerateIqArtifacts(
      input, format, 4'194'304, 64u * 1024u * 1024u);
  SeededPhase7Source seeded;
  const auto directory = fixture.root / "fhss-jobs" / seeded.job_id;
  std::filesystem::create_directories(directory);
  Phase7Write(directory / (format == "cf32_le" ? "iq.cf32" : "iq.cf64"),
              artifacts.iq_bytes);
  Phase7WriteJson(directory / "truth.withheld.json", artifacts.truth);
  Phase7WriteJson(directory / "iq.sigmf-meta", artifacts.sigmf);
  const auto receiver = fixture.configuration->GetReceiverGraphResponse().at("graph");
  Phase7WriteJson(directory / "receiver-minimal.json", receiver);
  const nlohmann::json result{{"availability", {{"state", "available"}}},
                              {"status", "accepted"}, {"accepted", true},
                              {"decoded_pulse_count", 18}};
  const nlohmann::json available{{"state", "available"}, {"reason", nullptr}};
  const nlohmann::json unavailable{{"state", "unavailable"},
                                    {"reason", "no_candidate_detected"}};
  const auto observation = nlohmann::json{
      {"schema", "graphx.dashboard.fhss_receiver_observation.v1"},
      {"semantic_class", "observed"}, {"generation", 1}, {"run_epoch", 1},
      {"config_revision", 1}, {"config_etag", "seed-etag"},
      {"observation_id", "observation-g1-r1"}, {"availability", available},
      {"timing_basis", {{"unit", "input_samples"}, {"global", true}}},
      {"sample_rate", {{"availability", available},
                       {"global_input_sample_rate_hz", artifacts.sigmf.at("global").at("core:sample_rate")},
                       {"receiver_capture_sample_rate_hz", artifacts.sigmf.at("global").at("core:sample_rate")},
                       {"input_samples_per_capture_sample", 1}}},
      {"observed_pulses", nlohmann::json::array()}, {"detected_count", 0},
      {"rejected_count", 0}, {"count_availability", available},
      {"count_semantics", {{"detected", "terminal_message_sink_count"},
                            {"rejected", "terminal_message_sink_count"},
                            {"deduplication_rule", "terminal sink counts supersede upstream detector counts; source kinds are never added together"}}},
      {"rejection_reason_codes", nlohmann::json::array()},
      {"preamble", {{"availability", available}, {"locked", false}}},
      {"receiver_derived_active_frequencies", {{"availability", unavailable},
                                                {"indices", nlohmann::json::array()}}},
      {"assembler", {{"availability", unavailable}}},
      {"receiver_message_result", {{"availability", available}, {"status", "accepted"},
                                    {"accepted", true}, {"decoded_pulse_count", 18}}},
      {"terminal_result", {{"availability", available}, {"code", "execution_completed"},
                            {"message", "completed"}, {"terminal_at", "2026-07-20T00:00:00Z"}}},
      {"sources", nlohmann::json::array()}, {"provenance", nlohmann::json::array()},
      {"truncation", {{"truncated", false}, {"original_pulse_count", 0},
                       {"returned_pulse_count", 0}, {"max_pulses", 512},
                       {"max_response_bytes", 1048576}}},
      {"observation_sha256", std::string(64, '1')}};
  const auto comparison = nlohmann::json{
      {"schema", "graphx.dashboard.fhss_comparison_result.v1"},
      {"semantic_class", "comparison"}, {"evaluation_state", "evaluated"},
      {"expected_truth_sha256", std::string(64, '2')},
      {"receiver_observation_sha256", std::string(64, '1')},
      {"generation", 1}, {"run_epoch", 1},
      {"config_identity", {{"expected_config_revision", 1}, {"expected_config_etag", "seed-etag"},
                            {"observed_config_revision", 1}, {"observed_config_etag", "seed-etag"},
                            {"agrees", true}}},
      {"algorithm", {{"name", "bounded_one_to_one_timing_channel_match"}, {"version", "1.1.0"},
                      {"timing_tolerance_samples", 64}, {"channel_rule", "logical_frequency_index_exact"},
                      {"tie_rule", "equal_distance_is_ambiguous_no_assignment"},
                      {"duplicate_rule", "each_expected_and_observed_used_at_most_once"}}},
      {"availability", available}, {"matches", nlohmann::json::array()},
      {"missed_expected_indices", nlohmann::json::array()},
      {"unexpected_observed_indices", nlohmann::json::array()},
      {"ambiguous", nlohmann::json::array()}, {"terminal_result_agrees", true},
      {"execution_lifecycle", {{"completed", true}, {"observed_code", "execution_completed"},
                                {"correlated_separately_from_receiver_message", true}}},
      {"comparison_sha256", std::string(64, '3')}};
  const auto file_hash = [](const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    const std::string text(std::istreambuf_iterator<char>(input), {});
    return graphx::examples::fhss::Sha256(
        std::as_bytes(std::span(text.data(), text.size())));
  };
  seeded.source = {
      .directory = directory,
      .job = {{"sample_format", format}, {"request_id", "seed-request"},
              {"controller_epoch", 1}, {"graph_generation", 1},
              {"run_epoch", 1},
              {"scenario_correlation_id", "s-000000000000000000000007"},
              {"config_revision", 1}, {"config_etag", "seed-etag"},
              {"artifacts", {
                  {"iq", {{"relative_path", format == "cf32_le" ? "iq.cf32" : "iq.cf64"},
                           {"bytes", artifacts.iq_bytes.size()}, {"sha256", artifacts.iq_sha256}}},
                  {"truth", {{"relative_path", "truth.withheld.json"}, {"sha256", file_hash(directory / "truth.withheld.json")}}},
                  {"sigmf", {{"relative_path", "iq.sigmf-meta"}, {"sha256", file_hash(directory / "iq.sigmf-meta")}}},
                  {"receiver_config", {{"relative_path", "receiver-minimal.json"}, {"sha256", file_hash(directory / "receiver-minimal.json")}}}}}},
      .observation = observation,
      .comparison = comparison,
      .receiver_result = result};
  return seeded;
}

nlohmann::json WaitPhase7(
    dsp::fhss::dashboard::FHSSInvestigationBundleService &service,
    const std::string &operation_id,
    std::chrono::seconds bound = std::chrono::seconds(120)) {
  const auto deadline = std::chrono::steady_clock::now() +
                        bound;
  while (std::chrono::steady_clock::now() < deadline) {
    auto document = service.Get(operation_id).document;
    const auto state = document.at("state").get<std::string>();
    if (state == "completed" || state == "cancelled" || state == "failed" ||
        state == "timed_out")
      return document;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return service.Get(operation_id).document;
}

std::string Phase7HashText(std::string_view text, bool sha512 = false) {
  const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
  return sha512 ? graphx::examples::fhss::Sha512(bytes)
                : graphx::examples::fhss::Sha256(bytes);
}

void Phase7RebindJsonArtifact(const std::filesystem::path &directory,
                              std::string_view name,
                              const nlohmann::json &document) {
  const auto text =
      dsp::fhss::dashboard::FHSSInvestigationBundleService::CanonicalJson(document);
  std::ofstream(directory / name, std::ios::binary) << text;
  auto manifest = Phase7LoadJson(directory / "manifest.json");
  auto entry = std::ranges::find_if(manifest.at("artifacts"), [&](const auto &item) {
    return item.value("path", "") == name;
  });
  ASSERT_NE(entry, manifest.at("artifacts").end());
  (*entry)["bytes"] = text.size();
  (*entry)["sha256"] = Phase7HashText(text);
  (*entry)["sha512"] = Phase7HashText(text, true);
  const auto manifest_text =
      dsp::fhss::dashboard::FHSSInvestigationBundleService::CanonicalJson(manifest);
  std::ofstream(directory / "manifest.json", std::ios::binary) << manifest_text;
  std::ofstream(directory / "manifest.sha256", std::ios::binary)
      << Phase7HashText(manifest_text) << '\n';
}

std::string Phase7Export(
    dsp::fhss::dashboard::FHSSInvestigationBundleService &service,
    const SeededPhase7Source &seeded, std::string name,
    std::string mode = "copy") {
  nlohmann::json request{{"request_id", "export-" + name},
                         {"bundle_name", name}, {"job_id", seeded.job_id},
                         {"iq_mode", mode},
                         {"timeout_ms", kPhase7TestTimeoutMs}};
  if (mode == "copy") request["confirm_copy"] = true;
  const auto submitted = service.SubmitExport(request, "key-" + name);
  EXPECT_EQ(submitted.status_code, 202);
  const auto terminal = WaitPhase7(
      service, submitted.document.at("operation_id").get<std::string>());
  EXPECT_EQ(terminal.at("state"), "completed") << terminal.dump(2);
  return name;
}

auto Phase7Hooks(const SeededPhase7Source &seeded) {
  auto hooks = std::make_shared<
      dsp::fhss::dashboard::FHSSInvestigationBundleService::TestHooks>();
  hooks->source_lookup = [seeded](std::string_view job_id)
      -> std::optional<dsp::fhss::dashboard::FHSSJobController::InvestigationSource> {
    return job_id == seeded.job_id ? std::optional(seeded.source) : std::nullopt;
  };
  return hooks;
}

TEST(FhssDashboardInvestigationBundleTest,
     MalformedTypedOverflowAndUnsupportedRequestsFailSafely) {
  Phase7Fixture fixture;
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root);
  const std::vector<nlohmann::json> invalid{
      nlohmann::json::array(),
      {{"request_id", 1}, {"bundle_name", "b"}},
      {{"request_id", "r"}, {"bundle_name", false}},
      {{"request_id", "r"}, {"bundle_name", "b"}, {"timeout_ms", -1}},
      {{"request_id", "r"}, {"bundle_name", "b"}, {"timeout_ms", 1ull << 63}},
      {{"request_id", "r"}, {"bundle_name", "../b"}},
      {{"request_id", "r"}, {"bundle_name", "b"}, {"unknown", true}}};
  for (std::size_t index = 0; index < invalid.size(); ++index)
    EXPECT_GE(service.SubmitValidation(invalid[index],
                                      "invalid-" + std::to_string(index)).status_code,
              400);
  EXPECT_GE(service.SubmitExport(
      {{"request_id", "r"}, {"bundle_name", "b"}, {"job_id", 7}}, "bad-job").status_code, 400);
  EXPECT_GE(service.SubmitExport(
      {{"request_id", "r"}, {"bundle_name", "b"}, {"job_id", "j-000000000000000000000001"},
       {"iq_mode", 7}}, "bad-mode").status_code, 400);
  EXPECT_GE(service.SubmitExport(
      {{"request_id", "r"}, {"bundle_name", "b"}, {"job_id", "j-000000000000000000000001"},
       {"confirm_copy", "yes"}}, "bad-confirm").status_code, 400);
  EXPECT_EQ(service.List().document.at("entries").size(), 0u);
}

TEST(FhssDashboardInvestigationBundleTest,
     ReferenceAndCopiedExportsPublishCanonicalSeparatedArtifactsAndValidate) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);
  auto hooks = Phase7Hooks(seeded);
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);
  for (const auto &mode : {std::string("reference"), std::string("copy")}) {
    const auto name = "bundle-" + mode;
    nlohmann::json request{{"request_id", "export-" + mode},
                           {"bundle_name", name}, {"job_id", seeded.job_id},
                           {"iq_mode", mode},
                           {"timeout_ms", kPhase7TestTimeoutMs}};
    if (mode == "copy") request["confirm_copy"] = true;
    const auto submitted = service.SubmitExport(request, "key-" + mode);
    ASSERT_EQ(submitted.status_code, 202);
    const auto terminal = WaitPhase7(
        service, submitted.document.at("operation_id").get<std::string>());
    ASSERT_EQ(terminal.at("state"), "completed") << terminal.dump(2);
    EXPECT_EQ(terminal.at("result").at("datatype"), "cf32_le");
    EXPECT_EQ(terminal.at("result").at("sample_count").get<std::uint64_t>(),
              terminal.at("result").at("iq_bytes").get<std::uint64_t>() / 8);
    const auto directory = fixture.root / "fhss-investigations" / name;
    const auto manifest = Phase7LoadJson(directory / "manifest.json");
    EXPECT_EQ(manifest.at("self_contained"), mode == "copy");
    EXPECT_EQ(manifest.at("receiver_truth_access"), "none");
    EXPECT_TRUE(std::filesystem::is_regular_file(directory / "truth.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(directory / "observation.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(directory / "comparison.json"));
    const auto receiver_config = Phase7LoadJson(directory / "receiver-config.json");
    EXPECT_EQ(receiver_config.at("schema"), "graphx.dashboard.receiver_graph.v1");
    EXPECT_FALSE(dsp::fhss::dashboard::FHSSInvestigationBundleService::
                     ContainsForbiddenReceiverKey(receiver_config.at("graph")));
    EXPECT_EQ(std::filesystem::exists(directory / "recording.sigmf-data"),
              mode == "copy");
    for (const auto &entry : manifest.at("artifacts")) {
      const auto classification = entry.at("classification").get<std::string>();
      if (entry.at("path") == "comparison.json")
        EXPECT_EQ(entry.at("schema"),
                  "graphx.dashboard.fhss_comparison_result.v1");
      EXPECT_EQ(entry.at("receiver_visible").get<bool>(),
                classification == "receiver_input" || classification == "raw_iq");
    }
    const auto validation = service.SubmitValidation(
        {{"request_id", "validate-" + mode}, {"bundle_name", name},
         {"timeout_ms", kPhase7TestTimeoutMs}},
        "validate-key-" + mode);
    ASSERT_EQ(validation.status_code, 202);
    EXPECT_EQ(WaitPhase7(service, validation.document.at("operation_id").get<std::string>()).at("state"),
              "completed");
  }
}

TEST(FhssDashboardInvestigationBundleTest,
     RepeatedSuccessfulReplayUsesRealRuntimeOwnerAndTerminatesWithinBound) {
  Phase7RealOwnerFixture fixture;
  fixture.owner->SetExecutorTimeoutForTesting(kRealOwnerTestEnvelope);
  auto seeded = SeedSource(fixture);

  // First use the same owner as the normal source-job receiver.  This is the
  // lifecycle sequence implicated by the external flake: successful receiver
  // execution, export, then repeated bundle replay on replacement executors.
  auto source_graph = fixture.configuration->GetReceiverGraphResponse().at("graph");
  auto source = std::ranges::find_if(
      source_graph.at("nodes"), [](const auto &node) {
        return node.value("id", "") == "source";
      });
  ASSERT_NE(source, source_graph.at("nodes").end());
  (*source)["node_config"]["file_path"] =
      (seeded.source.directory / "iq.cf32").string();
  (*source)["node_config"]["max_read_complex_samples"] = 4'194'304;
  ASSERT_EQ(fixture.runtime
                ->Rebuild({.receiver_graph = source_graph,
                           .config_revision = 1,
                           .config_etag = "seed-etag"})
                .status_code,
            200);
  ASSERT_EQ(fixture.runtime->Start().status_code, 202);
  const auto source_deadline =
      std::chrono::steady_clock::now() + kRealOwnerTestEnvelope;
  while (std::chrono::steady_clock::now() < source_deadline &&
         fixture.runtime->GetState() ==
             graph::dashboard::GraphRuntimeSession::State::running)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_EQ(fixture.runtime->GetState(),
            graph::dashboard::GraphRuntimeSession::State::completed)
      << fixture.runtime->SnapshotStatus().terminal_result_message;
  auto observations =
      std::make_shared<dsp::fhss::dashboard::FHSSObservationService>(
          fixture.configuration, fixture.runtime);
  const auto source_observation = observations->ReceiverObservation().document;
  ASSERT_TRUE(source_observation.contains("receiver_message_result"));
  seeded.source.receiver_result =
      source_observation.at("receiver_message_result");

  auto hooks = Phase7Hooks(seeded);
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      hooks);
  Phase7Export(service, seeded, "real-owner-reference", "reference");
  Phase7Export(service, seeded, "real-owner-copy", "copy");

  std::optional<std::string> semantic_hash;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::string bundle = attempt == 1 ? "real-owner-copy"
                                            : "real-owner-reference";
    const auto submitted = service.SubmitReplay(
        {{"request_id", "real-owner-replay-" + std::to_string(attempt)},
         {"bundle_name", bundle},
         {"timeout_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                            kRealOwnerTestEnvelope).count()}},
        "real-owner-replay-key-" + std::to_string(attempt));
    ASSERT_EQ(submitted.status_code, 202) << submitted.document.dump(2);
    EXPECT_EQ(submitted.document.at("bounds").at("timeout_ms"),
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  kRealOwnerTestEnvelope).count());
    const auto started = std::chrono::steady_clock::now();
    const auto terminal = WaitPhase7(
        service, submitted.document.at("operation_id").get<std::string>(),
        kRealOwnerTestEnvelope);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ASSERT_EQ(terminal.at("state"), "completed")
        << "attempt=" << attempt << "; elapsed_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        << "; operation=" << terminal.dump(2);
    ASSERT_TRUE(terminal.at("result").at("matches_expected").get<bool>());
    EXPECT_EQ(fixture.owner->StopSequenceCountForTesting(), 1u)
        << "each replacement executor must enter GraphExecutor::StopExpected "
           "once; same-thread defensive component stops are idempotent; "
           "attempt="
        << attempt;
    const auto current_hash = terminal.at("result")
                                  .at("semantic_receiver_result_sha256")
                                  .get<std::string>();
    if (semantic_hash)
      EXPECT_EQ(current_hash, *semantic_hash) << "attempt=" << attempt;
    semantic_hash = current_hash;
    EXPECT_LT(elapsed, kRealOwnerTestEnvelope) << "attempt=" << attempt;
    const auto shutdown_started = std::chrono::steady_clock::now();
    const auto shutdown = fixture.owner->Shutdown(0);
    const auto shutdown_elapsed =
        std::chrono::steady_clock::now() - shutdown_started;
    EXPECT_EQ(shutdown.status_code, 200)
        << "attempt=" << attempt << "; code=" << shutdown.code;
    EXPECT_LT(shutdown_elapsed, std::chrono::seconds(2))
        << "owner shutdown must join every edge, node, and worker; attempt="
        << attempt;
  }
}

TEST(FhssDashboardInvestigationBundleTest,
     PublicationCancelCollisionEnospcAndSourceSwapFailWithoutPartialOutput) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);
  auto hooks = Phase7Hooks(seeded);
  Phase7Gate publish_gate;
  hooks->before_publish = [&] { publish_gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);
  const auto submitted = service.SubmitExport(
      {{"request_id", "cancel-export"}, {"bundle_name", "cancelled-bundle"},
       {"job_id", seeded.job_id}, {"iq_mode", "reference"},
       {"timeout_ms", kPhase7TestTimeoutMs}}, "cancel-export-key");
  ASSERT_EQ(submitted.status_code, 202);
  publish_gate.Wait();
  const auto cancelled = service.Cancel(
      submitted.document.at("operation_id").get<std::string>());
  EXPECT_EQ(cancelled.status_code, 202);
  publish_gate.Release();
  EXPECT_EQ(WaitPhase7(service, submitted.document.at("operation_id").get<std::string>()).at("state"),
            "cancelled");
  EXPECT_FALSE(std::filesystem::exists(
      fixture.root / "fhss-investigations" / "cancelled-bundle"));
  service.Shutdown();

  auto collision_hooks = Phase7Hooks(seeded);
  collision_hooks->before_publish = [&] {
    const auto target = fixture.root / "fhss-investigations" / "collision";
    std::filesystem::create_directory(target);
    Phase7WriteJson(target / "marker.json", {{"preserved", true}});
  };
  dsp::fhss::dashboard::FHSSInvestigationBundleService collision_service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      collision_hooks);
  auto collision = collision_service.SubmitExport(
      {{"request_id", "collision"}, {"bundle_name", "collision"},
       {"job_id", seeded.job_id}, {"timeout_ms", kPhase7TestTimeoutMs}},
      "collision-key");
  ASSERT_EQ(collision.status_code, 202);
  EXPECT_EQ(WaitPhase7(collision_service, collision.document.at("operation_id").get<std::string>())
                .at("state"), "failed");
  EXPECT_TRUE(Phase7LoadJson(fixture.root / "fhss-investigations" /
                            "collision" / "marker.json").at("preserved"));
  collision_service.Shutdown();

  auto fault_hooks = Phase7Hooks(seeded);
  fault_hooks->inject_enospc = true;
  dsp::fhss::dashboard::FHSSInvestigationBundleService fault_service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      fault_hooks);
  auto fault = fault_service.SubmitExport(
      {{"request_id", "enospc"}, {"bundle_name", "enospc"},
       {"job_id", seeded.job_id}, {"iq_mode", "copy"},
       {"confirm_copy", true}, {"timeout_ms", kPhase7TestTimeoutMs}},
      "enospc-key");
  ASSERT_EQ(fault.status_code, 202);
  EXPECT_EQ(WaitPhase7(fault_service, fault.document.at("operation_id").get<std::string>()).at("state"),
            "failed");
  EXPECT_FALSE(std::filesystem::exists(
      fixture.root / "fhss-investigations" / "enospc"));
  fault_service.Shutdown();

  auto swap_hooks = Phase7Hooks(seeded);
  const auto iq_path = seeded.source.directory / "iq.cf32";
  swap_hooks->source_identity_change = [iq_path] {
    const auto displaced = iq_path.string() + ".displaced";
    std::filesystem::rename(iq_path, displaced);
    std::ofstream(iq_path, std::ios::binary) << "replacement";
  };
  dsp::fhss::dashboard::FHSSInvestigationBundleService swap_service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      swap_hooks);
  auto swap = swap_service.SubmitExport(
      {{"request_id", "swap"}, {"bundle_name", "swap"},
       {"job_id", seeded.job_id}, {"timeout_ms", kPhase7TestTimeoutMs}},
      "swap-key");
  ASSERT_EQ(swap.status_code, 202);
  EXPECT_EQ(WaitPhase7(swap_service, swap.document.at("operation_id").get<std::string>()).at("state"),
            "failed");
  EXPECT_FALSE(std::filesystem::exists(
      fixture.root / "fhss-investigations" / "swap"));
}

TEST(FhssDashboardInvestigationBundleTest,
     QualificationEnospcTransitionsAtCopyBoundaryAndCleansStaging) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);
  auto hooks = Phase7Hooks(seeded);
  hooks->qualification_sequence = true;
  Phase7Gate enospc_gate;
  hooks->qualification_enospc_checkpoint = [&] { enospc_gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);

  const auto submit = [&](int sequence, std::string_view mode) {
    const auto name = "qualification-" + std::to_string(sequence);
    nlohmann::json request{{"request_id", name}, {"bundle_name", name},
                           {"job_id", seeded.job_id}, {"iq_mode", mode},
                           {"timeout_ms", 30'000}};
    if (mode == "copy") request["confirm_copy"] = true;
    return service.SubmitExport(request, name + "-key");
  };

  const auto quota = submit(1, "reference");
  ASSERT_EQ(quota.status_code, 202);
  EXPECT_EQ(WaitPhase7(service, quota.document.at("operation_id")
                                    .get<std::string>())
                .at("terminal")
                .at("code"),
            "investigation_quota_exceeded");
  const auto copy_limit = submit(2, "copy");
  ASSERT_EQ(copy_limit.status_code, 202);
  EXPECT_EQ(WaitPhase7(service, copy_limit.document.at("operation_id")
                                    .get<std::string>())
                .at("terminal")
                .at("code"),
            "investigation_quota_exceeded");

  const auto enospc = submit(3, "copy");
  ASSERT_EQ(enospc.status_code, 202);
  const auto operation_id = enospc.document.at("operation_id").get<std::string>();
  const bool reached_copy_boundary =
      enospc_gate.WaitFor(std::chrono::seconds(5));
  EXPECT_TRUE(reached_copy_boundary);
  if (reached_copy_boundary)
    EXPECT_EQ(service.Get(operation_id).document.at("state"), "copying");
  enospc_gate.Release();

  const auto terminal = WaitPhase7(service, operation_id,
                                   std::chrono::seconds(5));
  EXPECT_EQ(terminal.at("state"), "failed");
  EXPECT_EQ(terminal.at("terminal").at("code"), "artifact_enospc");
  const auto bundle_root = fixture.root / "fhss-investigations";
  EXPECT_FALSE(std::filesystem::exists(bundle_root / "qualification-3"));
  EXPECT_TRUE(std::ranges::none_of(
      std::filesystem::directory_iterator(bundle_root), [](const auto &entry) {
        return entry.path().filename().string().starts_with(".tmp-");
      }));
}

TEST(FhssDashboardInvestigationBundleTest,
     CancellationAfterAtomicRenameObservesCompletedPointOfNoReturn) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);
  auto hooks = Phase7Hooks(seeded);
  Phase7Gate committed_gate;
  hooks->after_publish_rename = [&] { committed_gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);
  const auto submitted = service.SubmitExport(
      {{"request_id", "post-commit"}, {"bundle_name", "post-commit"},
       {"job_id", seeded.job_id}, {"timeout_ms", kPhase7TestTimeoutMs}},
      "post-commit-key");
  ASSERT_EQ(submitted.status_code, 202);
  committed_gate.Wait();
  const auto operation_id =
      submitted.document.at("operation_id").get<std::string>();
  const auto cancel = service.Cancel(operation_id);
  EXPECT_EQ(cancel.status_code, 200);
  EXPECT_EQ(cancel.document.at("state"), "completed");
  EXPECT_EQ(cancel.document.at("terminal").at("code"), "bundle_exported");
  EXPECT_TRUE(std::filesystem::is_directory(
      fixture.root / "fhss-investigations" / "post-commit"));
  committed_gate.Release();
  const auto terminal = WaitPhase7(service, operation_id);
  EXPECT_EQ(terminal.at("state"), "completed");
  EXPECT_TRUE(std::filesystem::is_regular_file(
      fixture.root / "fhss-investigations" / "post-commit" /
      "manifest.json"));
}

TEST(FhssDashboardInvestigationBundleTest,
     ExecutableIdentityHashHonorsCancellationAndDeadline) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);

  auto cancel_hooks = Phase7Hooks(seeded);
  Phase7Gate cancel_gate;
  cancel_hooks->executable_hash_checkpoint = [&] { cancel_gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService cancel_service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      cancel_hooks);
  auto submitted = cancel_service.SubmitExport(
      {{"request_id", "cancel-executable-hash"},
       {"bundle_name", "cancel-executable-hash"},
       {"job_id", seeded.job_id},
       {"timeout_ms", kPhase7TestTimeoutMs}},
      "cancel-executable-hash-key");
  ASSERT_EQ(submitted.status_code, 202);
  cancel_gate.Wait();
  EXPECT_EQ(cancel_service.Cancel(
                submitted.document.at("operation_id").get<std::string>())
                .status_code,
            202);
  cancel_gate.Release();
  EXPECT_EQ(WaitPhase7(
                cancel_service,
                submitted.document.at("operation_id").get<std::string>())
                .at("state"),
            "cancelled");
  EXPECT_FALSE(std::filesystem::exists(
      fixture.root / "fhss-investigations" / "cancel-executable-hash"));
  cancel_service.Shutdown();

  auto timeout_hooks = Phase7Hooks(seeded);
  Phase7Gate timeout_gate;
  timeout_hooks->executable_hash_checkpoint = [&] { timeout_gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService timeout_service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root,
      timeout_hooks);
  const auto timeout_started = std::chrono::steady_clock::now();
  submitted = timeout_service.SubmitExport(
      {{"request_id", "timeout-executable-hash"},
       {"bundle_name", "timeout-executable-hash"},
       {"job_id", seeded.job_id}, {"timeout_ms", 5'000}},
      "timeout-executable-hash-key");
  ASSERT_EQ(submitted.status_code, 202);
  timeout_gate.Wait();
  std::this_thread::sleep_until(timeout_started + std::chrono::milliseconds(5'100));
  timeout_gate.Release();
  EXPECT_EQ(WaitPhase7(
                timeout_service,
                submitted.document.at("operation_id").get<std::string>())
                .at("state"),
            "timed_out");
  EXPECT_FALSE(std::filesystem::exists(
      fixture.root / "fhss-investigations" / "timeout-executable-hash"));
}

TEST(FhssDashboardInvestigationBundleTest,
     QueuedCancellationAndShutdownAreTerminalWithoutDeadlock) {
  Phase7Fixture fixture;
  auto hooks = std::make_shared<
      dsp::fhss::dashboard::FHSSInvestigationBundleService::TestHooks>();
  Phase7Gate gate;
  hooks->before_processing = [&] { gate.Enter(); };
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);
  const auto first = service.SubmitValidation(
      {{"request_id", "first"}, {"bundle_name", "first"},
       {"timeout_ms", kPhase7TestTimeoutMs}}, "first-key");
  ASSERT_EQ(first.status_code, 202);
  gate.Wait();
  const auto second = service.SubmitValidation(
      {{"request_id", "second"}, {"bundle_name", "second"},
       {"timeout_ms", kPhase7TestTimeoutMs}}, "second-key");
  ASSERT_EQ(second.status_code, 202);
  const auto cancelled = service.Cancel(second.document.at("operation_id").get<std::string>());
  EXPECT_EQ(cancelled.status_code, 202);
  EXPECT_EQ(cancelled.document.at("state"), "cancelled");
  EXPECT_EQ(cancelled.document.at("terminal").at("code"), "cancelled_before_start");
  gate.Release();
  service.Shutdown();
}

TEST(FhssDashboardInvestigationBundleTest,
     SigMfIndependentSemanticChecksCoverBothDatatypesAndBounds) {
  const auto metadata = [](std::string datatype, std::string hash) {
    return nlohmann::json{{"global", {{"core:datatype", datatype},
                                      {"core:version", "1.2.6"},
                                      {"core:sample_rate", 500'000'000.0},
                                      {"core:sha512", hash}}},
                          {"captures", nlohmann::json::array({
                              {{"core:sample_start", 0},
                               {"core:frequency", 1'240'000'000.0}}})},
                          {"annotations", nlohmann::json::array()}};
  };
  const std::string hash(128, 'a');
  EXPECT_TRUE(dsp::fhss::dashboard::FHSSInvestigationBundleService::ValidateSigMf(
      metadata("cf32_le", hash), 16, hash));
  EXPECT_TRUE(dsp::fhss::dashboard::FHSSInvestigationBundleService::ValidateSigMf(
      metadata("cf64_le", hash), 32, hash));
  EXPECT_FALSE(dsp::fhss::dashboard::FHSSInvestigationBundleService::ValidateSigMf(
      metadata("cf32_le", hash), 15, hash));
  auto out_of_range = metadata("cf32_le", hash);
  out_of_range["annotations"] = nlohmann::json::array({
      {{"core:sample_start", 1}, {"core:sample_count", 2}}});
  EXPECT_FALSE(dsp::fhss::dashboard::FHSSInvestigationBundleService::ValidateSigMf(
      out_of_range, 16, hash));
  auto official_only_invalid = metadata("cf32_le", hash);
  official_only_invalid["unexpected"] = true;
  EXPECT_FALSE(dsp::fhss::dashboard::FHSSInvestigationBundleService::ValidateSigMf(
      official_only_invalid, 16, hash));
}

TEST(FhssDashboardInvestigationBundleTest,
     OfficialSchemaCrossIdentityAndPathSwapFailBeforeReplayConstruction) {
  Phase7Fixture fixture;
  const auto seeded = SeedSource(fixture);
  auto hooks = Phase7Hooks(seeded);
  bool construct_called = false;
  bool swap_armed = false;
  hooks->before_replay_construction = [&] { construct_called = true; };
  std::filesystem::path swapped_path;
  hooks->after_bundle_artifacts_hashed = [&] {
    if (!swap_armed) return;
    const auto displaced = swapped_path.string() + ".old";
    std::filesystem::rename(swapped_path, displaced);
    std::filesystem::copy_file(displaced, swapped_path);
  };
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);

  Phase7Export(service, seeded, "official-invalid");
  auto official_directory = fixture.root / "fhss-investigations" / "official-invalid";
  auto sigmf = Phase7LoadJson(official_directory / "recording.sigmf-meta");
  sigmf["unexpected"] = true;
  Phase7RebindJsonArtifact(official_directory, "recording.sigmf-meta", sigmf);
  auto replay = service.SubmitReplay(
      {{"request_id", "official-replay"}, {"bundle_name", "official-invalid"},
       {"timeout_ms", kPhase7TestTimeoutMs}},
      "official-replay-key");
  ASSERT_EQ(replay.status_code, 202);
  EXPECT_EQ(WaitPhase7(service, replay.document.at("operation_id").get<std::string>())
                .at("state"), "failed");
  EXPECT_FALSE(construct_called);

  const std::vector<std::pair<std::string, nlohmann::json>> committed_identities{
      {"source-job-id", "j-000000000000000000000008"},
      {"source-job-request-id", "seed-request-substituted"},
      {"controller-epoch", 2},
      {"scenario-correlation-id", "s-000000000000000000000008"}};
  for (const auto &[label, replacement] : committed_identities) {
    const auto bundle = "identity-invalid-" + label;
    Phase7Export(service, seeded, bundle);
    const auto identity_directory =
        fixture.root / "fhss-investigations" / bundle;
    auto provenance = Phase7LoadJson(identity_directory / "provenance.json");
    auto key = label;
    std::ranges::replace(key, '-', '_');
    provenance[key] = replacement;
    Phase7RebindJsonArtifact(identity_directory, "provenance.json", provenance);
    replay = service.SubmitReplay(
        {{"request_id", "identity-replay-" + label}, {"bundle_name", bundle},
         {"timeout_ms", kPhase7TestTimeoutMs}},
        "identity-replay-key-" + label);
    ASSERT_EQ(replay.status_code, 202);
    EXPECT_EQ(WaitPhase7(
                  service,
                  replay.document.at("operation_id").get<std::string>())
                  .at("state"),
              "failed")
        << label;
    EXPECT_FALSE(construct_called) << label;
  }

  std::size_t swap_index = 0;
  for (const auto artifact : {"recording.sigmf-meta", "receiver-config.json",
                              "provenance.json", "receiver-result.json",
                              "truth.json", "observation.json", "comparison.json",
                              "actions.json", "build-api.json"}) {
    const auto bundle = "path-swap-" + std::to_string(swap_index);
    swap_armed = false;
    Phase7Export(service, seeded, bundle);
    swapped_path = fixture.root / "fhss-investigations" / bundle / artifact;
    swap_armed = true;
    replay = service.SubmitReplay(
        {{"request_id", "swap-replay-" + std::to_string(swap_index)},
         {"bundle_name", bundle}, {"timeout_ms", kPhase7TestTimeoutMs}},
        "swap-replay-key-" + std::to_string(swap_index));
    ASSERT_EQ(replay.status_code, 202);
    EXPECT_EQ(WaitPhase7(service,
                         replay.document.at("operation_id").get<std::string>())
                  .at("state"), "failed") << artifact;
    EXPECT_FALSE(construct_called) << artifact;
    ++swap_index;
  }
}

TEST(FhssDashboardInvestigationBundleTest,
     CompletedJobDocumentSubstitutionAndAggregateQuotaFailClosed) {
  Phase7Fixture fixture;
  auto seeded = SeedSource(fixture);
  const auto original_truth =
      Phase7LoadJson(seeded.source.directory / "truth.withheld.json");
  auto substituted = original_truth;
  substituted["generator"] = "substituted-generator";
  Phase7WriteJson(seeded.source.directory / "truth.withheld.json", substituted);
  auto hooks = Phase7Hooks(seeded);
  dsp::fhss::dashboard::FHSSInvestigationBundleService service(
      fixture.configuration, fixture.runtime, fixture.jobs, fixture.root, hooks);
  auto submitted = service.SubmitExport(
      {{"request_id", "source-substitution"}, {"bundle_name", "substitution"},
       {"job_id", seeded.job_id}, {"iq_mode", "reference"},
       {"timeout_ms", kPhase7TestTimeoutMs}},
      "source-substitution-key");
  ASSERT_EQ(submitted.status_code, 202);
  EXPECT_EQ(WaitPhase7(service, submitted.document.at("operation_id").get<std::string>())
                .at("state"), "failed");
  EXPECT_FALSE(std::filesystem::exists(fixture.root / "fhss-investigations" /
                                       "substitution"));

  Phase7WriteJson(seeded.source.directory / "truth.withheld.json", original_truth);
  const auto filler = fixture.root / "fhss-investigations" / "quota-filler";
  std::filesystem::create_directories(filler);
  std::ofstream(filler / "sparse.bin", std::ios::binary) << 'x';
  std::filesystem::resize_file(
      filler / "sparse.bin",
      dsp::fhss::dashboard::FHSSInvestigationBundleService::kMaxRetainedBundleBytes);
  const auto quota = service.Quota().document;
  EXPECT_EQ(quota.at("retained_bundle_bytes"),
            dsp::fhss::dashboard::FHSSInvestigationBundleService::kMaxRetainedBundleBytes);
  EXPECT_EQ(quota.at("remaining_bundle_bytes"), 0);
  submitted = service.SubmitExport(
      {{"request_id", "quota-export"}, {"bundle_name", "quota-blocked"},
       {"job_id", seeded.job_id}, {"iq_mode", "reference"},
       {"timeout_ms", kPhase7TestTimeoutMs}}, "quota-export-key");
  ASSERT_EQ(submitted.status_code, 202);
  EXPECT_EQ(WaitPhase7(service, submitted.document.at("operation_id").get<std::string>())
                .at("state"), "failed");
  EXPECT_FALSE(std::filesystem::exists(fixture.root / "fhss-investigations" /
                                       "quota-blocked"));
  std::filesystem::remove_all(filler);
  Phase7Export(service, seeded, "quota-released", "reference");
}

TEST(FhssDashboardInvestigationBundleTest,
     ReceiverTruthIsolationIsRecursiveAndSemanticHashIsStable) {
  using Service = dsp::fhss::dashboard::FHSSInvestigationBundleService;
  EXPECT_TRUE(Service::ContainsForbiddenReceiverKey(
      {{"nodes", nlohmann::json::array({{{"node_config", {{"nested", nlohmann::json::array({
          {{"messages", nlohmann::json::array()}}})}}}}})}}));
  EXPECT_FALSE(Service::ContainsForbiddenReceiverKey(
      {{"nodes", nlohmann::json::array({{{"node_config", {{"preamble_pulses", 16}}}}})}}));
  const nlohmann::json result{{"accepted", true}, {"decoded_pulse_count", 18},
                              {"status", "accepted"}, {"timestamp", "ignored"}};
  auto changed = result;
  changed["timestamp"] = "different";
  EXPECT_EQ(Service::SemanticReceiverResultHash(result),
            Service::SemanticReceiverResultHash(changed));
}

} // namespace
