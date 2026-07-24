// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphConfigurationService.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace graph::dashboard {
namespace {

constexpr std::uint64_t kMaximumJsonSafeInteger = 9'007'199'254'740'991ULL;

std::string ToIso8601(const std::chrono::system_clock::time_point &time_point) {
  const auto now = std::chrono::system_clock::to_time_t(time_point);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

const nlohmann::json *FindNodeById(const nlohmann::json &graph,
                                   const std::string &node_id) {
  if (!graph.contains("nodes") || !graph["nodes"].is_array()) {
    return nullptr;
  }
  for (const auto &node : graph["nodes"]) {
    if (node.contains("id") && node["id"] == node_id) {
      return &node;
    }
  }
  return nullptr;
}

class IdentityConfigurationPolicy final : public GraphConfigurationPolicy {
public:
  nlohmann::json
  ExtractAuthoritative(const nlohmann::json &document) const override {
    return document;
  }
  nlohmann::json
  DeriveEffective(const nlohmann::json &base,
                  const nlohmann::json &authoritative) const override {
    (void)base;
    return authoritative;
  }
  nlohmann::json Validate(const nlohmann::json &authoritative) const override {
    const bool valid = authoritative.is_object();
    nlohmann::json errors = nlohmann::json::array();
    if (!valid) {
      errors.push_back(
          {{"level", "structure"},
           {"node_id", "document"},
           {"pointer", ""},
           {"code", "document_not_object"},
           {"message", "configuration document must be an object"}});
    }
    return {{"valid", valid},
            {"levels", {"syntax", "structure"}},
            {"errors", std::move(errors)}};
  }
  nlohmann::json GeneratedPaths() const override {
    return nlohmann::json::array();
  }
  nlohmann::json Provenance() const override { return nlohmann::json::array(); }
  nlohmann::json
  ReceiverMinimalGraph(const nlohmann::json &effective) const override {
    return effective;
  }
  std::optional<std::string>
  NormalizePatchPath(std::string_view path) const override {
    return std::string(path);
  }
  std::string AuthoritativeRootPointer() const override { return ""; }
};

bool IsUnderRoot(const std::filesystem::path &root,
                 const std::filesystem::path &candidate) {
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) {
    return false;
  }
  const auto canonical_candidate =
      std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return false;
  }
  return canonical_candidate.native().rfind(canonical_root.native(), 0) == 0;
}

} // namespace

GraphConfigurationService::GraphConfigurationService(
    nlohmann::json document,
    std::shared_ptr<const GraphConfigurationPolicy> policy)
    : effective_graph_(std::move(document)),
      policy_(policy ? std::move(policy)
                     : std::make_shared<IdentityConfigurationPolicy>()) {
  scenario_ = policy_->ExtractAuthoritative(effective_graph_);
  validation_ = ValidateAuthoritative(scenario_);
  committed_scenario_ = scenario_;
  staged_scenario_ = scenario_;
  effective_graph_ = policy_->DeriveEffective(effective_graph_, scenario_);
}

bool GraphConfigurationService::IsValid() const {
  std::lock_guard lock(configuration_mutex_);
  return effective_graph_.is_object() && effective_graph_.contains("nodes") &&
         effective_graph_["nodes"].is_array() &&
         effective_graph_.contains("edges") &&
         effective_graph_["edges"].is_array();
}

const std::string &GraphConfigurationService::Owner() const { return owner_; }

std::uint64_t GraphConfigurationService::ConfigRevision() const {
  std::lock_guard lock(configuration_mutex_);
  return revision_;
}

std::string GraphConfigurationService::ETag() const {
  std::lock_guard lock(configuration_mutex_);
  return "\"graphx-config-" + std::to_string(revision_) + "\"";
}

std::chrono::system_clock::time_point GraphConfigurationService::Now() {
  return std::chrono::system_clock::now();
}

std::string GraphConfigurationService::NowIso8601() { return ToIso8601(Now()); }

std::string
GraphConfigurationService::NormalizeJson(const nlohmann::json &json) {
  return json.dump();
}

nlohmann::json GraphConfigurationService::BuildErrorResponse(
    int status_code, std::string code, std::string message,
    nlohmann::json details, std::string command_id, std::uint64_t revision,
    std::string pointer) {
  nlohmann::json error{{"schema", "graphx.dashboard.error.v1"},
                       {"status", status_code},
                       {"code", std::move(code)},
                       {"message", std::move(message)},
                       {"details", std::move(details)},
                       {"request_id", NowIso8601()},
                       {"retriable", false}};
  if (!command_id.empty()) {
    error["command_id"] = std::move(command_id);
  }
  if (revision != 0) {
    error["revision"] = revision;
  }
  if (!pointer.empty()) {
    error["pointer"] = std::move(pointer);
  }
  return error;
}

nlohmann::json GraphConfigurationService::BuildValidationJson(
    const ValidationResult &validation) {
  nlohmann::json errors = nlohmann::json::array();
  for (const auto &error : validation.errors) {
    nlohmann::json record{{"level", error.level},
                          {"node_id", error.node_id},
                          {"pointer", error.pointer},
                          {"code", error.code},
                          {"message", error.message}};
    if (!error.generated_target_pointer.empty()) {
      record["generated_target_pointer"] = error.generated_target_pointer;
    }
    if (!error.authoritative_pointer.empty()) {
      record["authoritative_pointer"] = error.authoritative_pointer;
    }
    if (!error.details.is_null()) {
      record["details"] = error.details;
    }
    if (error.retriable) {
      record["retriable"] = true;
    }
    errors.push_back(std::move(record));
  }
  return nlohmann::json{{"valid", validation.valid},
                        {"levels", validation.levels},
                        {"errors", std::move(errors)}};
}

