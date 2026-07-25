#include <catch2/catch_test_macros.hpp>
#include "dsp/configuration/ConfigurationStateMachine.hpp"

using namespace dsp::configuration;

static SourceConfiguration CreateValidConfig() {
    SourceConfiguration cfg;
    cfg.messages = {"MSG_A", "MSG_B"};
    cfg.iq_center_frequency_hz = 1e9;
    cfg.iq_offsets = {0.0, 100.0};
    cfg.idle_mode = "continuous";
    cfg.idle_duration_samples = 1000;
    cfg.occupied_bandwidth_hz = 1e8;
    cfg.max_abs_cfo_hz = 1e6;
    cfg.enable_noise = false;
    cfg.enable_doppler = false;
    cfg.enable_multipath = false;
    cfg.allow_overlap = false;
    cfg.message_id = "TEST_001";
    cfg.transmit_start_sample = 0;
    cfg.frequency_index = 10;
    cfg.value = "default";
    cfg.role = "transmitter";
    return cfg;
}

// Test 1: State machine initialization
TEST_CASE("ConfigurationStateMachine: Initialization", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    CHECK(sm.GetCurrentRevision() == 1);
    CHECK(sm.IsCurrentlyValid() == true);
}

// Test 2: Get source configuration
TEST_CASE("ConfigurationStateMachine: Get source configuration", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = 2.5e9;
    
    ConfigurationStateMachine sm(cfg);
    auto retrieved = sm.GetSourceConfiguration();
    
    CHECK(retrieved.iq_center_frequency_hz == 2.5e9);
}

// Test 3: Get effective configuration
TEST_CASE("ConfigurationStateMachine: Get effective configuration", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto effective = sm.GetEffectiveConfiguration();
    
    CHECK(effective.revision == 1);
    CHECK(!effective.active_frequency_indices_source.empty());
}

// Test 4: ETag format
TEST_CASE("ConfigurationStateMachine: ETag format", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    std::string etag = sm.GetCurrentETag();
    
    CHECK(etag == "Rev:1");
}

// Test 5: Create staged edit
TEST_CASE("ConfigurationStateMachine: Create staged edit", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto result = sm.CreateStagedEdit();
    
    CHECK(result.success == true);
    CHECK(result.value.base_revision == 1);
}

// Test 6: Update staged field
TEST_CASE("ConfigurationStateMachine: Update staged field", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    CHECK(staged_result.success);
    
    auto handle = staged_result.value;
    auto update_result = sm.UpdateStagedField(handle, "iq_center_frequency_hz", "2.5e9");
    
    CHECK(update_result.success == true);
}

// Test 7: Validate staged edit
TEST_CASE("ConfigurationStateMachine: Validate staged edit", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "2.5e9");
    auto validate_result = sm.ValidateStagedEdit(handle);
    
    CHECK(validate_result.success == true);
}

// Test 8: Commit staged edit
TEST_CASE("ConfigurationStateMachine: Commit staged edit", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(handle);
    auto commit_result = sm.CommitStagedEdit(handle);
    
    CHECK(commit_result.success == true);
    CHECK(commit_result.value == 2);  // Revision incremented
}

// Test 9: Revision increments monotonically
TEST_CASE("ConfigurationStateMachine: Revision monotonic increment", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    CHECK(sm.GetCurrentRevision() == 1);
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    CHECK(sm.GetCurrentRevision() == 2);
    
    auto s2 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s2.value, "iq_center_frequency_hz", "3e9");
    sm.ValidateStagedEdit(s2.value);
    sm.CommitStagedEdit(s2.value);
    
    CHECK(sm.GetCurrentRevision() == 3);
}

// Test 10: Discard staged edit
TEST_CASE("ConfigurationStateMachine: Discard staged edit", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "2.5e9");
    sm.DiscardStagedEdit(handle);
    
    // Revision should not change
    CHECK(sm.GetCurrentRevision() == 1);
}

// Test 11: ETag conflict detection
TEST_CASE("ConfigurationStateMachine: ETag precondition failure", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    // Now current revision is 2, ETag is "Rev:2"
    auto s2 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s2.value, "iq_center_frequency_hz", "3e9");
    sm.ValidateStagedEdit(s2.value);
    
    // Try to commit with old ETag (should fail 409 Conflict)
    auto commit_result = sm.CommitStagedEdit(s2.value, "Rev:1");
    
    CHECK(commit_result.success == false);
    bool has_conflict_indicator = (commit_result.error_message.find("409") != std::string::npos || 
                                   commit_result.error_message.find("Conflict") != std::string::npos);
    CHECK(has_conflict_indicator);
}

