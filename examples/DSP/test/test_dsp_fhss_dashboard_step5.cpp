// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include "FHSSDashboardApi.hpp"
#include "FHSSDashboardConfigurationPolicy.hpp"
#include "FHSSIqArtifactGenerator.hpp"
#include "FHSSJobController.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <array>
#include <barrier>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

namespace {

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  nlohmann::json document;
  input >> document;
  return document;
}

bool ContainsKey(const nlohmann::json &value, std::string_view key) {
  if (value.is_object()) {
    for (const auto &[name, child] : value.items())
      if (name == key || ContainsKey(child, key))
        return true;
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (ContainsKey(child, key))
        return true;
  }
  return false;
}

std::filesystem::path UniqueTemp(std::string_view label) {
  return std::filesystem::temp_directory_path() /
         (std::string(label) + "-" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::string RetainedJobName(std::size_t index) {
  const auto digits = std::to_string(index);
  return "j-" + std::string(24 - digits.size(), '0') + digits;
}

void SeedRetainedArtifact(const std::filesystem::path &root, std::size_t index,
                          std::uintmax_t payload_bytes = 1) {
  const auto directory = root / "fhss-jobs" / RetainedJobName(index);
  std::filesystem::create_directories(directory);
  std::ofstream(directory / "manifest.json") << "{}\n";
  std::ofstream payload(directory / "iq.cf32", std::ios::binary);
  if (payload_bytes != 0) {
    payload.seekp(static_cast<std::streamoff>(payload_bytes - 1));
    payload.put('\0');
  }
}

class BlockingRuntimeOwner final : public graph::dashboard::IGraphRuntimeOwner {
public:
  Result Rebuild(std::uint64_t, const BuildSnapshot &snapshot) override {
    {
      std::lock_guard lock(mutex_);
      receiver_graph_ = snapshot.receiver_graph;
      rebuild_snapshots_.push_back(snapshot);
      rebuild_entered_ = true;
    }
    cv_.notify_all();
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return release_rebuild_; });
    manager_ = std::make_shared<graph::GraphManager>();
    return {200, "rebuild_succeeded", "rebuilt", manager_, false};
  }
  Result Start(std::uint64_t generation, std::uint64_t run_epoch) override {
    ++start_count_;
    {
      std::lock_guard lock(mutex_);
      start_entered_ = true;
      started_generation_ = generation;
      started_run_epoch_ = run_epoch;
    }
    cv_.notify_all();
    if (auto_complete_ && callback_)
      callback_(generation, run_epoch, true, "completed");
    return {202, "start_accepted", "started", manager_, false};
  }
  Result Stop(std::uint64_t) override {
    return {200, "stop_completed", "stopped", manager_, false};
  }
  Result Shutdown(std::uint64_t) override {
    Release();
    return {.status_code = 200,
            .code = "shutdown_complete",
            .message = "shutdown",
            .graph_manager = manager_,
            .cleanup_failed = false};
  }
  void SetCompletionCallback(CompletionCallback callback) override {
    callback_ = std::move(callback);
  }
  void WaitForRebuild() { WaitForRebuildCount(1); }
  void WaitForRebuildCount(std::size_t count) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock,
             [this, count] { return rebuild_snapshots_.size() >= count; });
  }
  void Release() {
    {
      std::lock_guard lock(mutex_);
      release_rebuild_ = true;
    }
    cv_.notify_all();
  }
  [[nodiscard]] nlohmann::json ReceiverGraph() const {
    std::lock_guard lock(mutex_);
    return receiver_graph_;
  }
  [[nodiscard]] std::uint64_t StartCount() const { return start_count_; }
  void SetAutoComplete(bool value) { auto_complete_ = value; }
  void WaitForStart() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return start_entered_; });
  }
  void Complete() {
    if (callback_)
      callback_(started_generation_, started_run_epoch_, true, "completed");
  }
  [[nodiscard]] std::vector<BuildSnapshot> RebuildSnapshots() const {
    std::lock_guard lock(mutex_);
    return rebuild_snapshots_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool rebuild_entered_ = false;
  bool release_rebuild_ = false;
  bool start_entered_ = false;
  bool auto_complete_ = true;
  std::uint64_t started_generation_ = 0;
  std::uint64_t started_run_epoch_ = 0;
  nlohmann::json receiver_graph_;
  std::vector<BuildSnapshot> rebuild_snapshots_;
  std::shared_ptr<graph::GraphManager> manager_;
  CompletionCallback callback_;
  std::atomic<std::uint64_t> start_count_{0};
};

