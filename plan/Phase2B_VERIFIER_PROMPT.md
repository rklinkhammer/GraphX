# Phase 2B Verifier Prompt

## Your Mission
Validate that Phase 2B implementation (GraphCoordinator, GraphHttpServer, GraphCli) meets specification exactly. Verify code quality, test coverage, thread-safety, compilation standards, and end-to-end workflows. Provide detailed acceptance or rejection with specific gaps.

---

## Verification Checkpoints

### Checkpoint 1: GraphCoordinator Code Review

**Files to Examine:**
- `libgraph/include/graph/GraphCoordinator.hpp`
- `libgraph/src/graph/GraphCoordinator.cpp`
- `libgraph/test/unit/test_graph_coordinator.cpp`

**Checklist: API Completeness**

```
☐ Constructor: explicit GraphCoordinator(nlohmann::json& graph)
  └─ Takes non-owning reference to graph
  └─ Initializes mutex
  
☐ GetGraphJson() const
  └─ Thread-safe (acquires lock)
  └─ Returns COPY (not reference)
  └─ Handles empty graph gracefully
  
☐ GetNode(id) const
  └─ Thread-safe
  └─ Returns COPY of node or empty json
  └─ Searches graph_["nodes"] by "id" field
  
☐ GetNodeConfig(id) const
  └─ Thread-safe
  └─ Returns COPY of node_config field only
  └─ Returns empty json if not found
  
☐ GetNodesByType(type) const
  └─ Thread-safe
  └─ Returns vector of COPIES
  └─ Filters by node["type"]
  └─ Returns empty vector if none found
  
☐ GetNodeIds() const
  └─ Thread-safe
  └─ Returns vector<string> of all node IDs
  └─ Returns empty vector for empty graph
  
☐ GetNodeCount() const
  └─ Thread-safe
  └─ Returns size of graph_["nodes"] array
  
☐ HasNode(id) const
  └─ Thread-safe
  └─ Returns true/false
  └─ Does NOT modify graph
  
☐ UpdateNodeConfig(id, node_config)
  └─ Thread-safe
  └─ Finds node by id
  └─ Updates node["node_config"] field
  └─ Returns true if found, false otherwise
  └─ Does NOT add nodes (only updates existing)
  └─ Does NOT validate node_config contents
  
☐ Deleted copy/move constructors
  └─ GraphCoordinator(const GraphCoordinator&) = delete;
  └─ GraphCoordinator& operator=(const GraphCoordinator&) = delete;
  └─ GraphCoordinator(GraphCoordinator&&) = delete;
  └─ GraphCoordinator& operator=(GraphCoordinator&&) = delete;
```

**Checklist: Thread Safety**

```
☐ All public methods use std::lock_guard<std::mutex>
  └─ Lock guard created at method entry
  └─ Lock released at method exit (RAII)
  
☐ mutable std::mutex graph_lock_ declared
  └─ Allows const methods to lock
  
☐ Returns are copies (not references)
  └─ GetGraphJson() returns json copy
  └─ GetNode() returns json copy
  └─ GetNodesByType() returns vector of copies
  └─ No references to locked data escape method scope
  
☐ No nested/recursive locking
  └─ Single lock_guard per public method
  └─ No calls to other public methods while locked
  
☐ No external calls while holding lock
  └─ No file I/O inside locked section
  └─ No network calls inside locked section
```

**Checklist: Test Coverage (12+ tests)**

