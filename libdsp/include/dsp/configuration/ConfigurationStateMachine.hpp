#pragma once

#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include "dsp/configuration/FHSSCrossNodeValidator.hpp"
#include <vector>
#include <map>
#include <memory>
#include <optional>

namespace dsp::configuration {

/// @brief Configuration state machine result type
template <typename T>
struct Result {
    bool success;
    T value;
    std::string error_message;
    std::vector<ValidationError> validation_errors;
    
    explicit operator bool() const { return success; }
};

/// @brief Template specialization for Result<void>
template <>
struct Result<void> {
    bool success;
    std::string error_message;
    std::vector<ValidationError> validation_errors;
    
    explicit operator bool() const { return success; }
};

/// @brief Configuration state machine handle for staged edits
struct StagedEditHandle {
    uint64_t id;
    uint64_t base_revision;
    SourceConfiguration staged_source;
    bool is_valid;
};

/// @brief ConfigurationStateMachine - Manages configuration lifecycle
/// 
/// State Transitions:
/// Source → Derive → Effective → Validate → Staged → Commit → Active
/// 
/// Key Features:
/// - Deterministic derivation (byte-identical for same input)
/// - Staged edits don't affect Active (lazy commit)
/// - Revision counter monotonically increases (never resets)
/// - ETag format: "Rev:<revision>" for If-Match preconditions
/// - Concurrent requests handled via ETag validation (409 conflict)
/// - Undo/Redo stack maintains last N revisions
class ConfigurationStateMachine {
public:
    static constexpr size_t MAX_REVISION_HISTORY = 10;  // Keep last 10 revisions

    /// @brief Initialize state machine with source configuration
    ConfigurationStateMachine(const SourceConfiguration& initial_source);

    // Configuration queries
    SourceConfiguration GetSourceConfiguration() const;
    EffectiveConfiguration GetEffectiveConfiguration() const;
    EffectiveConfiguration GetActiveConfiguration() const;

    // Staged editing
    Result<StagedEditHandle> CreateStagedEdit();
    Result<void> UpdateStagedField(
        const StagedEditHandle& handle,
        const std::string& field_path,
        const std::string& value
    );
    Result<std::vector<ValidationError>> ValidateStagedEdit(const StagedEditHandle& handle);
    Result<uint64_t> CommitStagedEdit(
        const StagedEditHandle& handle,
        const std::string& if_match_etag = ""  // For conflict detection
    );
    void DiscardStagedEdit(const StagedEditHandle& handle);

    // Inspection without execution
    Result<std::vector<std::string>> InspectParameters(const std::string& node_id);

    // Revision management
    uint64_t GetCurrentRevision() const;
    std::string GetCurrentETag() const;
    
    // Undo/Redo
    Result<uint64_t> Undo();
    Result<uint64_t> Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    // Atomic transactions (RFC 6902 JSON Patch style)
    Result<uint64_t> ApplyJsonPatch(
        const std::vector<std::pair<std::string, std::string>>& operations
    );

    // Validation verification
    bool IsCurrentlyValid() const;
    std::vector<ValidationError> GetCurrentValidationErrors() const;

private:
    // State data
    struct RevisionSnapshot {
        uint64_t revision;
        SourceConfiguration source;
        EffectiveConfiguration effective;
        std::vector<ValidationError> validation_errors;
        bool is_valid;
    };

    std::vector<RevisionSnapshot> revision_history_;  // Last N revisions
    size_t current_revision_index_;                    // Index into history
    uint64_t next_revision_counter_;                   // For generating new revision numbers

    // Staged edits (temporary)
    std::map<uint64_t, StagedEditHandle> staged_edits_;
    uint64_t next_staged_edit_id_;

    // Helper methods
    RevisionSnapshot CreateSnapshot(
        uint64_t revision,
        const SourceConfiguration& source
    );

    Result<void> ValidateAndUpdateSnapshot(RevisionSnapshot& snapshot);

    const RevisionSnapshot& GetCurrentSnapshot() const;
    RevisionSnapshot& GetMutableCurrentSnapshot();
};

}  // namespace dsp::configuration
