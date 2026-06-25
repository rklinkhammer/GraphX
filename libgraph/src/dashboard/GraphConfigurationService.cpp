// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphConfigurationService.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace graph::dashboard {
namespace {

constexpr const char *kScenarioRoot = "/fhss/scenario";
constexpr const char *kScenarioAliasRoot = "/nodes/source/node_config";

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

std::string JoinPointer(const std::string &root, const std::string &suffix) {
  if (suffix.empty() || suffix == "/") {
    return root;
  }
  if (suffix.front() == '/') {
    return root + suffix;
  }
  return root + '/' + suffix;
}

nlohmann::json MakeEmptyObject() { return nlohmann::json::object(); }

bool HasObjectNodeConfig(const nlohmann::json &node) {
  return node.contains("node_config") && node["node_config"].is_object();
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

nlohmann::json *FindNodeByIdMutable(nlohmann::json &graph,
                                    const std::string &node_id) {
  if (!graph.contains("nodes") || !graph["nodes"].is_array()) {
    return nullptr;
  }
  for (auto &node : graph["nodes"]) {
    if (node.contains("id") && node["id"] == node_id) {
      return &node;
    }
  }
  return nullptr;
}

std::vector<std::string> BuildDerivedTargetList() {
  return {"/nodes/source/node_config",
          "/nodes/preamble/node_config",
          "/nodes/assembler/node_config",
          "/nodes/channelizer/node_config"};
}

std::vector<std::uint32_t> DeriveActiveFrequencyIndices(const nlohmann::json &scenario) {
  std::set<std::uint32_t> unique;
  if (!scenario.contains("messages") || !scenario["messages"].is_array() ||
      scenario["messages"].empty()) {
    return {};
  }
  const auto &messages = scenario["messages"];
  if (!messages.front().contains("pulses") || !messages.front()["pulses"].is_array()) {
    return {};
  }
  const auto &pulses = messages.front()["pulses"];
  const auto limit = std::min<std::size_t>(16u, pulses.size());
  for (std::size_t i = 0; i < limit; ++i) {
    const auto &pulse = pulses[i];
    if (pulse.contains("frequency_index")) {
      unique.insert(pulse["frequency_index"].get<std::uint32_t>());
    }
  }
  return {unique.begin(), unique.end()};
}

nlohmann::json DerivePreamblePulses(const nlohmann::json &scenario) {
  nlohmann::json preamble = nlohmann::json::array();
  if (!scenario.contains("messages") || !scenario["messages"].is_array() ||
      scenario["messages"].empty()) {
    return preamble;
  }
  const auto &first_message = scenario["messages"].front();
  if (!first_message.contains("pulses") || !first_message["pulses"].is_array()) {
    return preamble;
  }
  const auto &pulses = first_message["pulses"];
  const auto limit = std::min<std::size_t>(16u, pulses.size());
  for (std::size_t i = 0; i < limit; ++i) {
    const auto &pulse = pulses[i];
    preamble.push_back({{"frequency_index", pulse.value("frequency_index", 0u)},
                        {"word_value", pulse.value("value", 0u)}});
  }
  return preamble;
}

nlohmann::json FlattenPulseFrequencies(const nlohmann::json &scenario) {
  nlohmann::json flattened = nlohmann::json::array();
  if (!scenario.contains("messages") || !scenario["messages"].is_array()) {
    return flattened;
  }
  for (const auto &message : scenario["messages"]) {
    if (!message.contains("pulses") || !message["pulses"].is_array()) {
      continue;
    }
    for (const auto &pulse : message["pulses"]) {
      flattened.push_back(pulse.value("frequency_index", 0u));
    }
  }
  return flattened;
}

bool IsUnderRoot(const std::filesystem::path &root, const std::filesystem::path &candidate) {
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) {
    return false;
  }
  const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return false;
  }
  return canonical_candidate.native().rfind(canonical_root.native(), 0) == 0;
}

} // namespace

GraphConfigurationService::GraphConfigurationService(nlohmann::json effective_graph)
    : effective_graph_(std::move(effective_graph)) {
  scenario_ = ExtractScenario(effective_graph_);
  committed_scenario_ = scenario_;
  staged_scenario_ = scenario_;
  effective_graph_ = DeriveEffectiveGraph(effective_graph_, scenario_);
}

bool GraphConfigurationService::IsValid() const {
  return effective_graph_.is_object() && effective_graph_.contains("nodes") &&
         effective_graph_["nodes"].is_array() && effective_graph_.contains("edges") &&
         effective_graph_["edges"].is_array();
}

