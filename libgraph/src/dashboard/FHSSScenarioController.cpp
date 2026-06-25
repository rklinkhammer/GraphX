// SPDX-License-Identifier: MIT

#include "graph/dashboard/FHSSScenarioController.hpp"

#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace graph::dashboard {

FHSSScenarioController::FHSSScenarioController(
    std::shared_ptr<GraphConfigurationService> configuration_service,
    std::shared_ptr<GraphRuntimeSession> runtime_session)
    : configuration_service_(std::move(configuration_service)),
      runtime_session_(std::move(runtime_session)) {
  std::lock_guard lock(mutex_);
  ResetScenarioLifecycleLocked();
}

void FHSSScenarioController::BindInjectionSource(
    std::shared_ptr<IFHSSMessageInjectionSource> source) {
  std::lock_guard lock(mutex_);
  source_ = std::move(source);
}

FHSSScenarioController::CommandResponse
FHSSScenarioController::StepOneMessage(const nlohmann::json &request) {
  const auto normalized = NormalizeJson(request);
  std::optional<FHSSMessageCorrelation> correlation_to_complete;
  CommandResponse response;
  {
    std::lock_guard lock(mutex_);
    if (const auto replay = ReplayIfIdempotent(request, normalized); replay) {
      const auto status = replay->value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? replay->value("status", 409)
                              : 202;
      return CommandResponse{.status_code = status, .body = *replay};
    }
    if (active_release_) {
      return CommandResponse{.status_code = 409,
                             .body = ErrorBody(409, "message_in_flight",
                                               "a message is already queued or in flight")};
    }

    OperationRecord operation;
    operation.command_id = request.value("command_id", std::string{});
    operation.request_fingerprint = normalized;
    operation.status = "queued";
    operation.submitted_revision = configuration_service_ ? configuration_service_->ConfigRevision() : 0;
    operation.created_at = NowIso8601();
    operation.created_at_time = Now();
    const auto operation_id = RegisterOperation(operation);
    ScheduleNextMessageLocked(operation_id, operation.submitted_revision, false);
    response = CommandResponse{.status_code = 202,
                               .body = BuildOperationJson(operations_.back())};
    if (auto_complete_for_testing_ && active_release_) {
      correlation_to_complete = active_release_->correlation;
    }
  }
  if (correlation_to_complete) {
    MaybeAutoComplete(*correlation_to_complete);
    const auto refreshed = GetOperationResponseIfKnown(response.body.at("operation_id").get<std::string>());
    if (refreshed) {
      response.body = *refreshed;
    }
  }
  return response;
}

FHSSScenarioController::CommandResponse
FHSSScenarioController::Continue(const nlohmann::json &request) {
  const auto normalized = NormalizeJson(request);
  std::optional<FHSSMessageCorrelation> correlation_to_complete;
  CommandResponse response;
  {
    std::lock_guard lock(mutex_);
    if (const auto replay = ReplayIfIdempotent(request, normalized); replay) {
      const auto status = replay->value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? replay->value("status", 409)
                              : 202;
      return CommandResponse{.status_code = status, .body = *replay};
    }
    if (active_release_ || continue_mode_) {
      return CommandResponse{.status_code = 409,
                             .body = ErrorBody(409, "message_in_flight",
                                               "continue is not allowed while a message is active")};
    }

    OperationRecord operation;
    operation.command_id = request.value("command_id", std::string{});
    operation.request_fingerprint = normalized;
    operation.status = "queued";
    operation.submitted_revision = configuration_service_ ? configuration_service_->ConfigRevision() : 0;
    operation.created_at = NowIso8601();
    operation.created_at_time = Now();
    const auto operation_id = RegisterOperation(operation);
    continue_mode_ = true;
    continue_operation_id_ = operation_id;
    continue_completed_messages_ = 0;
    ScheduleNextMessageLocked(operation_id, operation.submitted_revision, true);
    response = CommandResponse{.status_code = 202,
                               .body = BuildOperationJson(operations_.back())};
    if (auto_complete_for_testing_ && active_release_) {
      correlation_to_complete = active_release_->correlation;
    }
  }
  if (correlation_to_complete) {
    MaybeAutoComplete(*correlation_to_complete);
    const auto refreshed = GetOperationResponseIfKnown(response.body.at("operation_id").get<std::string>());
    if (refreshed) {
      response.body = *refreshed;
    }
  }
  return response;
}

