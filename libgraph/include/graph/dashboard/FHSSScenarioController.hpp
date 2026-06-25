// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/FHSSStepping.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace graph::dashboard {

class GraphConfigurationService;
class GraphRuntimeSession;

class FHSSScenarioController {
public:
  struct CommandResponse {
    int status_code = 200;
    nlohmann::json body;
  };

  FHSSScenarioController(std::shared_ptr<GraphConfigurationService> configuration_service,
                         std::shared_ptr<GraphRuntimeSession> runtime_session);

  void BindInjectionSource(std::shared_ptr<IFHSSMessageInjectionSource> source);

  [[nodiscard]] CommandResponse StepOneMessage(const nlohmann::json &request);
  [[nodiscard]] CommandResponse Continue(const nlohmann::json &request);
  [[nodiscard]] CommandResponse Reset(const nlohmann::json &request);

  [[nodiscard]] std::optional<nlohmann::json>
  GetOperationResponseIfKnown(const std::string &operation_id) const;
  [[nodiscard]] std::optional<nlohmann::json>
  CancelOperationIfKnown(const std::string &operation_id);
  [[nodiscard]] std::optional<bool>
  DeleteOperationIfKnown(const std::string &operation_id, std::string *error_code);

  void PublishTerminalResultForTesting(const FHSSMessageTerminalResult &result);
  void SetAutoCompleteForTesting(bool enabled);
  [[nodiscard]] std::optional<FHSSMessageCorrelation> ActiveCorrelationForTesting() const;

private:
  struct OperationRecord {
    std::string operation_id;
    std::string command_id;
    std::string request_fingerprint;
    std::string status;
    std::uint64_t submitted_revision = 0;
    nlohmann::json result = nullptr;
    bool terminal = false;
    std::string created_at;
    std::string completed_at;
    std::string expires_at;
    std::chrono::system_clock::time_point created_at_time{};
    std::chrono::system_clock::time_point completed_at_time{};
    std::chrono::system_clock::time_point expires_at_time{};
  };

  struct PendingRelease {
    std::string operation_id;
    FHSSMessageCorrelation correlation;
  };

  [[nodiscard]] static std::string NowIso8601();
  [[nodiscard]] static std::chrono::system_clock::time_point Now();
  [[nodiscard]] static std::string ToIso8601(std::chrono::system_clock::time_point time_point);
  [[nodiscard]] static std::string NormalizeJson(const nlohmann::json &json);
  [[nodiscard]] static std::string CorrelationKey(const FHSSMessageCorrelation &correlation);
  [[nodiscard]] static nlohmann::json ErrorBody(int status_code,
                                                const std::string &code,
                                                const std::string &message,
                                                nlohmann::json details = nullptr);
  [[nodiscard]] static std::string TerminalStatusToString(FHSSMessageTerminalStatus status);

  [[nodiscard]] nlohmann::json BuildOperationJson(const OperationRecord &operation) const;
  [[nodiscard]] std::optional<std::size_t> FindOperationIndex(const std::string &operation_id) const;
  [[nodiscard]] std::optional<nlohmann::json>
  ReplayIfIdempotent(const nlohmann::json &request,
                     const std::string &normalized_fingerprint) const;
  [[nodiscard]] std::string RegisterOperation(OperationRecord operation);
  void PurgeExpiredOperationsLocked() const;
  void CompleteOperationLocked(const std::string &operation_id,
                               const std::string &status,
                               nlohmann::json result);
  void FailOperationLocked(const std::string &operation_id,
                           const std::string &code,
                           const std::string &message,
                           nlohmann::json details = nullptr);
  [[nodiscard]] std::optional<nlohmann::json>
  ScheduleNextMessageLocked(const std::string &operation_id,
                            std::uint64_t submitted_revision,
                            bool continue_mode);
  void MaybeAutoComplete(const FHSSMessageCorrelation &correlation);
  [[nodiscard]] std::vector<nlohmann::json> CurrentMessages() const;
  void ResetScenarioLifecycleLocked();

  std::shared_ptr<GraphConfigurationService> configuration_service_;
  std::shared_ptr<GraphRuntimeSession> runtime_session_;
  std::shared_ptr<IFHSSMessageInjectionSource> source_;

  mutable std::mutex mutex_;
  mutable std::deque<OperationRecord> operations_;
  mutable std::unordered_map<std::string, std::string> command_index_;
  mutable std::size_t operation_retention_count_ = 1000;
  mutable std::uint64_t next_operation_id_ = 1;

  std::string scenario_id_;
  std::uint64_t next_scenario_id_ = 1;
  std::uint64_t next_release_sequence_ = 1;
  std::size_t next_message_index_ = 0;
  bool continue_mode_ = false;
  bool auto_complete_for_testing_ = false;
  std::string continue_operation_id_;
  std::uint64_t continue_completed_messages_ = 0;
  std::optional<PendingRelease> active_release_;
  std::unordered_map<std::string, PendingRelease> pending_by_correlation_;
  std::uint64_t duplicate_terminal_results_ = 0;
  std::uint64_t orphan_terminal_results_ = 0;
};

} // namespace graph::dashboard