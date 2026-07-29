# FHSS Dashboard V2 — Phase 2A Configuration API Implementation

> Archived historical dashboard-planning record. Not current authority.

**Phase:** 2A — Configuration State Machine with HTTP & CLI  
**Date:** 2026-07-24  
**Status:** Ready for Orchestrator Preparation  
**Duration Estimate:** 2–3 days  
**Workflow:** Orchestrator → Implementer → Verifier → Approval

---

## Phase 2A Objective

Deliver a fully tested, deterministic FHSS configuration management API with:

- **10 HTTP REST endpoints** (RFC 9110/9112 compliant via DashboardHttpServer pattern)
- **4 CLI commands** (--set-config, --config-patch, --validate-config, --show-config)
- **Deterministic derivation** (12 generated fields via FHSSConfigurationDeriver)
- **All-rules validation** (13 semantic rules via FHSSCrossNodeValidator)
- **Staged edit lifecycle** (Create → Update → Validate → Commit with Undo/Redo)
- **ETag optimistic locking** (409 Conflict on stale write attempts)
- **70+ integration tests** (covering all endpoints, CLI commands, and edge cases)
- **Zero compiler warnings** (C++26 with -Wall -Wextra -Werror)
- **Production-facing verification** (ASan/UBSan + operator acceptance workflow)

**Dependencies (Must Be Complete):**
- ✅ Phase 1: HTTP server infrastructure (DashboardHttpServer)
- ✅ Phase 2A Pre-req: FHSSConfigurationDeriver (deterministic derivation engine)
- ✅ Phase 2A Pre-req: FHSSCrossNodeValidator (all 13 validation rules)
- ✅ Phase 2A Pre-req: ConfigurationStateMachine (lifecycle + undo/redo)

**Out of Scope (Later Phases):**
- Real runtime execution (Phase 2B: Receiver graph integration)
- Metrics collection (Phase 3+)
- WebSocket streaming (Phase 6+)

---

## Multi-Agent Workflow

### Role Definitions (Same as Phase 1)

| Role | Responsibility | Scope | Authority |
|------|-----------------|-------|-----------|
| **Orchestrator** | Prepare, audit, assign, coordinate, route findings, gate approval | Pre-implementation, post-verification, decision-making | Phase gate authority |
| **Implementer** | Execute assigned work, add tests, create operator bundle, report results | Implementation only; changes within scope | Code changes within scope |
| **Verifier** (Independent) | Verify independently; cannot edit implementation | Verification and acceptance testing only | Quality gate authority |

---

## Orchestrator Preparation (Pre-Implementation)

**Duration:** 45 minutes  
**Responsible:** Orchestration Agent  
**Output:** Implementation brief + acceptance checklist

### 1. Audit Current Repository State

- [ ] Verify Phase 1 build succeeds (HTTP server infrastructure in place)
- [ ] Confirm Phase 2A pre-requisites compiled and tested:
  - [ ] `FHSSConfigurationDeriver` with 25 tests passing
  - [ ] `FHSSCrossNodeValidator` with 35 tests passing
  - [ ] `ConfigurationStateMachine` with 30 tests passing
- [ ] Verify no uncommitted changes in libdsp/
- [ ] Confirm current git commit hash and branch name
- [ ] List all currently modified files in Phase 2A scope

**Deliverable:** Pre-implementation repository snapshot

### 2. Preserve Baseline for Rollback

- [ ] Hash current `libdsp/include/dsp/configuration/` directory (baseline reference)
- [ ] Document current HTTP server API routes (for isolation verification)
- [ ] Save current test results (all 90 Phase 2A pre-req tests passing)
- [ ] Record current CMakeLists.txt configuration for libdsp

**Deliverable:** Baseline hash file + test results snapshot

### 3. Analyze Scope and Dependencies

- [ ] Verify DashboardHttpServer can wrap external state machine
- [ ] Check RequestHandler pattern is reusable for FHSSConfigurationHttpServer
- [ ] Map existing CLI argument parsing patterns (from examples/DSP/dashboard-server.cpp)
- [ ] Identify JSON Patch RFC 6902 requirements
- [ ] Confirm no CMakeLists circular dependencies with existing HTTP routes

**Deliverable:** Scope map (HTTP/CLI integration points, test framework)

### 4. Create File-Level Acceptance Checklist

Create `plan/Phase2A_Acceptance_Checklist.md` with:

#### HTTP Server Integration
- [ ] `FHSSConfigurationHttpServer.hpp` header created
- [ ] `FHSSConfigurationHttpServer.cpp` implementation created
- [ ] RequestHandler lambda captures state machine correctly
- [ ] ETag conflict detection (409 responses) implemented
- [ ] RFC 9457 error response format validated