const std::string &GraphConfigurationService::Owner() const { return owner_; }

std::uint64_t GraphConfigurationService::ConfigRevision() const { return revision_; }

std::chrono::system_clock::time_point GraphConfigurationService::Now() {
  return std::chrono::system_clock::now();
}

std::string GraphConfigurationService::NowIso8601() { return ToIso8601(Now()); }

std::string GraphConfigurationService::NormalizeJson(const nlohmann::json &json) {
  return json.dump();
}

nlohmann::json GraphConfigurationService::BuildErrorResponse(int status_code,
                                                            std::string code,
                                                            std::string message,
                                                            nlohmann::json details,
                                                            std::string command_id,
                                                            std::uint64_t revision,
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

nlohmann::json GraphConfigurationService::BuildValidationJson(const ValidationResult &validation) {
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

GraphConfigurationService::ValidationResult GraphConfigurationService::MakeValidationResult(
    bool valid, std::vector<std::string> levels, std::vector<ValidationError> errors) {
  return ValidationResult{.valid = valid, .levels = std::move(levels), .errors = std::move(errors)};
}

nlohmann::json GraphConfigurationService::GeneratedPathsJson() {
  return nlohmann::json::array({"/fhss/scenario/active_frequency_indices",
                                "/fhss/scenario/preamble_pulses",
                                "/nodes/source/node_config/active_frequency_indices",
                                "/nodes/preamble/node_config/active_frequency_indices",
                                "/nodes/preamble/node_config/preamble_pulses",
                                "/nodes/assembler/node_config/active_frequency_indices",
                                "/nodes/assembler/node_config/preamble_pulses",
                                "/nodes/assembler/node_config/truth_from_fixture",
                                "/nodes/channelizer/node_config/transmitted_active_frequency_indices",
                                "/nodes/channelizer/node_config/transmitted_pulse_frequency_indices"});
}

nlohmann::json GraphConfigurationService::ExtractScenario(const nlohmann::json &effective_graph) {
  if (const auto *source = FindNodeById(effective_graph, "source"); source && HasObjectNodeConfig(*source)) {
    return source->at("node_config");
  }
  if (effective_graph.contains("nodes") && effective_graph["nodes"].is_array()) {
    for (const auto &node : effective_graph["nodes"]) {
      if (node.value("type", std::string{}) == "FHSSSyntheticIqSourceNode" &&
          HasObjectNodeConfig(node)) {
        return node.at("node_config");
      }
    }
  }
  return MakeEmptyObject();
}

nlohmann::json GraphConfigurationService::DeriveEffectiveGraph(const nlohmann::json &base_graph,
                                                               const nlohmann::json &scenario) {
  nlohmann::json graph = base_graph;
  const auto active_frequencies = DeriveActiveFrequencyIndices(scenario);
  const auto preamble = DerivePreamblePulses(scenario);
  const auto flattened_pulses = FlattenPulseFrequencies(scenario);

  if (auto *source = FindNodeByIdMutable(graph, "source")) {
    (*source)["node_config"] = scenario;
    (*source)["node_config"]["active_frequency_indices"] = active_frequencies;
  }
  if (auto *preamble_node = FindNodeByIdMutable(graph, "preamble")) {
    auto &node_config = (*preamble_node)["node_config"];
    if (!node_config.is_object()) {
      node_config = nlohmann::json::object();
    }
    node_config["active_frequency_indices"] = active_frequencies;
    node_config["preamble_pulses"] = preamble;
    for (const auto &key : {"iq_center_frequency_hz", "occupied_bandwidth_hz",
                            "max_abs_cfo_hz", "allow_overlap"}) {
      if (scenario.contains(key)) {
        node_config[key] = scenario.at(key);
      }
    }
  }
  if (auto *assembler = FindNodeByIdMutable(graph, "assembler")) {
    auto &node_config = (*assembler)["node_config"];
    node_config = scenario;
    node_config["active_frequency_indices"] = active_frequencies;
    node_config["preamble_pulses"] = preamble;
    node_config["truth_from_fixture"] = true;
  }
  if (auto *channelizer = FindNodeByIdMutable(graph, "channelizer")) {
    auto &node_config = (*channelizer)["node_config"];
    if (!node_config.is_object()) {
      node_config = nlohmann::json::object();
    }
    node_config["transmitted_active_frequency_indices"] = active_frequencies;
    node_config["transmitted_pulse_frequency_indices"] = flattened_pulses;
    for (const auto &key : {"iq_center_frequency_hz", "occupied_bandwidth_hz",
                            "max_abs_cfo_hz"}) {
      if (scenario.contains(key)) {
        node_config[key] = scenario.at(key);
      }
    }
  }
  return graph;
}

GraphConfigurationService::ValidationResult
GraphConfigurationService::ValidateScenario(const nlohmann::json &scenario) {
  std::vector<std::string> levels = {"syntax", "structure", "semantic", "descriptor"};
  std::vector<ValidationError> errors;

  if (!scenario.is_object()) {
    errors.push_back({"syntax", "graph", "/fhss/scenario", "scenario_not_object",
                      "scenario must be a JSON object", {}, {}, scenario, false});
    return MakeValidationResult(false, std::move(levels), std::move(errors));
  }

  if (!scenario.contains("messages")) {
    return MakeValidationResult(true, std::move(levels));
  }
  if (!scenario["messages"].is_array()) {
    errors.push_back({"structure", "graph", "/fhss/scenario/messages", "messages_not_array",
                      "messages must be an array", {}, {}, scenario["messages"], false});
    return MakeValidationResult(false, std::move(levels), std::move(errors));
  }
  const auto &messages = scenario["messages"];
  if (messages.empty()) {
    errors.push_back({"semantic", "graph", "/fhss/scenario/messages", "missing_messages",
                      "scenario must contain at least one message"});
    return MakeValidationResult(false, std::move(levels), std::move(errors));
  }

  std::set<std::uint64_t> seen_message_ids;
  nlohmann::json reference_preamble = nlohmann::json::array();
  std::vector<std::uint32_t> active_frequencies = DeriveActiveFrequencyIndices(scenario);
  if (active_frequencies.size() != 4u) {
    errors.push_back({"semantic", "graph", "/fhss/scenario/messages/0/pulses/0/frequency_index",
                      "invalid_active_frequency_set",
                      "first preamble must define exactly four distinct active frequencies"});
  }

  for (std::size_t message_index = 0; message_index < messages.size(); ++message_index) {
    const auto &message = messages[message_index];
    const auto pointer_prefix = "/fhss/scenario/messages/" + std::to_string(message_index);
    if (!message.is_object()) {
      errors.push_back({"structure", "graph", pointer_prefix, "message_not_object",
                        "message entries must be objects"});
      continue;
    }
    const auto message_id = message.value("message_id", std::uint64_t{0});
    if (!seen_message_ids.insert(message_id).second) {
      errors.push_back({"semantic", "graph", pointer_prefix + "/message_id",
                        "duplicate_message_id", "message_id values must be unique"});
    }
    if (!message.contains("pulses") || !message["pulses"].is_array()) {
      errors.push_back({"structure", "graph", pointer_prefix + "/pulses",
                        "missing_pulses", "each message must contain a pulses array"});
      continue;
    }
    const auto &pulses = message["pulses"];
    if (pulses.size() < 16u || pulses.size() > 256u) {
      errors.push_back({"semantic", "graph", pointer_prefix + "/pulses",
                        "invalid_pulse_count", "each message must contain 16-256 pulses"});
    }
    nlohmann::json message_preamble = nlohmann::json::array();
    for (std::size_t pulse_index = 0; pulse_index < pulses.size(); ++pulse_index) {
      const auto &pulse = pulses[pulse_index];
      const auto pulse_pointer = pointer_prefix + "/pulses/" + std::to_string(pulse_index);
      if (!pulse.is_object()) {
        errors.push_back({"structure", "graph", pulse_pointer, "pulse_not_object",
                          "each pulse must be an object"});
        continue;
      }
      const auto role = pulse.value("role", std::string{});
      if (pulse_index < 16u && role != "preamble") {
        errors.push_back({"semantic", "graph", pulse_pointer + "/role",
                          "invalid_preamble_length",
                          "first 16 pulses must be marked preamble"});
      }
      if (pulse_index >= 16u && role != "body") {
        errors.push_back({"semantic", "graph", pulse_pointer + "/role",
                          "invalid_body_role", "body pulses must be marked body"});
      }
      const auto frequency_index = pulse.value("frequency_index", std::uint32_t{0});
      if (frequency_index < 1u || frequency_index > 62u) {
        errors.push_back({"semantic", "graph", pulse_pointer + "/frequency_index",
                          "fhss.frequency_index_out_of_range",
                          "frequency_index must be in [1,62]"});
      }
      if (!active_frequencies.empty() &&
          std::find(active_frequencies.begin(), active_frequencies.end(), frequency_index) ==
              active_frequencies.end()) {
        errors.push_back({"semantic", "graph", pulse_pointer + "/frequency_index",
                          "pulse_frequency_not_active", "pulse must use an active frequency"});
      }
      if (pulse_index < 16u) {
        message_preamble.push_back({{"frequency_index", frequency_index},
                                   {"word_value", pulse.value("value", 0u)}});
      }
    }
    if (message_index == 0u) {
      reference_preamble = std::move(message_preamble);
    } else if (!reference_preamble.empty() && message_preamble != reference_preamble) {
      errors.push_back({"semantic", "graph", pointer_prefix + "/pulses/0",
                        "inconsistent_message_preamble",
                        "later messages must reuse the first message preamble"});
    }
    if (message.contains("transmit_start_sample") && message_index > 0u) {
      const auto previous = messages[message_index - 1].value("transmit_start_sample", std::uint64_t{0});
      const auto previous_pulse_count = messages[message_index - 1].contains("pulses") &&
                                               messages[message_index - 1]["pulses"].is_array()
                                           ? messages[message_index - 1]["pulses"].size()
                                           : 0u;
      const auto previous_end = previous + previous_pulse_count * 1u;
      if (message.value("transmit_start_sample", std::uint64_t{0}) < previous_end) {
        errors.push_back({"semantic", "graph", pointer_prefix + "/transmit_start_sample",
                          "scheduled_messages_overlap", "scheduled messages must not overlap"});
      }
    }
  }

  if (!reference_preamble.empty()) {
    for (const auto &preamble_pulse : reference_preamble) {
      if (!preamble_pulse.contains("frequency_index")) {
        continue;
      }
      const auto frequency_index = preamble_pulse["frequency_index"].get<std::uint32_t>();
      if (std::find(active_frequencies.begin(), active_frequencies.end(), frequency_index) ==
          active_frequencies.end()) {
        errors.push_back({"semantic", "graph", "/fhss/scenario/messages/0/pulses/0/frequency_index",
                          "invalid_active_frequency_set",
                          "preamble frequencies must match the generated active set"});
        break;
      }
    }
  }

  return MakeValidationResult(errors.empty(), std::move(levels), std::move(errors));
}

nlohmann::json GraphConfigurationService::BuildOperationJson(const OperationRecord &operation) const {
  nlohmann::json json{{"schema", "graphx.dashboard.operation.v1"},
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

std::string GraphConfigurationService::RegisterOperation(OperationRecord operation) {
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
  operation.expires_at_time = operation.created_at_time + std::chrono::hours(24);
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
GraphConfigurationService::FindOperation(const std::string &operation_id) const {
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

void GraphConfigurationService::ExpireOperationForTesting(const std::string &operation_id) {
  std::lock_guard lock(mutex_);
  for (auto &operation : operations_) {
    if (operation.operation_id == operation_id) {
      operation.expires_at_time = Now() - std::chrono::seconds(1);
      operation.expires_at = ToIso8601(operation.expires_at_time);
      break;
    }
  }
}

void GraphConfigurationService::SetArtifactRoot(std::filesystem::path artifact_root) {
  artifact_root_ = std::move(artifact_root);
}

void GraphConfigurationService::SetValidationInjectorForTesting(ValidationInjector injector) {
  validation_injector_ = std::move(injector);
}

void GraphConfigurationService::SetUpdateEventSinkForTesting(UpdateEventSink sink) {
  update_event_sink_ = std::move(sink);
}

std::optional<GraphConfigurationService::ValidationError>
GraphConfigurationService::ValidateWithInjector(const nlohmann::json &request) const {
  if (!validation_injector_) {
    return std::nullopt;
  }
  return validation_injector_(request);
}

bool GraphConfigurationService::IsGeneratedPath(const std::string &pointer) const {
  const auto generated = GeneratedPathsJson();
  for (const auto &entry : generated) {
    const auto generated_pointer = entry.get<std::string>();
    if (pointer == generated_pointer || pointer.rfind(generated_pointer + '/', 0) == 0) {
      return true;
    }
  }
  return false;
}

bool GraphConfigurationService::IsWritablePath(const std::string &pointer) const {
  return pointer.rfind(kScenarioRoot, 0) == 0 || pointer.rfind(kScenarioAliasRoot, 0) == 0;
}

std::string GraphConfigurationService::NormalizeScenarioPointer(const std::string &pointer) const {
  if (pointer.rfind(kScenarioRoot, 0) == 0) {
    return pointer.substr(std::string{kScenarioRoot}.size());
  }
  if (pointer.rfind(kScenarioAliasRoot, 0) == 0) {
    return pointer.substr(std::string{kScenarioAliasRoot}.size());
  }
  return pointer;
}

nlohmann::json GraphConfigurationService::ApplyScenarioRequest(const nlohmann::json &request,
                                                               bool commit) {
  const auto expected_revision = request.value("expected_revision", revision_);
  if (expected_revision != revision_) {
    return BuildErrorResponse(409, "stale_revision_conflict",
                              "expected revision does not match current revision",
                              nlohmann::json{{"current_revision", revision_}},
                              request.value("command_id", std::string{}), revision_);
  }

  nlohmann::json candidate = scenario_;
  const auto command_id = request.value("command_id", std::string{});
  const auto dry_run = request.value("apply", std::string{"staged"}) == "validate";

  auto apply_single = [&](const std::string &pointer, const nlohmann::json &value) -> std::optional<nlohmann::json> {
    if (!IsWritablePath(pointer)) {
      return BuildErrorResponse(400, "pointer_not_allowed", "pointer is outside writable scope", nullptr,
                                    command_id, revision_, pointer);
    }
    if (IsGeneratedPath(pointer)) {
      const auto normalized = NormalizeScenarioPointer(pointer);
      const auto generated = pointer.rfind(kScenarioAliasRoot, 0) == 0 ? "/nodes/source/node_config" : "/fhss/scenario";
      return BuildErrorResponse(409, "derived_field_read_only",
                                "generated fields are read-only",
                                nlohmann::json{{"generated_target_pointer", JoinPointer(generated, normalized)},
                                               {"authoritative_pointer", "/fhss/scenario"}},
                                command_id, revision_, pointer);
    }
    try {
      const auto normalized = NormalizeScenarioPointer(pointer);
      if (normalized.empty() || normalized == "/") {
        if (value.is_object()) {
          candidate = value;
        } else {
          return BuildErrorResponse(400, "invalid_pointer", "scenario root replace requires an object",
                                    nullptr, command_id, revision_, pointer);
        }
      } else {
        candidate[nlohmann::json::json_pointer(normalized)] = value;
      }
    } catch (const std::exception &ex) {
      return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr, command_id, revision_, pointer);
    }
    return std::nullopt;
  };

  if (request.contains("pointer") && request.contains("value")) {
    if (auto error = apply_single(request.at("pointer").get<std::string>(), request.at("value")); error) {
      return *error;
    }
  } else if (request.contains("operations") && request["operations"].is_array()) {
    for (const auto &operation : request["operations"]) {
      const auto op = operation.value("op", std::string{});
      const auto path = operation.value("path", std::string{});
      if (op == "move" || op == "copy") {
        return BuildErrorResponse(400, "patch_op_not_allowed",
                                  "move and copy patch operations are not allowed", nullptr, command_id,
                                  revision_, path);
      }
      if (op == "test") {
        try {
          const auto normalized = NormalizeScenarioPointer(path);
          const auto current = candidate.at(nlohmann::json::json_pointer(normalized));
          if (current != operation.at("value")) {
            return BuildErrorResponse(409, "json_patch_test_failed",
                                      "test operation did not match current value", nullptr, command_id,
                                      revision_, path);
          }
        } catch (const std::exception &ex) {
          return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr, command_id, revision_, path);
        }
        continue;
      }
      if (op != "add" && op != "remove" && op != "replace") {
        return BuildErrorResponse(400, "patch_op_not_allowed",
                                  "unsupported patch operation", nullptr, command_id, revision_, path);
      }
      if (auto error = apply_single(path, operation.contains("value") ? operation.at("value") : nlohmann::json{});
          error) {
        return *error;
      }
      if (op == "remove") {
        try {
          const auto normalized = NormalizeScenarioPointer(path);
          candidate.erase(nlohmann::json::json_pointer(normalized));
        } catch (const std::exception &ex) {
          return BuildErrorResponse(400, "invalid_pointer", ex.what(), nullptr, command_id, revision_, path);
        }
      }
    }
  } else {
    return BuildErrorResponse(400, "invalid_request", "missing pointer/value or operations payload",
                              nullptr, command_id, revision_);
  }

  if (auto injector_error = ValidateWithInjector(request); injector_error) {
    return BuildErrorResponse(500, injector_error->code, injector_error->message, injector_error->details,
                              command_id, revision_, injector_error->pointer);
  }

  const auto validation = ValidateAndSummarize(candidate, nullptr);
  if (!validation.at("validation").at("valid").get<bool>()) {
    return validation;
  }

  PendingChange change{.request_fingerprint = NormalizeJson(request),
                       .scenario = candidate,
                       .validation = ValidateScenario(candidate),
                       .regenerated_targets = BuildDerivedTargetList()};
  if (commit && !dry_run) {
    return CommitScenarioChange(change, command_id, revision_, true);
  }
  return nlohmann::json{{"schema", "graphx.dashboard.config_result.v1"},
                        {"command_id", command_id},
                        {"status", "validated"},
                        {"old_revision", revision_},
                        {"new_revision", revision_},
                        {"rebuild_required", true},
                        {"regenerated_targets", change.regenerated_targets},
                        {"validation", BuildValidationJson(change.validation)}};
}

nlohmann::json GraphConfigurationService::CommitScenarioChange(const PendingChange &change,
                                                               const std::string &command_id,
                                                               std::uint64_t old_revision,
                                                               bool rebuild_required) {
  undo_stack_.push_back(scenario_);
  scenario_ = change.scenario;
  committed_scenario_ = scenario_;
  staged_scenario_ = scenario_;
  effective_graph_ = DeriveEffectiveGraph(effective_graph_, scenario_);
  ++revision_;

  nlohmann::json response{{"schema", "graphx.dashboard.config_result.v1"},
                          {"command_id", command_id},
                          {"status", "staged"},
                          {"old_revision", old_revision},
                          {"new_revision", revision_},
                          {"rebuild_required", rebuild_required},
                          {"regenerated_targets", change.regenerated_targets},
                          {"validation", BuildValidationJson(change.validation)}};
  return response;
}

nlohmann::json GraphConfigurationService::ValidateAndSummarize(const nlohmann::json &scenario,
                                                              std::vector<std::string> *regenerated_targets) const {
  const auto validation = ValidateScenario(scenario);
  nlohmann::json response{{"schema", "graphx.dashboard.config_validation.v1"},
                          {"validation", BuildValidationJson(validation)}};
  if (regenerated_targets) {
    *regenerated_targets = BuildDerivedTargetList();
  }
  return response;
}

nlohmann::json GraphConfigurationService::PatchConfig(const nlohmann::json &request) {
  return ApplyScenarioRequest(request, true);
}

nlohmann::json GraphConfigurationService::ValidateConfig(const nlohmann::json &request) const {
  nlohmann::json response = request;
  if (response.contains("pointer") || response.contains("operations")) {
    return const_cast<GraphConfigurationService *>(this)->ApplyScenarioRequest(request, false);
  }
  const auto validation = ValidateScenario(scenario_);
  return nlohmann::json{{"schema", "graphx.dashboard.config_validation.v1"},
                        {"validation", BuildValidationJson(validation)}};
}

nlohmann::json GraphConfigurationService::UndoLastEdit() {
  if (undo_stack_.empty()) {
    return BuildErrorResponse(409, "undo_not_available", "no prior edit is available to undo");
  }
  auto previous = undo_stack_.back();
  undo_stack_.pop_back();
  PendingChange change{.request_fingerprint = "undo",
                       .scenario = previous,
                       .validation = ValidateScenario(previous),
                       .regenerated_targets = BuildDerivedTargetList()};
  return CommitScenarioChange(change, "undo", revision_, true);
}

nlohmann::json GraphConfigurationService::DiscardEdits() {
  undo_stack_.clear();
  staged_scenario_ = scenario_;
  return nlohmann::json{{"schema", "graphx.dashboard.config_result.v1"},
                        {"status", "discarded"},
                        {"old_revision", revision_},
                        {"new_revision", revision_},
                        {"validation", BuildValidationJson(ValidateScenario(scenario_))}};
}

nlohmann::json GraphConfigurationService::GetScenarioResponse() const {
  return nlohmann::json{{"schema", "graphx.dashboard.fhss_scenario.v1"},
                        {"owner", owner_},
                        {"config_revision", revision_},
                        {"scenario", scenario_},
                        {"derived_paths", GeneratedPathsJson()},
                        {"validation", BuildValidationJson(ValidateScenario(scenario_))}};
}

nlohmann::json GraphConfigurationService::GetDerivedPathsResponse() const {
  return nlohmann::json{{"schema", "graphx.dashboard.derived_paths.v1"},
                        {"config_revision", revision_},
                        {"paths", GeneratedPathsJson()}};
}

nlohmann::json GraphConfigurationService::GetValueResponse(const std::string &pointer) const {
  const auto normalized = NormalizeScenarioPointer(pointer);
  try {
    if (pointer.rfind(kScenarioRoot, 0) == 0 || pointer.rfind(kScenarioAliasRoot, 0) == 0) {
      if (normalized.empty() || normalized == "/") {
        return nlohmann::json{{"schema", "graphx.dashboard.value.v1"}, {"pointer", pointer}, {"value", scenario_}};
      }
      return nlohmann::json{{"schema", "graphx.dashboard.value.v1"},
                            {"pointer", pointer},
                            {"value", scenario_.at(nlohmann::json::json_pointer(normalized))}};
    }
    if (pointer.rfind("/nodes/", 0) == 0) {
      return nlohmann::json{{"schema", "graphx.dashboard.value.v1"},
                            {"pointer", pointer},
                            {"value", effective_graph_.at(nlohmann::json::json_pointer(pointer))}};
    }
  } catch (const std::exception &ex) {
    return BuildErrorResponse(404, "pointer_not_found", ex.what(), nullptr, {}, revision_, pointer);
  }
  return BuildErrorResponse(404, "pointer_not_found", "pointer not found", nullptr, {}, revision_, pointer);
}

nlohmann::json GraphConfigurationService::GetNodeResponse(const std::string &node_id) const {
  if (const auto *node = FindNodeById(effective_graph_, node_id); node) {
    return nlohmann::json{{"schema", "graphx.dashboard.node.v1"},
                          {"config_revision", revision_},
                          {"node", *node}};
  }
  return BuildErrorResponse(404, "node_not_found", "node not found", nullptr, {}, revision_, node_id);
}

nlohmann::json GraphConfigurationService::GetNodeParametersResponse(const std::string &node_id) const {
  const auto *node = FindNodeById(effective_graph_, node_id);
  if (!node) {
    return BuildErrorResponse(404, "node_not_found", "node not found", nullptr, {}, revision_, node_id);
  }
  nlohmann::json node_config = node->contains("node_config") ? (*node)["node_config"] : nlohmann::json::object();
  nlohmann::json parameters{{"configured", nlohmann::json::object()},
                            {"runtime_reported", nlohmann::json::object()},
                            {"staged", node_config},
                            {"descriptions", nlohmann::json::object()},
                            {"provenance", nlohmann::json::object()}};
  if (node_id == "source") {
    parameters["configured"] = scenario_;
    parameters["provenance"]["source"] = "effective_config";
  }
  return nlohmann::json{{"schema", "graphx.dashboard.node_parameters.v1"},
                        {"config_revision", revision_},
                        {"node_id", node_id},
                        {"node", *node},
                        {"parameters", parameters},
                        {"ports", nlohmann::json{{"inputs", nlohmann::json::array()},
                                                  {"outputs", nlohmann::json::array()}}}};
}

nlohmann::json GraphConfigurationService::GetGraphResponse() const {
  return nlohmann::json{{"schema", "graphx.dashboard.graph.v1"},
                        {"owner", owner_},
                        {"config_revision", revision_},
                        {"graph", effective_graph_}};
}

nlohmann::json GraphConfigurationService::GetConfigResponse() const {
  return nlohmann::json{{"schema", "graphx.dashboard.config.v1"},
                        {"owner", owner_},
                        {"config_revision", revision_},
                        {"authoritative", scenario_},
                        {"effective", effective_graph_},
                        {"derived_paths", GeneratedPathsJson()}};
}

nlohmann::json GraphConfigurationService::GetOperationResponse(const std::string &operation_id) const {
  if (const auto operation = FindOperation(operation_id); operation) {
    return BuildOperationJson(*operation);
  }
  return BuildErrorResponse(404, "operation_not_found_or_expired",
                            "operation not found or expired", nullptr, {}, revision_, operation_id);
}

nlohmann::json GraphConfigurationService::ExportConfig(const nlohmann::json &request) {
  const auto expected_revision = request.value("expected_revision", revision_);
  if (expected_revision != revision_) {
    return BuildErrorResponse(409, "stale_revision_conflict",
                              "expected revision does not match current revision",
                              nlohmann::json{{"current_revision", revision_}},
                              request.value("command_id", std::string{}), revision_);
  }

  const auto output_path_value = request.value("output_path", std::string{});
  if (output_path_value.empty()) {
    return BuildErrorResponse(400, "missing_output_path", "output_path is required",
                              nullptr, request.value("command_id", std::string{}), revision_);
  }

  const std::filesystem::path output_path = output_path_value;
  if (!output_path.is_absolute()) {
    return BuildErrorResponse(400, "artifact_path_not_allowed",
                              "output_path must be absolute", nullptr,
                              request.value("command_id", std::string{}), revision_, output_path_value);
  }
  if (!artifact_root_.empty() && !IsUnderRoot(artifact_root_, output_path.parent_path())) {
    return BuildErrorResponse(400, "artifact_path_not_allowed",
                              "output_path must stay under the artifact root", nullptr,
                              request.value("command_id", std::string{}), revision_, output_path_value);
  }

  const auto command_id = request.value("command_id", std::string{});
  const auto normalized_fingerprint = NormalizeJson(request);
  if (!command_id.empty()) {
    const auto existing = command_index_.find(command_id);
    if (existing != command_index_.end()) {
      const auto operation = FindOperation(existing->second);
      if (operation && operation->request_fingerprint != normalized_fingerprint) {
        return BuildErrorResponse(409, "idempotency_key_reused_with_different_payload",
                                  "command_id already exists for a different request",
                                  nullptr, command_id, revision_);
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
  operation.submitted_revision = revision_;
  operation.created_at = NowIso8601();
  operation.created_at_time = Now();
  operation.validation = ValidateScenario(scenario_);

  if (update_event_sink_) {
    update_event_sink_(nlohmann::json{{"sequence", 1}, {"status", "queued"}, {"command_id", command_id}});
    update_event_sink_(nlohmann::json{{"sequence", 2}, {"status", "running"}, {"command_id", command_id}});
  }

  operation.status = "running";
  std::error_code error;
  std::filesystem::create_directories(output_path.parent_path(), error);
  if (error) {
    operation.status = "failed";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result = BuildErrorResponse(500, "artifact_write_failed", error.message(), nullptr,
                                         command_id, revision_, output_path_value);
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
      operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
      operation.expires_at = ToIso8601(operation.expires_at_time);
      operation.result = BuildErrorResponse(500, injector_error->code, injector_error->message,
                                           injector_error->details, command_id, revision_, injector_error->pointer);
      const auto operation_id = RegisterOperation(operation);
      auto stored = FindOperation(operation_id);
      return BuildOperationJson(stored ? *stored : operation);
    }
  }

  const auto resource = request.value("resource", std::string{"effective"});
  const auto &payload = resource == "authoritative" ? scenario_ : effective_graph_;
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.good()) {
    operation.status = "failed";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result = BuildErrorResponse(500, "artifact_write_failed", "failed to open output file",
                                         nullptr, command_id, revision_, output_path_value);
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
    operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    operation.result = BuildErrorResponse(500, "artifact_write_failed", "failed to write output file",
                                         nullptr, command_id, revision_, output_path_value);
    const auto operation_id = RegisterOperation(operation);
    auto stored = FindOperation(operation_id);
    return BuildOperationJson(stored ? *stored : operation);
  }

  operation.status = "succeeded";
  operation.terminal = true;
  operation.completed_at = NowIso8601();
  operation.completed_at_time = Now();
  operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
  operation.result = nlohmann::json{{"output_path", output_path_value},
                                    {"resource", resource},
                                    {"bytes_written", std::filesystem::file_size(output_path, error)}};
  const auto operation_id = RegisterOperation(operation);
  if (update_event_sink_) {
    update_event_sink_(nlohmann::json{{"sequence", 3}, {"status", "succeeded"}, {"command_id", command_id}});
  }
  auto stored = FindOperation(operation_id);
  return BuildOperationJson(stored ? *stored : operation);
}

nlohmann::json GraphConfigurationService::CancelOperation(const std::string &operation_id) {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsUnlocked();
  for (auto &operation : operations_) {
    if (operation.operation_id != operation_id) {
      continue;
    }
    if (operation.terminal) {
      return BuildErrorResponse(409, "operation_not_terminal",
                                "terminal operations cannot be cancelled", nullptr, operation.command_id,
                                operation.submitted_revision, operation_id);
    }
    operation.status = "cancelled";
    operation.terminal = true;
    operation.completed_at = NowIso8601();
    operation.completed_at_time = Now();
    operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
    operation.expires_at = ToIso8601(operation.expires_at_time);
    return BuildOperationJson(operation);
  }
  return BuildErrorResponse(404, "operation_not_found_or_expired",
                            "operation not found or expired", nullptr, {}, revision_, operation_id);
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
