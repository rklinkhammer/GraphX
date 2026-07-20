// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace graph::dashboard {
class GraphConfigurationService;
class GraphRuntimeSession;
} // namespace graph::dashboard

namespace dsp::fhss::dashboard {
class FHSSObservationService;

class FHSSJobController {
public:
  static constexpr std::size_t kMaxJobs = 32;
  static constexpr std::size_t kMaxIdempotencyEntries = 64;
  static constexpr std::size_t kMaxMessagesPerJob = 4;
  static constexpr std::size_t kMaxPulsesPerJob = 512;
  static constexpr std::size_t kMaxIqSamples = 4'194'304;
  static constexpr std::size_t kMaxIqBytes = 64u * 1024u * 1024u;
  static constexpr std::size_t kMaxRetainedArtifactBytes = 512u * 1024u * 1024u;
  static constexpr std::size_t kMaxMetadataBytes = 1u * 1024u * 1024u;
  static constexpr std::size_t kMaxHistoryBytes = 2u * 1024u * 1024u;
  static constexpr std::chrono::hours kMaxRetentionAge{1};
  static constexpr std::chrono::milliseconds kMinTimeout{100};
  static constexpr std::chrono::milliseconds kMaxTimeout{120'000};

  struct Result {
    int status_code = 200;
    nlohmann::json document;
  };

  struct TestHooks {
    std::function<void()> after_artifact_commit;
    std::function<void()> after_running;
  };

  using EventSink = std::function<void(std::string, nlohmann::json,
                                       nlohmann::json)>;

  FHSSJobController(
      std::shared_ptr<graph::dashboard::GraphConfigurationService>
          configuration_service,
      std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
      std::filesystem::path artifact_root,
      std::shared_ptr<TestHooks> test_hooks = nullptr);
  ~FHSSJobController();

  FHSSJobController(const FHSSJobController &) = delete;
  FHSSJobController &operator=(const FHSSJobController &) = delete;

  [[nodiscard]] Result Submit(const nlohmann::json &request,
                              std::string_view idempotency_key);
  [[nodiscard]] Result Get(std::string_view job_id) const;
  [[nodiscard]] Result List() const;
  [[nodiscard]] Result Cancel(std::string_view job_id);
  [[nodiscard]] Result Reset();
  void SetEventSink(EventSink sink);
  void Shutdown();

  [[nodiscard]] static bool IsTerminal(std::string_view state);
  [[nodiscard]] static bool IsLegalTransition(std::string_view from,
                                              std::string_view to);

private:
  struct Job;
  struct IdempotencyRecord;
  struct RetainedArtifact {
    std::filesystem::path path;
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified_at{};
  };

  void Worker(std::stop_token stop_token);
  void Process(const std::shared_ptr<Job> &job, std::stop_token stop_token);
  void Transition(const std::shared_ptr<Job> &job, std::string state);
  void Terminal(const std::shared_ptr<Job> &job, std::string state,
                std::string code, std::string detail);
  [[nodiscard]] nlohmann::json JobJson(const Job &job) const;
  [[nodiscard]] static std::string NowRfc3339();
  [[nodiscard]] static std::string Canonical(const nlohmann::json &request);
  [[nodiscard]] static std::string Digest(std::string_view value);
  void PurgeUnlocked();
  void ReconcileArtifactsAtStartup();
  void RemoveOldestRetainedUnlocked();

  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<FHSSObservationService> observation_service_;
  std::filesystem::path artifact_root_;
  std::shared_ptr<TestHooks> test_hooks_;
  mutable std::recursive_mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<std::shared_ptr<Job>> queue_;
  std::deque<std::shared_ptr<Job>> jobs_;
  std::unordered_map<std::string, IdempotencyRecord> idempotency_;
  std::deque<RetainedArtifact> retained_artifacts_;
  std::uintmax_t retained_artifact_bytes_ = 0;
  std::jthread worker_;
  std::uint64_t controller_epoch_ = 0;
  std::uint64_t next_job_sequence_ = 1;
  std::size_t message_cursor_ = 0;
  std::weak_ptr<Job> active_job_;
  bool shutting_down_ = false;
  EventSink event_sink_;
};

} // namespace dsp::fhss::dashboard