FHSSScenarioController::CommandResponse
FHSSScenarioController::Reset(const nlohmann::json &request) {
  const auto normalized = NormalizeJson(request);
  std::lock_guard lock(mutex_);
  if (const auto replay = ReplayIfIdempotent(request, normalized); replay) {
    const auto status = replay->value("schema", std::string{}) == "graphx.dashboard.error.v1"
                            ? replay->value("status", 409)
                            : 202;
    return CommandResponse{.status_code = status, .body = *replay};
  }
  if (active_release_) {
    return CommandResponse{.status_code = 409,
                           .body = ErrorBody(409, "message_in_flight",
                                             "reset is not allowed while a message is in flight")};
  }

  OperationRecord operation;
  operation.command_id = request.value("command_id", std::string{});
  operation.request_fingerprint = normalized;
  operation.status = "running";
  operation.submitted_revision = configuration_service_ ? configuration_service_->ConfigRevision() : 0;
  operation.created_at = NowIso8601();
  operation.created_at_time = Now();
  const auto operation_id = RegisterOperation(operation);

  continue_mode_ = false;
  continue_operation_id_.clear();
  pending_by_correlation_.clear();
  if (source_) {
    source_->ResetMessageInjectionQueue();
  }
  ResetScenarioLifecycleLocked();
  CompleteOperationLocked(operation_id, "succeeded",
                          nlohmann::json{{"command", "reset"},
                                         {"scenario_id", scenario_id_}});
  return CommandResponse{.status_code = 202,
                         .body = BuildOperationJson(operations_.back())};
}

std::optional<nlohmann::json>
FHSSScenarioController::GetOperationResponseIfKnown(const std::string &operation_id) const {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsLocked();
  const auto index = FindOperationIndex(operation_id);
  if (!index) {
    return std::nullopt;
  }
  return BuildOperationJson(operations_.at(*index));
}

std::optional<nlohmann::json>
FHSSScenarioController::CancelOperationIfKnown(const std::string &operation_id) {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsLocked();
  const auto index = FindOperationIndex(operation_id);
  if (!index) {
    return std::nullopt;
  }
  auto &operation = operations_.at(*index);
  if (operation.terminal) {
    return ErrorBody(409, "operation_not_terminal",
                     "terminal operations cannot be cancelled");
  }
  operation.status = "cancelled";
  operation.terminal = true;
  operation.completed_at_time = Now();
  operation.completed_at = ToIso8601(operation.completed_at_time);
  operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
  operation.result = nlohmann::json{{"terminal_status", "cancelled"}};
  if (active_release_ && active_release_->operation_id == operation.operation_id) {
    pending_by_correlation_.erase(CorrelationKey(active_release_->correlation));
    active_release_.reset();
  }
  if (continue_mode_ && operation.operation_id == continue_operation_id_) {
    continue_mode_ = false;
    continue_operation_id_.clear();
    continue_completed_messages_ = 0;
  }
  return BuildOperationJson(operation);
}

std::optional<bool>
FHSSScenarioController::DeleteOperationIfKnown(const std::string &operation_id,
                                               std::string *error_code) {
  std::lock_guard lock(mutex_);
  PurgeExpiredOperationsLocked();
  const auto index = FindOperationIndex(operation_id);
  if (!index) {
    return std::nullopt;
  }
  if (!operations_.at(*index).terminal) {
    if (error_code) {
      *error_code = "operation_not_terminal";
    }
    return false;
  }
  if (!operations_.at(*index).command_id.empty()) {
    command_index_.erase(operations_.at(*index).command_id);
  }
  operations_.erase(operations_.begin() + static_cast<std::ptrdiff_t>(*index));
  return true;
}