#### CLI Command Handler
- [ ] `FHSSConfigurationCli.hpp` header created
- [ ] `FHSSConfigurationCli.cpp` implementation created
- [ ] Four CLI commands implemented: set-config, config-patch, validate-config, show-config
- [ ] Key=value parsing with type coercion
- [ ] JSON Patch file parsing and validation

#### HTTP Endpoints (10 Total)
- [ ] `GET    /api/v2/fhss/config`
- [ ] `GET    /api/v2/fhss/config/effective`
- [ ] `GET    /api/v2/fhss/config/history`
- [ ] `POST   /api/v2/fhss/config/staged`
- [ ] `PATCH  /api/v2/fhss/config/staged/{id}`
- [ ] `POST   /api/v2/fhss/config/validate`
- [ ] `POST   /api/v2/fhss/config/commit`
- [ ] `DELETE /api/v2/fhss/config/staged/{id}`
- [ ] `POST   /api/v2/fhss/config/undo`
- [ ] `POST   /api/v2/fhss/config/redo`

#### CLI Commands (4 Total)
- [ ] `--set-config KEY=VALUE [KEY=VALUE ...]`
- [ ] `--config-patch FILE [--if-match ETAG]`
- [ ] `--validate-config FILE`
- [ ] `--show-config [--effective] [--history]`

#### Testing (70+ Tests)
- [ ] HTTP endpoint unit tests (20+ tests)
- [ ] CLI command tests (15+ tests)
- [ ] Integration tests (20+ tests)
- [ ] Determinism verification (10+ tests)
- [ ] ETag conflict scenarios (5+ tests)

#### Build and Warnings
- [ ] All source files compile with -Wall -Wextra -Werror
- [ ] Zero compiler warnings (C++26 mode)
- [ ] CMakeLists.txt updated with HTTP/CLI targets
- [ ] ASan/UBSan tests pass without issues

**Deliverable:** Detailed file-level checklist

### 5. Create Standards Requirements Map

Document which Phase 2A acceptance criteria map to which standards:

```
Phase 2A Gate → Standards Coverage

HTTP/REST:
  - ETag optimistic locking → RFC 7232 sect. 2.3 (ETag)
  - If-Match precondition → RFC 7232 sect. 3.2 (If-Match)
  - 409 Conflict response → RFC 9110 sect. 15.4.10 (409)
  - RFC 9457 error format → RFC 9457 (Problem Details)

Configuration Management:
  - Staged edit lifecycle → NIST SP 800-218 sect. 2.1 (Configuration)
  - Deterministic output → NIST SP 800-218 sect. 3.3 (Build Reproducibility)
  - Non-failing validation → NIST SP 800-218 sect. 4.1 (Release)
  - Monotonic revision counter → NIST SP 800-218 sect. 3.1 (Version Control)

JSON/Data Format:
  - Sorted-key serialization → JSON RFC 8259 sect. 1 (Determinism)
  - JSON Patch support → RFC 6902 (JSON Patch)
  - JSON Schema validation → JSON Schema 2020-12

Testing:
  - Determinism verification (10 iterations) → IEEE 1012 (V&V Plan)
  - Conflict detection (409 scenarios) → IEEE 1012 (V&V Implementation)
  - ASan/UBSan coverage → NIST SP 800-218 sect. 3.3 (Build Security)
```

**Deliverable:** Standards compliance map

### 6. Assign Implementer and Verifier

- [ ] Select implementer agent (fresh, not involved in pre-req components)
- [ ] Select independent verifier agent (cannot be implementer)
- [ ] Confirm both have access to workspace and Git
- [ ] Brief both on multi-agent workflow rules
- [ ] Confirm Phase 2A pre-requisites are available for linking

**Deliverable:** Agent assignments + workflow acknowledgment

### 7. Record Pre-Implementation State

Create `plan/Phase2A_PreImplementation_Snapshot.json`:

```json
{
  "phase": "2A",
  "orchestrator_date": "2026-07-24T...",
  "repository_state": {
    "commit": "abc123...",
    "branch": "main",
    "status": "clean",
    "phase1_tested": true,
    "phase2a_prereq_tests": "90/90 passing"
  },
  "baseline_hashes": {
    "configuration_dir": "sha256:...",
    "existing_state_machine": "sha256:..."
  },
  "scope_checklist": {
    "http_endpoints": 10,
    "cli_commands": 4,
    "tests_required": 70,
    "standards_mapped": 12,
    "acceptance_criteria": 35
  },
  "dependencies": {
    "fhss_configuration_deriver": "25/25 tests",
    "fhss_cross_node_validator": "35/35 tests",
    "configuration_state_machine": "30/30 tests"
  },
  "agent_assignments": {
    "implementer": "...",
    "verifier": "..."
  }
}
```

