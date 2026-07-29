# Phase 2 Configuration Service Design Specification

**Status:** Design Complete  
**Purpose:** Detailed configuration service interface design for Phase 2A implementation  
**Scope:** ConfigurationService methods, state transitions, error handling  

---

## ConfigurationService Public Interface

### 1. Configuration Query Methods

```cpp
/// Get effective configuration with all derivations applied
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.config.v1",
///   "owner": "fhss-dashboard",
///   "config_revision": 42,
///   "effective": { ... computed config ... },
///   "etag": "\"graphx-config-42\"",
///   "derived_paths": [ ... list of generated field paths ... ]
/// }
nlohmann::json GetConfigResponse() const;

/// Get authoritative (user-edited) configuration only
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.scenario.v1",
///   "scenario": { ... user-editable fields ... },
///   "config_revision": 42,
///   "etag": "\"graphx-config-42\""
/// }
nlohmann::json GetScenarioResponse() const;

/// Get derivation provenance (which generated field comes from which rule)
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.provenance.v1",
///   "derivations": {
///     "/effective/active_frequency_indices": "derived_from: /scenario/messages/*/pulses/*/frequency_index",
///     ...
///   }
/// }
nlohmann::json GetProvenanceResponse() const;

/// Get list of derived field JSON Pointers (marked read-only)
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.derived_paths.v1",
///   "derived_paths": [
///     "/effective/active_frequency_indices",
///     "/effective/preamble_pulses",
///     ...
///   ]
/// }
nlohmann::json GetDerivedPathsResponse() const;

/// Get single value at RFC 6901 JSON Pointer
/// 
/// Query: ?pointer=/scenario/iq_center_frequency_hz
/// Returns:
/// {
///   "schema": "graphx.dashboard.value.v1",
///   "pointer": "/scenario/iq_center_frequency_hz",
///   "value": 1240000000
/// }
nlohmann::json GetValueResponse(std::string_view pointer) const;
```

### 2. Validation Methods

```cpp
/// Validate authoritative configuration against all 13 rules
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.config_validation.v1",
///   "validation": {
///     "valid": true | false,
///     "errors": [
///       {
///         "rule": "ERR_TOPOLOGY_INVALID",
///         "pointer": "/scenario/messages/0",
///         "message": "Message 0 has invalid topology",
///         "details": { ... }
///       }
///     ]
///   }
/// }
nlohmann::json ValidateConfig(const nlohmann::json& request) const;

/// Get current validation status (read from last validation)
ValidationResult GetValidationStatus() const;
```

### 3. Mutation Methods (with RFC 6902 JSON Patch)

```cpp
/// Apply atomic JSON Patch with If-Match precondition
/// 
/// Request:
/// {
///   "operations": [
///     {"op": "replace", "path": "/scenario/iq_center_frequency_hz", "value": 1250000000}
///   ]
/// }
/// 
/// Headers:
/// Content-Type: application/json-patch+json
/// If-Match: "graphx-config-42"
/// 
/// Returns (on success):
/// {
///   "schema": "graphx.dashboard.config_result.v1",
///   "status": "applied",
///   "old_revision": 42,
///   "new_revision": 43,
///   "etag": "\"graphx-config-43\"",
///   "rebuild_required": true,
///   "regenerated_targets": [ ... ],
///   "validation": { ... }
/// }
/// 
/// Errors:
/// - 400: Malformed patch
/// - 409: Derived field mutation attempt
/// - 412: If-Match mismatch (stale)
/// - 428: If-Match missing
nlohmann::json ApplyJsonPatch(
    const nlohmann::json& patch,
    std::string_view if_match,
    bool validate_only = false
);

/// Deprecated: Apply config using old expected_revision wrapper
/// (for backward compatibility only)
nlohmann::json PatchConfig(const nlohmann::json& request);
```

### 4. Node Inspection Methods

