# Phase 2B Orchestrator Prompt

> Archived historical dashboard-planning record. Not current authority.

## Mission
Coordinate implementation of generic graph management layer (GraphCoordinator, GraphHttpServer, GraphCli) with GraphExecutor execution control integration. Break down work into parallel implementation tracks, manage dependencies, ensure components integrate correctly.

## Current Status
- **Specification**: COMPLETE (Phase2B_Generic_Graph_Management.md)
- **Baseline Commit**: 12fb9690f84ae16d148d941e8e317b51caa97aff
- **All Phase 2B incorrect files**: REMOVED
- **Phase 2A tests**: Passing (102-112 tests, 307 assertions)
- **Ready for**: Parallel implementation of 3 components

## Implementation Roadmap

### Track A: GraphCoordinator (Foundation - Must complete first)
**Owner**: Implementor-Coordinator  
**Deliverable**: libgraph/include/graph/GraphCoordinator.{hpp,cpp}  
**LOC**: ~120-150  
**Tests**: 12+ unit tests in test_graph_coordinator.cpp  
**Dependencies**: nlohmann::json, std::mutex  
**Deadline**: ASAP (blocks HttpServer and Cli)

**Specification Reference**: Phase2B_Generic_Graph_Management.md, Component 1 section

**Key Requirements**:
- Takes reference to nlohmann::json graph object
- Read-only methods: GetNode(), GetNodesByType(), GetNodeIds(), GetNodeCount(), HasNode(), GetGraphJson()
- Write method: UpdateNodeConfig(id, node_config)
- Thread-safe with std::mutex and std::lock_guard
- Returns copies (never references) to prevent post-unlock mutations
- Cannot add/remove nodes (only parameter editing allowed)
- Zero validation of node_config contents
- Compile with -Wall -Wextra -Werror -std=c++26

**Success Criteria**:
- ✅ All 12+ tests pass
- ✅ Thread-safety verified (mutex protected)
- ✅ Zero compiler warnings
- ✅ Handles empty graphs gracefully
- ✅ All node queries work (GetNode, GetNodesByType, GetNodeIds)
- ✅ UpdateNodeConfig modifies in-memory graph
- ✅ Copy return prevents post-unlock mutations

---

### Track B: GraphHttpServer (Depends on Track A)
**Owner**: Implementor-HttpServer  
**Deliverable**: libgraph/include/graph/GraphHttpServer.{hpp,cpp} + libgraph/resources/web/index.html  
**LOC**: ~200-250 (C++) + ~150-200 (HTML/CSS/JS)  
**Tests**: 15+ integration tests in test_graph_http_server.cpp  
**Dependencies**: GraphCoordinator, GraphExecutor, nlohmann::json, HTTP library (cpp-httplib or similar)  
**Prerequisite**: Track A complete  
**Deadline**: After Track A (can start immediately)

**Specification Reference**: Phase2B_Generic_Graph_Management.md, Component 2 section

**Key Requirements**:
- REST API for parameter viewing/editing (GET /api/v1/graph, /nodes, /nodes/{id}, PATCH /nodes/{id})
- REST API for execution control (POST /api/v1/execution/{init,start,run,stop,join}, GET /api/v1/execution/state)
- Optional advanced endpoints (pause, resume, step)
- Web UI: node table, parameter editor, execution controls, state display
- Proper HTTP status codes (200, 204, 400, 404, 409, 501)
- JSON response format with success/error fields
- CORS headers for browser access
- Port configurable (default 8080)

**Success Criteria**:
- ✅ All REST endpoints respond correctly
- ✅ PATCH updates reflect in GetNode() response
- ✅ Execution control endpoints trigger state changes
- ✅ State queries return current state accurately
- ✅ Web UI loads and renders node table
- ✅ Parameter editing form works and persists in-memory
- ✅ Start/Stop/Join buttons update state display
- ✅ 15+ integration tests pass
- ✅ Zero compiler warnings

---

### Track C: GraphCli (Depends on Track A)
**Owner**: Implementor-Cli  
**Deliverable**: libgraph/include/graph/GraphCli.{hpp,cpp} + CLI binary  
**LOC**: ~100-120 (Library) + ~50-100 (CLI main)  
**Tests**: 10+ unit tests in test_graph_cli.cpp  
**Dependencies**: GraphCoordinator, GraphExecutor, nlohmann::json, file I/O, CLI parsing  
**Prerequisite**: Track A complete  
**Deadline**: After Track A (can start immediately)

**Specification Reference**: Phase2B_Generic_Graph_Management.md, Component 3 section

**Key Requirements**:
- Commands: load, save, show, get-node, list-nodes, update-node
- Execution commands: init, start, run, stop, join, state, pause, resume, step
- Stateful CLI session (load graph → edit nodes → save)
- Output formats: JSON and table (--format flag)
- Proper error messages and handling
- Help text for all commands
- Load reads from disk into memory
- Save persists in-memory graph to disk
- Execute init/start/run creates and uses GraphExecutor

**Success Criteria**:
- ✅ load command reads file and populates graph
- ✅ list-nodes shows all nodes in table/JSON
- ✅ get-node retrieves specific node
- ✅ update-node modifies in-memory graph (not persisted)
- ✅ save command writes graph to disk
- ✅ Execution commands (init, start, run) work correctly
- ✅ show command displays graph in multiple formats
- ✅ 10+ tests pass
- ✅ Zero compiler warnings