**Deliverable:** Pre-implementation snapshot JSON

---

## Implementer Assignment (Implementation Phase)

**Duration:** 2–3 days  
**Responsible:** Implementer Agent  
**Constraints:** Must not edit verifier workflows; must provide evidence for all changes

### Implementer Responsibilities

1. **Audit Before Editing**
   - Review orchestrator's checklist and baseline
   - Understand Phase 2A scope boundaries
   - Identify reusable patterns from Phase 1 (DashboardHttpServer)
   - Study existing RequestHandler pattern and JSON serialization

2. **Implement Phase 2A Scope Only**
   - 10 HTTP REST endpoints for configuration management
   - 4 CLI commands with argument parsing
   - No Phase 2B receiver graph integration
   - No Phase 3+ runtime execution or metrics
   - Changes must enable Phase 2A acceptance only

3. **Preserve Determinism and Validation**
   - All JSON output uses SerializeWithSortedKeys() for byte-identical output
   - All 13 validation rules run non-failing (collect all errors simultaneously)
   - Revision counter monotonically increments 1 → 2 → 3 → ... (never resets)
   - ETag format: "Rev:N" where N is current revision number
   - 409 Conflict response on stale write (If-Match ETag mismatch)

4. **Add Production-Facing Tests**
   - Unit tests for HTTP request routing and response format
   - CLI command tests with input/output validation
   - Integration tests for full endpoint workflows
   - Determinism verification (10 iterations, byte-identical JSON)
   - ETag conflict detection (race condition scenarios)
   - ASan/UBSan tests (no memory safety issues)

5. **Create Operator Bundle**
   - Python tool with 4 CLI commands
   - README with documented examples
   - Expected output baselines for each command
   - Error scenario demonstrations

6. **Document Changes**
   - List all modified/created files with line counts
   - Record test coverage (70+ tests expected)
   - Document integration with existing State Machine
   - List any standards intentionally deferred

### Implementer Deliverables

```
deliverables/
├── IMPLEMENTATION_REPORT.md
│   ├── Files changed (with line counts and purpose)
│   ├── Tests added (with coverage and pass rate)
│   ├── HTTP endpoints implemented (10 total)
│   ├── CLI commands implemented (4 total)
│   ├── Build commands and results
│   ├── Operator workflow test results
│   ├── Determinism verification (10 iterations)
│   ├── ETag conflict detection results
│   ├── Known limitations
│   └── Standards compliance status (Phase 2A vs. deferred)
├── changed_files_list.txt (for verifier audit)
├── test_results.json (70+ tests, all passing)
├── http_endpoint_samples.json (sample request/response pairs)
├── cli_example_commands.txt (documented CLI usage)
├── operator_workflow_evidence/
│   ├── http_endpoint_responses (JSON samples)
│   ├── cli_command_output (text samples)
│   └── determinism_verification (10 iterations)
└── build_log.txt (clean build, zero warnings)
```

### Implementer Acceptance Gates (Pre-Verification)

Implementer must verify locally before submitting:

- [ ] C++26 build succeeds with -Wall -Wextra -Werror
- [ ] No compiler warnings on any platform (AppleClang, GCC, Clang)
- [ ] All Phase 2A pre-requisites (90/90 tests) still passing
- [ ] All Phase 2A implementation tests passing (70+ tests)
- [ ] Determinism verification passing (10 iterations, byte-identical JSON)
- [ ] ETag conflict detection verified (409 Conflict on stale write)
- [ ] All 13 validation rules tested and verified working
- [ ] Operator CLI workflow runs end-to-end
- [ ] ASan/UBSan tests pass (no memory issues)
- [ ] `git diff --check` clean (no trailing whitespace)
- [ ] IMPLEMENTATION_REPORT.md complete with evidence

**Stop here. Do not proceed to verifier if any gate fails.**

---

## Implementer Technical Requirements

**This section is the specification given to the Implementer Agent after orchestrator preparation.**

### 1. HTTP Server Integration (FHSSConfigurationHttpServer)

**Target:** Wrap ConfigurationStateMachine using DashboardHttpServer pattern

#### Tasks

