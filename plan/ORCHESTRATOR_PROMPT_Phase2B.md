# Phase 2B Orchestrator Prompt: Receiver Graph Integration & Advanced State Management

**Orchestrator Authority:** Complete Project Signoff  
**Launch Date:** 2026-07-25  
**Target Completion:** 2026-08-08  
**Duration Estimate:** 2 weeks  
**Estimated Effort:** 160-200 engineering hours  

---

## ORCHESTRATOR DIRECTIVE

**Status:** ✅ PHASE 2A APPROVED  
**Decision:** AUTHORIZE PHASE 2B EXECUTION  
**Mandate:** Execute Phase 2B implementation cycle with implementor and verifier agents until completion

---

## Phase 2B Scope & Architecture

### Strategic Overview

Phase 2B extends the Phase 2A configuration HTTP server and CLI handler to integrate with the **receiver graph architecture**—enabling dynamic configuration of receiver nodes that listen on specific frequency channels and process RF signal data.

This phase adds:
1. **Receiver Graph** — Dynamic mapping of receivers to RF channels
2. **Cross-Node State Coordination** — Configuration changes propagating through receiver topology
3. **Advanced State Management** — Complex state transitions involving multiple nodes
4. **Extended API Endpoints** — 8-12 new REST endpoints for receiver management
5. **Performance Optimization** — Sub-10ms latency for all state queries

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│              PHASE 2B ARCHITECTURE                      │
├─────────────────────────────────────────────────────────┤
│                                                          │
│   ┌──────────────────────────────────────────────────┐  │
│   │   Configuration State Machine (Phase 2A)        │  │
│   │   - Staged edits, validation, commits           │  │
│   │   - Monotonic revision tracking                 │  │
│   └────────────────┬─────────────────────────────────┘  │
│                    │ Uses                                │
│   ┌────────────────▼─────────────────────────────────┐  │
│   │   Receiver Graph Coordinator (NEW Phase 2B)     │  │
│   │   - Dynamic receiver topology                   │  │
│   │   - Cross-node state transitions                │  │
│   │   - Conflict detection & resolution             │  │
│   └────────────────┬─────────────────────────────────┘  │
│                    │ Manages                             │
│   ┌────────────────▼─────────────────────────────────┐  │
│   │   Receiver Nodes (Multiple Instances)           │  │
│   │   - Each node has frequency, bandwidth, role    │  │
│   │   - Connected via graph topology                │  │
│   │   - State must be consistent across all nodes   │  │
│   └──────────────────────────────────────────────────┘  │
│                                                          │
│   ┌──────────────────────────────────────────────────┐  │
│   │   HTTP Server (Phase 2A + Extensions)           │  │
│   │   - /api/v2/fhss/config/* (Phase 2A)           │  │
│   │   - /api/v2/fhss/receivers/* (NEW Phase 2B)    │  │
│   └──────────────────────────────────────────────────┘  │
│                                                          │
│   ┌──────────────────────────────────────────────────┐  │
│   │   CLI Handler (Phase 2A + Extensions)           │  │
│   │   - --show-config (Phase 2A)                    │  │
│   │   - --receiver-topology (NEW Phase 2B)          │  │
│   └──────────────────────────────────────────────────┘  │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

---

## Phase 2B Implementation Scope

### New Components to Implement

#### 1. ReceiverGraphCoordinator (NEW)
- **Purpose:** Manages receiver topology and cross-node state coordination
- **Responsibility:** 
  - Dynamic receiver registration/deregistration
  - State propagation across receiver graph
  - Conflict detection when configuration changes affect multiple receivers
  - Topology consistency validation
- **Interface to implement:**
  ```cpp
  class ReceiverGraphCoordinator {
  public:
      // Registration
      bool RegisterReceiver(ReceiverNode* node);
      bool UnregisterReceiver(const std::string& receiver_id);
      
      // Query
      std::vector<ReceiverNode*> GetReceiversByFrequency(uint64_t frequency_hz);
      ReceiverNode* GetReceiver(const std::string& receiver_id);
      
      // State coordination
      bool PropagateConfigurationChange(const SourceConfiguration& new_config);
      std::vector<std::string> DetectConflicts(const SourceConfiguration& new_config);
      bool ResolveConflicts(const std::vector<std::string>& conflict_ids);
      
      // Topology
      nlohmann::json GetTopologyAsJson();
      bool ValidateTopology();
  };
  ```

#### 2. ReceiverNode Enhancements (EXTEND)
- **Current State:** Basic node structure exists
- **Phase 2B Enhancements:**
  - Add frequency binding (each receiver listens on specific RF channel)
  - Add bandwidth allocation tracking
  - Add state synchronization mechanism
  - Add conflict state handling (can be in conflicted state)
- **New Fields:**
  - `frequency_hz` — RF channel frequency
  - `bandwidth_hz` — Receiver bandwidth
  - `role` — Role in topology (primary, secondary, relay)
  - `is_conflicted` — Whether node has unresolved config conflict
  - `conflict_reason` — Human-readable conflict description

#### 3. ReceiverGraphHttpServer (NEW)
- **Purpose:** HTTP REST interface for receiver topology management
- **New Endpoints (8-12):**
  1. `GET /api/v2/fhss/receivers` — List all receivers
  2. `POST /api/v2/fhss/receivers` — Register new receiver
  3. `GET /api/v2/fhss/receivers/{id}` — Get receiver details
  4. `PATCH /api/v2/fhss/receivers/{id}` — Update receiver configuration
  5. `DELETE /api/v2/fhss/receivers/{id}` — Unregister receiver
  6. `GET /api/v2/fhss/topology` — Get full receiver topology
  7. `POST /api/v2/fhss/topology/validate` — Validate topology
  8. `GET /api/v2/fhss/receivers/conflicts` — List topology conflicts
  9. `POST /api/v2/fhss/receivers/{id}/resolve-conflict` — Resolve receiver conflict
  10. `GET /api/v2/fhss/receivers/frequency/{freq_hz}` — Get receivers on frequency

- **Response Format (RFC Compliant):**
  ```json
  {
    "schema": "graphx.fhss_receiver_list.v1",
    "receivers": [
      {
        "id": "receiver-001",
        "frequency_hz": 2450000000,
        "bandwidth_hz": 20000000,
        "role": "primary",
        "is_conflicted": false,
        "state": "active",
        "revision": 42
      }
    ],
    "revision": 42
  }
  ```

#### 4. ReceiverGraphCli (NEW)
- **Purpose:** CLI commands for receiver topology management
- **New Commands (4-6):**
  - `--receiver-topology` — Display receiver topology
  - `--receiver-topology --validate` — Validate topology consistency
  - `--register-receiver ID FREQ_HZ BANDWIDTH_HZ [ROLE]` — Register receiver
  - `--unregister-receiver ID` — Unregister receiver
  - `--list-receivers [--frequency FREQ_HZ]` — List receivers
  - `--receiver-conflicts` — Show topology conflicts

---

### Phase 2A Integration Points

**DO NOT MODIFY Phase 2A Code** (it's locked and tested):
- FHSSConfigurationHttpServer.cpp — Use as-is
- FHSSConfigurationCli.cpp — Use as-is
- FHSSConfigurationDeriver.cpp — Use as-is
- FHSSConfigurationStateMachine.cpp — Use as-is
- All Phase 2A tests must continue passing (90/90 + 112/112)

**Phase 2B Extends Phase 2A:**
- ReceiverGraphCoordinator uses ConfigurationStateMachine (composition)
- ReceiverGraphHttpServer calls both FHSSConfigurationHttpServer and new ReceiverGraph endpoints
- CLI routes requests to both FHSSConfigurationCli and new ReceiverGraphCli

---

## Implementation Tasks

### Implementor Agent Tasks

**Task 1: ReceiverGraphCoordinator Implementation (Est. 4-6 hours)**
- File: `libdsp/src/configuration/ReceiverGraphCoordinator.cpp` (NEW, ~400-500 lines)
- File: `libdsp/include/dsp/configuration/ReceiverGraphCoordinator.hpp` (NEW, ~150-200 lines)
- Requirements:
  - Thread-safe receiver registration (use std::mutex)
  - O(1) receiver lookup by ID (use std::unordered_map)
  - O(log n) frequency-based queries (use std::multimap<freq, receiver*>)
  - Conflict detection algorithm (check bandwidth overlaps)
  - Topology validation (verify graph connectivity)
- Acceptance Criteria:
  - All 4 methods implemented (Register, Unregister, PropagateChange, DetectConflicts)
  - Thread safety verified
  - No resource leaks
  - Compile with -Wall -Wextra -Werror

**Task 2: ReceiverGraphHttpServer Implementation (Est. 5-7 hours)**
- File: `libdsp/src/configuration/ReceiverGraphHttpServer.cpp` (NEW, ~600-700 lines)
- File: `libdsp/include/dsp/configuration/ReceiverGraphHttpServer.hpp` (NEW, ~150-200 lines)
- Requirements:
  - All 10 endpoints implemented (GET, POST, PATCH, DELETE)
  - RFC 9457 error responses
  - RFC 7232 ETag handling (extend from Phase 2A pattern)
  - Deterministic JSON output (use SerializeWithSortedKeys from Phase 2A)
  - Conflict state responses (409 Conflict or 422 Unprocessable Entity)
- Acceptance Criteria:
  - All endpoints callable and returning correct status codes
  - Error responses follow Problem Details format
  - ETag headers present in all responses
  - Integration with ReceiverGraphCoordinator verified

**Task 3: ReceiverGraphCli Implementation (Est. 4-5 hours)**
- File: `libdsp/src/configuration/ReceiverGraphCli.cpp` (NEW, ~350-450 lines)
- File: `libdsp/include/dsp/configuration/ReceiverGraphCli.hpp` (NEW, ~100-150 lines)
- Requirements:
  - All 6 CLI commands implemented
  - Argument parsing and validation
  - Integration with ReceiverGraphCoordinator
  - Help text for all commands
  - Exit codes: 0 (success), 1 (error), 2 (invalid args)
- Acceptance Criteria:
  - All commands parse correctly
  - All commands execute without segfaults
  - Help output complete and accurate

**Task 4: Comprehensive Integration Tests (Est. 6-8 hours)**
- File: `libdsp/test/integration/test_receiver_graph_integration.cpp` (NEW, ~700-900 lines)
- Test Categories:
  - **Endpoint Tests (20+ tests):** All 10 endpoints callable, correct responses
  - **Topology Tests (10+ tests):** Receiver registration, topology validation, connectivity
  - **Conflict Tests (15+ tests):** Frequency conflicts, bandwidth overlaps, resolution
  - **CLI Tests (10+ tests):** All 6 commands working end-to-end
  - **Cross-Node Tests (10+ tests):** Multi-receiver scenarios, state coordination
  - **Standards Tests (5+ tests):** RFC 9457, 7232, 8259 compliance
  - **Edge Case Tests (10+ tests):** Empty graph, single receiver, large topologies
- Acceptance Criteria:
  - 100+ test cases, 300+ assertions
  - 100% pass rate required
  - All edge cases covered
  - Performance: all tests complete in < 5 seconds

**Task 5: CMake Integration (Est. 1-2 hours)**
- Update: `libdsp/test/CMakeLists.phase2b.txt` (NEW)
- Update: `libdsp/CMakeLists.txt` (add Phase 2B sources)
- Requirements:
  - All new .cpp and .hpp files in build system
  - Test target: test_phase2b_receiver_graph
  - Combined test target: test_phase2_full (runs all Phase 2A + 2B tests)
  - No build errors or warnings
- Acceptance Criteria:
  - Clean cmake configuration
  - All targets build successfully
  - test_phase2b_receiver_graph executable created

---

## Verifier Agent Tasks

### First Verification Pass (After Implementation)

**V1.1: Build Verification**
- Build project with: `cmake --build . --target test_phase2b_receiver_graph`
- Acceptance: Zero errors, zero warnings

**V1.2: Test Execution**
- Run: `./libdsp/test/test_phase2b_receiver_graph --reporter=json > results.json`
- Verify:
  - 100+ test cases executed
  - 300+ assertions evaluated
  - 0 failures, 0 errors
  - All tests pass

**V1.3: Regression Testing**
- Run all Phase 2A tests: `./libdsp/test/test_phase2a_configuration`
- Verify:
  - 112/112 tests still passing
  - 307 assertions still passing
  - Zero regressions introduced

**V1.4: Standards Compliance**
- Verify RFC 7232 (ETag): All responses include ETag headers
- Verify RFC 9457 (Problem Details): Error responses have correct structure
- Verify RFC 9110 (HTTP): Status codes and methods correct
- Verify RFC 8259 (JSON): Deterministic output verified

**V1.5: Edge Case Testing**
- Empty graph scenarios
- Single receiver scenarios
- Large topology scenarios (100+ receivers)
- Concurrent access scenarios
- Conflict resolution workflows

**V1.6: Performance Testing**
- Verify p99 latency < 10ms for all operations
- Measure receiver lookup performance (should be O(1))
- Measure topology validation performance (should be O(n))

### Second Verification Pass (If Issues Found)

**V2.1-V2.6:** Same as V1.1-V1.6, applied to remediated code

### Final Verification

**V3: Sign-off Verification**
- All test passes verified
- All regressions resolved
- All performance targets met
- Independent approval documented

---

## Acceptance Criteria (Phase 2B Completion)

### Functional Requirements
- [ ] ReceiverGraphCoordinator fully implemented and working
- [ ] ReceiverGraphHttpServer all 10 endpoints implemented
- [ ] ReceiverGraphCli all 6 commands implemented
- [ ] All endpoints route correctly to handlers
- [ ] All CLI commands parse and execute correctly
- [ ] Receiver topology registration/deregistration working
- [ ] Conflict detection algorithm implemented
- [ ] Topology validation implemented

### Quality Requirements
- [ ] 100+ test cases (minimum)
- [ ] 300+ assertions (minimum)
- [ ] 100% test pass rate (zero failures)
- [ ] Zero compiler errors
- [ ] Zero compiler warnings (-Wall -Wextra -Werror)
- [ ] All Phase 2A tests still passing (112/112, 307 assertions)
- [ ] Zero regressions in Phase 2A code
- [ ] ReceiverGraphCoordinator thread-safe

### Standards Requirements
- [ ] RFC 7232 (ETag) compliance in all responses
- [ ] RFC 9457 (Problem Details) error format
- [ ] RFC 9110 (HTTP Semantics) correct
- [ ] RFC 8259 (JSON) deterministic output

### Performance Requirements
- [ ] Receiver lookup: O(1) average, < 1ms
- [ ] Frequency query: O(log n) average, < 5ms
- [ ] Topology validation: O(n), < 10ms
- [ ] All state queries: p99 latency < 10ms

### Deliverable Requirements
- [ ] ReceiverGraphCoordinator.cpp (~400-500 lines)
- [ ] ReceiverGraphCoordinator.hpp (~150-200 lines)
- [ ] ReceiverGraphHttpServer.cpp (~600-700 lines)
- [ ] ReceiverGraphHttpServer.hpp (~150-200 lines)
- [ ] ReceiverGraphCli.cpp (~350-450 lines)
- [ ] ReceiverGraphCli.hpp (~100-150 lines)
- [ ] test_receiver_graph_integration.cpp (~700-900 lines)
- [ ] CMakeLists.phase2b.txt (NEW)
- [ ] Updated libdsp/CMakeLists.txt

### Documentation Requirements
- [ ] IMPLEMENTATION_REPORT_Phase2B.md created
- [ ] VERIFICATION_REPORT_Phase2B.md created
- [ ] PHASE2B_COMPLETION_SUMMARY.md created
- [ ] Inline code comments for complex logic
- [ ] API documentation in headers

### Verification Requirements
- [ ] Independent verification completed
- [ ] Verifier approval obtained
- [ ] All critical issues (if any) fixed
- [ ] Zero regressions confirmed
- [ ] All edge cases tested
- [ ] Performance targets verified

---

## Multi-Agent Workflow (Phase 2B Execution)

```
┌─────────────────────────────────────────────────────────────────────┐
│                   PHASE 2B EXECUTION WORKFLOW                       │
└─────────────────────────────────────────────────────────────────────┘

START
  │
  ├─→ [ORCHESTRATOR PREP] (1-2 hours)
  │   ├─ Repository audit
  │   ├─ Pre-requisite verification (90/90 + 112/112 must still pass)
  │   ├─ Baseline snapshot
  │   └─ Deliver Phase 2B prompt to Implementor
  │
  ├─→ [IMPLEMENTOR TASK 1] (4-6 hours)
  │   ├─ Implement ReceiverGraphCoordinator
  │   ├─ Compile, verify no warnings
  │   └─ Deliver to Verifier
  │
  ├─→ [VERIFIER V1.1-V1.2] (30 mins)
  │   ├─ Build verification
  │   ├─ Initial test execution
  │   └─ Report status to Implementor
  │
  ├─→ [IMPLEMENTOR TASK 2-3] (9-12 hours)
  │   ├─ Implement ReceiverGraphHttpServer
  │   ├─ Implement ReceiverGraphCli
  │   ├─ Compile, verify no warnings
  │   └─ Deliver to Verifier
  │
  ├─→ [VERIFIER V1.3-V1.6] (2-3 hours)
  │   ├─ Full verification suite
  │   ├─ Regression testing
  │   ├─ Standards compliance
  │   ├─ Performance validation
  │   └─ Report findings to Implementor
  │
  ├─→ [IMPLEMENTOR TASK 4-5] (7-10 hours)
  │   ├─ Implement comprehensive integration tests
  │   ├─ CMake integration
  │   ├─ Fix any issues found by Verifier
  │   └─ Deliver to Verifier
  │
  ├─→ [VERIFIER V2] (IF ISSUES FOUND) (1-2 hours)
  │   ├─ Re-verify all fixes
  │   ├─ Confirm no regressions
  │   └─ Re-verify standards compliance
  │
  ├─→ [VERIFIER V3 - FINAL] (1 hour)
  │   ├─ Sign-off verification
  │   ├─ Create verification report
  │   └─ Obtain final approval
  │
  ├─→ [ORCHESTRATOR FINALIZATION] (1-2 hours)
  │   ├─ Review all artifacts
  │   ├─ Verify acceptance criteria
  │   ├─ Create orchestrator sign-off
  │   └─ Authorize Phase 2C or Production
  │
  └─→ COMPLETE ✅

Total Duration: 160-200 engineering hours (~2 weeks)
```

---

## Critical Constraints & Rules

### Code Quality Rules
1. **C++26 Standard:** -std=c++2c required, no exceptions
2. **Compiler Flags:** -Wall -Wextra -Werror mandatory
3. **No Warnings:** Zero compiler warnings, period. Use `(void)unused_param;` if needed
4. **No Exceptions:** C++ exceptions are not allowed (use return codes)
5. **Thread Safety:** Use std::mutex for all shared state access
6. **Memory Safety:** Use smart pointers (std::unique_ptr, std::shared_ptr)

### Testing Rules
1. **Catch2 Macros:** No bare `||` in REQUIRE/CHECK macros; extract to boolean variable
2. **Test Coverage:** All functions must have at least one test case
3. **Edge Cases:** Test empty inputs, null pointers, boundary conditions
4. **Performance:** All tests must complete in < 5 seconds total
5. **Determinism:** JSON output must be byte-identical across 10 iterations

### Standards Rules
1. **RFC 7232:** All GET responses must include ETag header
2. **RFC 9457:** All error responses must include Problem Details JSON
3. **RFC 9110:** Status codes and methods must be semantically correct
4. **RFC 8259:** JSON must use sorted keys for determinism

### Integration Rules
1. **Phase 2A Lock:** Do not modify Phase 2A code
2. **Backward Compatibility:** All Phase 2A tests must continue passing
3. **Composition:** Use ReceiverGraphCoordinator to use ConfigurationStateMachine (don't inherit)
4. **No Global State:** All state must be in objects, not globals

---

## Success Criteria Summary

| Category | Target | Critical |
|----------|--------|----------|
| **Test Pass Rate** | 100% | YES |
| **Compiler Warnings** | 0 | YES |
| **Phase 2A Regression** | 0 failures | YES |
| **HTTP Endpoints** | 10 implemented | YES |
| **CLI Commands** | 6 implemented | YES |
| **Standards (RFC)** | 4/4 compliant | YES |
| **Code Lines** | 2,000-2,500 | NO |
| **Test Cases** | 100+ | YES |
| **Performance** | < 10ms p99 | NO |

---

## Orchestrator Authority & Responsibilities

**As Orchestrator, I will:**

1. **Ensure Coordination:** Monitor implementor/verifier communication
2. **Remove Blockers:** Provide immediate decisions on architectural questions
3. **Verify Acceptance:** Confirm all acceptance criteria met before Phase 2C
4. **Authorize Phase 2C:** Only after Phase 2B fully approved
5. **Track Timeline:** Ensure 2-week target maintained

**Implementor Responsibilities:**
- Implement all 5 tasks completely
- Achieve 100% test pass rate
- Compile with zero warnings
- Deliver production-quality code
- Respond to verifier feedback within 4 hours

**Verifier Responsibilities:**
- Execute comprehensive verification
- Identify critical issues before production
- Verify standards compliance
- Test edge cases and performance
- Provide independent approval

---

## Launch Checklist (Ready to Begin)

- [x] Phase 2A successfully approved and signed off
- [x] Phase 2A baseline stable (112/112 tests + 90/90 pre-requisites)
- [x] Phase 2B architecture clearly defined
- [x] Implementation scope detailed
- [x] Acceptance criteria comprehensive
- [x] Multi-agent workflow documented
- [x] Standards requirements clear
- [x] Constraints and rules established
- [x] Orchestrator authority delegated
- [x] **Ready to Launch Phase 2B Execution**

---

## Orchestrator Instructions to Implementor & Verifier

> **DIRECTIVE:** Execute Phase 2B implementation using the multi-agent workflow defined above. Implementor: begin with Task 1 (ReceiverGraphCoordinator) immediately. Verifier: prepare verification environment. Target completion: 2 weeks. Mandate: 100% test pass rate, zero warnings, all standards compliant. Report status daily. Escalate blockers immediately.

---

**Phase 2B Orchestrator Prompt: READY FOR EXECUTION** ✅  
**Effective Date:** 2026-07-25  
**Target Completion:** 2026-08-08  
**Orchestrator Authority:** Complete