class BlockingHook {
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
  void Release() {
    {
      std::lock_guard lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool entered_ = false;
  bool released_ = false;
};

struct ControllerFixture {
  std::filesystem::path root = UniqueTemp("graphx-dashboard-phase5");
  std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  std::shared_ptr<BlockingRuntimeOwner> owner =
      std::make_shared<BlockingRuntimeOwner>();
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime =
      std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  std::unique_ptr<dsp::fhss::dashboard::FHSSJobController> controller;
  std::shared_ptr<dsp::fhss::dashboard::FHSSJobController::TestHooks> hooks;

  explicit ControllerFixture(
      std::shared_ptr<dsp::fhss::dashboard::FHSSJobController::TestHooks>
          test_hooks = nullptr)
      : hooks(std::move(test_hooks)) {
    runtime->MarkReady();
    controller = std::make_unique<dsp::fhss::dashboard::FHSSJobController>(
        configuration, runtime, root, hooks);
  }
  ~ControllerFixture() {
    controller.reset();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

nlohmann::json
WaitForTerminal(dsp::fhss::dashboard::FHSSJobController &controller,
                const std::string &job_id) {
  nlohmann::json latest;
  for (std::size_t attempt = 0; attempt < 100'000; ++attempt) {
    latest = controller.Get(job_id).document;
    if (dsp::fhss::dashboard::FHSSJobController::IsTerminal(
            latest.at("state").get<std::string>()))
      return latest;
    std::this_thread::yield();
  }
  return latest;
}

TEST(FhssDashboardJobControllerTest, StateTableRejectsIllegalTransitions) {
  using Controller = dsp::fhss::dashboard::FHSSJobController;
  const std::array states{
      "queued",    "generating", "generated",           "replay_pending",
      "running",   "cancelling", "completed",           "cancelled",
      "timed_out", "failed",     "abandoned_on_restart"};
  const std::set<std::pair<std::string_view, std::string_view>> legal{
      {"queued", "generating"},
      {"queued", "cancelled"},
      {"queued", "failed"},
      {"generating", "generated"},
      {"generating", "cancelling"},
      {"generating", "cancelled"},
      {"generating", "timed_out"},
      {"generating", "failed"},
      {"generated", "replay_pending"},
      {"generated", "cancelling"},
      {"generated", "cancelled"},
      {"generated", "timed_out"},
      {"generated", "failed"},
      {"replay_pending", "running"},
      {"replay_pending", "cancelling"},
      {"replay_pending", "cancelled"},
      {"replay_pending", "timed_out"},
      {"replay_pending", "failed"},
      {"running", "cancelling"},
      {"running", "completed"},
      {"running", "timed_out"},
      {"running", "failed"},
      {"cancelling", "cancelled"},
      {"cancelling", "failed"}};
  for (const auto from : states)
    for (const auto to : states)
      EXPECT_EQ(Controller::IsLegalTransition(from, to),
                legal.contains({from, to}))
          << from << " -> " << to;
  for (const auto terminal : {"completed", "cancelled", "timed_out", "failed",
                              "abandoned_on_restart"})
    EXPECT_TRUE(Controller::IsTerminal(terminal));
}

TEST(FhssDashboardJobControllerTest,
     MalformedOverflowAndUnsupportedRequestsFailWithoutMutation) {
  ControllerFixture fixture;
  const auto before = fixture.controller->List().document;
  const std::array requests{
      std::pair{nlohmann::json::array(), std::string{"key-1"}},
      std::pair{nlohmann::json{{"request_id", "r"}, {"unknown", true}},
                std::string{"key-2"}},
      std::pair{nlohmann::json{{"request_id", "bad request"}},
                std::string{"key-3"}},
      std::pair{nlohmann::json{{"request_id", "r"}, {"operation", "pulse"}},
                std::string{"key-4"}},
      std::pair{nlohmann::json{{"request_id", "r"}, {"message_count", 0}},
                std::string{"key-5"}},
      std::pair{nlohmann::json{{"request_id", "r"},
                               {"operation", "step"},
                               {"message_count", 2}},
                std::string{"key-6"}},
      std::pair{
          nlohmann::json{{"request_id", "r"}, {"sample_format", "cf16_le"}},
          std::string{"key-7"}},
      std::pair{nlohmann::json{{"request_id", "r"}, {"timeout_ms", 99}},
                std::string{"key-8"}}};
  for (const auto &[request, key] : requests)
    EXPECT_GE(fixture.controller->Submit(request, key).status_code, 400);
  EXPECT_GE(
      fixture.controller->Submit({{"request_id", "r"}}, std::string(65, 'x'))
          .status_code,
      400);
  EXPECT_EQ(fixture.controller->List().document, before);
  EXPECT_EQ(fixture.controller->Get("j-does-not-exist").status_code, 404);
}

TEST(FhssDashboardJobControllerTest, GetAndListAreReadOnlySnapshots) {
  ControllerFixture fixture;
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "get-read-only"}},
      "get-read-only-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  fixture.owner->WaitForRebuildCount(1);
  const auto first_get = fixture.controller->Get(job_id).document;
  const auto second_get = fixture.controller->Get(job_id).document;
  const auto first_list = fixture.controller->List().document;
  const auto second_list = fixture.controller->List().document;
  EXPECT_EQ(first_get, second_get);
  EXPECT_EQ(first_list, second_list);
  fixture.owner->Release();
}

TEST(FhssDashboardJobControllerTest,
     CanonicalArtifactGeneratorUsesCompleteMessageAndByteEncoding) {
  auto graph = LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH);
  auto source = std::ranges::find_if(graph.at("nodes"), [](const auto &node) {
    return node.value("id", std::string{}) == "source";
  });
  ASSERT_NE(source, graph.at("nodes").end());
  auto input = source->at("node_config");
  input["messages"] = nlohmann::json::array({input.at("messages").at(0)});
  const auto cf32 = graphx::examples::fhss::GenerateIqArtifacts(
      input, "cf32_le", 4'194'304, 64u * 1024u * 1024u);
  const auto cf64 = graphx::examples::fhss::GenerateIqArtifacts(
      input, "cf64_le", 4'194'304, 64u * 1024u * 1024u);
  EXPECT_EQ(cf32.fixture.truth_pulses.size(), 18u);
  EXPECT_EQ(cf32.fixture.truth_pulses.at(1).global_start_sample -
                cf32.fixture.truth_pulses.at(0).global_start_sample,
            6'500u);
  EXPECT_EQ(cf32.iq_bytes.size(), cf32.fixture.samples.size() * 8u);
  EXPECT_EQ(cf64.iq_bytes.size(), cf64.fixture.samples.size() * 16u);
  EXPECT_EQ(cf32.truth.at("iq_sha256"), cf32.iq_sha256);
  EXPECT_EQ(cf32.sigmf.at("global").at("core:datatype"), "cf32_le");
}

TEST(FhssDashboardJobControllerTest,
     DuplicateConflictQueuedCancelAndTruthIsolationUseProductionController) {
  ControllerFixture fixture;
  const nlohmann::json first_request{{"operation", "step"},
                                     {"request_id", "request-1"},
                                     {"timeout_ms", 30'000}};
  const auto first = fixture.controller->Submit(first_request, "stable-key");
  ASSERT_EQ(first.status_code, 202);
  const auto job_id = first.document.at("job_id").get<std::string>();
  const auto duplicate =
      fixture.controller->Submit(first_request, "stable-key");
  ASSERT_EQ(duplicate.status_code, 200);
  EXPECT_EQ(duplicate.document.at("job_id"), job_id);
  EXPECT_TRUE(duplicate.document.at("idempotency").at("reused"));
  auto conflict_request = first_request;
  conflict_request["sample_format"] = "cf64_le";
  const auto conflict =
      fixture.controller->Submit(conflict_request, "stable-key");
  ASSERT_EQ(conflict.status_code, 409);
  EXPECT_EQ(conflict.document.at("code"),
            "idempotency_key_reused_with_different_payload");

  fixture.owner->WaitForRebuild();
  const auto withheld_truth =
      fixture.root / "fhss-jobs" / job_id / "truth.withheld.json";
  EXPECT_FALSE(std::filesystem::exists(withheld_truth));
  const auto queued = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "request-2"}}, "queued-key");
  ASSERT_EQ(queued.status_code, 202);
  const auto cancelled = fixture.controller->Cancel(
      queued.document.at("job_id").get<std::string>());
  ASSERT_EQ(cancelled.status_code, 202);
  EXPECT_EQ(cancelled.document.at("state"), "cancelled");
  EXPECT_FALSE(cancelled.document.at("work").at("generator_invoked"));
  EXPECT_FALSE(cancelled.document.at("work").at("receiver_replay_invoked"));
  EXPECT_EQ(
      cancelled.document.at("generation_result").at("availability").at("state"),
      "unavailable");
  EXPECT_TRUE(
      cancelled.document.at("generation_result").at("terminal").is_null());
  const auto repeated_cancel = fixture.controller->Cancel(
      queued.document.at("job_id").get<std::string>());
  EXPECT_EQ(repeated_cancel.status_code, 200);
  EXPECT_EQ(repeated_cancel.document.at("terminal"),
            cancelled.document.at("terminal"));
  fixture.owner->Release();

  const auto receiver_graph = fixture.owner->ReceiverGraph();
  for (const auto forbidden :
       {"messages", "truth", "expected_words", "scenario_correlation_id",
        "job_id", "active_frequency_indices"})
    EXPECT_FALSE(ContainsKey(receiver_graph, forbidden)) << forbidden;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(withheld_truth))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(withheld_truth));
}