void FHSSScenarioController::PublishTerminalResultForTesting(
    const FHSSMessageTerminalResult &result) {
  std::lock_guard lock(mutex_);
  const auto pending = pending_by_correlation_.find(CorrelationKey(result.correlation));
  if (pending == pending_by_correlation_.end()) {
    ++orphan_terminal_results_;
    return;
  }
  const auto index = FindOperationIndex(pending->second.operation_id);
  if (!index) {
    ++orphan_terminal_results_;
    pending_by_correlation_.erase(pending);
    return;
  }
  auto &operation = operations_.at(*index);
  if (operation.terminal) {
    ++duplicate_terminal_results_;
    return;
  }

  const bool is_continue_operation =
      continue_mode_ && pending->second.operation_id == continue_operation_id_;

  if (is_continue_operation && result.status == FHSSMessageTerminalStatus::Completed) {
    ++continue_completed_messages_;
    pending_by_correlation_.erase(pending);
    if (active_release_ &&
        CorrelationKey(active_release_->correlation) == CorrelationKey(result.correlation)) {
      active_release_.reset();
    }
    const auto schedule_result =
        ScheduleNextMessageLocked(continue_operation_id_, operation.submitted_revision, true);
    if (schedule_result && schedule_result->value("terminal", false)) {
      continue_mode_ = false;
      continue_operation_id_.clear();
      continue_completed_messages_ = 0;
    }
    return;
  }

  const auto status = result.status == FHSSMessageTerminalStatus::Completed ? "succeeded"
                     : result.status == FHSSMessageTerminalStatus::Cancelled ? "cancelled"
                                                                             : "failed";
  CompleteOperationLocked(
      operation.operation_id, status,
      nlohmann::json{{"terminal_status", TerminalStatusToString(result.status)},
                     {"code", result.code},
                     {"message", result.message},
                     {"correlation",
                      nlohmann::json{{"scenario_id", result.correlation.scenario_id},
                                     {"message_id", result.correlation.message_id},
                                     {"release_sequence", result.correlation.release_sequence}}},
                     {"diagnostics", result.diagnostics}});
  pending_by_correlation_.erase(pending);
  if (active_release_ && CorrelationKey(active_release_->correlation) == CorrelationKey(result.correlation)) {
    active_release_.reset();
  }
  if (is_continue_operation) {
    continue_mode_ = false;
    continue_operation_id_.clear();
    continue_completed_messages_ = 0;
  }
}

void FHSSScenarioController::SetAutoCompleteForTesting(bool enabled) {
  std::lock_guard lock(mutex_);
  auto_complete_for_testing_ = enabled;
}

std::optional<FHSSMessageCorrelation>
FHSSScenarioController::ActiveCorrelationForTesting() const {
  std::lock_guard lock(mutex_);
  if (!active_release_) {
    return std::nullopt;
  }
  return active_release_->correlation;
}

std::string FHSSScenarioController::NowIso8601() { return ToIso8601(Now()); }

std::chrono::system_clock::time_point FHSSScenarioController::Now() {
  return std::chrono::system_clock::now();
}

std::string FHSSScenarioController::ToIso8601(
    std::chrono::system_clock::time_point time_point) {
  if (time_point == std::chrono::system_clock::time_point{}) {
    return {};
  }
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(time_point);
  const auto fractional =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_point - seconds).count();
  const auto tt = std::chrono::system_clock::to_time_t(seconds);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
         << std::setfill('0') << fractional << 'Z';
  return stream.str();
}

std::string FHSSScenarioController::NormalizeJson(const nlohmann::json &json) {
  return json.dump();
}

std::string FHSSScenarioController::CorrelationKey(
    const FHSSMessageCorrelation &correlation) {
  return correlation.scenario_id + ':' + std::to_string(correlation.message_id) +
         ':' + std::to_string(correlation.release_sequence);
}

