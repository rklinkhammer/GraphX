/**
 * @file ConfigurationStateMachine.hpp
 * @brief State machine for FHSS configuration lifecycle management.
 *
 * @details Manages configuration lifecycle: Source → Effective → Staged → Active.
 * Tracks state transitions, revision counter, ETags, and undo/redo stack.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSConfigurationDeriver.hpp"
#include <vector>
#include <string>
#include <expected>
#include <optional>
#include <memory>

namespace dsp::fhss {

/**
 * @enum ConfigurationState
 * @brief Configuration lifecycle states.
 */
enum class ConfigurationState {
  Source,      // Authoritative user input (18 fields)
  Effective,   // Derived auto-computed output (source + 12 generated)
  Staged,      // Tentative edits (pending validation/commit)
  Active,      // Currently executing configuration
};

/**
 * @struct ConfigurationRevision
 * @brief Single configuration snapshot in history.
 */
struct ConfigurationRevision {
  uint64_t revision_number = 0;
  std::string etag;  // Format: "Rev:123"
  EffectiveConfiguration configuration;
  std::string timestamp;  // ISO 8601 format
  std::string description;  // Why this revision was created
};

/**
 * @struct StateMachineError
 * @brief Error from state machine operation.
 */
struct StateMachineError {
  std::string code;
  std::string message;
};

/**
 * @class ConfigurationStateMachine
 * @brief Manages FHSS configuration lifecycle with revision tracking.
 *
 * **State Machine Lifecycle:**
 * ```
 * Source ──validate──> Effective
 *                        ↓
 *                    [derive all 12 fields]
 *                        ↓
 *                    Effective + Staged
 *                        ↓
 *                    [validate staged edits]
 *                        ↓
 *                    Effective ──commit──> Active
 * ```
 *
 * **Key Properties:**
 * - Current state always valid (never in intermediate state)
 * - Staged edits don't affect Active (lazy commit)
 * - Revision counter monotonically increases (never resets)
 * - ETag format: "Rev:123" for If-Match preconditions
 * - Concurrent requests handled with optimistic locking (ETag validation)
 * - Undo/Redo stack maintains last 10 revisions
 */
class ConfigurationStateMachine {
public:
  /**
   * @brief Construct state machine with initial configuration.
   *
   * @param initial_source Initial source configuration
   */
  ConfigurationStateMachine(const SourceConfiguration& initial_source);

  /**
   * @brief Get current state.
   *
   * @return Current configuration state
   */
  [[nodiscard]] ConfigurationState GetCurrentState() const;

  /**
   * @brief Get current effective configuration.
   *
   * @return Const reference to effective configuration
   */
  [[nodiscard]] const EffectiveConfiguration& GetEffectiveConfiguration() const;

  /**
   * @brief Get current active configuration (if any).
   *
   * @return Optional active configuration
   */
  [[nodiscard]] std::optional<EffectiveConfiguration> GetActiveConfiguration() const;

  /**
   * @brief Get current revision number.
   *
   * @return Current revision number
   */
  [[nodiscard]] uint64_t GetCurrentRevision() const;

  /**
   * @brief Get current ETag.
   *
   * @return Current ETag in format "Rev:123"
   */
  [[nodiscard]] std::string GetCurrentETag() const;

  /**
   * @brief Stage an edit to the source configuration.
   *
   * @param staged_source Edited source configuration (partial)
   * @return Expected success or error
   */
  [[nodiscard]] std::expected<void, StateMachineError>
  StageEdit(const SourceConfiguration& staged_source);

  /**
   * @brief Validate staged edits and transition to Effective state.
   *
   * @return Expected success or error
   */
  [[nodiscard]] std::expected<void, StateMachineError>
  ValidateStaged();

  /**
   * @brief Commit staged changes to active configuration.
   *
   * @param etag_check If provided, verifies ETag matches before commit
   * @param description Human-readable description of change
   * @return Expected success or error (409 Conflict if ETag mismatch)
   */
  [[nodiscard]] std::expected<void, StateMachineError>
  CommitStaged(const std::optional<std::string>& etag_check = {},
               const std::string& description = "");

  /**
   * @brief Undo last revision.
   *
   * @return Expected success or error (if no undo available)
   */
  [[nodiscard]] std::expected<void, StateMachineError>
  Undo();

  /**
   * @brief Redo next revision.
   *
   * @return Expected success or error (if no redo available)
   */
  [[nodiscard]] std::expected<void, StateMachineError>
  Redo();

  /**
   * @brief Get list of available undo revisions (last 10).
   *
   * @return Vector of revision snapshots
   */
  [[nodiscard]] std::vector<ConfigurationRevision> GetUndoStack() const;

  /**
   * @brief Get list of available redo revisions (last 10).
   *
   * @return Vector of revision snapshots
   */
  [[nodiscard]] std::vector<ConfigurationRevision> GetRedoStack() const;

  /**
   * @brief Check if undo is available.
   *
   * @return true if undo is possible
   */
  [[nodiscard]] bool CanUndo() const;

  /**
   * @brief Check if redo is available.
   *
   * @return true if redo is possible
   */
  [[nodiscard]] bool CanRedo() const;

  /**
   * @brief Get revision history (all available revisions).
   *
   * @return Vector of all revisions in order
   */
  [[nodiscard]] std::vector<ConfigurationRevision> GetRevisionHistory() const;

  /**
   * @brief Get specific revision by number.
   *
   * @param revision_number Revision to retrieve
   * @return Optional configuration or empty if not found
   */
  [[nodiscard]] std::optional<ConfigurationRevision>
  GetRevision(uint64_t revision_number) const;

private:
  /// Current state
  ConfigurationState current_state_;

  /// Current effective configuration
  EffectiveConfiguration effective_config_;

  /// Staged (uncommitted) edits
  std::optional<SourceConfiguration> staged_config_;

  /// Currently active configuration (deployed)
  std::optional<EffectiveConfiguration> active_config_;

  /// Current revision number (increments monotonically)
  uint64_t revision_counter_;

  /// Revision history (maintains last 10 revisions)
  std::vector<ConfigurationRevision> revision_history_;

  /// Position in history for undo/redo
  size_t history_position_;

  /// Maximum revisions to keep in history
  static constexpr size_t kMaxHistorySize = 10;

  /// Helper to create error
  [[nodiscard]] static StateMachineError
  MakeError(const std::string& code, const std::string& message);

  /// Helper to save revision to history
  void SaveRevisionToHistory(const std::string& description);

  /// Helper to get current timestamp (ISO 8601)
  [[nodiscard]] static std::string GetCurrentTimestamp();
};

}  // namespace dsp::fhss