TEST(FhssDashboardJobControllerTest,
     ConcurrentIdempotentSubmissionsCreateExactlyOneJob) {
  ControllerFixture fixture;
  constexpr std::size_t thread_count = 8;
  std::barrier gate(static_cast<std::ptrdiff_t>(thread_count));
  std::array<dsp::fhss::dashboard::FHSSJobController::Result, thread_count>
      results;
  std::array<std::jthread, thread_count> threads;
  for (std::size_t index = 0; index < thread_count; ++index)
    threads[index] = std::jthread([&, index] {
      gate.arrive_and_wait();
      results[index] = fixture.controller->Submit(
          {{"operation", "step"}, {"request_id", "concurrent-request"}},
          "concurrent-key");
    });
  for (auto &thread : threads)
    thread.join();
  const auto job_id = results.front().document.at("job_id");
  EXPECT_EQ(std::ranges::count_if(
                results,
                [](const auto &result) { return result.status_code == 202; }),
            1);
  for (const auto &result : results) {
    EXPECT_TRUE(result.status_code == 200 || result.status_code == 202);
    EXPECT_EQ(result.document.at("job_id"), job_id);
  }
  EXPECT_EQ(fixture.controller->List().document.at("entries").size(), 1u);
  fixture.owner->Release();
}

TEST(FhssDashboardJobControllerTest,
     QueuedJobReplaysTheExactConfigurationSnapshotCapturedAtSubmission) {
  ControllerFixture fixture;
  const auto blocker = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "snapshot-blocker"}},
      "snapshot-blocker-key");
  ASSERT_EQ(blocker.status_code, 202);
  fixture.owner->WaitForRebuildCount(1);

  const auto queued = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "snapshot-under-test"}},
      "snapshot-under-test-key");
  ASSERT_EQ(queued.status_code, 202);
  EXPECT_EQ(
      queued.document.at("generation_result").at("availability").at("state"),
      "pending");
  EXPECT_TRUE(queued.document.at("generation_result").at("terminal").is_null());
  const auto captured_revision =
      queued.document.at("config_revision").get<std::uint64_t>();
  const auto captured_etag =
      queued.document.at("config_etag").get<std::string>();
  auto expected_graph =
      fixture.configuration->GetReceiverGraphResponse().at("graph");
  auto expected_source =
      std::ranges::find_if(expected_graph.at("nodes"), [](const auto &node) {
        return node.value("id", std::string{}) == "source";
      });
  ASSERT_NE(expected_source, expected_graph.at("nodes").end());
  const auto queued_job_id = queued.document.at("job_id").get<std::string>();
  (*expected_source)["node_config"]["file_path"] =
      (std::filesystem::weakly_canonical(fixture.root) / "fhss-jobs" /
       queued_job_id / "iq.cf32")
          .string();
  (*expected_source)["node_config"]["sample_format"] = "cf32_le";
  (*expected_source)["node_config"]["max_read_complex_samples"] =
      dsp::fhss::dashboard::FHSSJobController::kMaxIqSamples;

  const auto patch = fixture.configuration->ApplyJsonPatch(
      nlohmann::json::array({{{"op", "replace"},
                              {"path", "/iq_center_frequency_hz"},
                              {"value", 1'240'000'001.0}}}),
      fixture.configuration->ETag(), false);
  ASSERT_EQ(patch.at("status"), "applied");
  ASSERT_GT(fixture.configuration->ConfigRevision(), captured_revision);

  fixture.owner->Release();
  fixture.owner->WaitForRebuildCount(2);
  const auto snapshots = fixture.owner->RebuildSnapshots();
  ASSERT_EQ(snapshots.size(), 2u);
  const auto &queued_snapshot = snapshots.at(1);
  const auto actual_source = std::ranges::find_if(
      queued_snapshot.receiver_graph.at("nodes"), [](const auto &node) {
        return node.value("id", std::string{}) == "source";
      });
  ASSERT_NE(actual_source, queued_snapshot.receiver_graph.at("nodes").end());
  EXPECT_EQ(actual_source->at("node_config").at("file_path"),
            expected_source->at("node_config").at("file_path"))
      << "queued job " << queued_job_id;
  EXPECT_EQ(queued_snapshot.config_revision, captured_revision);
  EXPECT_EQ(queued_snapshot.config_etag, captured_etag);
  EXPECT_EQ(queued_snapshot.receiver_graph, expected_graph)
      << nlohmann::json::diff(expected_graph, queued_snapshot.receiver_graph)
             .dump(2);
  EXPECT_NE(queued_snapshot.receiver_graph,
            fixture.configuration->GetReceiverGraphResponse().at("graph"));
}

TEST(FhssDashboardJobControllerTest,
     CancelDuringBlockedRebuildNeverStartsReceiverAndIsWriteOnce) {
  ControllerFixture fixture;
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "cancel-rebuild"}},
      "cancel-rebuild-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  fixture.owner->WaitForRebuildCount(1);

  const auto accepted = fixture.controller->Cancel(job_id);
  ASSERT_EQ(accepted.status_code, 202);
  EXPECT_EQ(accepted.document.at("state"), "cancelling");
  fixture.owner->Release();

  nlohmann::json terminal;
  for (std::size_t attempt = 0; attempt < 10'000; ++attempt) {
    terminal = fixture.controller->Get(job_id).document;
    if (dsp::fhss::dashboard::FHSSJobController::IsTerminal(
            terminal.at("state").get<std::string>()))
      break;
    std::this_thread::yield();
  }
  ASSERT_EQ(terminal.at("state"), "cancelled");
  EXPECT_EQ(terminal.at("generation_result").at("terminal").at("status"),
            "succeeded");
  EXPECT_EQ(terminal.at("terminal").at("code"),
            "cancelled_before_receiver_start");
  EXPECT_EQ(fixture.owner->StartCount(), 0u);
  const auto repeated = fixture.controller->Cancel(job_id);
  EXPECT_EQ(repeated.status_code, 200);
  EXPECT_EQ(repeated.document.at("terminal"), terminal.at("terminal"));
}

TEST(FhssDashboardJobControllerTest,
     CancelAtArtifactCommitSeamHasGenerationAndTerminalPrecedence) {
  auto barrier = std::make_shared<BlockingHook>();
  auto hooks =
      std::make_shared<dsp::fhss::dashboard::FHSSJobController::TestHooks>();
  hooks->after_artifact_commit = [barrier] { barrier->Enter(); };
  ControllerFixture fixture(hooks);
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "cancel-artifact-commit"}},
      "cancel-artifact-commit-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  barrier->Wait();
  const auto accepted = fixture.controller->Cancel(job_id);
  ASSERT_EQ(accepted.status_code, 202);
  barrier->Release();
  const auto terminal = WaitForTerminal(*fixture.controller, job_id);
  ASSERT_EQ(terminal.at("state"), "cancelled");
  EXPECT_NE(terminal.at("terminal").at("code"), "illegal_state_transition");
  EXPECT_EQ(terminal.at("generation_result").at("terminal").at("status"),
            "cancelled");
  EXPECT_EQ(fixture.owner->StartCount(), 0u);
}