1. **Header Design** (`libdsp/include/dsp/configuration/FHSSConfigurationHttpServer.hpp`)
   - Define `FHSSConfigurationHttpServer` class
   - Accept `std::shared_ptr<ConfigurationStateMachine>` in constructor
   - Provide `GetRequestHandler()` → `RequestHandler` lambda
   - RequestHandler pattern: matches DashboardHttpServer::RequestHandler signature
   - Support Options struct with host, port, max_body_bytes, timeout_seconds

2. **JSON Serialization with Sorted Keys**
   ```cpp
   // ALL responses must use sorted keys for byte-identical output
   static nlohmann::json SerializeWithSortedKeys(const nlohmann::json& j);
   
   // Sorting rule: alphabetically by key at each level
   // Example: {z: 1, a: 2, m: 3} → {a: 2, m: 3, z: 1}
   ```

3. **RFC 9457 Error Format**
   ```cpp
   // All error responses use this format
   {
     "type": "about:blank",
     "status": <http_status>,
     "title": "<http_reason_phrase>",
     "detail": "<error_detail>",
     "instance": "<request_path>"
   }
   ```

4. **Success Response Format**
   ```cpp
   {
     "schema": "graphx.fhss_configuration.v1",
     "data": { /* configuration or result data */ },
     "revision": 1,
     "etag": "Rev:1"
   }
   ```

5. **Lambda Capture Pattern**
   - GetRequestHandler() returns a lambda capturing `this`
   - Lambda compatible with: `std::function<bool(method, path, headers, body, status, headers_out, body_out)>`
   - Internal route dispatcher: calls private HandleXxx methods
   - Error responses return false with status + body set

#### Implementation Notes

- Reuse DashboardHttpServer pattern for request routing
- No new HTTP server needed; wrap existing one
- Request handler parses path: `/api/v2/fhss/config...`
- All responses JSON (application/json media type)
- Parse request body as JSON if present
- Handle malformed JSON with 400 Bad Request

### 2. CLI Command Handler (FHSSConfigurationCli)

**Target:** Provide command-line interface to ConfigurationStateMachine

#### Tasks

1. **Header Design** (`libdsp/include/dsp/configuration/FHSSConfigurationCli.hpp`)
   - Define `FHSSConfigurationCli` class
   - Accept `std::shared_ptr<ConfigurationStateMachine>` in constructor
   - `ExecuteCommand(argc, argv)` → `CommandResult`
   - CommandResult struct: exit_code, output, error, result_json

2. **Argument Parsing**
   ```bash
   graphx-config --set-config KEY1=VALUE1 KEY2=VALUE2 ...
   graphx-config --config-patch FILE [--if-match ETAG]
   graphx-config --validate-config FILE
   graphx-config --show-config [--effective] [--history]
   graphx-config --help
   ```

3. **--set-config Command**
   - Parse KEY=VALUE pairs from command line
   - Update source configuration fields (staged edit)
   - Validate updates
   - Commit if valid
   - Return updated configuration + revision

4. **--config-patch Command**
   - Read JSON Patch file (RFC 6902 format)
   - Create staged edit
   - Apply patch operations
   - Validate result
   - Commit if valid and If-Match (if provided)
   - Handle --if-match ETAG for conditional commit

5. **--validate-config Command**
   - Read configuration from file
   - Run all 13 validators
   - Return validation errors (non-failing)
   - Do NOT commit; staged edit discarded after validation

6. **--show-config Command**
   - Show current source configuration (default)
   - --effective: show effective config + 12 derived fields
   - --history: include revision history (last 10 revisions)
   - Return JSON with configuration data

7. **Error Handling**
   - File not found → exit code 1, error message
   - Malformed JSON → exit code 1, error detail
   - Validation errors → exit code 1, all 13 rules reported
   - 409 Conflict (stale write) → exit code 1, suggest retry
   - Success → exit code 0, JSON result

#### Implementation Notes

- All output to stdout (success) or stderr (errors)
- JSON responses pretty-printed for readability
- Errors use RFC 9457 format
- No interactive prompts; command-line only
- Support quoted arguments for values with spaces

### 3. HTTP Endpoints (10 Total)

**Route Prefix:** `/api/v2/fhss/config`  
**All responses:** RFC 9457 error format or success JSON with sorted keys

#### Endpoint 1: GET /api/v2/fhss/config

**Purpose:** Retrieve current source configuration

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.v1",
  "data": {
    "messages": [...],
    "iq_center_frequency_hz": 2400000000,
    ...
  },
  "revision": 1,
  "etag": "Rev:1"
}
```

**Tests:**
- [ ] 200 OK with valid source configuration
- [ ] ETag header present in response
- [ ] Revision monotonically increases on commit

#### Endpoint 2: GET /api/v2/fhss/config/effective

**Purpose:** Retrieve effective configuration (source + 12 derived fields)

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.v1",
  "data": {
    "messages": [...],
    "iq_center_frequency_hz": 2400000000,
    ...
    "active_frequency_indices_source": [...],
    "preamble_pulses": [...],
    ...
  },
  "revision": 1,
  "etag": "Rev:1"
}
```