nlohmann::json FHSSScenarioController::ErrorBody(int status_code,
                                                 const std::string &code,
                                                 const std::string &message,
                                                 nlohmann::json details) {
  nlohmann::json body{{"schema", "graphx.dashboard.error.v1"},
                      {"status", status_code},
                      {"code", code},
                      {"message", message},
                      {"retriable", status_code >= 500}};
  if (!details.is_null()) {
    body["details"] = std::move(details);
  }
  return body;
}

std::string FHSSScenarioController::TerminalStatusToString(
    FHSSMessageTerminalStatus status) {
  switch (status) {
  case FHSSMessageTerminalStatus::Completed:
    return "completed";
  case FHSSMessageTerminalStatus::Rejected:
    return "rejected";
  case FHSSMessageTerminalStatus::Failed:
    return "failed";
  case FHSSMessageTerminalStatus::TimedOut:
    return "timed_out";
  case FHSSMessageTerminalStatus::Cancelled:
    return "cancelled";
  }
  return "failed";
}

nlohmann::json FHSSScenarioController::BuildOperationJson(
    const OperationRecord &operation) const {
  nlohmann::json json{{"schema", "graphx.dashboard.operation.v1"},
                      {"operation_id", operation.operation_id},
                      {"command_id", operation.command_id},
                      {"status", operation.status},
                      {"submitted_revision", operation.submitted_revision},
                      {"terminal", operation.terminal},
                      {"created_at", operation.created_at},
                      {"completed_at", operation.completed_at},
                      {"expires_at", operation.expires_at}};
  if (!operation.result.is_null()) {
    json["result"] = operation.result;
  }
  return json;
}