GraphConfigurationService::ValidationResult
GraphConfigurationService::MakeValidationResult(
    bool valid, std::vector<std::string> levels,
    std::vector<ValidationError> errors) {
  return ValidationResult{
      .valid = valid, .levels = std::move(levels), .errors = std::move(errors)};
}

GraphConfigurationService::ValidationResult
GraphConfigurationService::ValidateAuthoritative(
    const nlohmann::json &authoritative) const {
  nlohmann::json result;
  try {
    result = policy_->Validate(authoritative);
  } catch (const std::exception &error) {
    result = {{"valid", false},
              {"levels", {"syntax", "structure", "semantic"}},
              {"errors",
               nlohmann::json::array(
                   {{{"level", "structure"},
                     {"node_id", "document"},
                     {"pointer", policy_->AuthoritativeRootPointer()},
                     {"code", "invalid_configuration_type"},
                     {"message", error.what()}}})}};
  }
  std::vector<std::string> levels;
  std::vector<ValidationError> errors;
  for (const auto &level : result.value("levels", nlohmann::json::array())) {
    if (level.is_string())
      levels.push_back(level.get<std::string>());
  }
  for (const auto &item : result.value("errors", nlohmann::json::array())) {
    errors.push_back(
        {.level = item.value("level", std::string{"semantic"}),
         .node_id = item.value("node_id", std::string{"document"}),
         .pointer = item.value("pointer", std::string{}),
         .code = item.value("code", std::string{"invalid_configuration"}),
         .message =
             item.value("message", std::string{"configuration is invalid"}),
         .generated_target_pointer =
             item.value("generated_target_pointer", std::string{}),
         .authoritative_pointer =
             item.value("authoritative_pointer", std::string{}),
         .details = item.value("details", nlohmann::json(nullptr)),
         .retriable = item.value("retriable", false)});
  }
  return MakeValidationResult(result.value("valid", errors.empty()),
                              std::move(levels), std::move(errors));
}

nlohmann::json GraphConfigurationService::BuildOperationJson(
    const OperationRecord &operation) const {
  nlohmann::json json{
      {"schema", "graphx.dashboard.operation.v1"},
      {"operation_id", operation.operation_id},
      {"command_id", operation.command_id},
      {"status", operation.status},
      {"submitted_revision", operation.submitted_revision},
      {"terminal", operation.terminal},
      {"created_at", operation.created_at},
      {"completed_at", operation.completed_at},
      {"expires_at", operation.expires_at},
      {"validation", BuildValidationJson(operation.validation)}};
  if (!operation.result.is_null()) {
    json["result"] = operation.result;
  }
  return json;
}

std::string
GraphConfigurationService::RegisterOperation(OperationRecord operation) {
  std::lock_guard lock(mutex_);
  if (!operation.command_id.empty()) {
    const auto existing = command_index_.find(operation.command_id);
    if (existing != command_index_.end()) {
      return existing->second;
    }
  }
  operation.operation_id = "op-" + std::to_string(next_operation_id_++);
  if (operation.created_at.empty()) {
    operation.created_at = NowIso8601();
    operation.created_at_time = Now();
  }
  operation.expires_at_time =
      operation.created_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
  operations_.push_back(operation);
  if (!operation.command_id.empty()) {
    command_index_[operation.command_id] = operation.operation_id;
  }
  if (operations_.size() > operation_retention_count_) {
    operations_.erase(operations_.begin());
  }
  return operation.operation_id;
}

std::optional<GraphConfigurationService::OperationRecord>
GraphConfigurationService::FindOperation(
    const std::string &operation_id) const {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsUnlocked();
  for (const auto &operation : operations_) {
    if (operation.operation_id == operation_id) {
      return operation;
    }
  }
  return std::nullopt;
}

void GraphConfigurationService::PurgeExpiredOperationsUnlocked() const {
  const auto now = Now();
  auto it = operations_.begin();
  while (it != operations_.end()) {
    if (it->expires_at_time != std::chrono::system_clock::time_point{} &&
        it->expires_at_time <= now) {
      if (!it->command_id.empty()) {
        command_index_.erase(it->command_id);
      }
      it = operations_.erase(it);
      continue;
    }
    ++it;
  }
}

void GraphConfigurationService::ExpireOperationForTesting(
    const std::string &operation_id) {
  std::lock_guard lock(mutex_);
  for (auto &operation : operations_) {
    if (operation.operation_id == operation_id) {
      operation.expires_at_time = Now() - std::chrono::seconds(1);
      operation.expires_at = ToIso8601(operation.expires_at_time);
      break;
    }
  }
}

void GraphConfigurationService::SetArtifactRoot(
    std::filesystem::path artifact_root) {
  artifact_root_ = std::move(artifact_root);
}

void GraphConfigurationService::SetValidationInjectorForTesting(
    ValidationInjector injector) {
  validation_injector_ = std::move(injector);
}

void GraphConfigurationService::SetUpdateEventSinkForTesting(
    UpdateEventSink sink) {
  update_event_sink_ = std::move(sink);
}

