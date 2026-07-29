# Phase 2B Corrected Implementation Specification

**Document Status:** Corrected - Generic Graph Management Layer (No FHSS Specifics)
**Baseline Commit:** 12fb9690f84ae16d148d941e8e317b51caa97aff  
**Date:** 2026-07-25  
**Authority:** User-directed correction  

---

## Executive Summary

**Architectural Principle:** The JSON graph file is the single source of truth.

Phase 2B implements a **generic graph management layer** (not FHSS-specific) that provides:
- Web-based UI for viewing and editing graph nodes
- REST API for node CRUD operations
- CLI tool for command-line graph management

**Key Design**: This layer is completely generic. It works with any graph.json file, regardless of node types or DSP specifics. The GraphExecutor (execution plane) reads these JSON files and instantiates whatever nodes are defined.

**Removed**: All FHSS-specific dashboard code, receiver terminology, channel-specific UI elements.

---

## Correct Architecture Model

```
Generic Graph Management Layer (Phase 2B)
├── GraphCoordinator
│   ├── AddNodeToGraph(type, id, node_config)    → edit graph.json
│   ├── RemoveNodeFromGraph(id)                   → edit graph.json
│   ├── UpdateNodeConfig(id, node_config)         → edit graph.json
│   ├── GetNodeConfig(id)                         → query graph
│   └── GetGraphJson()                            → returns source of truth
├── GraphHttpServer
│   ├── Web UI for viewing/editing nodes
│   ├── REST API for node CRUD
│   └── Delegates to GraphCoordinator
└── GraphCli
    ├── CLI commands for node CRUD
    └── Delegates to GraphCoordinator

         ↓ (JSON graph file - fully generic)

Execution Plane
├── GraphExecutor
│   ├── Reads graph.json
│   ├── Instantiates nodes as defined:
│   │   ├── Any node type from any DSP domain
│   │   ├── FHSS nodes, audio nodes, vision nodes, etc.
│   │   └── User-defined custom nodes via plugins
│   └── Executes configured pipeline
```

**Key**: Management layer has ZERO knowledge of node types. It just edits the JSON structure.

---

## Component 1: GraphCoordinator

**Purpose:** Direct manipulation of graph JSON (CRUD operations on nodes array). Generic - works with any graph.json.

**File Locations:**
- Header: `libgraph/include/graph/GraphCoordinator.hpp` (~120 lines)
- Implementation: `libgraph/src/graph/GraphCoordinator.cpp` (~250 lines)

### Public Interface

```cpp
namespace graph {

/// @brief GraphCoordinator - Generic graph JSON editor
/// 
/// Direct manipulation of graph.json structure:
/// - Provides CRUD operations on nodes array
/// - Thread-safe access with std::mutex
/// - No knowledge of node types (completely generic)
/// - Works with any DSP domain (FHSS, audio, video, custom, etc.)
///
/// The JSON graph IS the single source of truth.
/// This coordinator just edits it. GraphExecutor will interpret it.
class GraphCoordinator {
public:
    // ============ CONSTRUCTION ============
    
    /// Construct coordinator with graph JSON to edit
    /// @param graph Reference to nlohmann::json graph object
    /// Thread-safe: all operations protected with mutex
    explicit GraphCoordinator(nlohmann::json& graph);
    
    /// Destructor
    ~GraphCoordinator() = default;
    
    // ============ NODE CRUD OPERATIONS ============
    // All of the following are thread-safe with std::lock_guard
    
    /// Add a node to the graph
    /// @param type Node type identifier (e.g., "CustomAudioNode", "FHSSDetector")
    /// @param id Unique node identifier
    /// @param node_config JSON object with node configuration (arbitrary structure)
    /// @return true if added successfully, false if ID already exists
    bool AddNodeToGraph(const std::string& type, 
                       const std::string& id, 
                       const nlohmann::json& node_config);
    
    /// Remove a node from the graph by ID
    /// @param id Unique node identifier
    /// @return true if removed successfully, false if not found
    bool RemoveNodeFromGraph(const std::string& id);
    
    /// Get node configuration by ID
    /// @param id Unique node identifier
    /// @return Copy of node object, or null json if not found
    nlohmann::json GetNodeConfig(const std::string& id) const;
    
    /// Update node configuration
    /// @param id Unique node identifier
    /// @param node_config New configuration for the node
    /// @return true if updated successfully, false if not found
    bool UpdateNodeConfig(const std::string& id, 
                         const nlohmann::json& node_config);
    
    /// Get all nodes of a specific type
    /// @param type Node type identifier (e.g., "PerChannelPulseDetectorNode")
    /// @return Vector of node objects with that type
    std::vector<nlohmann::json> GetNodesByType(const std::string& type) const;
    
    // ============ GRAPH INSPECTION ============
    
    /// Get the complete graph JSON (single source of truth)
    /// @return Copy of entire graph structure
    nlohmann::json GetGraphJson() const;
    
    /// Get count of all nodes in graph
    /// @return Number of nodes in nodes array
    size_t GetNodeCount() const;
    
    /// Validate graph structure
    /// @return true if graph is valid (required fields present, JSON well-formed)
    bool ValidateGraph() const;
    
    /// Get current revision number (incremented on every save)
    /// @return Revision counter
    uint32_t GetRevision() const;

private:
    // ============ PRIVATE MEMBERS ============
    
    /// Thread safety lock
    mutable std::mutex graph_lock_;
    
    /// Reference to the graph JSON being edited
    nlohmann::json& graph_;
    
    // ============ PRIVATE HELPERS ============
    
    /// Validate JSON structure against expected schema
    bool ValidateGraphSchema(const nlohmann::json& j) const;
    
    /// Find node by ID in graph
    /// @param id Node ID to search for
    /// @return Iterator to node in nodes array, or end() if not found
    auto FindNodeById(const std::string& id);
};

}  // namespace graph
```