**Tests:**
- [ ] 200 OK with effective configuration
- [ ] Derived fields present and deterministic
- [ ] 12 generated fields always present

#### Endpoint 3: GET /api/v2/fhss/config/history

**Purpose:** Retrieve revision history (last 10 revisions)

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.history.v1",
  "revisions": [
    {
      "revision": 3,
      "etag": "Rev:3",
      "source": {...},
      "effective": {...},
      "validation_errors": []
    },
    {
      "revision": 2,
      "etag": "Rev:2",
      "source": {...},
      "effective": {...},
      "validation_errors": []
    },
    {
      "revision": 1,
      "etag": "Rev:1",
      "source": {...},
      "effective": {...},
      "validation_errors": []
    }
  ]
}
```

**Tests:**
- [ ] 200 OK with revision history
- [ ] Revisions in descending order (newest first)
- [ ] History limited to last 10 revisions
- [ ] Each revision shows validation errors (if any)

#### Endpoint 4: POST /api/v2/fhss/config/staged

**Purpose:** Create a new staged edit

**Request Body:**
```json
{}  // Empty object (staged edit created from current source)
```

**Response (201 Created):**
```json
{
  "schema": "graphx.fhss_configuration.staged_edit.v1",
  "staged_id": "edit-uuid-1234",
  "base_revision": 1,
  "source": { ... },
  "effective": { ... },
  "validation_errors": []
}
```

**Tests:**
- [ ] 201 Created with staged edit handle
- [ ] Staged ID uniquely identifies edit
- [ ] Base revision captured for conflict detection
- [ ] Initially valid (no validation errors)

#### Endpoint 5: PATCH /api/v2/fhss/config/staged/{id}

**Purpose:** Update a single field in a staged edit

**Request Body:**
```json
{
  "field": "iq_center_frequency_hz",
  "value": 2500000000
}
```

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.staged_edit.v1",
  "staged_id": "edit-uuid-1234",
  "base_revision": 1,
  "source": { ... },
  "effective": { ... },
  "validation_errors": [ /* if validation failed */ ]
}
```

**Error Responses:**
- 400 Bad Request: missing field or value
- 404 Not Found: staged ID not found or expired
- 422 Unprocessable Entity: field type mismatch

**Tests:**
- [ ] 200 OK with updated field
- [ ] Validation runs on update
- [ ] Multiple updates accumulate
- [ ] 404 on invalid staged ID

#### Endpoint 6: POST /api/v2/fhss/config/validate

**Purpose:** Validate a staged edit without committing

**Request Body:**
```json
{
  "staged_id": "edit-uuid-1234"
}
```

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.validation_result.v1",
  "staged_id": "edit-uuid-1234",
  "is_valid": false,
  "validation_errors": [
    {
      "error_code": "ERR_FREQUENCY_001",
      "field": "iq_center_frequency_hz",
      "message": "Center frequency must be in range [0, 10 GHz]",
      "expected_constraint": "0 <= value <= 10000000000",
      "current_value": "11000000000"
    },
    ...
  ]
}
```

**Tests:**
- [ ] 200 OK with validation result
- [ ] All 13 rules reported simultaneously (non-failing)
- [ ] Validation errors include error codes and details
- [ ] is_valid: true/false based on error count

#### Endpoint 7: POST /api/v2/fhss/config/commit

**Purpose:** Commit a staged edit (with optional If-Match precondition)

**Request Headers:**
```http
If-Match: Rev:1  # Optional; if provided, must match current ETag
```

**Request Body:**
```json
{
  "staged_id": "edit-uuid-1234",
  "if_match": "Rev:1"  # Or in header; header takes precedence
}
```

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.commit_result.v1",
  "staged_id": "edit-uuid-1234",
  "new_revision": 2,
  "new_etag": "Rev:2",
  "source": { ... },
  "effective": { ... }
}
```

**Error Responses:**
- 400 Bad Request: validation errors present (not committable)
- 404 Not Found: staged ID not found or expired
- 409 Conflict: If-Match ETag does not match current revision
  ```json
  {
    "type": "about:blank",
    "status": 409,
    "title": "Conflict",
    "detail": "If-Match ETag does not match current revision",
    "instance": "/api/v2/fhss/config/commit",
    "current_etag": "Rev:2",
    "expected_etag": "Rev:1"
  }
  ```

