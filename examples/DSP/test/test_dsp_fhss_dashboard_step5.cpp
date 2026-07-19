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

class BlockingRuntimeOwner final : public graph::dashboard::IGraphRuntimeOwner {
public:
  Result Rebuild(std::uint64_t, const BuildSnapshot &snapshot) override {
    {
      std::lock_guard lock(mutex_);
      receiver_graph_ = snapshot.receiver_graph;
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
    if (callback_)
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
  void WaitForRebuild() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return rebuild_entered_; });
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

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool rebuild_entered_ = false;
  bool release_rebuild_ = false;
  nlohmann::json receiver_graph_;
  std::shared_ptr<graph::GraphManager> manager_;
  CompletionCallback callback_;
  std::atomic<std::uint64_t> start_count_{0};
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

  ControllerFixture() {
    runtime->MarkReady();
    controller = std::make_unique<dsp::fhss::dashboard::FHSSJobController>(
        configuration, runtime, root);
  }
  ~ControllerFixture() {
    controller.reset();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

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
  EXPECT_EQ(nlohmann::json::parse(history->body).at("entries").size(), 1u);
  (void)shared_controller->Cancel(created_json.at("job_id").get<std::string>());
  fixture.owner->Release();
  shared_controller->Shutdown();
}

} // namespace
