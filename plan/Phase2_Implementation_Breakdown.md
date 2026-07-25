# Phase 2 Implementation Plan Breakdown

**Duration:** 2 weeks (10 business days)  
**Team:** 2 engineers (80-95 hours/week capacity each)  
**Target:** 150+ tests, 95%+ pass rate  
**Status:** Ready to start Phase 2A (Days 3-5)

---

## Phase 2A: Core Infrastructure (Days 3-5)

### Deliverable 1: FHSSConfigurationDeriver

**Files:**
- `libdsp/include/dsp/fhss/FHSSConfigurationDeriver.hpp` (150 lines)
- `libdsp/src/dsp/fhss/FHSSConfigurationDeriver.cpp` (400 lines)

**Responsibilities:**
- Compute all 12 generated fields deterministically
- Input: Authoritative config (18 fields)
- Output: Effective config with derivations

**Implementation Checklist:**
- [ ] Extract active frequencies from preamble pulses
- [ ] Compute preamble pulse count
- [ ] Generate RF/impairment configurations
- [ ] Generate message assembler config
- [ ] Compute schedule duration and total pulses
- [ ] Calculate bandwidth per channel
- [ ] Calculate CFO margin
- [ ] Ensure byte-identical output for same input
- [ ] Unit tests (25 test cases)

**Estimated Hours:** 24

---

### Deliverable 2: FHSSCrossNodeValidator

**Files:**
- `libdsp/include/dsp/fhss/FHSSCrossNodeValidator.hpp` (100 lines)
- `libdsp/src/dsp/fhss/FHSSCrossNodeValidator.cpp` (350 lines)

**Responsibilities:**
- Enforce all 13 validation rules
- Return stable error codes per rule
- Provide clear error messages (no source paths)

**Rule Implementations:**
- [ ] Rule 1: Topology Invariant (ERR_TOPOLOGY_INVALID)
- [ ] Rule 2: Message Uniqueness (ERR_MSG_ID_DUP)
- [ ] Rule 3: Preamble Format (ERR_PREAMBLE_INVALID)
- [ ] Rule 4: Frequency Constraint (ERR_FREQ_OUT_OF_RANGE)
- [ ] Rule 5: Schedule Ordering (ERR_SCHEDULE_UNORDERED)
- [ ] Rule 6: Cross-Node Consistency (ERR_NODE_CONFIG_MISMATCH)
- [ ] Rule 7: Bandwidth/CFO Agreement (ERR_CFO_EXCEEDS_BANDWIDTH)
- [ ] Rule 8: Derived Projection Match (ERR_DERIVED_MISMATCH)
- [ ] Rule 9: Topology Preservation (ERR_TOPOLOGY_CHANGED)
- [ ] Rule 10: Idle Duration Valid (ERR_IDLE_INVALID)
- [ ] Rule 11: Message Window Valid (ERR_MSG_WINDOW_INVALID)
- [ ] Rule 12: Pulse Role Consistency (ERR_PULSE_ROLE_INVALID)
- [ ] Rule 13: Frequency Index Bounds (ERR_FREQ_INDEX_OOB)
- [ ] Unit tests (30 test cases, one per rule + combinations)

**Estimated Hours:** 20

---

### Deliverable 3: ConfigurationStateMachine

**Files:**
- `libgraph/include/graph/dashboard/ConfigurationStateMachine.hpp` (120 lines)
- `libgraph/src/graph/dashboard/ConfigurationStateMachine.cpp` (280 lines)

**Responsibilities:**
- Manage configuration state transitions
- Handle revision tracking with monotonic counter
- Implement revision conflict detection
- Manage undo/redo stack

**Implementation Checklist:**
- [ ] Monotonic revision counter (no wrap-around)
- [ ] ETag generation ("graphx-config-{revision}")
- [ ] Precondition checking (If-Match validation)
- [ ] Atomic state transitions (lock-based)
- [ ] Undo stack management (max 100 entries)
- [ ] Revision history (configurable retention)
- [ ] Conflict detection and resolution
- [ ] Thread safety (std::mutex protection)
- [ ] Unit tests (20 test cases)

**Estimated Hours:** 16

---

### Deliverable 4: Phase 2A Test Suite

**Files:**
- `libdsp/test/unit/test_fhss_configuration_deriver.cpp` (~600 lines, 35 tests)
- `libdsp/test/unit/test_fhss_cross_node_validator.cpp` (~500 lines, 30 tests)
- `libgraph/test/unit/test_configuration_state_machine.cpp` (~400 lines, 20 tests)