```
☐ Test: Constructor and properties
  └─ Creates coordinator successfully
  └─ GetNodeCount() reflects initial size
  
☐ Test: GetNode returns copy
  └─ Modify returned object
  └─ Verify original unmodified
  
☐ Test: GetNodesByType filtering
  └─ Multiple nodes of different types
  └─ Correct filtering
  
☐ Test: UpdateNodeConfig success
  └─ Update existing node
  └─ GetNode() reflects change
  
☐ Test: UpdateNodeConfig non-existent node
  └─ Try to update non-existent node
  └─ Returns false
  └─ No new nodes added
  
☐ Test: GetNodeIds returns all IDs
  └─ Verify completeness
  
☐ Test: HasNode for existent and non-existent
  └─ Returns correct boolean
  
☐ Test: GetNodeConfig extracts field
  └─ Returns only node_config, not whole node
  
☐ Test: Empty graph handling
  └─ Create empty graph {}
  └─ Methods handle gracefully
  └─ No crashes or exceptions
  
☐ Test: Thread safety concurrent reads
  └─ Spawn N threads calling GetNode simultaneously
  └─ All threads succeed
  └─ No data races (verified with sanitizers if available)
  
☐ Test: Thread safety read/write mix
  └─ Thread A: UpdateNodeConfig
  └─ Thread B: GetNode
  └─ No race conditions
  
☐ Test: Non-copyable assertion
  └─ static_assert or compile-time verification
  └─ Copy/move constructors deleted
```

**Checklist: Code Quality**

```
☐ Compiles with: -Wall -Wextra -Werror -std=c++26
  └─ Zero warnings
  
☐ No memory leaks
  └─ All allocations released
  └─ RAII patterns used correctly
  
☐ Const-correctness
  └─ Read-only methods marked const
  └─ Non-const methods for updates
  
☐ Documentation
  └─ Doxygen comments on all public methods
  └─ Thread-safety documented
  └─ Parameters and return values documented
  
☐ No circular references
  └─ Reference to graph_ only, no back-references
```

---

### Checkpoint 2: GraphHttpServer Code Review

**Files to Examine:**
- `libgraph/include/graph/GraphHttpServer.hpp`
- `libgraph/src/graph/GraphHttpServer.cpp`
- `libgraph/resources/web/index.html`
- `libgraph/test/unit/test_graph_http_server.cpp`

**Checklist: API Completeness**

```
☐ Constructor: GraphHttpServer(json& graph, GraphExecutor* executor, int port = 8080)
  └─ Takes graph reference
  └─ Takes executor pointer
  └─ Stores port
  └─ Constructs internal GraphCoordinator
  
☐ Start() method
  └─ Starts HTTP server
  └─ Listens on configured port
  └─ Returns true/false
  
☐ Stop() method
  └─ Cleanly stops HTTP server
  └─ Closes connections
  └─ Returns true/false
  
☐ IsRunning() method
  └─ Returns current server state
```

**Checklist: REST API Endpoints**

```
PARAMETER VIEWING
☐ GET /api/v1/graph
  └─ Returns { "success": true, "data": {...graph...} }
  └─ Status 200 OK
  └─ Full graph JSON in response
  
☐ GET /api/v1/nodes
  └─ Returns { "success": true, "data": [...nodes...] }
  └─ Status 200 OK
  └─ Array of all nodes
  
☐ GET /api/v1/nodes/{id}
  └─ Status 200 OK if found
  └─ Status 404 if not found with error response
  └─ Returns single node in data field
  
☐ GET /api/v1/nodes/type/{type}
  └─ Status 200 OK
  └─ Returns nodes matching type
  └─ Empty array if no matches

PARAMETER EDITING
☐ PATCH /api/v1/nodes/{id}
  └─ Request body: { "node_config": {...} }
  └─ Status 200 OK if successful
  └─ Status 404 if node not found
  └─ Status 400 if invalid JSON
  └─ Response: { "success": true, "data": {...updated_node...} }
  └─ In-memory update only (not saved to disk)
  
EXECUTION CONTROL
☐ GET /api/v1/execution/state
  └─ Returns { "success": true, "data": {"state": "RUNNING"} }
  └─ Always 200 OK (state always queryable)
  
☐ POST /api/v1/execution/init
  └─ Calls executor.Init()
  └─ Status 200 OK if successful
  └─ Status 409 Conflict if wrong state
  └─ Returns updated state in response
  
☐ POST /api/v1/execution/start
  └─ Calls executor.Start()
  └─ Status 200 OK if successful
  └─ Status 409 Conflict if not initialized
  └─ Returns updated state
  
☐ POST /api/v1/execution/run
  └─ Calls executor.Run() (blocking)
  └─ Status 200 OK on completion
  └─ Returns final state
  
☐ POST /api/v1/execution/stop
  └─ Calls executor.Stop()
  └─ Status 200 OK or 204 No Content
  └─ Returns immediately (doesn't wait)
  
☐ POST /api/v1/execution/join
  └─ Calls executor.Join() (blocking)
  └─ Status 200 OK on completion
  └─ Returns final state
  
☐ POST /api/v1/execution/pause (optional)
  └─ Status 501 Not Implemented if not supported
  └─ Status 200 OK if supported and successful
  
☐ POST /api/v1/execution/resume (optional)
  └─ Status 501 Not Implemented if not supported
  
☐ POST /api/v1/execution/step (optional)
  └─ Status 501 Not Implemented if not supported

WEB UI
☐ GET /
  └─ Serves index.html
  └─ Status 200 OK
  └─ Content-Type: text/html
```

