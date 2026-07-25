/**
 * @file ConfigurationStateMachine.cpp
 * @brief Implementation of FHSS configuration state machine.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/fhss/ConfigurationStateMachine.hpp"
#include "dsp/fhss/FHSSCrossNodeValidator.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace dsp::fhss {

// ============================================================================
// Constructor and Utilities
// ============================================================================

ConfigurationStateMachine::ConfigurationStateMachine(
    const SourceConfiguration& initial_source)
    : current_state_(ConfigurationState::Source),
      revision_counter_(0),
      history_position_(0) {
  // Derive initial effective configuration
  auto result = FHSSConfigurationDeriver::Derive(initial_source);
  if (result.has_value()) {
    effective_config_ = result.value();
    effective_config_.revision = ++revision_counter_;
    effective_config_.etag = "Rev:" + std::to_string(revision_counter_);
    current_state_ = ConfigurationState::Effective;

    // Save to history
    SaveRevisionToHistory("Initial configuration");
  }
}

StateMachineError ConfigurationStateMachine::MakeError(
    const std::string& code, const std::string& message) {
  return StateMachineError{code, message};
}

std::string ConfigurationStateMachine::GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count() %
      1000;

  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
  oss << "." << std::setfill('0') << std::setw(3) << ms << "Z";

  return oss.str();
}

void ConfigurationStateMachine::SaveRevisionToHistory(
    const std::string& description) {
  ConfigurationRevision rev;
  rev.revision_number = revision_counter_;
  rev.etag = effective_config_.etag;
  rev.configuration = effective_config_;
  rev.timestamp = GetCurrentTimestamp();
  rev.description = description;

  // Truncate redo stack
  revision_history_.erase(revision_history_.begin() + history_position_,
                          revision_history_.end());

  // Add new revision
  revision_history_.push_back(rev);

  // Keep only last kMaxHistorySize revisions
  if (revision_history_.size() > kMaxHistorySize) {
    revision_history_.erase(revision_history_.begin());
  } else {
    history_position_ = revision_history_.size() - 1;
  }
}

// ============================================================================
// State Query Methods
// ============================================================================

ConfigurationState ConfigurationStateMachine::GetCurrentState() const {
  return current_state_;
}

const EffectiveConfiguration&
ConfigurationStateMachine::GetEffectiveConfiguration() const {
  return effective_config_;
}

std::optional<EffectiveConfiguration>
ConfigurationStateMachine::GetActiveConfiguration() const {
  return active_config_;
}

uint64_t ConfigurationStateMachine::GetCurrentRevision() const {
  return revision_counter_;
}

std::string ConfigurationStateMachine::GetCurrentETag() const {
  return effective_config_.etag;
}

// ============================================================================
// Edit and Validation Methods
// ============================================================================

std::expected<void, StateMachineError>
ConfigurationStateMachine::StageEdit(const SourceConfiguration& staged_source) {
  // Store staged edits without modifying effective config
  staged_config_ = staged_source;
  current_state_ = ConfigurationState::Staged;

  return {};
}

std::expected<void, StateMachineError>
ConfigurationStateMachine::ValidateStaged() {
  if (!staged_config_.has_value()) {
    return std::unexpected(MakeError("ERR_NO_STAGED",
                                      "No staged configuration to validate"));
  }

  // Derive effective from staged
  auto result = FHSSConfigurationDeriver::Derive(staged_config_.value());
  if (!result.has_value()) {
    return std::unexpected(MakeError(
        "ERR_DERIVATION_FAILED",
        "Failed to derive configuration from staged changes: " +
            result.error().message));
  }

  // Validate the derived configuration
  auto validation_result = FHSSCrossNodeValidator::Validate(result.value());
  if (!validation_result.has_value()) {
    std::ostringstream error_msg;
    error_msg << "Configuration validation failed with "
              << validation_result.error().size() << " error(s):";
    for (const auto& err : validation_result.error()) {
      error_msg << " [" << static_cast<int>(err.code) << "] " << err.message;
    }

    return std::unexpected(
        MakeError("ERR_VALIDATION_FAILED", error_msg.str()));
  }

  // Update effective configuration
  effective_config_ = result.value();
  current_state_ = ConfigurationState::Effective;

  return {};
}

std::expected<void, StateMachineError>
ConfigurationStateMachine::CommitStaged(
    const std::optional<std::string>& etag_check,
    const std::string& description) {
  // Verify ETag if provided (optimistic locking)
  if (etag_check.has_value() && etag_check.value() != effective_config_.etag) {
    return std::unexpected(MakeError(
        "ERR_CONFLICT_409",
        "ETag mismatch: expected " + effective_config_.etag + " but got " +
            etag_check.value()));
  }

  // Require staged edits to be present
  if (!staged_config_.has_value()) {
    return std::unexpected(MakeError("ERR_NO_STAGED",
                                      "No staged configuration to commit"));
  }

  // Commit active configuration
  active_config_ = effective_config_;

  // Increment revision and save to history
  effective_config_.revision = ++revision_counter_;
  effective_config_.etag = "Rev:" + std::to_string(revision_counter_);

  SaveRevisionToHistory(description.empty() ? "Staged edit committed" : description);

  // Clear staged edits
  staged_config_.reset();
  current_state_ = ConfigurationState::Active;

  return {};
}

// ============================================================================
// Undo/Redo Methods
// ============================================================================

std::expected<void, StateMachineError>
ConfigurationStateMachine::Undo() {
  if (!CanUndo()) {
    return std::unexpected(
        MakeError("ERR_NO_UNDO", "No undo history available"));
  }

  // Move back in history
  --history_position_;

  effective_config_ = revision_history_[history_position_].configuration;
  revision_counter_ = revision_history_[history_position_].revision_number;

  return {};
}

std::expected<void, StateMachineError>
ConfigurationStateMachine::Redo() {
  if (!CanRedo()) {
    return std::unexpected(
        MakeError("ERR_NO_REDO", "No redo history available"));
  }

  // Move forward in history
  ++history_position_;

  effective_config_ = revision_history_[history_position_].configuration;
  revision_counter_ = revision_history_[history_position_].revision_number;

  return {};
}

bool ConfigurationStateMachine::CanUndo() const {
  return history_position_ > 0;
}

bool ConfigurationStateMachine::CanRedo() const {
  return history_position_ < revision_history_.size() - 1;
}

// ============================================================================
// History Query Methods
// ============================================================================

std::vector<ConfigurationRevision>
ConfigurationStateMachine::GetUndoStack() const {
  std::vector<ConfigurationRevision> undo_stack;

  if (history_position_ > 0) {
    // Return revisions before current position
    for (size_t i = history_position_ - 1; i > 0 && undo_stack.size() < 10;
         --i) {
      undo_stack.push_back(revision_history_[i - 1]);
    }
  }

  return undo_stack;
}

std::vector<ConfigurationRevision>
ConfigurationStateMachine::GetRedoStack() const {
  std::vector<ConfigurationRevision> redo_stack;

  if (history_position_ < revision_history_.size() - 1) {
    // Return revisions after current position
    for (size_t i = history_position_ + 1;
         i < revision_history_.size() && redo_stack.size() < 10; ++i) {
      redo_stack.push_back(revision_history_[i]);
    }
  }

  return redo_stack;
}

std::vector<ConfigurationRevision>
ConfigurationStateMachine::GetRevisionHistory() const {
  return revision_history_;
}

std::optional<ConfigurationRevision>
ConfigurationStateMachine::GetRevision(uint64_t revision_number) const {
  for (const auto& rev : revision_history_) {
    if (rev.revision_number == revision_number) {
      return rev;
    }
  }

  return std::nullopt;
}

}  // namespace dsp::fhss