**Test Categories:**

**FHSSConfigurationDeriver Tests (35):**
- [ ] Active frequency extraction (5 tests)
  - Empty preamble
  - Single preamble frequency
  - Multiple preambles
  - More than 16 preambles (use first 16 only)
  - Preambles with duplicates
- [ ] Preamble pulse counting (3 tests)
- [ ] Channelizer config derivation (4 tests)
- [ ] Message assembler config derivation (4 tests)
- [ ] Schedule computation (3 tests)
- [ ] Determinism verification (5 tests)
  - Same input, byte-identical output
  - Different orders produce same output
  - JSON key ordering consistent
- [ ] Edge cases (4 tests)
  - Empty message list
  - Single message
  - Large configuration (1000+ messages)
  - Min/max values

**FHSSCrossNodeValidator Tests (30):**
- [ ] All 13 rules individually (13 tests)
  - One test per rule passing case
- [ ] Rules in combination (10 tests)
  - Valid config with all rules satisfied
  - 3-4 invalid rules simultaneously
  - Cascading failures
- [ ] Error message quality (4 tests)
  - No source paths
  - Clear, actionable messages
  - Pointer to problematic field
- [ ] Performance (3 tests)
  - Validation under 100ms (large config)
  - No unnecessary copies

**ConfigurationStateMachine Tests (20):**
- [ ] Revision tracking (5 tests)
  - Monotonic increment
  - Max value handling (2^53-1)
  - Wrap-around protection
  - Revision never skips
- [ ] ETag generation (2 tests)
  - Format: "graphx-config-{revision}"
  - Quoting correct
- [ ] Precondition validation (5 tests)
  - If-Match present and matches
  - If-Match missing (error)
  - If-Match stale (error)
  - If-Match garbage (error)