**Checklist: Response Format**

```
☐ Success responses contain:
  └─ "success": true
  └─ "data": {...operation result...}
  └─ Optional "message" field
  
☐ Error responses contain:
  └─ "success": false
  └─ "error": "error_code" (specific code)
  └─ "message": "human readable explanation"
  
☐ HTTP Status Codes used correctly:
  └─ 200 OK - Success
  └─ 204 No Content - Stop (if appropriate)
  └─ 400 Bad Request - Invalid input
  └─ 404 Not Found - Resource not found
  └─ 409 Conflict - Wrong state for operation
  └─ 501 Not Implemented - Pause/resume/step not available
  
☐ CORS headers set (for browser access)
  └─ Access-Control-Allow-Origin: *
  └─ Access-Control-Allow-Methods: GET, PATCH, POST
  └─ Access-Control-Allow-Headers: Content-Type
```

**Checklist: Web UI**

```
☐ index.html loads without errors
  └─ Valid HTML structure
  
☐ Node table displays
  └─ Shows all nodes
  └─ Columns: ID, Type, Config snippet
  
☐ Search/filter functionality
  └─ Filter by node ID
  └─ Filter by node type
  
☐ Parameter editing
  └─ Edit button opens form/modal
  └─ JSON editor for parameters
  └─ Save button sends PATCH request
  └─ Cancel button closes form
  └─ Updated parameters visible in table
  
☐ Execution control panel
  └─ Status display (RUNNING/STOPPED/etc.)
  └─ Init button sends POST /api/v1/execution/init
  └─ Start button sends POST /api/v1/execution/start
  └─ Stop button sends POST /api/v1/execution/stop
  └─ Join button sends POST /api/v1/execution/join
  └─ State refreshes after each operation (polling or websocket)
  
☐ Error handling
  └─ HTTP errors displayed to user
  └─ Helpful error messages
```

**Checklist: Test Coverage (15+ tests)**

```
☐ Test: GET endpoints return correct status and format
☐ Test: PATCH updates in-memory graph
☐ Test: PATCH returns 404 for unknown node
☐ Test: PATCH returns 400 for invalid JSON
☐ Test: GET node after PATCH reflects changes
☐ Test: Execution state endpoint always succeeds
☐ Test: POST init transitions state
☐ Test: POST start requires init first
☐ Test: POST stop succeeds
☐ Test: POST join succeeds
☐ Test: State transitions visible in responses
☐ Test: Web UI HTML loads
☐ Test: CORS headers present
☐ Test: JSON response format validated
☐ Test: Type filtering works correctly
```

**Checklist: Code Quality**