void GraphConfigurationService::SetRevisionForTesting(std::uint64_t revision) {
  std::lock_guard lock(configuration_mutex_);
  revision_ = revision;
}

std::optional<GraphConfigurationService::ValidationError>
GraphConfigurationService::ValidateWithInjector(
    const nlohmann::json &request) const {
  if (!validation_injector_) {
    return std::nullopt;
  }
  return validation_injector_(request);
}

bool GraphConfigurationService::IsGeneratedPath(
    const std::string &pointer) const {
  const auto generated = policy_->GeneratedPaths();
  for (const auto &entry : generated) {
    const auto generated_pointer = entry.get<std::string>();
    if (pointer == generated_pointer ||
        pointer.rfind(generated_pointer + '/', 0) == 0) {
      return true;
    }
  }
  return false;
}

bool GraphConfigurationService::IsWritablePath(
    const std::string &pointer) const {
  return policy_->NormalizePatchPath(pointer).has_value();
}

std::string GraphConfigurationService::NormalizeScenarioPointer(
    const std::string &pointer) const {
  return policy_->NormalizePatchPath(pointer).value_or(pointer);
}

std::vector<std::string>
GraphConfigurationService::GeneratedTargetList() const {
  std::vector<std::string> targets;
  for (const auto &entry : policy_->GeneratedPaths()) {
    if (entry.is_string())
      targets.push_back(entry.get<std::string>());
  }
  return targets;
}

nlohmann::json
GraphConfigurationService::ApplyScenarioRequest(const nlohmann::json &request,
                                                bool commit) {
  nlohmann::json scenario_snapshot;
  nlohmann::json effective_snapshot;
  std::uint64_t snapshot_revision;
  {
    std::lock_guard lock(configuration_mutex_);
    scenario_snapshot = scenario_;
    effective_snapshot = effective_graph_;
    snapshot_revision = revision_;
  }
  const auto expected_revision =
      request.value("expected_revision", snapshot_revision);
  if (expected_revision != snapshot_revision) {
    return BuildErrorResponse(
        409, "stale_revision_conflict",
        "expected revision does not match current revision",
        nlohmann::json{{"current_revision", snapshot_revision}},
        request.value("command_id", std::string{}), snapshot_revision);
  }

  nlohmann::json candidate = scenario_snapshot;
  const auto command_id = request.value("command_id", std::string{});
  const auto dry_run =
      request.value("apply", std::string{"staged"}) == "validate";

  auto apply_single =
      [&](const std::string &pointer,
          const nlohmann::json &value) -> std::optional<nlohmann::json> {
    if (!IsWritablePath(pointer)) {
      return BuildErrorResponse(400, "pointer_not_allowed",
                                "pointer is outside writable scope", nullptr,
                                command_id, revision_, pointer);
    }
    const auto normalized = NormalizeScenarioPointer(pointer);
    if (IsGeneratedPath(normalized)) {
      return BuildErrorResponse(
          409, "derived_field_read_only", "generated fields are read-only",
          nlohmann::json{
              {"generated_target_pointer", pointer},
              {"authoritative_pointer", policy_->AuthoritativeRootPointer()}},
          command_id, revision_, pointer);
    }
    try {
      if (normalized.empty() || normalized == "/") {
        if (value.is_object()) {
          candidate = value;
        } else {
          return BuildErrorResponse(400, "invalid_pointer",
                                    "scenario root replace requires an object",
                                    nullptr, command_id, revision_, pointer);
        }
      } else {
        candidate[nlohmann::json::json_pointer(normalized)] = value;
      }
    } catch (const std::exception &ex) {
      return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr,
                                command_id, revision_, pointer);
    }
    return std::nullopt;
  };

  if (request.contains("pointer") && request.contains("value")) {
    if (auto error = apply_single(request.at("pointer").get<std::string>(),
                                  request.at("value"));
        error) {
      return *error;
    }
  } else if (request.contains("operations") &&
             request["operations"].is_array()) {
    for (const auto &operation : request["operations"]) {
      const auto op = operation.value("op", std::string{});
      const auto path = operation.value("path", std::string{});
      if (op == "move" || op == "copy") {
        return BuildErrorResponse(
            400, "patch_op_not_allowed",
            "move and copy patch operations are not allowed", nullptr,
            command_id, revision_, path);
      }
      if (op == "test") {
        try {
          const auto normalized = NormalizeScenarioPointer(path);
          const auto current =
              candidate.at(nlohmann::json::json_pointer(normalized));
          if (current != operation.at("value")) {
            return BuildErrorResponse(
                409, "json_patch_test_failed",
                "test operation did not match current value", nullptr,
                command_id, revision_, path);
          }
        } catch (const std::exception &ex) {
          return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr,
                                    command_id, revision_, path);
        }
        continue;
      }
      if (op != "add" && op != "remove" && op != "replace") {
        return BuildErrorResponse(400, "patch_op_not_allowed",
                                  "unsupported patch operation", nullptr,
                                  command_id, revision_, path);
      }
      if (auto error = apply_single(path, operation.contains("value")
                                              ? operation.at("value")
                                              : nlohmann::json{});
          error) {
        return *error;
      }
      if (op == "remove") {
        try {
          const auto normalized = NormalizeScenarioPointer(path);
          const nlohmann::json::json_pointer pointer(normalized);
          if (pointer.empty()) {
            return BuildErrorResponse(400, "invalid_pointer",
                                      "cannot remove the scenario root",
                                      nullptr, command_id, revision_, path);
          }

          nlohmann::json &parent = candidate.at(pointer.parent_pointer());
          const std::string token = pointer.back();
          if (parent.is_object()) {
            parent.erase(token);
          } else if (parent.is_array()) {
            std::size_t index = 0;
            const auto [ptr, ec] = std::from_chars(
                token.data(), token.data() + token.size(), index);
            if (ec != std::errc{} || ptr != token.data() + token.size() ||
                index >= parent.size()) {
              return BuildErrorResponse(400, "invalid_pointer",
                                        "array index out of range", nullptr,
                                        command_id, revision_, path);
            }
            parent.erase(parent.begin() +
                         static_cast<nlohmann::json::difference_type>(index));
          } else {
            return BuildErrorResponse(
                400, "invalid_pointer",
                "pointer parent is not an object or array", nullptr, command_id,
                revision_, path);
          }
        } catch (const std::exception &ex) {
          return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr,
                                    command_id, revision_, path);
        }
      }
    }
  } else {
    return BuildErrorResponse(400, "invalid_request",
                              "missing pointer/value or operations payload",
                              nullptr, command_id, revision_);
  }

  if (auto injector_error = ValidateWithInjector(request); injector_error) {
    return BuildErrorResponse(500, injector_error->code,
                              injector_error->message, injector_error->details,
                              command_id, revision_, injector_error->pointer);
  }

  const auto validation = ValidateAndSummarize(candidate, nullptr);
  if (!validation.at("validation").at("valid").get<bool>()) {
    return validation;
  }

  PendingChange change{.request_fingerprint = NormalizeJson(request),
                       .scenario = candidate,
                       .validation = ValidateAuthoritative(candidate),
                       .regenerated_targets = GeneratedTargetList()};
  if (commit && !dry_run) {
    auto effective_candidate =
        policy_->DeriveEffective(effective_snapshot, change.scenario);
    std::uint64_t new_revision;
    {
      std::lock_guard lock(configuration_mutex_);
      if (revision_ != snapshot_revision) {
        return BuildErrorResponse(
            409, "stale_revision_conflict",
            "configuration changed before this transaction could commit",
            {{"current_revision", revision_}}, command_id, revision_);
      }
      if (revision_ >= kMaximumJsonSafeInteger) {
        return BuildErrorResponse(
            409, "revision_space_exhausted",
            "configuration revision cannot advance within JavaScript safe "
            "integer bounds",
            {{"current_revision", revision_}}, command_id, revision_);
      }
      undo_stack_.push_back(scenario_);
      scenario_ = change.scenario;
      validation_ = change.validation;
      committed_scenario_ = scenario_;
      staged_scenario_ = scenario_;
      effective_graph_ = std::move(effective_candidate);
      new_revision = ++revision_;
    }
    return {{"schema", "graphx.dashboard.config_result.v1"},
            {"command_id", command_id},
            {"status", "staged"},
            {"old_revision", snapshot_revision},
            {"new_revision", new_revision},
            {"rebuild_required", true},
            {"regenerated_targets", change.regenerated_targets},
            {"validation", BuildValidationJson(change.validation)}};
  }
  return nlohmann::json{{"schema", "graphx.dashboard.config_result.v1"},
                        {"command_id", command_id},
                        {"status", "validated"},
                        {"old_revision", snapshot_revision},
                        {"new_revision", snapshot_revision},
                        {"rebuild_required", true},
                        {"regenerated_targets", change.regenerated_targets},
                        {"validation", BuildValidationJson(change.validation)}};
}