### Key Behaviors

1. **Constructor:**
   - Takes reference to nlohmann::json graph object
   - All edits go directly to the referenced object
   - Caller is responsible for file I/O (load before, save after)

2. **Node Management:**
   - `AddNodeToGraph()` appends to `graph["nodes"]` array
   - `RemoveNodeFromGraph()` removes from array
   - `UpdateNodeConfig()` modifies existing node
   - `GetNodeConfig()` queries a single node
   - All maintain JSON structure

3. **Thread Safety:**
   - All public methods wrap access with `std::lock_guard<std::mutex> lock(graph_lock_);`
   - Returns are copies, not references (to prevent mutation after release)
   - Same `graph_lock_` protects all access

4. **Generic Design:**
   - No knowledge of node types
   - No domain-specific logic
   - Works with any JSON structure
   - `node_config` is arbitrary JSON (can contain any fields)
   - Works identically for FHSS, audio, video, custom nodes

---

## Component 2: GraphHttpServer

**Purpose:** Web server providing:
- Web UI for viewing and editing graph nodes
- REST API for node management
- Generic - works with any graph.json

**File Locations:**
- Header: `libgraph/include/graph/GraphHttpServer.hpp` (~100 lines)
- Implementation: `libgraph/src/graph/GraphHttpServer.cpp` (~350 lines)
- Web UI: `libgraph/resources/web/index.html` (~400 lines)

### REST API Endpoints

| Method | Path | Operation | Response |
|--------|------|-----------|----------|
| GET | `/api/v1/graph` | Get entire graph JSON | 200 |
| GET | `/api/v1/nodes` | List all nodes | 200 |
| GET | `/api/v1/nodes/{id}` | Get single node by ID | 200/404 |
| POST | `/api/v1/nodes` | Add node to graph | 201/409 |
| PATCH | `/api/v1/nodes/{id}` | Update node config | 200/404 |
| DELETE | `/api/v1/nodes/{id}` | Remove node from graph | 204/404 |
| GET | `/api/v1/nodes/type/{type}` | Get nodes by type | 200 |
| GET | `/` | Web UI (HTML) | 200 |

### Web UI Features
- Display all nodes in table format
- Search/filter by node ID or type
- Add node: form with type, id, config JSON
- Edit node: click to modify config
- Delete node: confirm and remove
- JSON viewer for raw graph
- Real-time updates

### API Design
- **No file operations** - caller (CLI/web framework) handles load/save
- **Generic node operations** - no domain knowledge
- **Standard HTTP semantics**:
  - POST creates (201), PATCH updates (200), DELETE removes (204)
  - 404 for not found, 409 for conflict (duplicate ID)
  - 400 for bad request (invalid JSON, missing fields)
- **JSON responses** with consistent structure
- **Error responses**: Standard JSON with message and details

---

## Component 3: GraphCli

**Purpose:** Command-line interface for graph node management

**File Locations:**
- Header: `libgraph/include/graph/GraphCli.hpp` (~70 lines)
- Implementation: `libgraph/src/graph/GraphCli.cpp` (~200 lines)