TEST(FhssDashboardJobControllerTest,
     AcceptedRunningCancellationWinsConcurrentCompletion) {
  auto barrier = std::make_shared<BlockingHook>();
  auto hooks =
      std::make_shared<dsp::fhss::dashboard::FHSSJobController::TestHooks>();
  hooks->after_running = [barrier] { barrier->Enter(); };
  ControllerFixture fixture(hooks);
  fixture.owner->SetAutoComplete(false);
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "cancel-completion-race"}},
      "cancel-completion-race-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  fixture.owner->WaitForRebuildCount(1);
  fixture.owner->Release();
  fixture.owner->WaitForStart();
  barrier->Wait();
  const auto accepted = fixture.controller->Cancel(job_id);
  ASSERT_EQ(accepted.status_code, 202);
  fixture.owner->Complete();
  barrier->Release();
  const auto terminal = WaitForTerminal(*fixture.controller, job_id);
  ASSERT_EQ(terminal.at("state"), "cancelled");
  EXPECT_NE(terminal.at("terminal").at("code"), "illegal_state_transition");
  EXPECT_EQ(fixture.owner->StartCount(), 1u);
  const auto repeated = fixture.controller->Cancel(job_id);
  EXPECT_EQ(repeated.status_code, 200);
  EXPECT_EQ(repeated.document.at("terminal"), terminal.at("terminal"));
}

