# Phase 2B: Generic Graph Management Layer

**Status:** Design specification - completely generic (no FHSS specifics)  
**Baseline Commit:** 12fb9690f84ae16d148d941e8e317b51caa97aff  
**Created:** 2026-07-25  
**Scope Clarification:** View nodes and edit parameters only. No add/remove/change-type operations.

---

## Executive Summary

Phase 2B implements a **generic graph viewer and parameter editor** that provides:

1. **GraphCoordinator** - In-memory object for viewing and editing node parameters
2. **GraphHttpServer** - Web UI + REST API for viewing and editing parameters
3. **GraphCli** - Command-line tool for viewing and editing node parameters

**Key Principle:** This layer is a **read + limited-write interface**. It can:
- View all nodes
- View node parameters
- Edit node parameters (in-memory only)

It **cannot**:
- Add nodes
- Remove nodes
- Change node types
- Persist changes to disk (user must save via CLI)

Works identically for FHSS graphs, audio graphs, video graphs, or any custom graph.

---

## Architecture

```
Graph Management (Phase 2B) - View + Edit Parameters
├─ GraphCoordinator
│  └─ Update parameters on existing nodes (in-memory)
├─ GraphHttpServer  
│  ├─ Web UI (parameter editor for existing nodes)
│  └─ REST API (/api/v1/nodes/* - GET, PATCH only)
└─ GraphCli
   └─ CLI commands (load, list-nodes, update-node, save)

        ↓ (JSON file I/O via CLI - caller responsibility)

    graph.json (source of truth)

        ↓

Graph Execution (GraphX existing)
└─ GraphExecutor
   ├─ Loads graph.json
   └─ Instantiates whatever nodes are defined
```

---

## Component 1: GraphCoordinator

**Purpose:** In-memory viewer and parameter editor for graph nodes.

**Location:** `libgraph/include/graph/GraphCoordinator.hpp` and `.cpp`

```cpp
namespace graph {

/// @brief Generic graph parameter editor
/// 
/// Works with any graph.json structure. No domain knowledge.
/// Allows viewing nodes and editing their parameters only.
/// All operations thread-safe with std::mutex.
///
/// Note: Cannot add/remove/change nodes - only edit existing parameters.
class GraphCoordinator {
public:
    /// Construct with graph JSON to manage
    /// @param graph Reference to nlohmann::json graph object
    explicit GraphCoordinator(nlohmann::json& graph);
    
    ~GraphCoordinator() = default;
    
    // ========== Node Inspection (Read-Only) ==========
    
    /// Get entire graph
    /// @return Copy of graph JSON
    nlohmann::json GetGraphJson() const;
    
    /// Get single node (includes id, type, node_config)
    /// @param id Node ID
    /// @return Copy of node JSON, or null if not found
    nlohmann::json GetNode(const std::string& id) const;
    
    /// Get node's configuration/parameters
    /// @param id Node ID
    /// @return Copy of node_config, or null if not found
    nlohmann::json GetNodeConfig(const std::string& id) const;
    
    /// Get all nodes of a specific type
    /// @param type Node type to filter
    /// @return Vector of matching node objects
    std::vector<nlohmann::json> GetNodesByType(const std::string& type) const;
    
    /// Get count of nodes
    size_t GetNodeCount() const;
    
    /// Get list of all node IDs
    std::vector<std::string> GetNodeIds() const;
    
    /// Check if node exists
    bool HasNode(const std::string& id) const;
    
    // ========== Parameter Editing (In-Memory) ==========
    
    /// Update node's parameters (node_config only)
    /// @param id Node ID
    /// @param node_config New parameter object (replaces entire node_config)
    /// @return true if updated, false if not found
    /// Note: Type and ID are immutable. Only node_config is updated.
    /// Changes are IN-MEMORY only until explicitly saved to file.
    bool UpdateNodeConfig(const std::string& id,
                         const nlohmann::json& node_config);

private:
    mutable std::mutex graph_lock_;
    nlohmann::json& graph_;
    
    auto FindNodeById(const std::string& id);
};

}  // namespace graph
```

