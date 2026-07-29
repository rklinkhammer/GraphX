# Phase 2B Multi-Agent Implementation Plan

## Overview

Phase 2B (Generic Graph Management Layer) will be implemented using a three-agent orchestration model:

1. **Orchestrator** → Breaks down work, manages dependencies, coordinates timelines
2. **Implementor** → Writes code for assigned track (Coordinator, HttpServer, or Cli)
3. **Verifier** → Validates implementation against specification

This document explains the workflow and coordination between agents.

---

## Document Map

| Document | Purpose | Audience |
|----------|---------|----------|
| **Phase2B_Generic_Graph_Management.md** | Source of truth specification | All agents |
| **Phase2B_ORCHESTRATOR_PROMPT.md** | Coordination & planning | Orchestrator agent |
| **Phase2B_IMPLEMENTOR_PROMPT.md** | Implementation details | Implementor agents (3x) |
| **Phase2B_VERIFIER_PROMPT.md** | Validation & testing | Verifier agent |
| **Phase2B_Multi_Agent_Implementation_Plan.md** | This document - workflow overview | All agents |

---

## Three-Track Parallel Implementation

### Track A: GraphCoordinator (Foundation)
```
ORCHESTRATOR: Assigns to Implementor-A
    ↓
IMPLEMENTOR-A: 
    • Reads Phase2B_IMPLEMENTOR_PROMPT.md (Track A section)
    • Reads Phase2B_Generic_Graph_Management.md (Component 1)
    • Implements GraphCoordinator (~150 LOC, ~12 tests)
    ↓
VERIFIER:
    • Checkpoint 1: GraphCoordinator Code Review
    • Runs unit tests
    • Validates thread-safety
    • Reports acceptance or specific gaps
    ↓
ORCHESTRATOR: Approves or requests fixes (repeat until complete)
```

**Completion Criteria for Track A:**
- ✅ 12+ tests passing
- ✅ Zero compiler warnings
- ✅ Thread-safety verified
- ✅ Code review approved

**Blocks:** Tracks B and C cannot start until Track A is verified complete

---

### Track B: GraphHttpServer (Depends on Track A)
```
ORCHESTRATOR: Waits for Track A completion, assigns to Implementor-B
    ↓
IMPLEMENTOR-B:
    • Reads Phase2B_IMPLEMENTOR_PROMPT.md (Track B section)
    • Reads Phase2B_Generic_Graph_Management.md (Component 2)
    • Imports and uses GraphCoordinator (from Track A)
    • Implements GraphHttpServer (~400+ LOC, ~15 tests, HTML UI)
    ↓
VERIFIER:
    • Checkpoint 2: GraphHttpServer Code Review
    • Runs integration tests
    • Tests REST endpoints
    • Tests web UI
    • Reports acceptance or specific gaps
    ↓
ORCHESTRATOR: Approves or requests fixes (repeat until complete)
```

**Completion Criteria for Track B:**
- ✅ 15+ tests passing
- ✅ Web UI interactive
- ✅ REST endpoints functional
- ✅ Zero compiler warnings
- ✅ Code review approved

---

### Track C: GraphCli (Depends on Track A)
```
ORCHESTRATOR: Waits for Track A completion, assigns to Implementor-C
    ↓
IMPLEMENTOR-C:
    • Reads Phase2B_IMPLEMENTOR_PROMPT.md (Track C section)
    • Reads Phase2B_Generic_Graph_Management.md (Component 3)
    • Imports and uses GraphCoordinator (from Track A)
    • Implements GraphCli (~250 LOC, ~10 tests, CLI binary)
    ↓
VERIFIER:
    • Checkpoint 3: GraphCli Code Review
    • Runs unit tests
    • Tests CLI commands
    • Tests file I/O
    • Reports acceptance or specific gaps
    ↓
ORCHESTRATOR: Approves or requests fixes (repeat until complete)
```

**Completion Criteria for Track C:**
- ✅ 10+ tests passing
- ✅ CLI commands working
- ✅ File I/O functional
- ✅ Zero compiler warnings
- ✅ Code review approved

---

## Execution Timeline