---

## Integration Sequence

```
Phase 2B Implementation Timeline
=================================

1. GraphCoordinator Implementation (Track A)
   ├─ Code implementation (~2-3 hours)
   ├─ Unit tests (~2-3 hours)
   └─ Verification
        └─ All 12+ tests pass ✓
        └─ Thread-safe verified ✓
        └─ Zero warnings ✓

2. GraphHttpServer + GraphCli (Tracks B & C in parallel)
   ├─ Track B: HttpServer (~3-4 hours)
   │  ├─ Code implementation
   │  ├─ HTML UI (~1-2 hours)
   │  ├─ Integration tests (~2-3 hours)
   │  └─ Verification
   │       └─ All 15+ tests pass ✓
   │       └─ Endpoints respond correctly ✓
   │       └─ Web UI interactive ✓
   │
   └─ Track C: CLI (~2-3 hours)
      ├─ Code implementation
      ├─ CLI parsing (~1 hour)
      ├─ Unit tests (~1-2 hours)
      └─ Verification
           └─ All 10+ tests pass ✓
           └─ Commands work correctly ✓

3. Full Integration Testing
   ├─ Load graph via CLI
   ├─ Edit parameters via HTTP API
   ├─ Execute via CLI commands
   ├─ Monitor state via Web UI
   └─ Verify execution control workflow

4. Regression Testing
   └─ Verify Phase 2A tests still pass (102-112 tests)

Total Estimated Time: 12-18 hours of focused implementation
```

---

## Component Dependencies

```
GraphCoordinator (Foundation)
├─ nlohmann::json (existing)
└─ std::mutex (stdlib)

GraphHttpServer (Depends on Coordinator)
├─ GraphCoordinator
├─ GraphExecutor (existing)
├─ HTTP library (cpp-httplib)
└─ nlohmann::json

GraphCli (Depends on Coordinator)
├─ GraphCoordinator
├─ GraphExecutor (existing)
├─ File I/O (stdlib)
└─ CLI parsing (boost::program_options or similar)
```

---

## Testing Strategy

### Unit Tests (per component)
- **GraphCoordinator**: GetNode, GetNodesByType, GetNodeIds, UpdateNodeConfig, thread-safety
- **GraphHttpServer**: REST endpoints, HTTP status codes, JSON format validation, state transitions
- **GraphCli**: Command parsing, file I/O, error handling, output formatting

### Integration Tests
- CLI workflow: load → edit → save → execute
- HTTP workflow: GET nodes → PATCH parameters → POST execution control
- State consistency: GraphCoordinator state matches HTTP responses
- Execution control: init → start → run → stop → join

### Regression Tests
- All Phase 2A tests remain passing (102-112 tests)
- No warnings with -Wall -Wextra -Werror -std=c++26

---

## Build & Test Commands

```bash
# Build
cd /Users/rklinkhammer/workspace/GraphX
mkdir -p build-phase2b
cd build-phase2b
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_PHASE2B=ON ..
make -j4

# Run Phase 2B tests
ctest --verbose --filter "GraphCoordinator|GraphHttpServer|GraphCli"

# Run all tests (regression check)
ctest --verbose

# Check warnings
make VERBOSE=1 2>&1 | grep -i warning
```

---

## Verifier Checkpoints

**After Track A (GraphCoordinator):**
- ✅ 12+ tests passing
- ✅ Thread-safety validation
- ✅ No compiler warnings
- ✅ Code review for const-correctness

**After Tracks B & C (HttpServer, Cli):**
- ✅ 15+ HttpServer tests passing
- ✅ 10+ Cli tests passing
- ✅ Integration tests (load → edit → execute)
- ✅ No compiler warnings
- ✅ Code review for error handling

**Final Verification:**
- ✅ All 37+ tests passing
- ✅ Phase 2A regression tests still passing
- ✅ End-to-end workflows (Web UI, CLI, REST API)
- ✅ Execution control fully integrated
- ✅ Zero warnings with -Wall -Wextra -Werror

---

## Handoff Notes

- **To Implementor-Coordinator**: Start with GraphCoordinator - it's the foundation for both HttpServer and Cli
- **To Implementor-HttpServer**: Can start after Coordinator is tested; use mock GraphExecutor if needed for parallel work
- **To Implementor-Cli**: Can start after Coordinator is tested; integration with GraphExecutor comes later
- **To Verifier**: Use Phase2B_Generic_Graph_Management.md as source of truth for all acceptance criteria

## Success Definition

**Phase 2B is COMPLETE when:**
1. ✅ All 37+ tests pass (12 Coordinator + 15 HttpServer + 10 Cli)
2. ✅ Phase 2A regression tests pass (102-112 tests)
3. ✅ Zero compiler warnings (-Wall -Wextra -Werror -std=c++26)
4. ✅ Full end-to-end workflows validated:
   - Load graph from file
   - Edit parameters via Web UI, CLI, or REST API
   - Initialize and execute graph
   - Query execution state
   - Stop and join execution
5. ✅ Code review approved
6. ✅ Documentation complete