TEST(FhssDashboardJobControllerTest,
     TimeoutDuringBlockedRebuildNeverStartsReceiver) {
  ControllerFixture fixture;
  const auto submitted =
      fixture.controller->Submit({{"operation", "step"},
                                  {"request_id", "timeout-rebuild"},
                                  {"timeout_ms", 2'000}},
                                 "timeout-rebuild-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  fixture.owner->WaitForRebuildCount(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(2'100));
  fixture.owner->Release();

  nlohmann::json terminal;
  for (std::size_t attempt = 0; attempt < 10'000; ++attempt) {
    terminal = fixture.controller->Get(job_id).document;
    if (dsp::fhss::dashboard::FHSSJobController::IsTerminal(
            terminal.at("state").get<std::string>()))
      break;
    std::this_thread::yield();
  }
  ASSERT_EQ(terminal.at("state"), "timed_out");
  EXPECT_EQ(terminal.at("terminal").at("code"), "job_timeout");
  EXPECT_EQ(fixture.owner->StartCount(), 0u);
}

TEST(FhssDashboardJobControllerTest, StartupRetentionIsCountAgeAndSymlinkSafe) {
  const auto root = UniqueTemp("graphx-dashboard-retention");
  const auto artifact_root = root / "fhss-jobs";
  std::filesystem::create_directories(artifact_root);
  for (std::size_t index = 0;
       index < dsp::fhss::dashboard::FHSSJobController::kMaxJobs + 3; ++index)
    SeedRetainedArtifact(root, index);
  const auto expired = artifact_root / RetainedJobName(0);
  std::filesystem::last_write_time(
      expired,
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
  const auto incomplete = artifact_root / RetainedJobName(100);
  std::filesystem::create_directories(incomplete);
  std::ofstream(incomplete / "partial.tmp") << "partial";

  const auto outside = UniqueTemp("graphx-dashboard-outside");
  std::filesystem::create_directories(outside);
  const auto sentinel = outside / "sentinel.tmp";
  std::ofstream(sentinel) << "must survive";
  const auto link = artifact_root / RetainedJobName(101);
  std::filesystem::create_directory_symlink(outside, link);

  auto configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  auto owner = std::make_shared<BlockingRuntimeOwner>();
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  runtime->MarkReady();
  {
    dsp::fhss::dashboard::FHSSJobController controller(configuration, runtime,
                                                       root);
    std::size_t retained = 0;
    for (const auto &entry : std::filesystem::directory_iterator(artifact_root))
      if (entry.path().filename().string().starts_with("j-") &&
          entry.symlink_status().type() ==
              std::filesystem::file_type::directory)
        ++retained;
    EXPECT_LE(retained, dsp::fhss::dashboard::FHSSJobController::kMaxJobs);
    EXPECT_FALSE(std::filesystem::exists(expired));
    EXPECT_FALSE(std::filesystem::exists(incomplete));
    EXPECT_FALSE(std::filesystem::exists(link));
    EXPECT_TRUE(std::filesystem::is_regular_file(sentinel));
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::remove_all(outside, ignored);
}

TEST(FhssDashboardJobControllerTest, StartupRetentionIsByteBounded) {
  const auto root = UniqueTemp("graphx-dashboard-retention-bytes");
  for (std::size_t index = 0; index < 9; ++index)
    SeedRetainedArtifact(root, index,
                         dsp::fhss::dashboard::FHSSJobController::kMaxIqBytes);
  auto configuration =
      std::make_shared<graph::dashboard::GraphConfigurationService>(
          LoadJson(DSP_FHSS_CHANNELIZED_CONFIG_PATH),
          std::make_shared<
              dsp::fhss::dashboard::FHSSDashboardConfigurationPolicy>());
  auto owner = std::make_shared<BlockingRuntimeOwner>();
  auto runtime = std::make_shared<graph::dashboard::GraphRuntimeSession>(owner);
  runtime->MarkReady();
  {
    dsp::fhss::dashboard::FHSSJobController controller(configuration, runtime,
                                                       root);
    std::uintmax_t retained_bytes = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator(root / "fhss-jobs"))
      if (entry.symlink_status().type() ==
          std::filesystem::file_type::directory)
        for (const auto &artifact :
             std::filesystem::directory_iterator(entry.path()))
          if (artifact.is_regular_file())
            retained_bytes += artifact.file_size();
    EXPECT_LE(
        retained_bytes,
        dsp::fhss::dashboard::FHSSJobController::kMaxRetainedArtifactBytes);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

TEST(FhssDashboardJobControllerTest,
     ResetRejectsActiveThenAdvancesEpochAndRetainsHistory) {
  ControllerFixture fixture;
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "reset-1"}}, "reset-key");
  ASSERT_EQ(submitted.status_code, 202);
  fixture.owner->WaitForRebuild();
  EXPECT_EQ(fixture.controller->Reset().status_code, 409);
  fixture.owner->Release();
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  for (std::size_t attempt = 0; attempt < 10'000; ++attempt) {
    if (dsp::fhss::dashboard::FHSSJobController::IsTerminal(
            fixture.controller->Get(job_id)
                .document.at("state")
                .get<std::string>()))
      break;
    std::this_thread::yield();
  }
  const auto before_epoch =
      fixture.controller->Get(job_id).document.at("controller_epoch");
  const auto reset = fixture.controller->Reset();
  ASSERT_EQ(reset.status_code, 200);
  EXPECT_NE(reset.document.at("controller_epoch"), before_epoch);
  EXPECT_EQ(fixture.controller->Get(job_id).status_code, 200);
}

TEST(FhssDashboardJobControllerTest,
     RestartDoesNotResumeJobsAndRetainsCommittedReplayArtifacts) {
  ControllerFixture fixture;
  const auto submitted = fixture.controller->Submit(
      {{"operation", "step"}, {"request_id", "restart-job"}}, "restart-key");
  ASSERT_EQ(submitted.status_code, 202);
  const auto job_id = submitted.document.at("job_id").get<std::string>();
  fixture.owner->WaitForRebuild();
  fixture.owner->Release();
  nlohmann::json terminal;
  for (int attempt = 0; attempt < 200; ++attempt) {
    terminal = fixture.controller->Get(job_id).document;
    if (dsp::fhss::dashboard::FHSSJobController::IsTerminal(
            terminal.at("state").get<std::string>()))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(dsp::fhss::dashboard::FHSSJobController::IsTerminal(
      terminal.at("state").get<std::string>()));
  const auto committed = fixture.root / "fhss-jobs" / job_id / "manifest.json";
  ASSERT_TRUE(std::filesystem::is_regular_file(committed));
  const auto old_epoch = terminal.at("controller_epoch").get<std::uint64_t>();

  fixture.controller.reset();
  fixture.controller =
      std::make_unique<dsp::fhss::dashboard::FHSSJobController>(
          fixture.configuration, fixture.runtime, fixture.root);
  const auto restarted = fixture.controller->List().document;
  EXPECT_TRUE(restarted.at("entries").empty());
  EXPECT_NE(restarted.at("controller_epoch").get<std::uint64_t>(), old_epoch);
  EXPECT_TRUE(std::filesystem::is_regular_file(committed));
}

TEST(FhssDashboardJobControllerTest,
     ProductionApiRoutesExposeStrictJobResourcesAndRfc9457Conflicts) {
  ControllerFixture fixture;
  auto shared_controller =
      std::shared_ptr<dsp::fhss::dashboard::FHSSJobController>(
          fixture.controller.release());
  const auto handler = dsp::fhss::dashboard::MakeApiHandler(
      fixture.configuration, fixture.runtime, shared_controller);
  const graph::dashboard::EmbeddedDashboardServer::ApiContext context{
      .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5)};
  const auto request = [&](std::string method, std::string path,
                           nlohmann::json body, std::string key) {
    return handler({.method = std::move(method),
                    .path = std::move(path),
                    .body = body.dump(),
                    .headers = {{"idempotency-key", std::move(key)}}},
                   context);
  };
  const auto created = request("POST", "/api/v1/fhss/commands/step",
                               {{"request_id", "api-step"}}, "api-key");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status_code, 202);
  const auto created_json = nlohmann::json::parse(created->body);
  const auto duplicate = request("POST", "/api/v1/fhss/commands/step",
                                 {{"request_id", "api-step"}}, "api-key");
  ASSERT_TRUE(duplicate);
  EXPECT_EQ(duplicate->status_code, 200);
  EXPECT_EQ(nlohmann::json::parse(duplicate->body).at("job_id"),
            created_json.at("job_id"));
  const auto conflict =
      request("POST", "/api/v1/fhss/commands/continue",
              {{"request_id", "api-step"}, {"message_count", 2}}, "api-key");
  ASSERT_TRUE(conflict);
  EXPECT_EQ(conflict->status_code, 409);
  EXPECT_EQ(conflict->content_type, "application/problem+json");
  EXPECT_EQ(nlohmann::json::parse(conflict->body).at("status"), 409);
  const auto history =
      handler({.method = "GET", .path = "/api/v1/fhss/jobs"}, context);
  ASSERT_TRUE(history);
  EXPECT_EQ(history->status_code, 200);
  const auto history_json = nlohmann::json::parse(history->body);
  EXPECT_EQ(history_json.at("entries").size(), 1u);
  constexpr std::uint64_t kMaxJsonSafeInteger = (std::uint64_t{1} << 53) - 1;
  EXPECT_LE(history_json.at("controller_epoch").get<std::uint64_t>(),
            kMaxJsonSafeInteger);
  (void)shared_controller->Cancel(created_json.at("job_id").get<std::string>());
  fixture.owner->Release();
  shared_controller->Shutdown();
}

} // namespace