**Tests:**
- [ ] 200 OK with new revision on successful commit
- [ ] 409 Conflict on stale If-Match ETag
- [ ] 400 Bad Request if validation errors present
- [ ] Revision increments monotonically
- [ ] Committed edit removed from staged storage

#### Endpoint 8: DELETE /api/v2/fhss/config/staged/{id}

**Purpose:** Discard a staged edit

**Response (204 No Content):**
```
(empty body)
```

**Error Responses:**
- 404 Not Found: staged ID not found or expired

**Tests:**
- [ ] 204 No Content on successful discard
- [ ] Staged edit removed from storage
- [ ] 404 if already discarded

#### Endpoint 9: POST /api/v2/fhss/config/undo

**Purpose:** Undo the last committed configuration change

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.undo_result.v1",
  "new_revision": 1,
  "new_etag": "Rev:1",
  "source": { ... },
  "effective": { ... }
}
```

**Error Responses:**
- 400 Bad Request: cannot undo (no history or at beginning)
  ```json
  {
    "type": "about:blank",
    "status": 400,
    "title": "Bad Request",
    "detail": "Cannot undo: at beginning of history",
    "instance": "/api/v2/fhss/config/undo"
  }
  ```

**Tests:**
- [ ] 200 OK with previous revision
- [ ] Revision decrements correctly
- [ ] 400 Bad Request at beginning of history
- [ ] Undo then redo returns to original state

#### Endpoint 10: POST /api/v2/fhss/config/redo

**Purpose:** Redo the last undone configuration change

**Response (200 OK):**
```json
{
  "schema": "graphx.fhss_configuration.redo_result.v1",
  "new_revision": 3,
  "new_etag": "Rev:3",
  "source": { ... },
  "effective": { ... }
}
```

**Error Responses:**
- 400 Bad Request: cannot redo (nothing to redo)

**Tests:**
- [ ] 200 OK with next revision
- [ ] 400 Bad Request when nothing to redo
- [ ] Undo + Redo returns to state before undo

### 4. Determinism Verification (Byte-Identical JSON)

**Requirement:** All JSON responses must be byte-identical across multiple executions

#### Implementation

```cpp
// MANDATORY: All responses use this function
nlohmann::json SerializeWithSortedKeys(const nlohmann::json& j) {
  // Recursively sort all object keys alphabetically
  // Arrays preserve order (no sorting)
  // Return new JSON with sorted keys
}

// Usage:
auto response = CreateSuccessResponse(data);
std::string json_str = SerializeWithSortedKeys(response).dump();
```

#### Tests (10 Tests)

- [ ] Single field serializes identically (10 iterations)
- [ ] Multiple fields serialize with alphabetically sorted keys
- [ ] Nested objects have all levels sorted
- [ ] Arrays preserve order (not sorted)
- [ ] JSON roundtrip (dump → parse → dump) produces identical bytes
- [ ] Empty object {} serializes consistently
- [ ] Empty array [] serializes consistently
- [ ] Large configuration (100+ fields) serializes identically

### 5. ETag Optimistic Locking

**Requirement:** 409 Conflict on stale If-Match ETag

#### Implementation

```cpp
// ETag format: "Rev:N" where N is revision number
std::string current_etag = "Rev:" + std::to_string(GetCurrentRevision());