```cpp
/// Get parameters from specific node (calls node->GetParameters())
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.node_parameters.v1",
///   "node_id": "fhss_detector_1",
///   "parameters": {
///     "detector_id": "detector_1",
///     "noise_power_quantile": 0.95,
///     "threshold_above_noise_linear": 2.5,
///     ...
///   }
/// }
nlohmann::json GetNodeParametersResponse(const std::string& node_id) const;

/// Get parameter metadata (description, type, range) for a specific parameter
/// 
/// Returns:
/// {
///   "schema": "graphx.dashboard.parameter_description.v1",
///   "node_id": "fhss_detector_1",
///   "parameter": "noise_power_quantile",
///   "description": {
///     "type": "number",
///     "minimum": 0.0,
///     "maximum": 1.0,
///     "description": "Quantile for noise estimation",
///     "default": 0.95
///   }
/// }
nlohmann::json GetNodeParameterDescription(
    const std::string& node_id,
    const std::string& param_name
) const;
```

### 5. Revision & Versioning Methods

```cpp
/// Get current configuration revision number
uint64_t ConfigRevision() const;

/// Get ETag string for current revision
/// Format: "graphx-config-{revision}"
std::string ETag() const;

/// Undo last successful edit (pop from undo stack)
/// 
/// Returns: new config (before undo was applied)
nlohmann::json UndoLastEdit();
```

### 6. Lifecycle Methods

```cpp
/// Check if service is ready to accept requests
bool IsReady() const;

/// Get graph response (for visualization/topology)
nlohmann::json GetGraphResponse() const;

/// Export configuration to file
nlohmann::json ExportConfig(const nlohmann::json& request);
```

---

## Error Response Format

All errors use RFC 9457 Problem Details:

```json
{
  "type": "urn:graphx:dashboard:problem:{error_code}",
  "title": "Short Title",
  "detail": "User-friendly description (no source paths)",
  "schema": "graphx.dashboard.error.v1",
  "status": 400,
  "code": "error_code_identifier",
  "message": "Full message for logs",
  "details": {"context": "additional details"},
  "request_id": "fhss-dashboard",
  "retriable": false,
  "pointer": "/scenario/messages/0"
}
```

---

## Validation Error Codes (13 Rules)

| Code | Rule | HTTP Status | Retriable |
|------|------|-----------|-----------|
| ERR_TOPOLOGY_INVALID | Graph structure invalid | 409 | No |
| ERR_MSG_ID_DUP | Duplicate message ID | 409 | No |
| ERR_PREAMBLE_INVALID | Invalid preamble format | 409 | No |
| ERR_FREQ_OUT_OF_RANGE | Frequency index/offset invalid | 409 | No |
| ERR_SCHEDULE_UNORDERED | Messages not in time order | 409 | No |
| ERR_NODE_CONFIG_MISMATCH | Node config disagreement | 409 | No |
| ERR_CFO_EXCEEDS_BANDWIDTH | CFO margin insufficient | 409 | No |
| ERR_DERIVED_MISMATCH | Derived field mismatch | 409 | No |
| ERR_TOPOLOGY_CHANGED | Graph structure changed | 409 | No |
| ERR_IDLE_INVALID | Idle duration invalid | 409 | No |
| ERR_MSG_WINDOW_INVALID | Negative/infinite window | 409 | No |
| ERR_PULSE_ROLE_INVALID | Preamble/body inconsistent | 409 | No |
| ERR_FREQ_INDEX_OOB | Frequency index out of bounds | 409 | No |

---

## Precondition Flow

```
GET /api/v1/fhss/config
↓ (receive ETag: "graphx-config-42")
│
PATCH /api/v1/fhss/config
├─ Content-Type: application/json-patch+json
├─ If-Match: "graphx-config-42"
├─ Body: [{"op": "replace", ...}]
│
├─ Server Check: if revision != 42
│  └─→ 412 Precondition Failed (stale)
│
├─ Validate patch operations
│  ├─→ 400 if malformed
│  ├─→ 409 if generated field mutation
│  └─→ 409 if pointer not in authoritative
│
├─ Apply patch to candidate = scenario.patch(ops)
├─ Validate candidate against 13 rules
│  └─→ 409 if invalid
│
├─ Commit (atomic):
│  ├─ scenario_ ← candidate
│  ├─ revision_++  (now 43)
│  ├─ effective_ ← derive(candidate)
│  ├─ etag ← "graphx-config-43"
│  └─ undo_stack_.push(old_scenario)
│
└─ 200 OK with new_revision=43, etag="graphx-config-43"
```