```
☐ Compiles with: -Wall -Wextra -Werror -std=c++26
  └─ Zero warnings
  
☐ Uses GraphCoordinator correctly
  └─ Doesn't bypass it to modify graph
  └─ Calls public methods only
  
☐ Handles executor errors
  └─ Checks return values from executor methods
  └─ Reports errors to client
  
☐ Thread-safe with executor
  └─ Doesn't make concurrent calls to executor (if not allowed)
  └─ Protects shared state
  
☐ Documentation
  └─ Doxygen comments on public methods
  └─ REST endpoint descriptions
```

---

### Checkpoint 3: GraphCli Code Review

**Files to Examine:**
- `libgraph/include/graph/GraphCli.hpp`
- `libgraph/src/graph/GraphCli.cpp`
- `tools/graph-cli.cpp`
- `libgraph/test/unit/test_graph_cli.cpp`

**Checklist: API Completeness**

```
☐ File Operations
  └─ LoadGraph(filepath) - loads JSON, creates coordinator
  └─ SaveGraph(filepath) - writes graph to file
  
☐ Query Operations
  └─ ShowGraph(format) - displays full graph
  └─ ListNodes(type, format) - lists nodes with optional filter
  └─ GetNode(id, format) - single node details
  
☐ Edit Operations
  └─ UpdateNode(id, config) - modifies parameters
  
☐ Execution Operations
  └─ Init() - initializes executor
  └─ Start() - starts execution
  └─ Run() - executes to completion
  └─ Stop() - requests shutdown
  └─ Join() - waits for completion
  └─ GetState() - queries current state
  
☐ Output Handling
  └─ Table format: ID | Type | Config
  └─ JSON format: full JSON output
  └─ Error messages: clear and actionable
```

**Checklist: Command Line Interface**

```
☐ graph-cli load <path>
  └─ Reads file successfully
  └─ Parses JSON correctly
  └─ Handles file not found error
  └─ Handles invalid JSON error
  
☐ graph-cli save <path>
  └─ Writes file successfully
  └─ Valid JSON output
  └─ Includes all edits made in session
  
☐ graph-cli show [--format json|table]
  └─ Default format: table
  └─ Table format readable
  └─ JSON format valid
  
☐ graph-cli list-nodes [--type TYPE] [--format json|table]
  └─ Lists all nodes
  └─ --type filters correctly
  └─ Format options work
  
☐ graph-cli get-node --id ID
  └─ Shows single node
  └─ Includes full node_config
  
☐ graph-cli update-node --id ID --config JSON
  └─ Parses JSON config parameter
  └─ Updates in-memory (not persisted)
  └─ Shows confirmation
  
☐ graph-cli init
  └─ Initializes executor
  └─ Shows success/error
  
☐ graph-cli start
  └─ Starts execution
  └─ Shows state after start
  
☐ graph-cli run
  └─ Executes to completion (blocking)
  └─ Shows final state
  
☐ graph-cli stop
  └─ Requests shutdown
  └─ Returns immediately
  
☐ graph-cli join
  └─ Waits for completion
  └─ Shows final state
  
☐ graph-cli state
  └─ Queries and displays current state
  
☐ Help text
  └─ graph-cli help shows usage
  └─ graph-cli COMMAND --help shows command help
```

**Checklist: Test Coverage (10+ tests)**

```
☐ Test: LoadGraph reads valid file
☐ Test: LoadGraph handles file not found
☐ Test: LoadGraph handles invalid JSON
☐ Test: SaveGraph writes valid JSON
☐ Test: ListNodes returns all nodes
☐ Test: ListNodes --type filters
☐ Test: GetNode returns single node
☐ Test: UpdateNode modifies graph
☐ Test: SaveGraph persists changes
☐ Test: ShowGraph formats as table
☐ Test: ShowGraph formats as JSON
```

**Checklist: Code Quality**

```
☐ Compiles with: -Wall -Wextra -Werror -std=c++26
  └─ Zero warnings
  
☐ Uses GraphCoordinator correctly
  └─ Creates after load
  └─ Calls public methods
  
☐ File I/O robust
  └─ Handles missing files gracefully
  └─ Handles permission errors
  └─ Handles disk full errors
  
☐ Error messages
  └─ Clear and actionable
  └─ Specific about what went wrong
  
☐ Documentation
  └─ Help text for all commands
  └─ Doxygen comments on methods
```