- [ ] Undo/Redo (4 tests)
  - Undo stack push/pop
  - Undo increments revision
  - Undo empty stack error
  - Redo not supported (test that it's not available)
- [ ] Thread safety (4 tests)
  - TSAN clean (no data races)
  - Concurrent reads (allowed)
  - Concurrent writes (serialized)
  - Read-write interactions

**Total Phase 2A Tests:** 85+ test cases

**Estimated Hours:** 20

---

## Phase 2A Summary

| Deliverable | Files | Lines | Hours |
|-------------|-------|-------|-------|
| ConfigurationDeriver | 2 | 550 | 24 |
| CrossNodeValidator | 2 | 450 | 20 |
| StateMachine | 2 | 400 | 16 |
| Test Suite | 3 | 1500 | 20 |
| **TOTAL** | **9** | **2900** | **80** |

**Expected Outcome:**
- ✅ 85+ unit tests passing
- ✅ ASan/UBSan clean
- ✅ All 13 validation rules working
- ✅ All 12 generated fields deterministic
- ✅ Thread-safe state machine
- ✅ Revision conflict detection functional

---

## Phase 2B: Transaction & HTTP Layer (Days 6-8)

### Deliverable 5: JSON Pointer/Patch Implementation

**Files:**
- `libgraph/include/graph/dashboard/JsonPointer.hpp` (80 lines)
- `libgraph/src/graph/dashboard/JsonPointer.cpp` (150 lines)

**Responsibilities:**
- RFC 6901 JSON Pointer parsing
- RFC 6902 JSON Patch application
- Atomicity guarantees (all-or-nothing)
- Read-only field protection

**Implementation Checklist:**
- [ ] Parse RFC 6901 pointers
  - Root (`""`)
  - Path traversal (`/a/b/c`)
  - Array indices (`/items/0`, `/items/-`)
  - Escape sequences (`~0` for `~`, `~1` for `/`)
- [ ] Apply RFC 6902 operations
  - `add` — Insert or replace
  - `remove` — Delete field/element
  - `replace` — Replace value
  - `move` — Relocate field
  - `copy` — Duplicate field
  - `test` — Assertion (fail patch if condition false)
- [ ] Atomic application (no partial updates)
- [ ] Validate generated field protection
- [ ] Unit tests (15 tests)

**Estimated Hours:** 10

---

### Deliverable 6: HTTP Configuration Endpoints (10 total)

**File:**
- `libgraph/src/dashboard/ConfigurationHttpHandler.cpp` (400 lines)

**Endpoints:**

1. **GET /api/v1/fhss/config**
   - Return effective configuration
   - Include ETag header

2. **GET /api/v1/fhss/config/authoritative**
   - Return authoritative config only
   - Include ETag header

3. **GET /api/v1/fhss/config/effective**
   - Alias for /api/v1/fhss/config
   - Return effective configuration

4. **GET /api/v1/fhss/config/provenance**
   - Return derivation provenance
   - Which generated field from which rule

5. **GET /api/v1/fhss/config/derived-paths**
   - List read-only field pointers
   - Used for UI to mark fields as read-only

6. **GET /api/v1/fhss/config/value?pointer=/path**
   - Get single value at pointer
   - RFC 6901 pointer required
   - Return 404 if not found

7. **POST /api/v1/fhss/config/validate**
   - Validate without applying
   - Return validation report

8. **PATCH /api/v1/fhss/config**
   - Apply RFC 6902 JSON Patch
   - Require If-Match header
   - Atomic: all operations or none

9. **POST /api/v1/fhss/config/export**
   - Export config to file (Phase 1 pattern reused)
   - Return operation ID

10. **POST /api/v1/fhss/config/undo**
    - Undo last configuration change
    - Create new revision

**Implementation Checklist:**
- [ ] All 10 endpoints implemented
- [ ] RFC 9457 error handling
- [ ] Precondition validation (If-Match)
- [ ] Security headers on all responses
- [ ] Content-Type: application/json
- [ ] ETag generation/validation
- [ ] Request/response validation
- [ ] Status code mapping

**Estimated Hours:** 18

---

### Deliverable 7: CLI Commands (4 total)

**File:**
- `examples/DSP/tools/fhss_config_cli.py` (300 lines)

**Commands:**

1. **get** — Retrieve current configuration
   ```bash
   fhss_config_cli get [--format json|yaml] [--pointer /path]
   ```

2. **set** — Update single field
   ```bash
   fhss_config_cli set --pointer /scenario/iq_center_frequency_hz --value 1250000000
   ```

3. **validate** — Validate configuration
   ```bash
   fhss_config_cli validate [--file config.json]
   ```

4. **export** — Export configuration to file
   ```bash
   fhss_config_cli export --output config_backup.json
   ```

**Implementation Checklist:**
- [ ] Command-line argument parsing
- [ ] HTTP client integration
- [ ] JSON/YAML output formatting
- [ ] Error reporting
- [ ] Help text
- [ ] Bash completion (optional)

**Estimated Hours:** 14

---

### Deliverable 8: Phase 2B Integration Tests

**Files:**
- `libgraph/test/integration/test_configuration_http_endpoints.cpp` (~800 lines, 50 tests)
- `examples/DSP/test/test_fhss_config_cli.py` (~400 lines, 20 tests)

**HTTP Endpoint Tests (50):**
- [ ] GET endpoints (10 tests)
  - /config returns effective
  - /config/authoritative returns source
  - /config/provenance has derivations
  - /config/derived-paths lists read-only
  - /config/value with valid pointer
  - /config/value with invalid pointer (404)
  - All include correct ETag
  - All include security headers
- [ ] POST validate (5 tests)
  - Valid config accepted
  - Invalid config rejected with reasons
  - All 13 validation rules exercised
  - Error codes stable
- [ ] PATCH operations (15 tests)
  - Simple replace operation
  - Multiple operations (RFC 6902)
  - If-Match present (200 success)
  - If-Match missing (428)
  - If-Match stale (412)
  - Attempt to modify generated field (409)
  - Attempt to modify outside authoritative (409)
  - Atomic: all succeed or all fail
  - New revision on success
  - New ETag on success
- [ ] PATCH move/copy (5 tests)
- [ ] POST export (5 tests)
- [ ] POST undo (5 tests)
  - Undo success
  - Undo empty stack (error)
  - Revision incremented
  - New ETag generated

**CLI Tests (20):**
- [ ] CLI basic commands (5 tests)
- [ ] Error handling (5 tests)
- [ ] Output formatting (5 tests)
- [ ] Integration with HTTP API (5 tests)

**Total Phase 2B Tests:** 70+ integration test cases

**Estimated Hours:** 22

---

## Phase 2B Summary

| Deliverable | Files | Lines | Hours |
|-------------|-------|-------|-------|
| JSON Pointer/Patch | 2 | 230 | 10 |
| HTTP Endpoints | 1 | 400 | 18 |
| CLI Commands | 1 | 300 | 14 |
| Integration Tests | 2 | 1200 | 22 |
| **TOTAL** | **6** | **2130** | **64** |

**Expected Outcome:**
- ✅ 70+ integration tests passing
- ✅ All 10 HTTP endpoints operational
- ✅ RFC 6902 JSON Patch fully functional
- ✅ 4 CLI commands working end-to-end
- ✅ 428/412/409 precondition errors correct
- ✅ Atomic transactions verified

---

## Phase 2C: Frontend & Verification (Days 9-10)

### Deliverable 9: Frontend Components

**Files:**
- `examples/DSP/dashboard/frontend/src/ConfigEditor.tsx` (200 lines)
- `examples/DSP/dashboard/frontend/src/ParameterInspector.tsx` (150 lines)

**Components:**

**ConfigEditor:**
- [ ] Display authoritative config in tabs
- [ ] Edit mode for user fields
- [ ] Read-only display for generated fields
- [ ] Inline validation
- [ ] Save/Cancel buttons
- [ ] Undo/Redo UI controls

**ParameterInspector:**
- [ ] Node selector dropdown
- [ ] Parameter list with descriptions
- [ ] Type display (number, string, boolean, etc.)
- [ ] Range validation (min/max)
- [ ] Current value display
- [ ] Help text from GetParameterDescription()

**Estimated Hours:** 16

---

### Deliverable 10: End-to-End Tests

**Files:**
- `examples/DSP/test/test_fhss_configuration_e2e.cpp` (~600 lines, 30 tests)
- Browser tests (Playwright/Selenium) TBD

**Test Scenarios (30):**
- [ ] Create valid config from scratch (3 tests)
  - Minimal valid config
  - Full-featured config
  - Large config (1000+ messages)

- [ ] Modify and validate (5 tests)
  - Single field update
  - Multiple fields (JSON Patch)
  - Invalid modification (reject)
  - Partial modification attempt

- [ ] Revision tracking (3 tests)
  - Revision increments
  - ETag changes
  - Preconditions enforced

- [ ] Error scenarios (5 tests)
  - Invalid pointer
  - Stale If-Match
  - Generated field modification
  - Malformed JSON Patch
  - Validation failure

- [ ] Concurrency (4 tests)
  - Two clients race (one wins)
  - Concurrent reads allowed
  - Serialized writes

- [ ] CLI integration (3 tests)
  - Get current config
  - Update via CLI
  - Validate before commit

- [ ] Node inspection (2 tests)
  - Get node parameters
  - Get parameter descriptions

**Estimated Hours:** 18

---

### Deliverable 11: Verification Gates

**Checklist:**
- [ ] ASan clean (no heap issues)
  ```bash
  cd build && cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..
  ctest -R phase2 --output-on-failure
  ```

- [ ] UBSan clean (no undefined behavior)
  ```bash
  cd build && cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_UBSAN=ON ..
  ctest -R phase2 --output-on-failure
  ```

- [ ] clang-format passes
  ```bash
  clang-format -i src/**/*.{hpp,cpp}
  git diff --exit-code
  ```

- [ ] Doxygen generates without warnings
  ```bash
  doxygen && grep "^warning:" doxygen-warnings.txt
  ```

- [ ] All tests passing
  ```bash
  ctest -R phase2 --output-on-failure -j4
  ```

- [ ] Valgrind clean (optional)
  ```bash
  valgrind --leak-check=full test_configuration_service
  ```

**Estimated Hours:** 12

---

## Phase 2C Summary

| Deliverable | Files | Lines | Hours |
|-------------|-------|-------|-------|
| Frontend Components | 2 | 350 | 16 |
| E2E Tests | 1 | 600 | 18 |
| Verification Gates | - | - | 12 |
| **TOTAL** | **3+** | **950** | **46** |

**Expected Outcome:**
- ✅ UI parameter editor functional
- ✅ 30+ end-to-end test scenarios passing
- ✅ No ASan/UBSan violations
- ✅ Code formatted (clang-format)
- ✅ Documentation generated (Doxygen)
- ✅ All tests passing (150+)

---

## Overall Timeline

```
Week 1 (Days 1-5)
├─ Days 1-2: Pre-Implementation Audit ✅
│           (THIS REPORT + design documents)
├─ Day 3:   Phase 2A Kickoff
│           ├─ ConfigurationDeriver implementation
│           └─ CrossNodeValidator starts
├─ Day 4:   Continued Phase 2A
│           ├─ ConfigurationDeriver complete
│           ├─ CrossNodeValidator complete
│           └─ StateMachine starts
├─ Day 5:   Phase 2A Completion
│           ├─ StateMachine complete
│           ├─ All Phase 2A tests passing
│           └─ ASan/UBSan verification

Week 2 (Days 6-10)
├─ Day 6:   Phase 2B Kickoff
│           ├─ JSON Pointer/Patch
│           └─ HTTP endpoints starts
├─ Day 7:   Continued Phase 2B
│           ├─ HTTP endpoints complete
│           ├─ CLI commands
│           └─ Integration tests
├─ Day 8:   Phase 2B Completion
│           ├─ All endpoints tested
│           ├─ All CLI commands working
│           └─ 70+ integration tests passing
├─ Day 9:   Phase 2C Kickoff
│           ├─ Frontend components
│           └─ E2E tests
└─ Day 10:  Phase 2C Completion + Handoff
            ├─ UI functional
            ├─ 30+ E2E tests passing
            ├─ Verification gates passing
            └─ Ready for Phase 3 (HTTP Server Integration)
```

---

## Resource Allocation

**Team:** 2 engineers (assume 80-hour weeks typical)

| Phase | Engineer 1 | Engineer 2 | Overlap |
|-------|-----------|-----------|---------|
| 2A | ConfigurationDeriver (24h) + Tests (10h) | CrossNodeValidator (20h) + StateMachine (16h) + Tests (10h) | 4h (code review) |
| 2B | JsonPointer (10h) + HTTPEndpoints (18h) | CLI (14h) + IntegrationTests (22h) | 4h (integration) |
| 2C | Frontend (16h) | E2ETests (18h) + Verification (12h) | 4h (validation) |
| **Total** | **54h** | **104h** | **12h overlap** |

**Total Team Hours:** ~146 hours (net 158 - 12 overlap)  
**Elapsed Time:** 10 business days (2 weeks)  
**Velocity:** ~16 hours/day per team

---

## Success Criteria

✅ **Code Quality:**
- Zero ASan/UBSan violations
- clang-format passing
- Doxygen clean

✅ **Functionality:**
- All 13 validation rules working
- All 12 generated fields deterministic
- All 10 HTTP endpoints operational
- 4 CLI commands functional

✅ **Testing:**
- 150+ total test cases
- 85+ unit tests (Phase 2A)
- 70+ integration tests (Phase 2B)
- 30+ E2E tests (Phase 2C)
- 95%+ pass rate

✅ **Documentation:**
- ConfigurationService design complete
- API endpoints documented
- Error codes stable and documented
- CLI help text complete

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Derivation non-determinism | HIGH | Golden dataset tests + byte-comparison |
| Validation rule gaps | MEDIUM | Fuzz testing + rule enumeration checklist |
| Concurrency bugs | MEDIUM | ThreadSanitizer (TSAN) + stress tests |
| HTTP precondition errors | MEDIUM | Unit tests for each error path |
| Timeline slip | LOW | Defer UI/E2E to Phase 3 if needed |

---

## Deliverables Summary

| Phase | Deliverable | Count | Lines | Hours | Status |
|-------|-------------|-------|-------|-------|--------|
| 2A | ConfigurationDeriver | 1 | 550 | 24 | ⏳ Ready |
| 2A | CrossNodeValidator | 1 | 450 | 20 | ⏳ Ready |
| 2A | StateMachine | 1 | 400 | 16 | ⏳ Ready |
| 2A | Unit Tests | 3 | 1500 | 20 | ⏳ Ready |
| 2B | JSON Pointer/Patch | 1 | 230 | 10 | ⏳ Ready |
| 2B | HTTP Endpoints | 1 | 400 | 18 | ⏳ Ready |
| 2B | CLI Commands | 1 | 300 | 14 | ⏳ Ready |
| 2B | Integration Tests | 2 | 1200 | 22 | ⏳ Ready |
| 2C | Frontend | 1 | 350 | 16 | ⏳ Ready |
| 2C | E2E Tests | 1 | 600 | 18 | ⏳ Ready |
| 2C | Verification | - | - | 12 | ⏳ Ready |
| **TOTAL** | **11** | **6380** | **190** | **✅ READY** |

---

## Handoff Criteria (End of Phase 2)

Before moving to Phase 3:

- [ ] All 150+ tests passing
- [ ] No ASan/UBSan violations
- [ ] ConfigurationService fully implemented (16 methods)
- [ ] All 13 validation rules working
- [ ] All 10 HTTP endpoints operational
- [ ] All 4 CLI commands functional
- [ ] UI parameter editor complete
- [ ] Doxygen documentation complete
- [ ] All code reviewed and approved
- [ ] Git commit history clean
- [ ] Phase 2 verification report submitted

**Estimated Completion:** End of Day 10 (2026-08-07)  
**Verification Target:** 2026-08-14  
**Phase 3 Start:** 2026-08-21

