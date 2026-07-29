#include "dsp/configuration/ConfigurationStateMachine.hpp"
#include <stdexcept>
#include <algorithm>
#include <sstream>

namespace dsp::configuration {

ConfigurationStateMachine::ConfigurationStateMachine(const SourceConfiguration& initial_source)
    : current_revision_index_(0), next_revision_counter_(1), next_staged_edit_id_(1) {
    
    // Create initial snapshot with revision 1
    RevisionSnapshot snapshot = CreateSnapshot(1, initial_source);
    ValidateAndUpdateSnapshot(snapshot);
    revision_history_.push_back(snapshot);
}

ConfigurationStateMachine::RevisionSnapshot ConfigurationStateMachine::CreateSnapshot(
    uint64_t revision,
    const SourceConfiguration& source
) {
    RevisionSnapshot snapshot;
    snapshot.revision = revision;
    snapshot.source = source;
    snapshot.effective = FHSSConfigurationDeriver::Derive(source, revision);
    snapshot.is_valid = true;  // Assume valid until proven otherwise
    snapshot.validation_errors.clear();
    
    return snapshot;
}

Result<void> ConfigurationStateMachine::ValidateAndUpdateSnapshot(RevisionSnapshot& snapshot) {
    snapshot.validation_errors = FHSSCrossNodeValidator::ValidateAll(
        snapshot.source,
        snapshot.effective
    );
    
    snapshot.is_valid = snapshot.validation_errors.empty();
    
    Result<void> result;
    result.success = true;
    result.validation_errors = snapshot.validation_errors;
    
    if (!snapshot.is_valid) {
        result.error_message = FHSSCrossNodeValidator::GetErrorSummary(snapshot.validation_errors);
    }
    
    return result;
}

const ConfigurationStateMachine::RevisionSnapshot& ConfigurationStateMachine::GetCurrentSnapshot() const {
    if (current_revision_index_ >= revision_history_.size()) {
        throw std::runtime_error("Invalid revision index");
    }
    return revision_history_[current_revision_index_];
}

ConfigurationStateMachine::RevisionSnapshot& ConfigurationStateMachine::GetMutableCurrentSnapshot() {
    if (current_revision_index_ >= revision_history_.size()) {
        throw std::runtime_error("Invalid revision index");
    }
    return revision_history_[current_revision_index_];
}

SourceConfiguration ConfigurationStateMachine::GetSourceConfiguration() const {
    return GetCurrentSnapshot().source;
}

EffectiveConfiguration ConfigurationStateMachine::GetEffectiveConfiguration() const {
    return GetCurrentSnapshot().effective;
}

EffectiveConfiguration ConfigurationStateMachine::GetActiveConfiguration() const {
    // Active = current effective (no separate active state yet)
    return GetCurrentSnapshot().effective;
}

Result<StagedEditHandle> ConfigurationStateMachine::CreateStagedEdit() {
    Result<StagedEditHandle> result;
    
    const auto& current = GetCurrentSnapshot();
    
    StagedEditHandle handle;
    handle.id = next_staged_edit_id_++;
    handle.base_revision = current.revision;
    handle.staged_source = current.source;  // Start with current source
    handle.is_valid = false;  // Not validated yet
    
    staged_edits_[handle.id] = handle;
    
    result.success = true;
    result.value = handle;
    
    return result;
}

Result<void> ConfigurationStateMachine::UpdateStagedField(
    const StagedEditHandle& handle,
    const std::string& field_path,
    const std::string& value
) {
    Result<void> result;
    
    auto it = staged_edits_.find(handle.id);
    if (it == staged_edits_.end()) {
        result.success = false;
        result.error_message = "Staged edit handle not found";
        return result;
    }
    
    // Update field by simple path (e.g., "iq_center_frequency_hz")
    auto& staged_src = it->second.staged_source;
    
    try {
        if (field_path == "iq_center_frequency_hz") {
            staged_src.iq_center_frequency_hz = std::stod(value);
        } else if (field_path == "max_abs_cfo_hz") {
            staged_src.max_abs_cfo_hz = std::stod(value);
        } else if (field_path == "occupied_bandwidth_hz") {
            staged_src.occupied_bandwidth_hz = std::stod(value);
        } else if (field_path == "idle_duration_samples") {
            staged_src.idle_duration_samples = std::stoi(value);
        } else if (field_path == "transmit_start_sample") {
            staged_src.transmit_start_sample = std::stoi(value);
        } else if (field_path == "frequency_index") {
            staged_src.frequency_index = std::stoi(value);
        } else if (field_path == "allow_overlap") {
            staged_src.allow_overlap = (value == "true" || value == "1");
        } else if (field_path == "enable_noise") {
            staged_src.enable_noise = (value == "true" || value == "1");
        } else if (field_path == "enable_doppler") {
            staged_src.enable_doppler = (value == "true" || value == "1");
        } else if (field_path == "enable_multipath") {
            staged_src.enable_multipath = (value == "true" || value == "1");
        } else if (field_path == "idle_mode") {
            staged_src.idle_mode = value;
        } else if (field_path == "message_id") {
            staged_src.message_id = value;
        } else if (field_path == "role") {
            staged_src.role = value;
        } else if (field_path == "value") {
            staged_src.value = value;
        } else {
            result.success = false;
            result.error_message = "Unknown field: " + field_path;
            return result;
        }
        
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = "Failed to parse value for field " + field_path + ": " + e.what();
    }
    
    return result;
}

Result<std::vector<ValidationError>> ConfigurationStateMachine::ValidateStagedEdit(
    const StagedEditHandle& handle
) {
    Result<std::vector<ValidationError>> result;
    
    auto it = staged_edits_.find(handle.id);
    if (it == staged_edits_.end()) {
        result.success = false;
        result.error_message = "Staged edit handle not found";
        return result;
    }
    
    const auto& staged_src = it->second.staged_source;
    auto effective = FHSSConfigurationDeriver::Derive(staged_src, handle.base_revision);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(staged_src, effective);
    
    result.success = errors.empty();
    result.value = errors;
    result.validation_errors = errors;
    
    if (!result.success) {
        result.error_message = FHSSCrossNodeValidator::GetErrorSummary(errors);
    }
    
    // Update validity in staged edit
    it->second.is_valid = result.success;
    
    return result;
}

Result<uint64_t> ConfigurationStateMachine::CommitStagedEdit(
    const StagedEditHandle& handle,
    const std::string& if_match_etag
) {
    Result<uint64_t> result;
    
    auto it = staged_edits_.find(handle.id);
    if (it == staged_edits_.end()) {
        result.success = false;
        result.error_message = "Staged edit handle not found";
        return result;
    }
    
    const auto& current = GetCurrentSnapshot();
    
    // Check ETag precondition
    if (!if_match_etag.empty()) {
        std::string current_etag = "Rev:" + std::to_string(current.revision);
        if (if_match_etag != current_etag) {
            result.success = false;
            result.error_message = "Precondition Failed: ETag mismatch (409 Conflict)";
            result.value = 0;
            return result;
        }
    }
    
    const auto& staged_src = it->second.staged_source;
    
    // Increment revision counter BEFORE creating new snapshot
    next_revision_counter_++;
    
    // Validate before commit
    auto effective = FHSSConfigurationDeriver::Derive(staged_src, next_revision_counter_);
    auto errors = FHSSCrossNodeValidator::ValidateAll(staged_src, effective);
    
    if (!errors.empty()) {
        // Decrement counter since we're not committing
        next_revision_counter_--;
        result.success = false;
        result.error_message = "Validation failed: " + FHSSCrossNodeValidator::GetErrorSummary(errors);
        result.validation_errors = errors;
        return result;
    }
    
    // Create new snapshot with incremented revision
    RevisionSnapshot new_snapshot = CreateSnapshot(next_revision_counter_, staged_src);
    ValidateAndUpdateSnapshot(new_snapshot);
    
    // Update history
    if (current_revision_index_ + 1 < revision_history_.size()) {
        // Truncate history if we're not at the end (user did undo then new edit)
        revision_history_.erase(revision_history_.begin() + current_revision_index_ + 1, revision_history_.end());
    }
    
    // Add new snapshot
    revision_history_.push_back(new_snapshot);
    current_revision_index_++;
    
    // Maintain max history size
    if (revision_history_.size() > MAX_REVISION_HISTORY) {
        revision_history_.erase(revision_history_.begin());
        current_revision_index_--;
    }
    
    // Remove staged edit
    staged_edits_.erase(it);
    
    result.success = true;
    result.value = new_snapshot.revision;
    
    return result;
}

void ConfigurationStateMachine::DiscardStagedEdit(const StagedEditHandle& handle) {
    staged_edits_.erase(handle.id);
}

Result<std::vector<std::string>> ConfigurationStateMachine::InspectParameters(
    const std::string& node_id
) {
    (void)node_id;  // node_id reserved for future node-specific parameter inspection
    Result<std::vector<std::string>> result;
    
    const auto& effective = GetEffectiveConfiguration();
    
    // Return list of parameters for the node
    std::vector<std::string> params = {
        "iq_center_frequency_hz",
        "iq_offsets",
        "idle_mode",
        "idle_duration_samples",
        "occupied_bandwidth_hz",
        "max_abs_cfo_hz",
        "enable_noise",
        "enable_doppler",
        "enable_multipath",
        "allow_overlap",
        "message_id"
    };
    
    result.success = true;
    result.value = params;
    
    return result;
}

uint64_t ConfigurationStateMachine::GetCurrentRevision() const {
    return GetCurrentSnapshot().revision;
}

std::string ConfigurationStateMachine::GetCurrentETag() const {
    return "Rev:" + std::to_string(GetCurrentRevision());
}

Result<uint64_t> ConfigurationStateMachine::Undo() {
    Result<uint64_t> result;
    
    if (!CanUndo()) {
        result.success = false;
        result.error_message = "Cannot undo: at beginning of history";
        return result;
    }
    
    current_revision_index_--;
    result.success = true;
    result.value = GetCurrentRevision();
    
    return result;
}

Result<uint64_t> ConfigurationStateMachine::Redo() {
    Result<uint64_t> result;
    
    if (!CanRedo()) {
        result.success = false;
        result.error_message = "Cannot redo: at end of history";
        return result;
    }
    
    current_revision_index_++;
    result.success = true;
    result.value = GetCurrentRevision();
    
    return result;
}

bool ConfigurationStateMachine::CanUndo() const {
    return current_revision_index_ > 0;
}

bool ConfigurationStateMachine::CanRedo() const {
    return current_revision_index_ + 1 < revision_history_.size();
}

Result<uint64_t> ConfigurationStateMachine::ApplyJsonPatch(
    const std::vector<std::pair<std::string, std::string>>& operations
) {
    Result<uint64_t> result;
    
    // Create a staged edit
    auto staged_result = CreateStagedEdit();
    if (!staged_result.success) {
        result.success = false;
        result.error_message = "Failed to create staged edit";
        return result;
    }
    
    StagedEditHandle handle = staged_result.value;
    
    // Apply all operations
    for (const auto& [field, value] : operations) {
        auto update_result = UpdateStagedField(handle, field, value);
        if (!update_result.success) {
            DiscardStagedEdit(handle);
            result.success = false;
            result.error_message = "Failed to apply patch: " + update_result.error_message;
            return result;
        }
    }
    
    // Validate
    auto validate_result = ValidateStagedEdit(handle);
    if (!validate_result.success) {
        DiscardStagedEdit(handle);
        result.success = false;
        result.error_message = "Patch validation failed";
        result.validation_errors = validate_result.value;
        return result;
    }
    
    // Commit atomically
    auto commit_result = CommitStagedEdit(handle);
    if (!commit_result.success) {
        DiscardStagedEdit(handle);
        result.success = false;
        result.error_message = commit_result.error_message;
        result.validation_errors = commit_result.validation_errors;
        return result;
    }
    
    result.success = true;
    result.value = commit_result.value;
    
    return result;
}

bool ConfigurationStateMachine::IsCurrentlyValid() const {
    return GetCurrentSnapshot().is_valid;
}

std::vector<ValidationError> ConfigurationStateMachine::GetCurrentValidationErrors() const {
    return GetCurrentSnapshot().validation_errors;
}

}  // namespace dsp::configuration