nlohmann::json GraphConfigurationService::CommitScenarioChange(
    const PendingChange &change, const std::string &command_id,
    std::uint64_t old_revision, bool rebuild_required) {
  if (revision_ >= kMaximumJsonSafeInteger) {
    return BuildErrorResponse(
        409, "revision_space_exhausted",
        "configuration revision cannot advance within JavaScript safe integer "
        "bounds",
        {{"current_revision", revision_}}, command_id, revision_);
  }
  undo_stack_.push_back(scenario_);
  scenario_ = change.scenario;
  validation_ = change.validation;
  committed_scenario_ = scenario_;
  staged_scenario_ = scenario_;
  effective_graph_ = policy_->DeriveEffective(effective_graph_, scenario_);
  ++revision_;

  nlohmann::json response{
      {"schema", "graphx.dashboard.config_result.v1"},
      {"command_id", command_id},
      {"status", "staged"},
      {"old_revision", old_revision},
      {"new_revision", revision_},
      {"rebuild_required", rebuild_required},
      {"regenerated_targets", change.regenerated_targets},
      {"validation", BuildValidationJson(change.validation)}};
  return response;
}

nlohmann::json GraphConfigurationService::ValidateAndSummarize(
    const nlohmann::json &scenario,
    std::vector<std::string> *regenerated_targets) const {
  const auto validation = ValidateAuthoritative(scenario);
  nlohmann::json response{{"schema", "graphx.dashboard.config_validation.v1"},
                          {"validation", BuildValidationJson(validation)}};
  if (regenerated_targets) {
    *regenerated_targets = GeneratedTargetList();
  }
  return response;
}

nlohmann::json
GraphConfigurationService::PatchConfig(const nlohmann::json &request) {
  return ApplyScenarioRequest(request, true);
}

