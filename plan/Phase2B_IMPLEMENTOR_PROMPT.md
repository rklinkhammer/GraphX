# Phase 2B Implementor Prompt

## Your Mission
Implement ONE of three parallel tracks for Phase 2B generic graph management layer. Each track is independent after GraphCoordinator is complete. Follow specification in Phase2B_Generic_Graph_Management.md precisely.

**Before starting:** Check orchestrator status - ensure GraphCoordinator is tested before starting HttpServer or Cli.

---

## Track A: GraphCoordinator Implementation

### Overview
GraphCoordinator is an in-memory parameter editor with thread-safe access. It does NOT handle file I/O - that's the caller's responsibility.

### Files to Create
```
libgraph/include/graph/GraphCoordinator.hpp       (45-55 lines)
libgraph/src/graph/GraphCoordinator.cpp           (80-100 lines)
libgraph/test/unit/test_graph_coordinator.cpp    (250-350 lines)
```

### API Specification

```cpp
namespace graph {

class GraphCoordinator {
public:
    // Constructor: takes reference to existing JSON graph object
    explicit GraphCoordinator(nlohmann::json& graph);
    
    // IMPORTANT: Do NOT own the graph object - only reference it
    
    // Read-only operations (thread-safe)
    nlohmann::json GetGraphJson() const;
    nlohmann::json GetNode(const std::string& id) const;
    nlohmann::json GetNodeConfig(const std::string& id) const;
    std::vector<nlohmann::json> GetNodesByType(const std::string& type) const;
    std::vector<std::string> GetNodeIds() const;
    size_t GetNodeCount() const;
    bool HasNode(const std::string& id) const;
    
    // Write operation (thread-safe)
    bool UpdateNodeConfig(const std::string& id, const nlohmann::json& node_config);
    
    // Deleted copy/move (non-copyable, non-movable)
    GraphCoordinator(const GraphCoordinator&) = delete;
    GraphCoordinator& operator=(const GraphCoordinator&) = delete;
    
private:
    mutable std::mutex graph_lock_;
    nlohmann::json& graph_;  // Reference, not owned
};

}  // namespace graph
```

### Implementation Requirements

1. **Constructor**
   ```cpp
   explicit GraphCoordinator(nlohmann::json& graph) : graph_(graph) {}
   ```

2. **GetGraphJson()**
   - Acquire lock with std::lock_guard
   - Return COPY of entire graph_ object
   - Never return reference (prevents post-unlock mutations)
   - Ensure graph_ has "nodes" array, return empty if missing

3. **GetNode(id)**
   - Acquire lock
   - Find node in graph_["nodes"] array by matching node["id"]
   - Return COPY of node object or empty json if not found
   - Never return reference

4. **GetNodesByType(type)**
   - Acquire lock
   - Iterate graph_["nodes"], collect nodes where node["type"] == type
   - Return std::vector of COPIES (not references)
   - Return empty vector if none found

5. **GetNodeIds()**
   - Acquire lock
   - Extract all "id" fields from graph_["nodes"]
   - Return vector of strings
   - Return empty vector if no nodes

6. **UpdateNodeConfig(id, node_config)**
   - Acquire lock
   - Find node with matching id
   - Update node["node_config"] = node_config
   - Return true if successful, false if node not found
   - Do NOT validate node_config contents
   - Do NOT add node if not found (only update existing)

7. **Thread Safety**
   - Use std::lock_guard<std::mutex> in ALL public methods
   - Lock BEFORE accessing graph_, release at method end
   - Never hold lock across external calls
   - mutable mutex allows const methods to lock

### Test Requirements (12+ tests)