std::optional<std::size_t>
FHSSScenarioController::FindOperationIndex(const std::string &operation_id) const {
  for (std::size_t index = 0; index < operations_.size(); ++index) {
    if (operations_[index].operation_id == operation_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<nlohmann::json>
FHSSScenarioController::ReplayIfIdempotent(const nlohmann::json &request,
                                           const std::string &normalized_fingerprint) const {
  const auto command_id = request.value("command_id", std::string{});
  if (command_id.empty()) {
    return std::nullopt;
  }
  const auto existing = command_index_.find(command_id);
  if (existing == command_index_.end()) {
    return std::nullopt;
  }
  const auto index = FindOperationIndex(existing->second);
  if (!index) {
    return std::nullopt;
  }
  const auto &operation = operations_.at(*index);
  if (operation.request_fingerprint != normalized_fingerprint) {
    return ErrorBody(409, "idempotency_key_reused_with_different_payload",
                     "command_id already exists for a different request");
  }
  return BuildOperationJson(operation);
}

std::string FHSSScenarioController::RegisterOperation(OperationRecord operation) {
  operation.operation_id = "fhss-op-" + std::to_string(next_operation_id_++);
  operation.expires_at_time = operation.created_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
  operations_.push_back(std::move(operation));
  if (!operations_.back().command_id.empty()) {
    command_index_[operations_.back().command_id] = operations_.back().operation_id;
  }
  return operations_.back().operation_id;
}

void FHSSScenarioController::PurgeExpiredOperationsLocked() const {
  const auto now = Now();
  while (!operations_.empty() &&
         operations_.front().expires_at_time != std::chrono::system_clock::time_point{} &&
         operations_.front().expires_at_time <= now) {
    if (!operations_.front().command_id.empty()) {
      command_index_.erase(operations_.front().command_id);
    }
    operations_.pop_front();
  }
}

void FHSSScenarioController::CompleteOperationLocked(const std::string &operation_id,
                                                     const std::string &status,
                                                     nlohmann::json result) {
  const auto index = FindOperationIndex(operation_id);
  if (!index) {
    return;
  }
  auto &operation = operations_.at(*index);
  operation.status = status;
  operation.terminal = true;
  operation.result = std::move(result);
  operation.completed_at_time = Now();
  operation.completed_at = ToIso8601(operation.completed_at_time);
  operation.expires_at_time = operation.completed_at_time + std::chrono::hours(24);
  operation.expires_at = ToIso8601(operation.expires_at_time);
}

void FHSSScenarioController::FailOperationLocked(const std::string &operation_id,
                                                 const std::string &code,
                                                 const std::string &message,
                                                 nlohmann::json details) {
  CompleteOperationLocked(operation_id, "failed",
                          ErrorBody(500, code, message, std::move(details)));
}

std::optional<nlohmann::json>
FHSSScenarioController::ScheduleNextMessageLocked(const std::string &operation_id,
                                                  std::uint64_t submitted_revision,
                                                  bool continue_mode) {
  (void)submitted_revision;
  const auto messages = CurrentMessages();
  if (!source_) {
    FailOperationLocked(operation_id, "fhss_source_unavailable",
                        "FHSS injection source is not available");
    return BuildOperationJson(operations_.back());
  }
  if (next_message_index_ >= messages.size()) {
    CompleteOperationLocked(operation_id, "succeeded",
                            nlohmann::json{{"command", continue_mode ? "continue" : "step-message"},
                                           {"message_count", continue_mode ? continue_completed_messages_ : 0}});
    return BuildOperationJson(operations_.back());
  }
  auto &queue = source_->GetMessageInjectionQueue();
  const auto &message = messages.at(next_message_index_);
  const auto correlation = FHSSMessageCorrelation{.scenario_id = scenario_id_,
                                                  .message_id = message.value("message_id", std::uint64_t{0}),
                                                  .release_sequence = next_release_sequence_++};
  const FHSSMessageInjectionRequest injection_request{
      .kind = FHSSMessageInjectionKind::ScheduledMessage,
      .correlation = correlation,
      .scheduled_message = message,
      .end_of_stream_after_produce = continue_mode && next_message_index_ + 1 >= messages.size()};
  if (!queue.Enqueue(injection_request)) {
    FailOperationLocked(operation_id, "queue_enqueue_failed",
                        "failed to enqueue the next FHSS message request");
    return BuildOperationJson(operations_.back());
  }
  const auto index = FindOperationIndex(operation_id);
  if (index) {
    operations_.at(*index).status = "running";
  }
  active_release_ = PendingRelease{.operation_id = operation_id, .correlation = correlation};
  pending_by_correlation_[CorrelationKey(correlation)] = *active_release_;
  ++next_message_index_;
  return std::nullopt;
}

void FHSSScenarioController::MaybeAutoComplete(const FHSSMessageCorrelation &correlation) {
  if (!auto_complete_for_testing_ || !source_) {
    return;
  }
  std::optional<FHSSMessageCorrelation> next_correlation = correlation;
  while (next_correlation) {
    const auto produced = source_->ProduceInjectedMessage();
    if (!produced) {
      PublishTerminalResultForTesting(FHSSMessageTerminalResult{
          .correlation = *next_correlation,
          .status = FHSSMessageTerminalStatus::Failed,
          .code = "queue_disabled",
          .message = "source queue disabled before production completed"});
      return;
    }
    PublishTerminalResultForTesting(FHSSMessageTerminalResult{
        .correlation = *produced,
        .status = FHSSMessageTerminalStatus::Completed,
        .code = "message_completed",
        .message = "exactly one scheduled message completed"});
    next_correlation = ActiveCorrelationForTesting();
  }
}

std::vector<nlohmann::json> FHSSScenarioController::CurrentMessages() const {
  if (!configuration_service_) {
    return {};
  }
  const auto scenario =
      configuration_service_->GetScenarioResponse().value("scenario", nlohmann::json::object());
  if (!scenario.contains("messages") || !scenario.at("messages").is_array()) {
    return {};
  }
  std::vector<nlohmann::json> messages;
  for (const auto &message : scenario.at("messages")) {
    messages.push_back(message);
  }
  return messages;
}

void FHSSScenarioController::ResetScenarioLifecycleLocked() {
  scenario_id_ = "scenario-" + std::to_string(next_scenario_id_++);
  next_release_sequence_ = 1;
  next_message_index_ = 0;
  continue_completed_messages_ = 0;
  active_release_.reset();
}

} // namespace graph::dashboard