// On commit with If-Match:
if (!if_match_etag.empty() && if_match_etag != current_etag) {
  return 409_Conflict;
}
```

#### Tests (5+ Tests)

- [ ] 409 Conflict when If-Match does not match current revision
- [ ] 200 OK when If-Match matches current revision
- [ ] Multiple concurrent edits detect stale writes
- [ ] ETag increments with each commit
- [ ] If-Match header takes precedence over request body

### 6. Testing Strategy

#### Unit Tests (HTTP Server)

```cpp
TEST(FHSSConfigurationHttpServer, GetConfigReturnsCurrentSourceConfiguration)
TEST(FHSSConfigurationHttpServer, GetEffectiveConfigReturnsDerivedFields)
TEST(FHSSConfigurationHttpServer, GetHistoryReturnsLast10Revisions)
TEST(FHSSConfigurationHttpServer, CreateStagedEditReturnsHandle)
TEST(FHSSConfigurationHttpServer, UpdateStagedFieldValidatesOnUpdate)
TEST(FHSSConfigurationHttpServer, ValidateStagedEditReturnsAllRules)
TEST(FHSSConfigurationHttpServer, CommitStagedEditIncrementsRevision)
TEST(FHSSConfigurationHttpServer, CommitWith409ConflictOnStaleETag)
TEST(FHSSConfigurationHttpServer, DiscardStagedEditRemovesEdit)
TEST(FHSSConfigurationHttpServer, UndoReturnsToPrevi ousRevision)
TEST(FHSSConfigurationHttpServer, RedoReturnsToNextRevision)
```

#### Unit Tests (CLI)

```cpp
TEST(FHSSConfigurationCli, SetConfigParsesKeyValuePairs)
TEST(FHSSConfigurationCli, ConfigPatchReadsJsonFile)
TEST(FHSSConfigurationCli, ConfigPatchAppliesConditional)
TEST(FHSSConfigurationCli, ValidateConfigRunsAll13Rules)
TEST(FHSSConfigurationCli, ShowConfigDisplaysSourceConfig)
TEST(FHSSConfigurationCli, ShowConfigWithEffectiveFlag)
TEST(FHSSConfigurationCli, ShowConfigWithHistoryFlag)
TEST(FHSSConfigurationCli, CLIReturnsCorrectExitCodes)
```

#### Integration Tests (70+ Tests)

```cpp
TEST(FHSSConfigurationIntegration, EndToEndStagedEditWorkflow)
TEST(FHSSConfigurationIntegration, DeterminismVerification_10Iterations)
TEST(FHSSConfigurationIntegration, ETagConflictDetection)
TEST(FHSSConfigurationIntegration, All13ValidationRulesTriggered)
TEST(FHSSConfigurationIntegration, UndoRedoStateTransitions)
TEST(FHSSConfigurationIntegration, ConcurrentStagedEditsIsolated)
TEST(FHSSConfigurationIntegration, HistoryLimitedTo10Revisions)
TEST(FHSSConfigurationIntegration, JsonRoundTripPreservesData)
// ... 62+ more tests
```

#### Determinism Tests

```cpp
TEST(FHSSConfigurationDeterminism, ByteIdenticalJSONAcross10Iterations)
TEST(FHSSConfigurationDeterminism, SortedKeysAlphabetically)
TEST(FHSSConfigurationDeterminism, NestedObjectsAllSorted)
TEST(FHSSConfigurationDeterminism, ArraysPreserveOrder)
TEST(FHSSConfigurationDeterminism, RoundTripBytesIdentical)
```

#### Sanitizer Tests

```cpp
// Compile with -fsanitize=address -fsanitize=undefined
// Run all 70+ tests with sanitizers enabled
// Verify no memory leaks, use-after-free, or undefined behavior
```

#### Operator Acceptance Workflow

```bash
# Python operator tool (examples/DSP/configuration/operator/)
$ python3 fhss_configuration_operator.py prepare
$ python3 fhss_configuration_operator.py set-config \
    iq_center_frequency_hz=2400000000 \
    occupied_bandwidth_hz=40000000
$ python3 fhss_configuration_operator.py validate-config config.json
$ python3 fhss_configuration_operator.py show-config --effective
$ python3 fhss_configuration_operator.py report
```

#### Build and Compilation

```bash
# Clean build with warnings as errors
cd /Users/rklinkhammer/workspace/GraphX/build-ninja/ninja-debug-metal-native
cmake .. && ninja clean
ninja test_phase2a_configuration
./libdsp/test/test_phase2a_configuration
ctest -R "phase2a" -V

