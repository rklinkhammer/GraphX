// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

namespace graph::dashboard {

class GraphConfigurationService {
public:
  explicit GraphConfigurationService(nlohmann::json effective_graph);

  struct ValidationError {
    std::string level;
    std::string node_id;
    std::string pointer;
    std::string code;
    std::string message;
    std::string generated_target_pointer = {};
    std::string authoritative_pointer = {};
    nlohmann::json details = nullptr;
    bool retriable = false;
  };

  struct ValidationResult {
    bool valid = false;
    std::vector<std::string> levels;
    std::vector<ValidationError> errors;
  };

  struct OperationRecord {
    std::string operation_id;
    std::string command_id;
    std::string request_fingerprint;
    std::string status;
    std::uint64_t submitted_revision = 0;
    nlohmann::json result = nullptr;
    ValidationResult validation;
    std::string created_at;
    std::string completed_at;
    std::string expires_at;
    std::chrono::system_clock::time_point created_at_time{};
    std::chrono::system_clock::time_point completed_at_time{};
    std::chrono::system_clock::time_point expires_at_time{};
    bool terminal = false;
  };

  using ValidationInjector = std::function<std::optional<ValidationError>(
      const nlohmann::json &request)>;
  using UpdateEventSink = std::function<void(const nlohmann::json &event)>;

  [[nodiscard]] bool IsValid() const;
  [[nodiscard]] const std::string &Owner() const;
  [[nodiscard]] std::uint64_t ConfigRevision() const;

  [[nodiscard]] nlohmann::json GetGraphResponse() const;
  [[nodiscard]] nlohmann::json GetConfigResponse() const;
  [[nodiscard]] nlohmann::json GetScenarioResponse() const;
  [[nodiscard]] nlohmann::json GetDerivedPathsResponse() const;
  [[nodiscard]] nlohmann::json GetValueResponse(const std::string &pointer) const;
  [[nodiscard]] nlohmann::json GetNodeResponse(const std::string &node_id) const;
  [[nodiscard]] nlohmann::json GetNodeParametersResponse(const std::string &node_id) const;

  [[nodiscard]] nlohmann::json PatchConfig(const nlohmann::json &request);
  [[nodiscard]] nlohmann::json ValidateConfig(const nlohmann::json &request) const;
  [[nodiscard]] nlohmann::json UndoLastEdit();
  [[nodiscard]] nlohmann::json DiscardEdits();
  [[nodiscard]] nlohmann::json ExportConfig(const nlohmann::json &request);

  [[nodiscard]] nlohmann::json GetOperationResponse(const std::string &operation_id) const;
  [[nodiscard]] nlohmann::json CancelOperation(const std::string &operation_id);
  [[nodiscard]] bool DeleteOperation(const std::string &operation_id, std::string *error_code = nullptr);

  void ExpireOperationForTesting(const std::string &operation_id);
  void SetArtifactRoot(std::filesystem::path artifact_root);
  void SetValidationInjectorForTesting(ValidationInjector injector);
  void SetUpdateEventSinkForTesting(UpdateEventSink sink);

  [[nodiscard]] static nlohmann::json GeneratedPathsJson();
  [[nodiscard]] static nlohmann::json ExtractScenario(const nlohmann::json &effective_graph);
  [[nodiscard]] static nlohmann::json DeriveEffectiveGraph(const nlohmann::json &base_graph,
                                                           const nlohmann::json &scenario);
  [[nodiscard]] static ValidationResult ValidateScenario(const nlohmann::json &scenario);

private:
  struct PendingChange {
    std::string request_fingerprint;
    nlohmann::json scenario;
    ValidationResult validation;
    std::vector<std::string> regenerated_targets;
  };

  [[nodiscard]] static std::string NormalizeJson(const nlohmann::json &json);
  [[nodiscard]] static nlohmann::json BuildErrorResponse(int status_code,
                                                         std::string code,
                                                         std::string message,
                                                         nlohmann::json details = nullptr,
                                                         std::string command_id = {},
                                                         std::uint64_t revision = 0,
                                                         std::string pointer = {});
  [[nodiscard]] static nlohmann::json BuildValidationJson(const ValidationResult &validation);
  [[nodiscard]] static ValidationResult MakeValidationResult(bool valid,
                                                            std::vector<std::string> levels,
                                                            std::vector<ValidationError> errors = {});
  [[nodiscard]] static std::string NowIso8601();
  [[nodiscard]] static std::chrono::system_clock::time_point Now();

  [[nodiscard]] bool IsGeneratedPath(const std::string &pointer) const;
  [[nodiscard]] bool IsWritablePath(const std::string &pointer) const;
  [[nodiscard]] std::string NormalizeScenarioPointer(const std::string &pointer) const;
  [[nodiscard]] nlohmann::json ApplyScenarioRequest(const nlohmann::json &request,
                                                   bool commit);
  [[nodiscard]] nlohmann::json CommitScenarioChange(const PendingChange &change,
                                                    const std::string &command_id,
                                                    std::uint64_t old_revision,
                                                    bool rebuild_required = true);
  [[nodiscard]] nlohmann::json ValidateAndSummarize(const nlohmann::json &scenario,
                                                    std::vector<std::string> *regenerated_targets) const;
  [[nodiscard]] std::optional<ValidationError> ValidateWithInjector(const nlohmann::json &request) const;
  [[nodiscard]] nlohmann::json BuildOperationJson(const OperationRecord &operation) const;
  [[nodiscard]] std::string RegisterOperation(OperationRecord operation);
  [[nodiscard]] std::optional<OperationRecord> FindOperation(const std::string &operation_id) const;
  void PurgeExpiredOperationsUnlocked() const;

  nlohmann::json effective_graph_;
  nlohmann::json scenario_;
  nlohmann::json committed_scenario_;
  nlohmann::json staged_scenario_;
  std::vector<nlohmann::json> undo_stack_;
  std::string owner_ = "GraphConfigurationService";
  std::uint64_t revision_ = 1;
  std::filesystem::path artifact_root_ = std::filesystem::temp_directory_path();
  mutable std::vector<OperationRecord> operations_;
  mutable std::unordered_map<std::string, std::string> command_index_;
  mutable ValidationInjector validation_injector_;
  mutable UpdateEventSink update_event_sink_;
  mutable std::uint64_t next_operation_id_ = 1;
  mutable std::chrono::system_clock::time_point operation_retention_ =
      std::chrono::system_clock::now() + std::chrono::hours(24);
  mutable std::size_t operation_retention_count_ = 1000;
  mutable std::mutex mutex_;
};

} // namespace graph::dashboard