```cpp
// test_graph_coordinator.cpp

TEST_CASE("GraphCoordinator: Constructor and basic properties") {
    nlohmann::json graph = {
        {"nodes", nlohmann::json::array({
            {{"id", "node1"}, {"type", "TypeA"}, {"node_config", {{"param", 10}}}},
            {{"id", "node2"}, {"type", "TypeB"}, {"node_config", {{"param", 20}}}}
        })}
    };
    GraphCoordinator coord(graph);
    
    REQUIRE(coord.GetNodeCount() == 2);
    REQUIRE(coord.GetNodeIds().size() == 2);
}

TEST_CASE("GraphCoordinator: GetNode returns copy, not reference") {
    // Verify that modifying returned object doesn't affect original
}

TEST_CASE("GraphCoordinator: GetNodesByType filters correctly") {
    // Add multiple nodes of different types, verify filtering
}

TEST_CASE("GraphCoordinator: UpdateNodeConfig modifies graph") {
    // Update parameter, verify GetNode reflects change
}

TEST_CASE("GraphCoordinator: UpdateNodeConfig only updates existing nodes") {
    // Try to "update" non-existent node, verify returns false
}

TEST_CASE("GraphCoordinator: Thread safety with concurrent reads") {
    // Spawn multiple threads calling GetNode simultaneously
    // Verify no data races
}

TEST_CASE("GraphCoordinator: Thread safety with read/write mix") {
    // Thread A: UpdateNodeConfig
    // Thread B: GetNode
    // Verify no race conditions
}

TEST_CASE("GraphCoordinator: HasNode returns correct boolean") {
    // Check existing and non-existing nodes
}

TEST_CASE("GraphCoordinator: Empty graph handling") {
    // Create empty graph {}, verify methods handle gracefully
}

TEST_CASE("GraphCoordinator: GetNodeConfig extracts node_config field") {
    // Verify returns only node_config, not whole node
}

TEST_CASE("GraphCoordinator: GetGraphJson returns full graph copy") {
    // Verify entire structure copied, modifications don't affect original
}

TEST_CASE("GraphCoordinator: Non-copyable, non-movable") {
    // Verify delete of copy/move constructors and assignment
    // static_assert to verify
}
```

### Build Integration
Add to CMakeLists.txt:
```cmake
# libgraph/CMakeLists.txt
target_sources(graph PUBLIC
    include/graph/GraphCoordinator.hpp
    src/graph/GraphCoordinator.cpp
)

# Tests
add_executable(test_graph_coordinator test/unit/test_graph_coordinator.cpp)
target_link_libraries(test_graph_coordinator PRIVATE graph Catch2::Catch2WithMain)
add_test(NAME GraphCoordinator COMMAND test_graph_coordinator)
```

### Compilation Check
```bash
cd /Users/rklinkhammer/workspace/GraphX/build-phase2b
make -j4 2>&1 | grep -i "error\|warning" | grep -v "^--"
```
Must compile with ZERO warnings using: `-Wall -Wextra -Werror -std=c++26`

---

## Track B: GraphHttpServer Implementation

### Overview
GraphHttpServer provides REST API and web UI for parameter viewing/editing and execution control. It uses GraphCoordinator internally and exposes GraphExecutor lifecycle methods.

### Files to Create
```
libgraph/include/graph/GraphHttpServer.hpp        (60-80 lines)
libgraph/src/graph/GraphHttpServer.cpp            (250-350 lines)
libgraph/resources/web/index.html                 (150-200 lines)
libgraph/test/unit/test_graph_http_server.cpp    (300-400 lines)
```

### Dependency: GraphCoordinator
Must be implemented and tested first. HttpServer will instantiate GraphCoordinator internally.

### API Specification

```cpp
namespace graph {

class GraphHttpServer {
public:
    // Constructor
    GraphHttpServer(nlohmann::json& graph, GraphExecutor* executor, int port = 8080);
    
    // Lifecycle
    bool Start();     // Start HTTP server
    bool Stop();      // Stop HTTP server
    bool IsRunning() const;
    
    // Deleted copy/move
    GraphHttpServer(const GraphHttpServer&) = delete;
    GraphHttpServer& operator=(const GraphHttpServer&) = delete;
    
private:
    // Private helpers for route handlers
    void SetupRoutes();
    std::string HandleGetGraph();
    std::string HandleGetNodes();
    std::string HandleGetNode(const std::string& id);
    std::string HandlePatchNode(const std::string& id, const std::string& body);
    std::string HandleExecutionInit();
    std::string HandleExecutionStart();
    // ... other execution handlers
    
    GraphCoordinator coordinator_;
    GraphExecutor* executor_;
    int port_;
    // HTTP server instance (from cpp-httplib or similar)
};

}  // namespace graph
```

### REST Endpoints to Implement