```
PHASE 2B IMPLEMENTATION TIMELINE
==================================

Week of [Date]

Mon-Tue: Track A (GraphCoordinator)
├─ ORCHESTRATOR: Brief implementor on spec, assign Track A
├─ IMPLEMENTOR-A: Code + tests (~4 hours focused work)
└─ VERIFIER: Review code, run tests (30 min checkpoint)

Wed (post Track A approval): Tracks B & C in parallel
├─ ORCHESTRATOR: Assign Track B and C to different implementors
├─ IMPLEMENTOR-B: Code HttpServer + UI (~4-5 hours)
├─ IMPLEMENTOR-C: Code CLI + CLI binary (~3-4 hours)
└─ VERIFIER: (Staggered reviews as components complete)

Thu-Fri: Integration & Finalization
├─ ORCHESTRATOR: Monitor integration testing
├─ All: Full end-to-end testing
├─ VERIFIER: Final acceptance criteria verification
└─ SIGN-OFF: Phase 2B complete

Total Estimated Time: 12-18 hours of focused implementation
```

---

## Agent Responsibilities

### Orchestrator Role

**Inputs:**
- Specification: Phase2B_Generic_Graph_Management.md
- Implementation prompts for each track
- Dependency map (Track A → B,C)

**Tasks:**
1. Brief all implementors on specification
2. Assign tracks based on available resources
3. Manage timeline and dependencies
4. Monitor verifier checkpoint reports
5. Request fixes or approve for next stage
6. Coordinate final integration testing
7. Sign off on completion

**Outputs:**
- Track assignments
- Timeline
- Final status report

**Communication Pattern:**
- Initial briefing: All three agents + specification
- Daily: Check status of assigned tracks
- On completion: Route to verifier
- On verification: Approve or request fixes

---

### Implementor Role

**Inputs (specific to track):**
- Phase2B_IMPLEMENTOR_PROMPT.md (Track section)
- Phase2B_Generic_Graph_Management.md (Component section)
- Working codebase (GraphX at Phase 2A baseline)

**Tasks:**
1. Read and understand specification
2. Understand dependencies (if Track B/C, review Track A code)
3. Implement assigned component:
   - Code structure and methods
   - Unit tests (12-15 tests)
   - Integration with dependencies
4. Ensure zero compiler warnings
5. Commit code with clear message
6. Report completion to orchestrator

**Outputs:**
- Implementation files (.hpp + .cpp)
- Test files (test_*.cpp)
- Resources (HTML UI if Track B)
- Build succeeds with zero warnings
- All tests pass

**Communication Pattern:**
- Start: Receive track assignment from orchestrator
- During: Ask clarifying questions to orchestrator
- Completion: Submit code for verification
- Feedback: Address specific gaps from verifier

---

### Verifier Role

**Inputs (specific to checkpoint):**
- Phase2B_VERIFIER_PROMPT.md (Checkpoint section)
- Phase2B_Generic_Graph_Management.md (Specification)
- Implemented code + tests

**Tasks:**
1. Code review against checklist
2. Compile and check for warnings
3. Run unit/integration tests
4. Validate against specification
5. Test workflows (if final)
6. Report acceptance or specific gaps

**Outputs:**
- Checkpoint report (PASS/FAIL)
- Specific issues if failing (file, line, gap description)
- Sign-off if passing

**Communication Pattern:**
- Receive: Code submitted for verification
- Review: Check against checklist
- Report: Accept or reject with specifics
- If reject: Implementor addresses gaps, resubmit

---

## Dependency Graph

```
START
  ↓
[ORCHESTRATOR: Assign Track A]
  ↓
[IMPLEMENTOR-A: Code GraphCoordinator]
  ↓
[VERIFIER: Checkpoint 1 - GraphCoordinator]
  ↓
  ├─ FAIL → [IMPLEMENTOR-A: Fix issues]
  │           ↑_________________↓
  │                             [VERIFIER: Re-verify]
  │
  └─ PASS
      ↓
[ORCHESTRATOR: Assign Tracks B & C] ─ (parallel)
      ↙                             ↘
[IMPL-B: Code HttpServer]    [IMPL-C: Code Cli]
      ↓                             ↓
[VERIF: Checkpoint 2]        [VERIF: Checkpoint 3]
      ↓                             ↓
      ├─FAIL→[Fix]          ├─FAIL→[Fix]
      │      ↓               │      ↓
      └─OK   │              └─OK   │
          (re-verify)      (re-verify)
      ↓                     ↓
[ORCHESTRATOR: Approve B & C]
      ↓
[VERIFIER: Integration Testing]
      ├─ CLI + Web UI workflow
      ├─ REST API full cycle
      └─ Regression tests
      ↓
[ORCHESTRATOR: SIGN-OFF - Phase 2B COMPLETE]
  ↓
END
```

---

## When Implementors Get Stuck

