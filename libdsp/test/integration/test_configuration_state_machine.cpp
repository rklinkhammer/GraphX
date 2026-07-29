/**
 * @file test_configuration_state_machine.cpp
 * @brief Unit tests for ConfigurationStateMachine (25+ tests)
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "dsp/fhss/ConfigurationStateMachine.hpp"

using namespace dsp::fhss;
using json = nlohmann::json;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

SourceConfiguration SimpleValidConfiguration() {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 10}, {"message_id", "msg1"}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 100000.0;
  config.idle_duration_samples = 1000;
  return config;
}

// ============================================================================
// State Management Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Initial state is Effective") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  CHECK(sm.GetCurrentState() == ConfigurationState::Effective);
}

TEST_CASE("ConfigurationStateMachine: Effective configuration is accessible") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto& effective = sm.GetEffectiveConfiguration();
  CHECK(effective.active_frequency_indices_source.size() == 1);
  CHECK(effective.active_frequency_indices_source[0] == 10);
}

TEST_CASE("ConfigurationStateMachine: Active configuration not set initially") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto active = sm.GetActiveConfiguration();
  CHECK(!active.has_value());
}

// ============================================================================
// Revision Tracking Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Initial revision is 1") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  CHECK(sm.GetCurrentRevision() == 1);
}

TEST_CASE("ConfigurationStateMachine: Revision counter increments on commit") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  uint64_t initial_rev = sm.GetCurrentRevision();

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  CHECK(sm.GetCurrentRevision() == initial_rev + 1);
}

TEST_CASE("ConfigurationStateMachine: Revision never skips numbers") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  std::vector<uint64_t> revisions;
  revisions.push_back(sm.GetCurrentRevision());

  for (int i = 0; i < 5; ++i) {
    SourceConfiguration edited = config;
    edited.messages.push_back(json{{"frequency_index", 10 + i}, {"message_id", "msg" + std::to_string(i)}}); 
    sm.StageEdit(edited);
    sm.ValidateStaged();
    sm.CommitStaged();
    revisions.push_back(sm.GetCurrentRevision());
  }

  // Check that revisions are consecutive
  for (size_t i = 1; i < revisions.size(); ++i) {
    CHECK(revisions[i] == revisions[i - 1] + 1);
  }
}

// ============================================================================
// ETag Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: ETag format is 'Rev:N'") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto etag = sm.GetCurrentETag();
  CHECK(etag.find("Rev:") == 0);
  CHECK(etag == "Rev:1");
}

TEST_CASE("ConfigurationStateMachine: ETag changes on commit") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  std::string initial_etag = sm.GetCurrentETag();

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  std::string new_etag = sm.GetCurrentETag();
  CHECK(new_etag != initial_etag);
  CHECK(new_etag == "Rev:2");
}

TEST_CASE("ConfigurationStateMachine: ETag validation prevents stale writes (409)") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  std::string etag = sm.GetCurrentETag();

  // Make first edit
  SourceConfiguration edited1 = config;
  edited1.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});
  sm.StageEdit(edited1);
  sm.ValidateStaged();
  sm.CommitStaged();

  // Try second edit with stale ETag
  SourceConfiguration edited2 = config;
  edited2.messages.push_back(json{{"frequency_index", 30}, {"message_id", "msg3"}});
  sm.StageEdit(edited2);
  sm.ValidateStaged();

  auto result = sm.CommitStaged(etag);  // Pass stale ETag
  REQUIRE(!result.has_value());
  CHECK(result.error().code == "ERR_CONFLICT_409");
}

// ============================================================================
// Staged Edit Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Staged edits don't affect Effective until committed") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto& initial_effective = sm.GetEffectiveConfiguration();
  size_t initial_msg_count = initial_effective.source.messages.size();

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);

  auto& effective_after_stage = sm.GetEffectiveConfiguration();
  CHECK(effective_after_stage.source.messages.size() == initial_msg_count);
}

TEST_CASE("ConfigurationStateMachine: Stage, Validate, Commit workflow") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  auto stage_result = sm.StageEdit(edited);
  REQUIRE(stage_result.has_value());

  auto validate_result = sm.ValidateStaged();
  REQUIRE(validate_result.has_value());

  auto commit_result = sm.CommitStaged();
  REQUIRE(commit_result.has_value());

  // Check effective was updated
  auto& effective = sm.GetEffectiveConfiguration();
  CHECK(effective.source.messages.size() == 2);
}

TEST_CASE("ConfigurationStateMachine: Commit requires staged edits") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto result = sm.CommitStaged();
  REQUIRE(!result.has_value());
  CHECK(result.error().code == "ERR_NO_STAGED");
}

// ============================================================================
// Active Configuration Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Commit makes configuration Active") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto active_before = sm.GetActiveConfiguration();
  CHECK(!active_before.has_value());

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  auto active_after = sm.GetActiveConfiguration();
  REQUIRE(active_after.has_value());
  CHECK(active_after->source.messages.size() == 2);
}

// ============================================================================
// Undo/Redo Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Undo not available initially") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  CHECK(!sm.CanUndo());
}

TEST_CASE("ConfigurationStateMachine: Undo available after commit") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  CHECK(sm.CanUndo());
}

TEST_CASE("ConfigurationStateMachine: Undo reverts to previous revision") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  uint64_t rev_before_edit = sm.GetCurrentRevision();
  auto& eff_before_edit = sm.GetEffectiveConfiguration();
  size_t msg_count_before = eff_before_edit.source.messages.size();

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  uint64_t rev_after_edit = sm.GetCurrentRevision();
  CHECK(rev_after_edit > rev_before_edit);

  sm.Undo();

  uint64_t rev_after_undo = sm.GetCurrentRevision();
  auto& eff_after_undo = sm.GetEffectiveConfiguration();

  CHECK(rev_after_undo == rev_before_edit);
  CHECK(eff_after_undo.source.messages.size() == msg_count_before);
}

TEST_CASE("ConfigurationStateMachine: Redo available after Undo") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  CHECK(sm.CanRedo() == false);  // No redo before undo

  sm.Undo();
  CHECK(sm.CanRedo() == true);  // Redo available after undo
}

TEST_CASE("ConfigurationStateMachine: Redo restores undone revision") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  uint64_t rev_after_commit = sm.GetCurrentRevision();

  sm.Undo();
  sm.Redo();

  uint64_t rev_after_redo = sm.GetCurrentRevision();
  CHECK(rev_after_redo == rev_after_commit);
}

TEST_CASE("ConfigurationStateMachine: Undo/Redo stack maintains last 10 revisions") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  // Create 15 edits
  for (int i = 0; i < 15; ++i) {
    SourceConfiguration edited = config;
    for (int j = 0; j <= i; ++j) {
      edited.messages.push_back(
          json{{"frequency_index", 10 + j}, {"message_id", "msg" + std::to_string(j)}}); 
    }
    sm.StageEdit(edited);
    sm.ValidateStaged();
    sm.CommitStaged();
  }

  auto history = sm.GetRevisionHistory();
  CHECK(history.size() <= 10);  // Should maintain max 10
}

TEST_CASE("ConfigurationStateMachine: GetUndoStack returns available undo revisions") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  // Create 3 commits
  for (int i = 0; i < 3; ++i) {
    SourceConfiguration edited = config;
    edited.messages.push_back(json{{"frequency_index", 10 + i}, {"message_id", "msg" + std::to_string(i)}}); 
    sm.StageEdit(edited);
    sm.ValidateStaged();
    sm.CommitStaged();
  }

  auto undo_stack = sm.GetUndoStack();
  CHECK(undo_stack.size() == 3);
}

TEST_CASE("ConfigurationStateMachine: GetRedoStack returns available redo revisions") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  // Create 3 commits
  for (int i = 0; i < 3; ++i) {
    SourceConfiguration edited = config;
    edited.messages.push_back(json{{"frequency_index", 10 + i}, {"message_id", "msg" + std::to_string(i)}}); 
    sm.StageEdit(edited);
    sm.ValidateStaged();
    sm.CommitStaged();
  }

  // Undo twice
  sm.Undo();
  sm.Undo();

  auto redo_stack = sm.GetRedoStack();
  CHECK(redo_stack.size() == 2);
}

// ============================================================================
// Revision History Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: GetRevision retrieves specific revision") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  uint64_t rev_1 = sm.GetCurrentRevision();

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});
  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged();

  uint64_t rev_2 = sm.GetCurrentRevision();

  auto retrieved_rev_1 = sm.GetRevision(rev_1);
  auto retrieved_rev_2 = sm.GetRevision(rev_2);

  REQUIRE(retrieved_rev_1.has_value());
  REQUIRE(retrieved_rev_2.has_value());
  CHECK(retrieved_rev_1->revision_number == rev_1);
  CHECK(retrieved_rev_2->revision_number == rev_2);
}

TEST_CASE("ConfigurationStateMachine: GetRevision returns empty for non-existent revision") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto retrieved = sm.GetRevision(999999);
  CHECK(!retrieved.has_value());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("ConfigurationStateMachine: Undo with empty stack returns error") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto result = sm.Undo();
  REQUIRE(!result.has_value());
  CHECK(result.error().code == "ERR_NO_UNDO");
}

TEST_CASE("ConfigurationStateMachine: Redo with empty stack returns error") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  auto result = sm.Redo();
  REQUIRE(!result.has_value());
  CHECK(result.error().code == "ERR_NO_REDO");
}

TEST_CASE("ConfigurationStateMachine: Invalid staged configuration caught by Validate") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration invalid = config;
  invalid.occupied_bandwidth_hz = -5e6;  // Invalid!

  sm.StageEdit(invalid);
  auto result = sm.ValidateStaged();

  REQUIRE(!result.has_value());
}

TEST_CASE("ConfigurationStateMachine: Description saved with revision") {
  auto config = SimpleValidConfiguration();
  ConfigurationStateMachine sm(config);

  SourceConfiguration edited = config;
  edited.messages.push_back(json{{"frequency_index", 20}, {"message_id", "msg2"}});

  sm.StageEdit(edited);
  sm.ValidateStaged();
  sm.CommitStaged(std::nullopt, "Test edit");

  auto history = sm.GetRevisionHistory();
  REQUIRE(history.size() >= 2);
  CHECK(history.back().description == "Test edit");
}