### Commands

```bash
# File operations
graph-cli load <path>                    # Load graph from file
graph-cli save <path>                    # Save graph to file
graph-cli show [--format json|table]     # Display graph (default: table)

# Node operations
graph-cli add-node --type TYPE --id ID --config '{json}'
graph-cli update-node --id ID --config '{json}'
graph-cli remove-node --id ID
graph-cli get-node --id ID
graph-cli list-nodes [--type TYPE] [--format json|table]

# Queries
graph-cli node-count
graph-cli node-ids
graph-cli nodes-by-type <type>

# Information
graph-cli help [COMMAND]
```

### Design
- Caller (script) manages file I/O: `graph-cli load file.json` (modifies current graph)
- Commands always operate on "current" graph in memory
- `save` writes modified graph back to file
- No FHSS-specific commands
- Works with any graph.json structure

---

## Integration with GraphExecutor

**Data Flow:**
```
1. User via CLI or Web UI:
   graph-cli load graph.json
   graph-cli add-node --type CustomNode --id my_node --config '{...}'
   graph-cli save graph.json
   
2. File System:
   graph.json (updated)
   
3. Execution Plane:
   GraphExecutor executor = GraphExecutorBuilder()
     .WithJsonConfig("graph.json")
     .Build();
   executor.Execute();
```

**Philosophy:**
- Management layer: Generic graph editor (no DSP knowledge)
- Execution layer: Interprets graph and instantiates nodes
- They communicate through graph.json only
- No cross-plane coordination
- No configuration derivation in management layer

---

## Testing Strategy

### Unit Tests (test_graph_coordinator.cpp)
- Add node to graph
- Remove node from graph
- Update node config
- Get node by ID
- Get nodes by type
- Thread safety with concurrent access
- Idempotency (same operation twice)

### Integration Tests (test_graph_http_server.cpp)
- HTTP endpoints (GET/POST/PATCH/DELETE)
- Error handling (404, 409, 400 responses)
- JSON request/response format
- Node add/remove/update via API
- Web UI rendering

### CLI Tests (test_graph_cli.cpp)
- Load/save graph files
- Add/remove/update node commands
- Table and JSON output formats
- Error messages
- Help text

### Acceptance Criteria
- Add/remove/update operations work correctly
- No data loss on file round-trip
- Zero compiler warnings
- 100% thread-safe access to graph
- Works with any graph.json (FHSS, audio, custom, etc.)
- Web UI responsive and intuitive

---

## Phase 2B Scope - Simplified

**In Scope:**
- ✅ Generic node CRUD operations on any graph.json
- ✅ Thread-safe access to graph
- ✅ REST API for node management
- ✅ Web UI for viewing/editing nodes
- ✅ CLI for command-line graph editing
- ✅ Arbitrary node_config JSON (no validation of contents)

**Out of Scope:**
- ❌ File I/O (caller responsibility)
- ❌ Domain-specific logic (FHSS, audio, video, etc.)
- ❌ Graph validation (that's GraphExecutor's job)
- ❌ Node instantiation or execution
- ❌ FHSS-specific dashboard features

---

## Deliverables Summary

| File | Purpose | Status |
|------|---------|--------|
| libgraph/include/graph/GraphCoordinator.hpp | Graph editor interface | TODO |
| libgraph/src/graph/GraphCoordinator.cpp | Graph editor implementation | TODO |
| libgraph/include/graph/GraphHttpServer.hpp | REST API + Web UI interface | TODO |
| libgraph/src/graph/GraphHttpServer.cpp | REST API + Web UI implementation | TODO |
| libgraph/resources/web/index.html | Web UI for graph editing | TODO |
| libgraph/include/graph/GraphCli.hpp | CLI interface | TODO |
| libgraph/src/graph/GraphCli.cpp | CLI implementation | TODO |
| libgraph/test/unit/test_graph_coordinator.cpp | Coordinator unit tests | TODO |
| libgraph/test/unit/test_graph_http_server.cpp | HTTP server tests | TODO |
| libgraph/test/unit/test_graph_cli.cpp | CLI tests | TODO |

**Total Lines of Code:** ~1200-1500 lines  
**Test Coverage:** 50+ test cases, 150+ assertions  
**Compiler:** C++26, -Wall -Wextra -Werror  
**Zero warnings guarantee:** Yes  
**Generic:** Yes - works with any graph.json  