**If Implementor needs clarification on specification:**
- First: Re-read the specific component section in Phase2B_Generic_Graph_Management.md
- Second: Check the relevant section of IMPLEMENTOR_PROMPT.md
- Third: Ask ORCHESTRATOR for clarification
- Note: Verifier will validate against spec, not implementor's interpretation

**If Implementor needs code reference:**
- GraphExecutor: `libgraph/include/graph/GraphExecutor.hpp` (for execution control methods)
- nlohmann::json: Already in dependencies
- HTTP library: Choose cpp-httplib (already documented in IMPLEMENTOR_PROMPT)

**If Test Case Design is unclear:**
- Review test examples in IMPLEMENTOR_PROMPT.md
- Look at Phase 2A test patterns (libgraph/test/unit/test_graph.cpp)
- Ask ORCHESTRATOR

---

## When Verifier Finds Issues

**Verifier Report Format (from Checkpoint):**
```
CHECKPOINT 2: GraphHttpServer Code Review
STATUS: FAILED

ISSUES IDENTIFIED:

1. CRITICAL - Missing execution control endpoints
   File: libgraph/src/graph/GraphHttpServer.cpp:150
   Gap: POST /api/v1/execution/init not implemented
   Specification: Phase2B_Generic_Graph_Management.md, line 198
   Fix: Add endpoint handler for init()

2. MAJOR - Web UI doesn't show execution state
   File: libgraph/resources/web/index.html:45
   Gap: State indicator missing from HTML
   Specification: Phase2B_Generic_Graph_Management.md, Component 2
   Fix: Add <div id="state-display"></div> and JavaScript refresh

3. MINOR - Compiler warning in error handling
   File: libgraph/src/graph/GraphHttpServer.cpp:200
   Gap: Unused variable 'result'
   Specification: N/A (code quality)
   Fix: Remove unused variable or assign to error_code
```

**Implementor receives this report and:**
1. Fixes each identified issue
2. Re-runs tests
3. Re-submits to verifier
4. Verifier re-checks only the fixed items

---

## Sign-Off Criteria

**Phase 2B is COMPLETE when:**

```
✅ TRACK A (GraphCoordinator)
   - 12+ tests passing
   - Zero compiler warnings
   - Thread-safety verified
   - Code review approved

✅ TRACK B (GraphHttpServer)
   - 15+ tests passing
   - Web UI interactive
   - REST endpoints working
   - Zero compiler warnings
   - Code review approved

✅ TRACK C (GraphCli)
   - 10+ tests passing
   - CLI commands working
   - File I/O functional
   - Zero compiler warnings
   - Code review approved

✅ INTEGRATION TESTING
   - CLI + Web UI workflow: PASS
   - REST API full cycle: PASS
   - Phase 2A regression tests: PASS (102-112 tests)

✅ FINAL ACCEPTANCE
   - All 37+ new tests passing
   - All 102-112 Phase 2A tests passing
   - Total: 140-150+ tests passing
   - No compiler warnings
   - Code review approved
   - Documentation complete

ORCHESTRATOR SIGN-OFF: Phase 2B Complete ✅
```

---

## Git Workflow

```bash
# At start
git checkout -b phase-2b-implementation
git log --oneline | head -1
# Should show: 12fb9690 (baseline commit)

# Implementor-A commits
git add libgraph/include/graph/GraphCoordinator.*
git add libgraph/src/graph/GraphCoordinator.cpp
git add libgraph/test/unit/test_graph_coordinator.cpp
git commit -m "Phase 2B: Implement GraphCoordinator (Track A)"

# Implementor-B commits (after Track A merged)
git add libgraph/include/graph/GraphHttpServer.*
git add libgraph/src/graph/GraphHttpServer.cpp
git add libgraph/resources/web/index.html
git add libgraph/test/unit/test_graph_http_server.cpp
git commit -m "Phase 2B: Implement GraphHttpServer (Track B)"

# Implementor-C commits (after Track A merged)
git add libgraph/include/graph/GraphCli.*
git add libgraph/src/graph/GraphCli.cpp
git add tools/graph-cli.cpp
git add libgraph/test/unit/test_graph_cli.cpp
git commit -m "Phase 2B: Implement GraphCli (Track C)"

# Final PR to main
git push origin phase-2b-implementation
# Create pull request
# Verifier approves
# Orchestrator merges to main
```

---

## Communication Protocol