nlohmann::json
GraphConfigurationService::ApplyJsonPatch(const nlohmann::json &patch,
                                          std::string_view if_match,
                                          bool validate_only) {
  nlohmann::json snapshot;
  nlohmann::json effective_snapshot;
  std::uint64_t snapshot_revision = 0;
  std::string snapshot_etag;
  {
    std::lock_guard lock(configuration_mutex_);
    snapshot = scenario_;
    effective_snapshot = effective_graph_;
    snapshot_revision = revision_;
    snapshot_etag = "\"graphx-config-" + std::to_string(revision_) + "\"";
  }
  if (if_match.empty()) {
    return BuildErrorResponse(428, "if_match_required",
                              "If-Match is required for configuration mutation",
                              {{"current_etag", snapshot_etag}}, {},
                              snapshot_revision);
  }
  if (if_match != snapshot_etag) {
    return BuildErrorResponse(
        412, "etag_precondition_failed",
        "If-Match does not match the current configuration",
        {{"current_etag", snapshot_etag}}, {}, snapshot_revision);
  }
  if (!patch.is_array()) {
    return BuildErrorResponse(400, "json_patch_not_array",
                              "JSON Patch document must be an array", nullptr,
                              {}, snapshot_revision);
  }
  static constexpr std::array<std::string_view, 6> supported{
      "add", "remove", "replace", "move", "copy", "test"};
  for (const auto &operation : patch) {
    if (!operation.is_object() || !operation.contains("op") ||
        !operation.at("op").is_string() || !operation.contains("path") ||
        !operation.at("path").is_string()) {
      return BuildErrorResponse(
          400, "malformed_json_patch_operation",
          "each patch operation requires string op and path", nullptr, {},
          snapshot_revision);
    }
    const auto op = operation.at("op").get<std::string>();
    const auto path = operation.at("path").get<std::string>();
    if (std::find(supported.begin(), supported.end(), op) == supported.end()) {
      return BuildErrorResponse(400, "unsupported_json_patch_operation",
                                "unsupported JSON Patch operation", nullptr, {},
                                snapshot_revision, path);
    }
    if (!policy_->NormalizePatchPath(path).has_value()) {
      return BuildErrorResponse(
          400, "pointer_not_allowed",
          "patch path is outside the authoritative document", nullptr, {},
          snapshot_revision, path);
    }
    const auto normalized_path = NormalizeScenarioPointer(path);
    if (IsGeneratedPath(normalized_path)) {
      return BuildErrorResponse(
          409, "derived_field_read_only", "generated fields are read-only",
          {{"generated_target_pointer", path},
           {"authoritative_pointer", policy_->AuthoritativeRootPointer()}},
          {}, snapshot_revision, path);
    }
    if ((op == "move" || op == "copy")) {
      if (!operation.contains("from") || !operation.at("from").is_string()) {
        return BuildErrorResponse(400, "malformed_json_patch_operation",
                                  "move and copy require a string from pointer",
                                  nullptr, {}, snapshot_revision, path);
      }
      const auto from = operation.at("from").get<std::string>();
      const auto normalized_from = NormalizeScenarioPointer(from);
      if (!policy_->NormalizePatchPath(from).has_value()) {
        return BuildErrorResponse(
            400, "pointer_not_allowed",
            "from pointer is outside writable authoritative data", nullptr, {},
            snapshot_revision, from);
      }
      if (IsGeneratedPath(normalized_from)) {
        return BuildErrorResponse(
            409, "derived_field_read_only", "generated fields are read-only",
            {{"generated_target_pointer", normalized_from},
             {"authoritative_pointer", policy_->AuthoritativeRootPointer()}},
            {}, snapshot_revision, from);
      }
    }
  }

  nlohmann::json normalized_patch = patch;
  for (auto &operation : normalized_patch) {
    operation["path"] =
        NormalizeScenarioPointer(operation.at("path").get<std::string>());
    if (operation.contains("from")) {
      operation["from"] =
          NormalizeScenarioPointer(operation.at("from").get<std::string>());
    }
  }
  nlohmann::json candidate;
  try {
    candidate = snapshot.patch(normalized_patch);
  } catch (const nlohmann::json::other_error &error) {
    return BuildErrorResponse(error.id == 501 ? 409 : 400,
                              error.id == 501 ? "json_patch_test_failed"
                                              : "json_patch_failed",
                              error.what(), nullptr, {}, snapshot_revision);
  } catch (const nlohmann::json::exception &error) {
    return BuildErrorResponse(400, "invalid_json_pointer", error.what(),
                              nullptr, {}, snapshot_revision);
  }

  auto validation = ValidateAuthoritative(candidate);
  if (!validation.valid || validate_only) {
    return {{"schema", "graphx.dashboard.config_validation.v1"},
            {"status", validation.valid ? "validated" : "invalid"},
            {"config_revision", snapshot_revision},
            {"etag", snapshot_etag},
            {"validation", BuildValidationJson(validation)}};
  }
  auto effective_candidate =
      policy_->DeriveEffective(effective_snapshot, candidate);
  PendingChange change{.request_fingerprint = NormalizeJson(patch),
                       .scenario = std::move(candidate),
                       .validation = std::move(validation),
                       .regenerated_targets = GeneratedTargetList()};
  std::uint64_t new_revision = 0;
  {
    std::lock_guard lock(configuration_mutex_);
    if (revision_ != snapshot_revision) {
      const auto current_etag =
          "\"graphx-config-" + std::to_string(revision_) + "\"";
      return BuildErrorResponse(
          412, "etag_precondition_failed",
          "configuration changed before this transaction could commit",
          {{"current_etag", current_etag}}, {}, revision_);
    }
    if (revision_ >= kMaximumJsonSafeInteger) {
      return BuildErrorResponse(
          409, "revision_space_exhausted",
          "configuration revision cannot advance within JavaScript safe "
          "integer bounds",
          {{"current_revision", revision_}}, {}, revision_);
    }
    undo_stack_.push_back(scenario_);
    scenario_ = change.scenario;
    validation_ = change.validation;
    committed_scenario_ = scenario_;
    staged_scenario_ = scenario_;
    effective_graph_ = std::move(effective_candidate);
    new_revision = ++revision_;
  }
  return {{"schema", "graphx.dashboard.config_result.v1"},
          {"status", "applied"},
          {"old_revision", snapshot_revision},
          {"new_revision", new_revision},
          {"etag", "\"graphx-config-" + std::to_string(new_revision) + "\""},
          {"rebuild_required", false},
          {"regenerated_targets", change.regenerated_targets},
          {"validation", BuildValidationJson(change.validation)}};
}