---

## Full Integration Testing

### Test Workflow 1: Edit via CLI, Execute via Web UI

```bash
# 1. Setup
cd /Users/rklinkhammer/workspace/GraphX/build-phase2b

# 2. CLI: Load graph
graph-cli load /path/to/test_graph.json

# 3. CLI: List initial nodes
graph-cli list-nodes --format table
# Verify: Shows node "audio_filter" with frequency: 2000

# 4. CLI: Edit parameter
graph-cli update-node --id audio_filter --config '{"frequency": 3000}'

# 5. CLI: Verify change (in-memory)
graph-cli get-node --id audio_filter
# Verify: frequency is now 3000

# 6. Start HTTP server (in separate terminal)
./graph-http-server &

# 7. Web UI: Open http://localhost:8080
# 8. Web UI: Verify node table shows frequency: 3000
# 9. Web UI: Click "Init" button
# 10. Web UI: Verify state = INITIALIZED
# 11. Web UI: Click "Start" button
# 12. Web UI: Verify state = RUNNING
# 13. Web UI: Monitor execution status
# 14. Web UI: Click "Stop" button
# 15. Web UI: Click "Join" button
# 16. Web UI: Verify state = STOPPED

# 17. CLI: Save graph
graph-cli save /path/to/saved_graph.json

# 18. Verify: Load saved graph
graph-cli load /path/to/saved_graph.json
graph-cli get-node --id audio_filter
# Verify: frequency is 3000 (changes persisted)
```

**Acceptance Criteria:**
- ✅ All 6 CLI commands executed successfully
- ✅ Parameter editing persisted through save/load
- ✅ Web UI displayed nodes correctly
- ✅ Execution control buttons worked
- ✅ State displayed accurately
- ✅ No errors or warnings

---

### Test Workflow 2: REST API Full Cycle

```bash
# 1. Start HTTP server
./graph-http-server --config /path/to/test_graph.json &

# 2. GET /api/v1/nodes
curl http://localhost:8080/api/v1/nodes | jq .
# Verify: Array of all nodes

# 3. GET /api/v1/nodes/{id}
curl http://localhost:8080/api/v1/nodes/audio_filter | jq .
# Verify: Single node with full node_config

# 4. PATCH /api/v1/nodes/{id} (update parameter)
curl -X PATCH http://localhost:8080/api/v1/nodes/audio_filter \
  -H "Content-Type: application/json" \
  -d '{"node_config": {"frequency": 4000}}'
# Verify: 200 OK, returns updated node

# 5. GET to verify change
curl http://localhost:8080/api/v1/nodes/audio_filter | jq '.data.node_config.frequency'
# Verify: Returns 4000

# 6. GET /api/v1/execution/state
curl http://localhost:8080/api/v1/execution/state | jq .
# Verify: {"success": true, "data": {"state": "INITIALIZED"}}

# 7. POST /api/v1/execution/init
curl -X POST http://localhost:8080/api/v1/execution/init | jq .
# Verify: 200 OK, state changes

# 8. POST /api/v1/execution/start
curl -X POST http://localhost:8080/api/v1/execution/start | jq .
# Verify: 200 OK, state = RUNNING

# 9. GET /api/v1/execution/state
curl http://localhost:8080/api/v1/execution/state | jq '.data.state'
# Verify: "RUNNING"

# 10. POST /api/v1/execution/stop
curl -X POST http://localhost:8080/api/v1/execution/stop | jq .
# Verify: 200 or 204, returns immediately

# 11. POST /api/v1/execution/join
curl -X POST http://localhost:8080/api/v1/execution/join | jq .
# Verify: 200 OK, final state returned
```

**Acceptance Criteria:**
- ✅ All REST endpoints responded correctly
- ✅ HTTP status codes accurate
- ✅ JSON response format valid
- ✅ Execution control transitions correct
- ✅ Parameters persisted in-memory
- ✅ No server errors