### Daily Standup (Orchestrator → All)
```
Subject: Phase 2B Implementation Status

TRACK A (GraphCoordinator):
  - Status: [IN PROGRESS | PENDING REVIEW | APPROVED]
  - Implementor: [Name]
  - ETA: [Time]
  - Blockers: [None / specific issues]

TRACK B (GraphHttpServer):
  - Status: [NOT STARTED | IN PROGRESS | PENDING REVIEW | APPROVED]
  - Implementor: [Name]
  - ETA: [Time]
  - Blockers: [None / specific issues]

TRACK C (GraphCli):
  - Status: [NOT STARTED | IN PROGRESS | PENDING REVIEW | APPROVED]
  - Implementor: [Name]
  - ETA: [Time]
  - Blockers: [None / specific issues]

NEXT STEPS: [Integration timeline / blockers to resolve]
```

### Verifier Checkpoint Report → Orchestrator
```
Subject: CHECKPOINT [N] - GraphCoordinator Code Review

RESULT: [PASS / FAIL]

If PASS:
  ✅ 12+ tests passing
  ✅ Zero compiler warnings
  ✅ Thread-safety verified
  APPROVED FOR NEXT STAGE

If FAIL:
  ❌ [Specific issues in numbered list]
  REJECTED - IMPLEMENTOR TO FIX
  Re-verification ETA: [Date/Time]
```

---

## Key Files Reference

```
/Users/rklinkhammer/workspace/GraphX/
├─ plan/
│  ├─ Phase2B_Generic_Graph_Management.md       (Specification - Read first!)
│  ├─ Phase2B_ORCHESTRATOR_PROMPT.md            (Orchestrator guide)
│  ├─ Phase2B_IMPLEMENTOR_PROMPT.md             (Implementation guide - 3 tracks)
│  ├─ Phase2B_VERIFIER_PROMPT.md                (Verification guide - 3 checkpoints)
│  └─ Phase2B_Multi_Agent_Implementation_Plan.md (This file - workflow)
│
├─ libgraph/
│  ├─ include/graph/
│  │  ├─ GraphCoordinator.hpp      ← Track A output
│  │  ├─ GraphHttpServer.hpp       ← Track B output
│  │  └─ GraphCli.hpp              ← Track C output
│  │
│  ├─ src/graph/
│  │  ├─ GraphCoordinator.cpp      ← Track A output
│  │  ├─ GraphHttpServer.cpp       ← Track B output
│  │  └─ GraphCli.cpp              ← Track C output
│  │
│  ├─ resources/web/
│  │  └─ index.html                ← Track B output
│  │
│  └─ test/unit/
│     ├─ test_graph_coordinator.cpp ← Track A output
│     ├─ test_graph_http_server.cpp ← Track B output
│     └─ test_graph_cli.cpp         ← Track C output
│
└─ tools/
   └─ graph-cli.cpp                ← Track C output (main entry point)
```

---

## Success Indicators

**You'll know Phase 2B is on track when:**

✅ Track A (Coordinator) tests pass first  
✅ Track B and C implementors can import GraphCoordinator  
✅ All 37+ new tests pass without warnings  
✅ Web UI loads and shows nodes  
✅ CLI commands execute and show output  
✅ REST API responds to requests  
✅ Execution control works (init/start/stop/join)  
✅ Phase 2A regression tests still pass  

**You'll know Phase 2B is complete when:**

✅ All checkpoints PASS  
✅ Orchestrator provides SIGN-OFF  
✅ Code merged to main branch  
✅ 140-150+ total tests passing  
✅ Zero compiler warnings  

---

## Contingency Plan

**If Track A is delayed:**
- Blocks Tracks B and C (cannot proceed in parallel)
- Estimator must extend timeline by Track A slip amount

**If Track B or C stalls on 1-2 tests:**
- Can still verify other functionality
- Fix remaining tests in revision cycle

**If verifier finds architecture issue:**
- Stop all tracks
- Orchestrator consults with implementor
- Decide: Fix implementation or update specification?
- Resume only after resolution

**If regression tests fail:**
- Critical issue - Phase 2B may not be compatible with Phase 2A
- Orchestrator must investigate
- May require specification revision

---

## Next Steps

1. **Orchestrator**: Read this plan + Phase2B_Generic_Graph_Management.md
2. **Implementor-A**: Read IMPLEMENTOR_PROMPT.md (Track A section) + Specification
3. **Verifier**: Read VERIFIER_PROMPT.md + Specification
4. **Begin**: Track A implementation
5. **Complete**: Checkpoints 1 → 2 → 3 → Integration → Sign-Off

Let's build Phase 2B! 🚀