nlohmann::json
GraphConfigurationService::ValidateConfig(const nlohmann::json &request) const {
  nlohmann::json response = request;
  if (response.contains("pointer") || response.contains("operations")) {
    return const_cast<GraphConfigurationService *>(this)->ApplyScenarioRequest(
        request, false);
  }
  ValidationResult validation;
  {
    std::lock_guard lock(configuration_mutex_);
    validation = validation_;
  }
  return nlohmann::json{{"schema", "graphx.dashboard.config_validation.v1"},
                        {"validation", BuildValidationJson(validation)}};
}

nlohmann::json GraphConfigurationService::UndoLastEdit() {
  nlohmann::json previous;
  nlohmann::json effective_snapshot;
  std::uint64_t snapshot_revision;
  {
    std::lock_guard lock(configuration_mutex_);
    if (undo_stack_.empty()) {
      return BuildErrorResponse(409, "undo_not_available",
                                "no prior edit is available to undo");
    }
    previous = undo_stack_.back();
    effective_snapshot = effective_graph_;
    snapshot_revision = revision_;
  }
  auto validation = ValidateAuthoritative(previous);
  auto effective = policy_->DeriveEffective(effective_snapshot, previous);
  std::uint64_t new_revision;
  {
    std::lock_guard lock(configuration_mutex_);
    if (revision_ != snapshot_revision || undo_stack_.empty() ||
        undo_stack_.back() != previous) {
      return BuildErrorResponse(409, "stale_revision_conflict",
                                "configuration changed before undo commit",
                                {{"current_revision", revision_}}, "undo",
                                revision_);
    }
    if (revision_ >= kMaximumJsonSafeInteger) {
      return BuildErrorResponse(
          409, "revision_space_exhausted",
          "configuration revision cannot advance within JavaScript safe "
          "integer bounds",
          {{"current_revision", revision_}}, "undo", revision_);
    }
    undo_stack_.pop_back();
    scenario_ = previous;
    committed_scenario_ = previous;
    staged_scenario_ = previous;
    validation_ = validation;
    effective_graph_ = std::move(effective);
    new_revision = ++revision_;
  }
  return {{"schema", "graphx.dashboard.config_result.v1"},
          {"command_id", "undo"}, {"status", "staged"},
          {"old_revision", snapshot_revision}, {"new_revision", new_revision},
          {"rebuild_required", true},
          {"regenerated_targets", GeneratedTargetList()},
          {"validation", BuildValidationJson(validation)}};
}

nlohmann::json GraphConfigurationService::DiscardEdits() {
  ValidationResult validation;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    undo_stack_.clear();
    staged_scenario_ = scenario_;
    validation = validation_;
    revision = revision_;
  }
  return nlohmann::json{{"schema", "graphx.dashboard.config_result.v1"},
                        {"status", "discarded"},
                        {"old_revision", revision},
                        {"new_revision", revision},
                        {"validation", BuildValidationJson(validation)}};
}

nlohmann::json GraphConfigurationService::GetScenarioResponse() const {
  nlohmann::json scenario;
  ValidationResult validation;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    scenario = scenario_;
    validation = validation_;
    revision = revision_;
  }
  return nlohmann::json{{"schema", "graphx.dashboard.scenario.v1"},
                        {"owner", owner_},
                        {"config_revision", revision},
                        {"scenario", scenario},
                        {"etag", "\"graphx-config-" + std::to_string(revision) + "\""},
                        {"derived_paths", policy_->GeneratedPaths()},
                        {"validation", BuildValidationJson(validation)}};
}

nlohmann::json GraphConfigurationService::GetDerivedPathsResponse() const {
  const auto revision = ConfigRevision();
  return nlohmann::json{{"schema", "graphx.dashboard.derived_paths.v1"},
                        {"config_revision", revision},
                        {"paths", policy_->GeneratedPaths()}};
}

nlohmann::json GraphConfigurationService::GetProvenanceResponse() const {
  const auto revision = ConfigRevision();
  return {{"schema", "graphx.dashboard.configuration_provenance.v1"},
          {"config_revision", revision},
          {"etag", "\"graphx-config-" + std::to_string(revision) + "\""},
          {"provenance", policy_->Provenance()}};
}

nlohmann::json GraphConfigurationService::GetReceiverGraphResponse() const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  return {{"schema", "graphx.dashboard.receiver_graph.v1"},
          {"config_revision", revision},
          {"etag", "\"graphx-config-" + std::to_string(revision) + "\""},
          {"graph", policy_->ReceiverMinimalGraph(effective)}};
}