---

## Final Acceptance Criteria

### Code Quality (All components)
- ✅ Compiles with `-Wall -Wextra -Werror -std=c++26`
- ✅ Zero compiler warnings
- ✅ Zero memory leaks (valgrind/asan if available)
- ✅ Code review approved
- ✅ Thread-safety verified
- ✅ Const-correctness verified
- ✅ RAII patterns used correctly

### Test Coverage
- ✅ 12+ GraphCoordinator tests passing
- ✅ 15+ GraphHttpServer tests passing
- ✅ 10+ GraphCli tests passing
- ✅ **Total: 37+ tests passing**
- ✅ All test output clean (no warnings)

### Regression
- ✅ Phase 2A tests still passing (102-112 tests)
- ✅ **Total: 140-150+ tests passing**
- ✅ No new failures introduced

### Functionality
- ✅ All REST endpoints working
- ✅ Web UI interactive and responsive
- ✅ CLI commands execute successfully
- ✅ File I/O works (load/save)
- ✅ Parameter editing works (in-memory)
- ✅ Execution control works (init/start/run/stop/join)
- ✅ State queries accurate
- ✅ Error handling robust

### Documentation
- ✅ Doxygen comments on all public APIs
- ✅ Thread-safety documented
- ✅ Return types documented
- ✅ Error conditions documented
- ✅ Usage examples provided

---

## Verification Steps (In Order)

### Step 1: Inspect Files
```
☐ Read GraphCoordinator.hpp - verify API
☐ Read GraphCoordinator.cpp - verify implementation
☐ Read GraphHttpServer.hpp - verify structure
☐ Read GraphHttpServer.cpp - verify endpoints
☐ Read index.html - verify UI structure
☐ Read GraphCli.hpp - verify commands
☐ Read graph-cli.cpp - verify main entry point
```

### Step 2: Compile
```bash
cd /Users/rklinkhammer/workspace/GraphX/build-phase2b
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ..
make clean
make -j4 2>&1 | tee build.log

# Verify no warnings
grep -i "warning:" build.log
# Should return empty (no warnings)
```

### Step 3: Run Unit Tests
```bash
ctest --verbose --filter "GraphCoordinator|GraphHttpServer|GraphCli"
# Verify: All 37+ tests pass
```

### Step 4: Run Integration Tests
Execute the two workflows above (CLI + Web UI, REST API)

### Step 5: Regression Testing
```bash
ctest --verbose
# Verify: All tests pass (140-150+)
```

### Step 6: Code Review
- Check thread-safety patterns
- Check const-correctness
- Check error handling
- Check documentation completeness

---

## Rejection Criteria

**Phase 2B will be REJECTED if:**
- ❌ Any component has compiler warnings
- ❌ Tests fail to pass
- ❌ REST endpoints don't respond correctly
- ❌ Web UI doesn't load or is non-interactive
- ❌ CLI commands don't work as specified
- ❌ Thread-safety issues detected
- ❌ Memory leaks found
- ❌ Phase 2A regression tests fail
- ❌ Code review identifies critical issues
- ❌ Documentation is incomplete or wrong

**Each rejection must include:**
1. Specific file and line number of issue
2. Exact error message or behavior observed
3. Specification reference showing the violation
4. Suggested fix (if applicable)

---

## Sign-Off

**Phase 2B COMPLETE when all verifications pass:**

```
VERIFICATION CHECKLIST
☐ All files reviewed and approved
☐ Compilation: zero warnings
☐ Unit tests: 37/37 passing
☐ Regression tests: Phase 2A still passing
☐ Integration tests: both workflows successful
☐ REST API: all endpoints functional
☐ Web UI: interactive and responsive
☐ CLI: all commands working
☐ Code quality: high standard
☐ Documentation: complete

SIGNED OFF: [Date] [Verifier Name]
COMMIT HASH: [Hash of final implementation]
BRANCH: Phase2B-implementation
```