**Key Design:**
- Takes reference to existing `nlohmann::json` graph object
- All edits modify the referenced object directly
- Thread-safe with std::mutex
- Returns copies (not references) to prevent post-unlock mutations
- **Cannot add/remove nodes** - only edit existing parameters
- No file I/O - caller (CLI) handles load/save
- No validation of node_config contents (caller's responsibility)

---

## Component 2: GraphHttpServer

**Purpose:** Web UI + REST API for viewing and editing node parameters

**Locations:**
- Header: `libgraph/include/graph/GraphHttpServer.hpp`
- Implementation: `libgraph/src/graph/GraphHttpServer.cpp`
- Web UI: `libgraph/resources/web/index.html`

### Web UI Features
- **Node Table**: Display all nodes with ID, type, parameter summary
- **View Details**: Click node to see full node_config JSON
- **Edit Parameters**: Form/editor to modify node_config (JSON in/out)
- **Search/Filter**: By node ID or type
- **Execution Control**:
  - Start/Stop buttons
  - Pause/Resume (if supported by executor)
  - Step execution (manual stepping mode)
  - Status display (RUNNING, STOPPED, PAUSED, ERROR, etc.)
- **Metrics Display**: Show execution metrics (if available)

### REST API

```
GET    /api/v1/graph
       → Returns entire graph JSON (read-only)

GET    /api/v1/nodes
       → Returns array of all nodes (read-only)

GET    /api/v1/nodes/{id}
       → Returns single node details (read-only)
       → 404 if not found

GET    /api/v1/nodes/type/{type}
       → Returns nodes filtered by type (read-only)

PATCH  /api/v1/nodes/{id}
       Body: { "node_config": { ... } }
       → Updates node parameters in-memory
       → 200 ok / 404 not found / 400 bad request
       → Changes NOT persisted to disk (in-memory only)

GET    /
       → Serves web UI (index.html)

EXECUTION CONTROL ENDPOINTS
============================

GET    /api/v1/execution/state
       → Returns current execution state (RUNNING, STOPPED, PAUSED, etc.)
       → Always succeeds (200)

POST   /api/v1/execution/init
       → Initialize the graph and executor
       → 200 ok / 400 bad request / 409 conflict (wrong state)

POST   /api/v1/execution/start
       → Start execution (enters RUNNING state)
       → 200 ok / 400 bad request / 409 conflict (not initialized)

POST   /api/v1/execution/run
       → Execute graph (blocking on server, response on completion)
       → 200 ok / 400 bad request / 409 conflict (wrong state)

POST   /api/v1/execution/stop
       → Request graceful shutdown
       → 200 ok / 204 no content
       → Returns immediately (doesn't wait)

POST   /api/v1/execution/join
       → Wait for graph to complete execution
       → 200 ok / 400 bad request

POST   /api/v1/execution/pause
       → Pause execution (if supported by executor)
       → 200 ok / 501 not implemented / 409 conflict (not running)

POST   /api/v1/execution/resume
       → Resume execution (if paused)
       → 200 ok / 501 not implemented / 409 conflict (not paused)

POST   /api/v1/execution/step
       → Single step in stepping mode (if supported)
       → 200 ok / 501 not implemented / 409 conflict (not in stepping mode)
```

**HTTP Status Codes:**
- 200 OK
- 204 No Content (Stop succeeds)
- 400 Bad Request (invalid JSON, missing fields, invalid state)
- 404 Not Found (node ID doesn't exist)
- 409 Conflict (wrong execution state for operation)
- 501 Not Implemented (pause/resume/step not supported)

**JSON Response Format (Success - Parameter Update):**
```json
{
  "success": true,
  "data": { /* updated node or state */ },
  "message": "optional message"
}
```

**JSON Response Format (Success - Execution State):**
```json
{
  "success": true,
  "data": {
    "state": "RUNNING",
    "message": "Graph is executing",
    "stop_sequence_count": 0
  }
}
```

**JSON Response Format (Success - Execution Result):**
```json
{
  "success": true,
  "data": {
    "state": "STOPPED",
    "message": "Execution completed successfully",
    "error_details": ""
  }
}
```

**JSON Response Format (Error):**
```json
{
  "success": false,
  "error": "error_code",
  "message": "Human-readable error message"
}
```

**Important:** 
- Only PATCH modifies state (in-memory only)
- Changes are lost on server restart unless explicitly saved via CLI
- To persist changes: use CLI `save` command

---

## Component 3: GraphCli

**Purpose:** Command-line tool for viewing and editing node parameters

**Locations:**
- Header: `libgraph/include/graph/GraphCli.hpp`
- Implementation: `libgraph/src/graph/GraphCli.cpp`

### Commands

```bash
# File operations
graph-cli load <path>
  Load graph from JSON file (loads into current session)

graph-cli save <path>
  Save current graph to JSON file (persists all edits)

graph-cli show [--format json|table]
  Display graph (default: pretty table)
  
# Node inspection
graph-cli get-node --id ID
  Display single node with full parameters

graph-cli list-nodes [--type TYPE] [--format json|table]
  List all nodes, optionally filtered by type

# Node parameter editing
graph-cli update-node --id ID --config '{"param": value, ...}'
  Update node parameters in-memory
  (Does NOT persist to disk - use save command)

# Execution control
graph-cli init
  Initialize the graph and executor

graph-cli start
  Start graph execution

graph-cli run
  Execute graph to completion (blocking)

graph-cli stop
  Request graceful shutdown

graph-cli join
  Wait for execution to complete

graph-cli state
  Display current execution state

graph-cli pause
  Pause execution (if supported)

graph-cli resume
  Resume execution (if paused)

graph-cli step
  Single step in stepping mode (if supported)

# Queries
graph-cli node-count
  Print number of nodes

graph-cli node-ids
  Print all node IDs

graph-cli nodes-by-type <type>
  Print nodes of specific type

# Help
graph-cli help [COMMAND]
  Show help text
```

**Design:**
- Stateful within a CLI session (load → edit → save workflow)
- Not a daemon - runs command and exits
- Works with any graph.json structure
- Output formats: JSON for scripting, table for humans
- Only `update-node` modifies state (in-memory)
- Must explicitly `save` to persist changes

---

## Data Flow Example: Editing Node Parameters

### Via Web UI:
```
1. User opens http://localhost:8080
2. Sees table: "audio_filter" (AudioFilter) with frequency: 2000
3. Clicks edit button
4. JSON editor: {"frequency": 2000}
5. Changes to: {"frequency": 3000}
6. Clicks "Save"
7. PATCH /api/v1/nodes/audio_filter
8. GraphCoordinator.UpdateNodeConfig("audio_filter", {"frequency": 3000})
9. Node parameters updated IN-MEMORY
10. REST response: 200 OK
11. UI shows updated parameters
12. ⚠️  Changes NOT persisted to disk!
13. User clicks "Start" button
14. POST /api/v1/execution/init → POST /api/v1/execution/start
15. Execution begins
```

### Via CLI (with persistence and execution):
```bash
$ graph-cli load graph.json
  → loads from disk into memory

$ graph-cli list-nodes
  audio_filter (AudioFilter) frequency: 2000
  
$ graph-cli update-node --id audio_filter --config '{"frequency": 3000}'
  → GraphCoordinator.UpdateNodeConfig()
  → node parameters updated in memory

$ graph-cli save graph.json
  → writes updated graph to disk

$ graph-cli init
  → Initialize executor

$ graph-cli start
  → Start execution (enters RUNNING state)

$ graph-cli run
  → Execute to completion (blocking)

$ graph-cli state
  → STOPPED (execution complete)
```

### Via REST API - Execution Control (in-memory):
```bash
$ curl -X POST http://localhost:8080/api/v1/execution/init
  → Initializes graph

$ curl -X POST http://localhost:8080/api/v1/execution/start
  → Starts execution (enters RUNNING state)

$ curl -X GET http://localhost:8080/api/v1/execution/state
  {"success": true, "data": {"state": "RUNNING"}}

$ curl -X POST http://localhost:8080/api/v1/execution/stop
  → Request graceful shutdown

$ curl -X POST http://localhost:8080/api/v1/execution/join
  → Wait for completion
  {"success": true, "data": {"state": "STOPPED"}}
```

### Then: GraphExecutor loads and executes:
```cpp
auto executor = GraphExecutorBuilder()
  .WithJsonConfig("graph.json")  // loads the saved file with updated params
  .Build();

executor.Execute();  // runs with new parameters
```

---

## Design Principles

1. **Generic**: Zero knowledge of node types, domains, parameters
2. **Simple**: Just view and edit parameters (no structural changes)
3. **Thread-safe**: All operations protected with mutex
4. **Immutable Structure**: Cannot add/remove/change nodes via UI
5. **Caller Manages Persistence**: We edit in-memory, you save to disk
6. **Separation of concerns**:
   - GraphCoordinator = in-memory parameter editor
   - GraphHttpServer/GraphCli = user interfaces
   - GraphExecutor = node instantiation and execution
7. **No derivation**: No configuration computation, no defaults, no inference
8. **In-memory edits**: Changes don't persist until explicitly saved
9. **Idempotent operations**: Same operation twice = predictable result

---

## Testing Strategy

### Unit Tests: GraphCoordinator
- Get node (exists, doesn't exist)
- Get node by type
- Update node config (exists, doesn't exist, invalid JSON)
- Thread safety (concurrent updates)
- Idempotent updates (same operation twice)
- Parameter preservation (all fields maintained)

### Integration Tests: GraphHttpServer
- HTTP GET endpoints (200 responses)
- HTTP PATCH endpoint (200, 404, 400 responses)
- HTTP execution control endpoints (POST to init/start/run/stop/join, GET state)
- State transitions (INITIALIZED → RUNNING → STOPPED)
- Error responses (409 conflict for invalid state)
- JSON request/response format
- Web UI loads and is interactive
- Parameter edits update in-memory graph
- Execution controls trigger state changes

### Integration Tests: GraphCli
- Load/save graph files
- List-nodes query
- Get-node query
- Update-node command (in-memory)
- Execution control commands (init, start, run, stop, join, state)
- State transitions through execution
- Output formats (JSON, table)
- Error handling
- Verify updates don't persist until save

---

## Scope

**In Scope:**
- ✅ View graph structure (nodes, types, parameters)
- ✅ Edit node_config parameters (in-memory)
- ✅ Thread-safe access
- ✅ REST API for viewing/editing nodes (GET, PATCH)
- ✅ REST API for execution control (POST init/start/run/stop/join, GET state)
- ✅ Web UI for viewing and editing parameters
- ✅ Web UI for execution control (start/stop buttons, state display)
- ✅ CLI commands for viewing and editing parameters
- ✅ CLI commands for execution control (init, start, run, stop, join, state, pause, resume, step)
- ✅ Works with any graph.json (FHSS, audio, video, custom)
- ✅ Load/save via CLI

**Out of Scope (not allowed):**
- ❌ Add nodes to graph
- ❌ Remove nodes from graph
- ❌ Change node types
- ❌ File I/O in coordinator (caller responsibility)
- ❌ Domain-specific logic
- ❌ Graph validation (GraphExecutor's job)
- ❌ Node instantiation
- ❌ FHSS-specific features
- ❌ Dashboard domain-specific UI
- ❌ Automatic persistence (must save explicitly)

---

## Deliverables

| Component | Files | LOC | Tests |
|-----------|-------|-----|-------|
| GraphCoordinator | `.hpp`, `.cpp` | 120-150 | 12+ |
| GraphHttpServer | `.hpp`, `.cpp`, HTML | 200-250 | 15+ |
| GraphCli | `.hpp`, `.cpp` | 100-120 | 10+ |
| Tests | 3 files | 300+ | 37+ |

**Total:** ~700-850 lines of code + tests (very simple scope)
**Compiler:** C++26, -Wall -Wextra -Werror
**Zero warnings:** Yes
**Generic:** Yes - works with any graph.json