nlohmann::json
GraphConfigurationService::GetValueResponse(const std::string &pointer) const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  try {
    if (pointer.rfind("/nodes/", 0) == 0) {
      return nlohmann::json{
          {"schema", "graphx.dashboard.value.v1"},
          {"pointer", pointer},
          {"value",
           effective.at(nlohmann::json::json_pointer(pointer))}};
    }
  } catch (const std::exception &ex) {
    return BuildErrorResponse(404, "pointer_not_found", ex.what(), nullptr, {},
                              revision, pointer);
  }
  return BuildErrorResponse(404, "pointer_not_found", "pointer not found",
                            nullptr, {}, revision, pointer);
}

nlohmann::json
GraphConfigurationService::GetNodeResponse(const std::string &node_id) const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  if (const auto *node = FindNodeById(effective, node_id); node) {
    return nlohmann::json{{"schema", "graphx.dashboard.node.v1"},
                          {"config_revision", revision},
                          {"node", *node}};
  }
  return BuildErrorResponse(404, "node_not_found", "node not found", nullptr,
                            {}, revision, node_id);
}

nlohmann::json GraphConfigurationService::GetNodeParametersResponse(
    const std::string &node_id) const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  const auto *node = FindNodeById(effective, node_id);
  if (!node) {
    return BuildErrorResponse(404, "node_not_found", "node not found", nullptr,
                              {}, revision, node_id);
  }
  nlohmann::json node_config = node->contains("node_config")
                                   ? (*node)["node_config"]
                                   : nlohmann::json::object();
  nlohmann::json parameters{{"configured", nlohmann::json::object()},
                            {"runtime_reported", nlohmann::json::object()},
                            {"staged", node_config},
                            {"descriptions", nlohmann::json::object()},
                            {"provenance", nlohmann::json::object()}};
  parameters["configured"] = node_config;
  parameters["provenance"]["source"] = "receiver_effective_config";
  return nlohmann::json{
      {"schema", "graphx.dashboard.node_parameters.v1"},
      {"config_revision", revision},
      {"node_id", node_id},
      {"node", *node},
      {"parameters", parameters},
      {"ports", nlohmann::json{{"inputs", nlohmann::json::array()},
                               {"outputs", nlohmann::json::array()}}}};
}

nlohmann::json GraphConfigurationService::GetGraphResponse() const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  return nlohmann::json{{"schema", "graphx.dashboard.graph.v1"},
                        {"owner", owner_},
                        {"config_revision", revision},
                        {"etag",
                         "\"graphx-config-" + std::to_string(revision) + "\""},
                        {"graph", effective}};
}

nlohmann::json GraphConfigurationService::GetConfigResponse() const {
  nlohmann::json effective;
  std::uint64_t revision;
  {
    std::lock_guard lock(configuration_mutex_);
    effective = effective_graph_;
    revision = revision_;
  }
  return nlohmann::json{{"schema", "graphx.dashboard.config.v1"},
                        {"owner", owner_},
                        {"config_revision", revision},
                        {"effective", effective},
                        {"etag", "\"graphx-config-" + std::to_string(revision) + "\""},
                        {"derived_paths", policy_->GeneratedPaths()}};
}

nlohmann::json GraphConfigurationService::GetOperationResponse(
    const std::string &operation_id) const {
  if (const auto operation = FindOperation(operation_id); operation) {
    return BuildOperationJson(*operation);
  }
  return BuildErrorResponse(404, "operation_not_found_or_expired",
                            "operation not found or expired", nullptr, {},
                            revision_, operation_id);
}