---

## Concurrency Model

**Lock Granularity:**
- Single `configuration_mutex_` protects: `scenario_`, `effective_`, `revision_`, `validation_`

**Read Operations:**
```cpp
{
    std::lock_guard lock(configuration_mutex_);
    snapshot = effective_;
    snapshot_revision = revision_;
}
// Lock released; subsequent read/write independent
```

**Write Operations:**
```cpp
{
    std::lock_guard lock(configuration_mutex_);
    
    // Check precondition under lock
    if (revision_ != expected) return 412;
    
    // Perform mutation under lock
    scenario_ = new_scenario;
    effective_ = derive(scenario_);
    revision_++;
}
// Lock released; next operation sees new revision
```

**No Retry:**
- Client responsible for retry with new If-Match
- Server never auto-retries
- Prevents live-lock and simplifies semantics

---

## Generated Fields Derivation

**Deterministic Computation:**

```cpp
struct EffectiveConfiguration {
    // From messages[*].pulses[*].frequency_index (first 16 preambles only)
    std::vector<uint32_t> active_frequency_indices;
    
    // Count of pulses with role="preamble"
    uint32_t preamble_pulses;
    
    // Derived node configs
    FHSSChannelizerConfig channelizer_config;
    FHSSMessageAssemblerConfig assembler_config;
    FHSSImpairmentConfig impairment_config;
    
    // Schedule properties
    uint64_t total_schedule_samples;
    uint32_t total_pulse_count;
    uint32_t active_channel_count;
    double bandwidth_per_channel_hz;
    double cfo_margin_hz;
};

// Computation properties:
// - Same input → byte-identical output
// - Fixed JSON key ordering (sorted)
// - No random numbers
// - Deterministic iteration order
```

---

## Undo/Redo Stack

**Undo Stack Management:**

```cpp
std::vector<nlohmann::json> undo_stack_;  // LIFO stack of scenarios

// After successful commit:
undo_stack_.push_back(old_scenario);  // Save for undo

// On UndoLastEdit():
if (undo_stack_.empty()) return error;
scenario_ = undo_stack_.back();
undo_stack_.pop_back();
effective_ = derive(scenario_);
revision_++;  // Undo creates new revision
```

**Undo Revision:**
- Undo increments revision (it's a state change)
- Redo (forward) not supported (only undo available)
- Max 100 undo entries (configurable)

---

## Phase 2A Implementation Files

**Header:** `libgraph/include/graph/dashboard/ConfigurationService.hpp`
**Implementation:** `libgraph/src/graph/dashboard/ConfigurationService.cpp`
**Tests:** `libgraph/test/unit/test_configuration_service.cpp`

**Methods to Implement:**
- GetConfigResponse()
- GetScenarioResponse()
- GetProvenanceResponse()
- GetDerivedPathsResponse()
- GetValueResponse(pointer)
- ValidateConfig(request)
- ApplyJsonPatch(patch, if_match, validate_only)
- PatchConfig(request) [deprecated]
- GetNodeParametersResponse(node_id)
- GetNodeParameterDescription(node_id, param_name)
- ConfigRevision()
- ETag()
- UndoLastEdit()
- IsReady()
- GetGraphResponse()
- ExportConfig(request)

**Total Methods:** 16

---

## Test Coverage (Phase 2A)

**Unit Tests:** 100+ test cases

| Category | Tests | Focus |
|----------|-------|-------|
| Query Methods | 12 | GetConfig*, validation status |
| Mutation (JSON Patch) | 20 | RFC 6902 ops, preconditions |
| Validation Rules | 13+ | One test per rule |
| Concurrency | 8 | Race conditions, locking |
| Revision Tracking | 6 | Monotonic increment, wrap-around |
| Error Handling | 10 | All error codes + HTTP status |
| Undo/Redo | 4 | Stack management |
| Node Inspection | 5 | Parameter queries |
| **TOTAL** | **100+** | **Full coverage** |

