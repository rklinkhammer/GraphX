// SPDX-License-Identifier: MIT
#pragma once

#include "FHSSJobController.hpp"

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
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <atomic>

#include <nlohmann/json.hpp>

namespace graph::dashboard {
class GraphConfigurationService;
class GraphRuntimeSession;
} // namespace graph::dashboard

namespace dsp::fhss::dashboard {
class FHSSObservationService;

class FHSSInvestigationBundleService {
public:
  static constexpr std::size_t kMaxOperations = 32;
  static constexpr std::size_t kMaxBundles = 32;
  static constexpr std::size_t kMaxArtifacts = 64;
  static constexpr std::size_t kMaxJsonBytes = 1u * 1024u * 1024u;
  static constexpr std::uint64_t kMaxCopiedIqBytes = 64u * 1024u * 1024u;
  static constexpr std::uint64_t kMaxReferencedIqBytes = 512u * 1024u * 1024u;
  static constexpr std::uint64_t kMaxRetainedBundleBytes =
      512u * 1024u * 1024u;
  static constexpr std::size_t kChunkBytes = 256u * 1024u;
  static constexpr std::size_t kMaxPathBytes = 240;
  static constexpr std::size_t kMaxComponentBytes = 64;
  static constexpr std::size_t kMaxAnnotations = 128;
  static constexpr std::chrono::milliseconds kDefaultTimeout{30'000};
  static constexpr std::chrono::milliseconds kMaxTimeout{120'000};
  static constexpr std::chrono::milliseconds kCheckpointBound{100};

  struct Result {
    int status_code = 200;
    nlohmann::json document;
  };

  struct TestHooks {
    std::function<std::optional<FHSSJobController::InvestigationSource>(
        std::string_view)> source_lookup;
    std::function<void()> before_processing;
    std::function<void()> after_hashing;
    std::function<void()> after_bundle_artifacts_hashed;
    std::function<void()> after_copying;
    std::function<void()> executable_hash_checkpoint;
    std::function<void()> before_publish;
    std::function<void()> after_publish_rename;
    std::function<void()> before_replay_construction;
    std::function<void()> source_identity_change;
    bool inject_enospc = false;
    // Startup-only qualification profile used by the external operator. It is
    // deliberately not reachable from an HTTP request.
    bool qualification_sequence = false;
  };

  FHSSInvestigationBundleService(
      std::shared_ptr<graph::dashboard::GraphConfigurationService>
          configuration_service,
      std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
      std::shared_ptr<FHSSJobController> job_controller,
      std::filesystem::path artifact_root,
      std::shared_ptr<TestHooks> test_hooks = nullptr);
  ~FHSSInvestigationBundleService();

  FHSSInvestigationBundleService(const FHSSInvestigationBundleService &) =
      delete;
  FHSSInvestigationBundleService &
  operator=(const FHSSInvestigationBundleService &) = delete;

  [[nodiscard]] Result SubmitExport(const nlohmann::json &request,
                                    std::string_view idempotency_key);
  [[nodiscard]] Result SubmitValidation(const nlohmann::json &request,
                                        std::string_view idempotency_key);
  [[nodiscard]] Result SubmitReplay(const nlohmann::json &request,
                                    std::string_view idempotency_key);
  [[nodiscard]] Result Get(std::string_view operation_id) const;
  [[nodiscard]] Result List() const;
  [[nodiscard]] Result Cancel(std::string_view operation_id);
  [[nodiscard]] Result Quota() const;
  void Shutdown();

  [[nodiscard]] static bool ContainsForbiddenReceiverKey(
      const nlohmann::json &value);
  [[nodiscard]] static bool ValidateSigMf(const nlohmann::json &metadata,
                                          std::uint64_t iq_bytes,
                                          std::string_view iq_sha512,
                                          std::string *error = nullptr);
  [[nodiscard]] static std::string CanonicalJson(
      const nlohmann::json &document);
  [[nodiscard]] static std::string SemanticReceiverResultHash(
      const nlohmann::json &receiver_result);

private:
  struct Operation;
  struct IdempotencyRecord;
  struct ValidatedBundle;

  [[nodiscard]] Result Submit(std::string kind, const nlohmann::json &request,
                              std::string_view idempotency_key);
  void Worker(std::stop_token stop_token);
  void Process(const std::shared_ptr<Operation> &operation,
               std::stop_token stop_token);
  void Export(const std::shared_ptr<Operation> &operation,
              std::stop_token stop_token);
  [[nodiscard]] ValidatedBundle
  ValidateBundle(const std::shared_ptr<Operation> &operation,
                 std::stop_token stop_token);
  void Replay(const std::shared_ptr<Operation> &operation,
              std::stop_token stop_token);
  void Transition(const std::shared_ptr<Operation> &operation,
                  std::string state);
  void Terminal(const std::shared_ptr<Operation> &operation, std::string state,
                std::string code, std::string detail,
                nlohmann::json result = nullptr);
  [[nodiscard]] nlohmann::json OperationJson(const Operation &operation) const;
  [[nodiscard]] bool CancelledOrTimedOut(const Operation &operation,
                                         std::stop_token stop_token) const;
  void PurgeUnlocked();

  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
  std::shared_ptr<FHSSJobController> job_controller_;
  std::shared_ptr<FHSSObservationService> observation_service_;
  std::filesystem::path artifact_root_;
  std::filesystem::path bundle_root_;
  std::filesystem::path iq_root_;
  std::shared_ptr<TestHooks> test_hooks_;
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  std::deque<std::shared_ptr<Operation>> queue_;
  std::deque<std::shared_ptr<Operation>> operations_;
  std::unordered_map<std::string, IdempotencyRecord> idempotency_;
  std::jthread worker_;
  std::weak_ptr<Operation> active_operation_;
  std::uint64_t next_sequence_ = 1;
  std::atomic<std::uint64_t> qualification_export_sequence_{0};
  bool shutting_down_ = false;
};

} // namespace dsp::fhss::dashboard