nlohmann::json
GraphConfigurationService::ExportConfig(const nlohmann::json &request) {
  nlohmann::json scenario_snapshot;
  nlohmann::json effective_snapshot;
  ValidationResult validation_snapshot;
  std::uint64_t snapshot_revision;
  {
    std::lock_guard lock(configuration_mutex_);
    scenario_snapshot = scenario_;
    effective_snapshot = effective_graph_;
    validation_snapshot = validation_;
    snapshot_revision = revision_;
  }
  const auto expected_revision =
      request.value("expected_revision", snapshot_revision);
  if (expected_revision != snapshot_revision) {
    return BuildErrorResponse(
        409, "stale_revision_conflict",
        "expected revision does not match current revision",
        nlohmann::json{{"current_revision", snapshot_revision}},
        request.value("command_id", std::string{}), snapshot_revision);
  }

  const auto output_path_value = request.value("output_path", std::string{});
  if (output_path_value.empty()) {
    return BuildErrorResponse(
        400, "missing_output_path", "output_path is required", nullptr,
        request.value("command_id", std::string{}), revision_);
  }

  const std::filesystem::path output_path = output_path_value;
  if (!output_path.is_absolute()) {
    return BuildErrorResponse(400, "artifact_path_not_allowed",
                              "output_path must be absolute", nullptr,
                              request.value("command_id", std::string{}),
                              revision_, output_path_value);
  }
  if (!artifact_root_.empty() &&
      !IsUnderRoot(artifact_root_, output_path.parent_path())) {
    return BuildErrorResponse(400, "artifact_path_not_allowed",
                              "output_path must stay under the artifact root",
                              nullptr,
                              request.value("command_id", std::string{}),
                              revision_, output_path_value);
  }

  const auto command_id = request.value("command_id", std::string{});
  const auto normalized_fingerprint = NormalizeJson(request);
  if (!command_id.empty()) {
    const auto existing = command_index_.find(command_id);
    if (existing != command_index_.end()) {
      const auto operation = FindOperation(existing->second);
      if (operation &&
          operation->request_fingerprint != normalized_fingerprint) {
        return BuildErrorResponse(
            409, "idempotency_key_reused_with_different_payload",
            "command_id already exists for a different request", nullptr,
            command_id, revision_);
      }
      if (operation) {
        return BuildOperationJson(*operation);
      }
    }
  }

  OperationRecord operation;
  operation.command_id = command_id;
  operation.request_fingerprint = normalized_fingerprint;
  operation.status = "queued";
  operation.submitted_revision = snapshot_revision;
  operation.created_at = NowIso8601();
  operation.created_at_time = Now();
  operation.validation = validation_snapshot;

  if (update_event_sink_) {
    update_event_sink_(nlohmann::json{
        {"sequence", 1}, {"status", "queued"}, {"command_id", command_id}});
    update_event_sink_(nlohmann::json{
        {"sequence", 2}, {"status", "running"}, {"command_id", command_id}});
  }

  operation.status = "running";
  std::error_code error;
  std::filesystem::create_directories(output_path.parent_path(), error);
  if (error) {
    operation.status = "failed";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time =
        operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result =
        BuildErrorResponse(500, "artifact_write_failed", error.message(),
                           nullptr, command_id, revision_, output_path_value);
    const auto operation_id = RegisterOperation(operation);
    auto stored = FindOperation(operation_id);
    return BuildOperationJson(stored ? *stored : operation);
  }

  if (validation_injector_) {
    if (auto injector_error = validation_injector_(request); injector_error) {
      operation.status = "failed";
      operation.terminal = true;
      operation.completed_at = NowIso8601();
      operation.completed_at_time = Now();
      operation.expires_at_time =
          operation.completed_at_time + std::chrono::hours(24);
      operation.expires_at = ToIso8601(operation.expires_at_time);
      operation.result =
          BuildErrorResponse(500, injector_error->code, injector_error->message,
                             injector_error->details, command_id, revision_,
                             injector_error->pointer);
      const auto operation_id = RegisterOperation(operation);
      auto stored = FindOperation(operation_id);
      return BuildOperationJson(stored ? *stored : operation);
    }
  }

  const auto resource = request.value("resource", std::string{"effective"});
  const auto &payload = resource == "authoritative" ? scenario_snapshot
                                                     : effective_snapshot;
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.good()) {
    operation.status = "failed";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time =
        operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result = BuildErrorResponse(
        500, "artifact_write_failed", "failed to open output file", nullptr,
        command_id, revision_, output_path_value);
    const auto operation_id = RegisterOperation(operation);
    auto stored = FindOperation(operation_id);
    return BuildOperationJson(stored ? *stored : operation);
  }

  output << std::setw(2) << payload << '\n';
  output.close();
  if (!output.good()) {
    operation.status = "failed";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time =
        operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result = BuildErrorResponse(
        500, "artifact_write_failed", "failed to write output file", nullptr,
        command_id, revision_, output_path_value);
    const auto operation_id = RegisterOperation(operation);
    auto stored = FindOperation(operation_id);
    return BuildOperationJson(stored ? *stored : operation);
  }

  operation.status = "succeeded";
  operation.terminal = true;
  operation.completed_at = NowIso8601();
  operation.completed_at_time = Now();
  operation.expires_at_time =
      operation.completed_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
  operation.result = nlohmann::json{
      {"output_path", output_path_value},
      {"resource", resource},
      {"bytes_written", std::filesystem::file_size(output_path, error)}};
  const auto operation_id = RegisterOperation(operation);
  if (update_event_sink_) {
    update_event_sink_(nlohmann::json{
        {"sequence", 3}, {"status", "succeeded"}, {"command_id", command_id}});
  }
  auto stored = FindOperation(operation_id);
  return BuildOperationJson(stored ? *stored : operation);
}

nlohmann::json
GraphConfigurationService::CancelOperation(const std::string &operation_id) {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsUnlocked();
  for (auto &operation : operations_) {
    if (operation.operation_id != operation_id) {
      continue;
    }
    if (operation.terminal) {
      return BuildErrorResponse(409, "operation_not_terminal",
                                "terminal operations cannot be cancelled",
                                nullptr, operation.command_id,
                                operation.submitted_revision, operation_id);
    }
    operation.status = "cancelled";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time =
        operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    return BuildOperationJson(operation);
  }
  return BuildErrorResponse(404, "operation_not_found_or_expired",
                            "operation not found or expired", nullptr, {},
                            revision_, operation_id);
}

bool GraphConfigurationService::DeleteOperation(const std::string &operation_id,
                                                std::string *error_code) {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsUnlocked();
  for (auto it = operations_.begin(); it != operations_.end(); ++it) {
    if (it->operation_id != operation_id) {
      continue;
    }
    if (!it->terminal) {
      if (error_code) {
        *error_code = "operation_not_terminal";
      }
      return false;
    }
    if (!it->command_id.empty()) {
      command_index_.erase(it->command_id);
    }
    operations_.erase(it);
    return true;
  }
  if (error_code) {
    *error_code = "operation_not_found_or_expired";
  }
  return false;
}

} // namespace graph::dashboard