**Parameter Viewing (Read-only):**
- `GET /api/v1/graph` → Returns full graph JSON
- `GET /api/v1/nodes` → Returns nodes array
- `GET /api/v1/nodes/{id}` → Returns single node
- `GET /api/v1/nodes/type/{type}` → Filter by type
- `GET /` → Serve index.html (web UI)

**Parameter Editing:**
- `PATCH /api/v1/nodes/{id}` → Update node_config
  - Request: `{"node_config": {...}}`
  - Response: `{"success": true, "data": {...}}`

**Execution Control:**
- `GET /api/v1/execution/state` → Current state
- `POST /api/v1/execution/init` → Call executor.Init()
- `POST /api/v1/execution/start` → Call executor.Start()
- `POST /api/v1/execution/run` → Call executor.Run() (blocking)
- `POST /api/v1/execution/stop` → Call executor.Stop()
- `POST /api/v1/execution/join` → Call executor.Join()
- `POST /api/v1/execution/pause` → Call executor.Pause() if supported
- `POST /api/v1/execution/resume` → Call executor.Resume() if supported
- `POST /api/v1/execution/step` → Call executor.Step() if supported

### HTTP Response Format

**Success (200 OK):**
```json
{
  "success": true,
  "data": { /* operation result */ },
  "message": "optional"
}
```

**Error (4xx):**
```json
{
  "success": false,
  "error": "error_code",
  "message": "human readable message"
}
```

### HTTP Status Codes
- 200 OK - Success
- 204 No Content - Stop succeeded
- 400 Bad Request - Invalid JSON/parameters
- 404 Not Found - Node not found
- 409 Conflict - Wrong execution state
- 501 Not Implemented - pause/resume/step not supported

### Web UI Requirements (index.html)

```html
<!DOCTYPE html>
<html>
<head>
    <title>GraphX Management</title>
    <style>
        /* Basic styling for node table, forms, buttons */
    </style>
</head>
<body>
    <div id="container">
        <!-- Header: GraphX Management -->
        <!-- Execution Control Panel -->
        <!--   Status display (current execution state) -->
        <!--   Buttons: Init, Start, Run, Stop, Join -->
        <!--   State indicator: RUNNING/STOPPED/PAUSED/ERROR -->
        
        <!-- Node List/Editor -->
        <!--   Search/filter by ID or type -->
        <!--   Table: Node ID | Type | Status | Actions -->
        <!--   Edit button → opens parameter editor modal -->
        <!--   Parameter editor: JSON form with save/cancel -->
    </div>
    
    <script>
        // JavaScript for:
        // - Fetch nodes from /api/v1/nodes
        // - Display in table
        // - Handle edit button → show parameter editor
        // - POST to /api/v1/nodes/{id} for updates
        // - Handle execution control buttons → POST to /api/v1/execution/*
        // - Poll /api/v1/execution/state for state updates
    </script>
</body>
</html>
```

### Implementation Steps

1. **Setup HTTP Server**
   - Use cpp-httplib or similar lightweight HTTP library
   - Configure CORS headers for browser access
   - Register route handlers

2. **Implement Parameter Endpoints**
   - GET routes use GraphCoordinator::Get* methods
   - PATCH route uses GraphCoordinator::UpdateNodeConfig
   - Return proper JSON format

3. **Implement Execution Control Endpoints**
   - Call corresponding GraphExecutor methods
   - Handle ExecutionState enum values
   - Return state in response
   - Handle exceptions/errors from executor

4. **Create Web UI**
   - Simple HTML table for nodes
   - JSON editor form for parameters
   - Execution control buttons
   - State indicator
   - No frameworks required (vanilla JS OK)

### Test Requirements (15+ tests)

```cpp
// test_graph_http_server.cpp

TEST_CASE("GraphHttpServer: GET /api/v1/graph returns full graph") { }
TEST_CASE("GraphHttpServer: GET /api/v1/nodes returns array") { }
TEST_CASE("GraphHttpServer: GET /api/v1/nodes/{id} returns node") { }
TEST_CASE("GraphHttpServer: GET /api/v1/nodes/{id} returns 404 if not found") { }
TEST_CASE("GraphHttpServer: GET /api/v1/nodes/type/{type} filters by type") { }
TEST_CASE("GraphHttpServer: PATCH /api/v1/nodes/{id} updates parameters") { }
TEST_CASE("GraphHttpServer: PATCH returns 404 for unknown node") { }
TEST_CASE("GraphHttpServer: PATCH returns 400 for invalid JSON") { }
TEST_CASE("GraphHttpServer: GET / serves index.html") { }
TEST_CASE("GraphHttpServer: POST /api/v1/execution/init transitions state") { }
TEST_CASE("GraphHttpServer: POST /api/v1/execution/start after init") { }
TEST_CASE("GraphHttpServer: GET /api/v1/execution/state returns current state") { }
TEST_CASE("GraphHttpServer: Execution state visible after start/stop") { }
TEST_CASE("GraphHttpServer: PATCH updates reflected in next GET") { }
TEST_CASE("GraphHttpServer: Web UI loads without errors") { }
```