// Test 12: ETag with correct precondition passes
TEST_CASE("ConfigurationStateMachine: ETag precondition success", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    std::string current_etag = sm.GetCurrentETag();
    CHECK(current_etag == "Rev:1");
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(handle);
    
    auto commit_result = sm.CommitStagedEdit(handle, current_etag);
    
    CHECK(commit_result.success == true);
}

// Test 13: Undo functionality
TEST_CASE("ConfigurationStateMachine: Undo", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    uint64_t rev1 = sm.GetCurrentRevision();
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    uint64_t rev2 = sm.GetCurrentRevision();
    CHECK(rev2 == rev1 + 1);
    
    auto undo_result = sm.Undo();
    
    CHECK(undo_result.success == true);
    CHECK(sm.GetCurrentRevision() == rev1);
}

// Test 14: Redo functionality
TEST_CASE("ConfigurationStateMachine: Redo", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    uint64_t rev2 = sm.GetCurrentRevision();
    
    sm.Undo();
    CHECK(sm.GetCurrentRevision() == 1);
    
    auto redo_result = sm.Redo();
    
    CHECK(redo_result.success == true);
    CHECK(sm.GetCurrentRevision() == rev2);
}

// Test 15: CanUndo check
TEST_CASE("ConfigurationStateMachine: CanUndo check", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    CHECK(sm.CanUndo() == false);  // At beginning
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    CHECK(sm.CanUndo() == true);  // Now have history
}

// Test 16: CanRedo check
TEST_CASE("ConfigurationStateMachine: CanRedo check", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s1 = sm.CreateStagedEdit();
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s1.value);
    sm.CommitStagedEdit(s1.value);
    
    CHECK(sm.CanRedo() == false);  // At end
    
    sm.Undo();
    
    CHECK(sm.CanRedo() == true);  // Can redo after undo
}

// Test 17: IsCurrentlyValid check
TEST_CASE("ConfigurationStateMachine: IsCurrentlyValid", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    CHECK(sm.IsCurrentlyValid() == true);
}

// Test 18: GetCurrentValidationErrors
TEST_CASE("ConfigurationStateMachine: GetCurrentValidationErrors", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto errors = sm.GetCurrentValidationErrors();
    
    CHECK(errors.empty());  // Valid config has no errors
}

// Test 19: Staged edit validation failure
TEST_CASE("ConfigurationStateMachine: Staged edit validation fails", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    
    // Set invalid frequency
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "-1e9");
    auto validate_result = sm.ValidateStagedEdit(handle);
    
    CHECK(validate_result.success == false);
    CHECK(!validate_result.value.empty());  // Has validation errors
}

// Test 20: Commit with invalid config fails
TEST_CASE("ConfigurationStateMachine: Commit invalid config fails", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto staged_result = sm.CreateStagedEdit();
    auto handle = staged_result.value;
    
    sm.UpdateStagedField(handle, "iq_center_frequency_hz", "-1e9");
    auto commit_result = sm.CommitStagedEdit(handle);
    
    CHECK(commit_result.success == false);
}

// Test 21: JSON Patch atomic transaction - success
TEST_CASE("ConfigurationStateMachine: JSON Patch atomic transaction success", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    std::vector<std::pair<std::string, std::string>> operations = {
        {"iq_center_frequency_hz", "2.5e9"},
        {"max_abs_cfo_hz", "2e6"}
    };
    
    auto patch_result = sm.ApplyJsonPatch(operations);
    
    CHECK(patch_result.success == true);
    CHECK(sm.GetCurrentRevision() == 2);
}

// Test 22: JSON Patch atomic transaction - rollback on failure
TEST_CASE("ConfigurationStateMachine: JSON Patch atomic transaction rollback", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    std::vector<std::pair<std::string, std::string>> operations = {
        {"iq_center_frequency_hz", "2.5e9"},
        {"max_abs_cfo_hz", "-1e6"}  // Invalid - will fail validation
    };
    
    auto patch_result = sm.ApplyJsonPatch(operations);
    
    CHECK(patch_result.success == false);
    CHECK(sm.GetCurrentRevision() == 1);  // Revision unchanged
}

// Test 23: Inspect parameters
TEST_CASE("ConfigurationStateMachine: Inspect parameters", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto inspect_result = sm.InspectParameters("node_0");
    
    CHECK(inspect_result.success == true);
    CHECK(inspect_result.value.size() > 0);
}