# Expected: 70+ tests, all passing
# Expected: 0 compiler warnings
# Expected: ASan/UBSan: no issues
```

---

## Verifier Assignment (Verification Phase)

**Duration:** 1–2 days  
**Responsible:** Independent Verifier Agent  
**Constraints:** Cannot edit implementation files; must verify independently with direct evidence

### Verifier Responsibilities

1. **Independent Verification (No Implementation Changes)**
   - Verifier may NOT edit source code, tests, or CLI tool
   - Verifier must verify each criterion with direct evidence
   - Run all tests independently; do not trust console output

2. **Verify Each HTTP Endpoint**
   - Test each of 10 endpoints with valid and invalid inputs
   - Verify response format (RFC 9457 or success JSON)
   - Check ETag conflict detection (409 responses)
   - Validate sorted-key JSON serialization

3. **Verify CLI Commands**
   - Test all 4 CLI commands with documented examples
   - Verify exit codes (0 for success, 1 for errors)
   - Check output format (JSON or error messages)
   - Verify file parsing (JSON files with relative paths)

4. **Verify Determinism**
   - Run configuration through same state 10 times
   - Dump JSON each time
   - Verify bytes are 100% identical
   - Test with large configurations (100+ fields)

5. **Verify Validation Rules**
   - Trigger each of 13 validation rules
   - Confirm all 13 reported simultaneously (non-failing)
   - Check error codes match specification
   - Verify error messages have no source paths

6. **Verify Revision Management**
   - Create staged edits, commit, verify revision increments
   - Test undo/redo: verify revision transitions are correct
   - Confirm revision never resets (monotonic)
   - Test history limit (max 10 revisions)

7. **Verify Installed-Tree Launch**
   - Build and install to temporary directory
   - Launch HTTP server from installed location
   - Execute CLI commands against installed configuration
   - Verify works without source directory

### Verifier Deliverables

```
verification/
├── VERIFICATION_REPORT.md
│   ├── Executive Summary (PASS/FAIL)
│   ├── HTTP Endpoint Tests (10 endpoints × 3 test cases = 30 tests)
│   ├── CLI Command Tests (4 commands × 2-3 test cases = 10+ tests)
│   ├── Determinism Verification (10 iterations, byte-identical)
│   ├── Validation Rule Coverage (all 13 rules triggered)
│   ├── Revision Management (monotonic increment, undo/redo)
│   ├── ETag Conflict Detection (409 scenarios)
│   ├── Installed-Tree Launch (verified working)
│   ├── Build Quality (0 warnings, ASan/UBSan pass)
│   ├── Known Issues (if any)
│   └── Standards Compliance (Phase 2A coverage)
├── test_execution_logs.txt
├── http_endpoint_evidence/
│   ├── get_config_response.json
│   ├── get_effective_response.json
│   ├── commit_conflict_409.json
│   └── ... (all 10 endpoints)
├── cli_evidence/
│   ├── set_config_output.txt
│   ├── validate_config_errors.json
│   └── ... (all 4 commands)
├── determinism_verification/
│   ├── iteration_1.json
│   ├── iteration_2.json
│   └── ... (10 iterations)
├── validation_rules_evidence.md
│   └── (All 13 rules triggered with error codes)
└── installed_tree_test_log.txt
```

### Acceptance Criteria (All Must Pass)

**HTTP Server Integration:**
- [ ] All 10 endpoints implemented and responding
- [ ] RFC 9457 error format used for all errors
- [ ] Success responses use sorted-key JSON
- [ ] ETag header present in responses

**CLI Commands:**
- [ ] --set-config parses and applies changes
- [ ] --config-patch reads file and applies RFC 6902 patch
- [ ] --validate-config runs all 13 rules without committing
- [ ] --show-config displays current or effective configuration
- [ ] All exit codes correct (0 for success, 1 for errors)

**Determinism:**
- [ ] JSON output byte-identical across 10 iterations
- [ ] Sorted keys alphabetically at all levels
- [ ] Arrays preserve insertion order

**Validation:**
- [ ] All 13 validation rules tested and verified
- [ ] Non-failing validation (all errors reported simultaneously)
- [ ] Error messages without source paths
- [ ] Error codes match specification

**Revision Management:**
- [ ] Revision increments monotonically (1 → 2 → 3...)
- [ ] ETag format: "Rev:N" where N is revision
- [ ] 409 Conflict on stale If-Match ETag
- [ ] Undo/Redo state transitions correct
- [ ] History limited to last 10 revisions

**Build Quality:**
- [ ] C++26 compilation succeeds
- [ ] Zero compiler warnings (-Wall -Wextra -Werror)
- [ ] ASan/UBSan tests pass (no memory issues)
- [ ] All 70+ tests passing

**Installed-Tree:**
- [ ] Build and install to /tmp/graphx-phase2a-test
- [ ] HTTP server launches from installed location
- [ ] CLI commands execute against installed config
- [ ] No source directory references in binaries

---

## Phase 2A Success Criteria

**Phase 2A is COMPLETE when:**

✅ 90 existing Phase 2A pre-req tests passing (FHSSConfigurationDeriver + FHSSCrossNodeValidator + ConfigurationStateMachine)  
✅ 70+ new Phase 2A HTTP/CLI tests passing  
✅ All 10 HTTP endpoints verified working  
✅ All 4 CLI commands verified working  
✅ Determinism verified (10 iterations, byte-identical JSON)  
✅ All 13 validation rules triggered and verified  
✅ ETag conflict detection verified (409 responses)  
✅ Zero compiler warnings (C++26 with -Wall -Wextra -Werror)  
✅ ASan/UBSan tests passing (no memory safety issues)  
✅ Installed-tree launch verified working  
✅ Orchestrator, Implementer, and Verifier reports complete  

**Timeline:** 2–3 days from orchestrator go-ahead to verifier approval