---

## Track C: GraphCli Implementation

### Overview
GraphCli is a command-line tool for loading, editing, and executing graphs. It provides commands for file I/O (which GraphCoordinator doesn't do) and wraps GraphExecutor lifecycle methods.

### Files to Create
```
libgraph/include/graph/GraphCli.hpp               (40-60 lines)
libgraph/src/graph/GraphCli.cpp                   (150-200 lines)
tools/graph-cli.cpp                               (100-150 lines, main entry point)
libgraph/test/unit/test_graph_cli.cpp            (250-350 lines)
```

### Dependency: GraphCoordinator
Must be implemented and tested first. GraphCli will instantiate GraphCoordinator internally.

### Command Specification

```bash
graph-cli load <path>
  Load graph from JSON file
  Usage: graph-cli load mymap.json
  
graph-cli save <path>
  Save graph to JSON file (persists all in-memory edits)
  Usage: graph-cli save mymap.json
  
graph-cli show [--format json|table]
  Display entire graph
  Usage: graph-cli show --format table
  
graph-cli list-nodes [--type TYPE] [--format json|table]
  List all nodes
  Usage: graph-cli list-nodes --type AudioFilter --format table
  
graph-cli get-node --id ID
  Show single node details
  Usage: graph-cli get-node --id audio_processor
  
graph-cli update-node --id ID --config JSON
  Update node parameters (in-memory only, doesn't persist)
  Usage: graph-cli update-node --id audio_processor --config '{"frequency": 2000}'
  
graph-cli init
  Initialize graph executor
  Usage: graph-cli init
  
graph-cli start
  Start graph execution
  Usage: graph-cli start
  
graph-cli run
  Execute graph to completion (blocking)
  Usage: graph-cli run
  
graph-cli stop
  Request graceful shutdown
  Usage: graph-cli stop
  
graph-cli join
  Wait for graph execution to complete
  Usage: graph-cli join
  
graph-cli state
  Query current execution state
  Usage: graph-cli state
```

### API Specification

```cpp
namespace graph {

class GraphCli {
public:
    // Session management
    bool LoadGraph(const std::string& filepath);
    bool SaveGraph(const std::string& filepath);
    
    // Query operations
    std::string ShowGraph(const std::string& format = "table");
    std::string ListNodes(const std::string& type = "", const std::string& format = "table");
    std::string GetNode(const std::string& id, const std::string& format = "table");
    
    // Edit operations
    bool UpdateNode(const std::string& id, const nlohmann::json& config);
    
    // Execution operations
    bool Init();
    bool Start();
    bool Run();
    bool Stop();
    bool Join();
    std::string GetState();
    
private:
    nlohmann::json graph_;
    GraphCoordinator* coordinator_;  // Created after graph loaded
    GraphExecutor* executor_;        // Created after graph initialized
};

}  // namespace graph
```

### Implementation Requirements

1. **LoadGraph(filepath)**
   - Read JSON from file
   - Parse into graph_ object
   - Create new GraphCoordinator(&graph_)
   - Return success/failure

2. **SaveGraph(filepath)**
   - Write graph_ to file as JSON
   - Pretty-print with indentation
   - Return success/failure

3. **ShowGraph/ListNodes/GetNode**
   - Use GraphCoordinator methods
   - Format as table (pretty columns) or JSON (compact)
   - Table format: ID | Type | Parameters

4. **UpdateNode**
   - Use GraphCoordinator::UpdateNodeConfig
   - Verify node exists before updating
   - Show success/failure message

5. **Init/Start/Run/Stop/Join/GetState**
   - Call corresponding GraphExecutor methods
   - Handle return values (ExecutionResult or std::expected)
   - Show state after each operation

6. **Error Handling**
   - File not found → clear error message
   - Invalid JSON → parse error details
   - Executor errors → report GraphExecutionFailure details

### Main Entry Point (tools/graph-cli.cpp)

```cpp
int main(int argc, char* argv[]) {
    // Parse command line arguments
    // Create GraphCli instance
    // Dispatch to appropriate command handler
    // Print results to stdout
    // Return 0 for success, 1 for error
}
```

### Test Requirements (10+ tests)

```cpp
// test_graph_cli.cpp

TEST_CASE("GraphCli: Load valid graph file") { }
TEST_CASE("GraphCli: Load invalid file returns error") { }
TEST_CASE("GraphCli: Save writes valid JSON") { }
TEST_CASE("GraphCli: ListNodes returns all nodes") { }
TEST_CASE("GraphCli: ListNodes with --type filters") { }
TEST_CASE("GraphCli: GetNode returns single node") { }
TEST_CASE("GraphCli: UpdateNode modifies in-memory graph") { }
TEST_CASE("GraphCli: SaveGraph persists edits") { }
TEST_CASE("GraphCli: ShowGraph formats as table") { }
TEST_CASE("GraphCli: ShowGraph formats as JSON") { }
```

---

## Common Implementation Notes

### 1. Compilation Flags
All components must compile with:
```bash
-Wall -Wextra -Werror -std=c++26
```
**Zero warnings tolerated.**

### 2. Use Copies, Not References
**CRITICAL**: When returning data from public methods, return copies of JSON objects, never references. This prevents use-after-free bugs in multithreaded code.

```cpp
// ✅ CORRECT
nlohmann::json GetNode(const std::string& id) const {
    std::lock_guard lock(mutex_);
    auto it = find_node(id);
    return it->second;  // COPY returned
}

// ❌ WRONG
const nlohmann::json& GetNode(const std::string& id) const {
    std::lock_guard lock(mutex_);
    return nodes_[id];  // Reference to locked data - DANGER!
}
```

### 3. Thread Safety Pattern
Always use this pattern in public methods:
```cpp
Type PublicMethod() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Access shared state here
    return result;  // Copy returned
}  // Lock automatically released
```

### 4. JSON Handling
- Use nlohmann::json library (already in dependencies)
- Always check for required fields before accessing:
  ```cpp
  if (!graph.contains("nodes")) graph["nodes"] = nlohmann::json::array();
  if (!node.contains("id")) return false;  // Invalid node
  ```
- Use get() for optional fields with defaults:
  ```cpp
  std::string type = node.value("type", "unknown");
  ```

### 5. Error Messages
Be specific and actionable:
```cpp
// ❌ Bad
if (!LoadFile(path)) return false;

// ✅ Good
if (!LoadFile(path)) {
    std::cerr << "Error: Could not load file '" << path << "' (file not found or invalid JSON)\n";
    return false;
}
```

### 6. Documentation
Add Doxygen comments to all public methods:
```cpp
/// Get node by ID
/// @param id The unique node identifier
/// @return Copy of node object or empty json if not found
/// @thread_safe Yes (acquires internal mutex)
nlohmann::json GetNode(const std::string& id) const;
```

### 7. Testing
- Use Catch2 v3.14.0 framework
- Test both happy path and error cases
- Test with empty inputs
- Test thread safety where applicable
- Name tests descriptively: `TEST_CASE("GraphCoordinator: GetNode returns copy")`

---

## Handoff Criteria

**Before marking Track A (Coordinator) COMPLETE:**
- ✅ All 12+ tests pass
- ✅ Zero compiler warnings
- ✅ Code review approved
- ✅ Thread-safety verified

**Before marking Track B (HttpServer) COMPLETE:**
- ✅ All 15+ tests pass
- ✅ Web UI loads and is interactive
- ✅ REST endpoints respond correctly
- ✅ Execution control buttons work
- ✅ Zero compiler warnings
- ✅ Code review approved

**Before marking Track C (Cli) COMPLETE:**
- ✅ All 10+ tests pass
- ✅ Commands parse correctly
- ✅ File I/O works (load/save)
- ✅ Execution control works
- ✅ Zero compiler warnings
- ✅ Code review approved