// Test 24: Revision never resets
TEST_CASE("ConfigurationStateMachine: Revision never resets", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    for (int i = 0; i < 5; ++i) {
        auto s = sm.CreateStagedEdit();
        sm.UpdateStagedField(s.value, "iq_center_frequency_hz", 
                           std::to_string(1e9 + i * 1e8));
        sm.ValidateStagedEdit(s.value);
        sm.CommitStagedEdit(s.value);
    }
    
    CHECK(sm.GetCurrentRevision() == 6);  // Started at 1, now at 6
    
    // Undo to earlier revision
    for (int i = 0; i < 4; ++i) {
        sm.Undo();
    }
    
    // Create new edits - revisions should continue incrementing, not reset
    auto s = sm.CreateStagedEdit();
    sm.UpdateStagedField(s.value, "iq_center_frequency_hz", "3e9");
    sm.ValidateStagedEdit(s.value);
    sm.CommitStagedEdit(s.value);
    
    CHECK(sm.GetCurrentRevision() == 7);  // Incremented from 6, not reset
}

// Test 25: Multiple staged edits can exist
TEST_CASE("ConfigurationStateMachine: Multiple staged edits", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s1 = sm.CreateStagedEdit();
    auto s2 = sm.CreateStagedEdit();
    
    CHECK(s1.value.id != s2.value.id);
    
    sm.UpdateStagedField(s1.value, "iq_center_frequency_hz", "2e9");
    sm.UpdateStagedField(s2.value, "iq_center_frequency_hz", "3e9");
    
    sm.ValidateStagedEdit(s1.value);
    sm.ValidateStagedEdit(s2.value);
    
    // Discard one, commit the other
    sm.DiscardStagedEdit(s2.value);
    auto commit_result = sm.CommitStagedEdit(s1.value);
    
    CHECK(commit_result.success == true);
    CHECK(sm.GetCurrentRevision() == 2);
}

// Test 26: Active configuration follows current state
TEST_CASE("ConfigurationStateMachine: Active configuration", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto active1 = sm.GetActiveConfiguration();
    
    auto s = sm.CreateStagedEdit();
    sm.UpdateStagedField(s.value, "iq_center_frequency_hz", "2.5e9");
    sm.ValidateStagedEdit(s.value);
    sm.CommitStagedEdit(s.value);
    
    auto active2 = sm.GetActiveConfiguration();
    
    CHECK(active1.revision == 1);
    CHECK(active2.revision == 2);
    CHECK(active2.iq_center_frequency_hz == 2.5e9);
}

// Test 27: Boolean field updates
TEST_CASE("ConfigurationStateMachine: Boolean field updates", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s = sm.CreateStagedEdit();
    sm.UpdateStagedField(s.value, "allow_overlap", "true");
    sm.UpdateStagedField(s.value, "enable_noise", "true");
    
    // Should be able to validate
    auto validate = sm.ValidateStagedEdit(s.value);
    CHECK(validate.success == true);
}

// Test 28: Integer field updates
TEST_CASE("ConfigurationStateMachine: Integer field updates", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s = sm.CreateStagedEdit();
    sm.UpdateStagedField(s.value, "idle_duration_samples", "5000");
    sm.UpdateStagedField(s.value, "frequency_index", "100");
    
    auto validate = sm.ValidateStagedEdit(s.value);
    CHECK(validate.success == true);
}

// Test 29: Invalid field name handled
TEST_CASE("ConfigurationStateMachine: Invalid field name", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    auto s = sm.CreateStagedEdit();
    auto result = sm.UpdateStagedField(s.value, "nonexistent_field", "value");
    
    CHECK(result.success == false);
}

// Test 30: History limit respected
TEST_CASE("ConfigurationStateMachine: History limit respected", "[state_machine]") {
    SourceConfiguration cfg = CreateValidConfig();
    ConfigurationStateMachine sm(cfg);
    
    // Create more revisions than MAX_REVISION_HISTORY (10)
    for (int i = 0; i < 15; ++i) {
        auto s = sm.CreateStagedEdit();
        sm.UpdateStagedField(s.value, "iq_center_frequency_hz", 
                           std::to_string(1e9 + i * 1e7));
        sm.ValidateStagedEdit(s.value);
        sm.CommitStagedEdit(s.value);
    }
    
    // Should be at revision 16 (started at 1, added 15)
    CHECK(sm.GetCurrentRevision() == 16);
    
    // But undo should be limited
    int undo_count = 0;
    while (sm.CanUndo() && undo_count < 20) {
        sm.Undo();
        undo_count++;
    }
    
    // Should only be able to undo ~10 times (MAX_REVISION_HISTORY)
    CHECK(static_cast<size_t>(undo_count) <= ConfigurationStateMachine::MAX_REVISION_HISTORY);
}
