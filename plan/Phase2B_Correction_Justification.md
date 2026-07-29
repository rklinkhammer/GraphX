# Phase 2B Specification Correction - Justification

**Date:** 2026-07-25  
**Reset Commit:** 12fb9690f84ae16d148d941e8e317b51caa97aff  
**Status:** Baseline restored, corrected specification created

---

## What Changed

### The Problem With Original Specification

The original Phase2B_Implementation_Specification.md (created by orchestrator) specified:

```cpp
class ReceiverGraphCoordinator {
    // In-memory abstract topology
    std::unordered_map<std::string, std::unique_ptr<ReceiverNode>> receivers_by_id_;
    
    // Methods disconnected from actual graph files
    bool RegisterReceiver(ReceiverNode* node);
    bool UnregisterReceiver(const std::string& receiver_id);
    std::vector<std::string> DetectConflicts(const SourceConfiguration& new_config);
    bool PropagateConfigurationChange(const SourceConfiguration& new_config);
    nlohmann::json GetTopologyAsJson() const;  // Export only
```

**This creates a phantom abstraction layer** that:
- ❌ Doesn't load/save graph files
- ❌ Doesn't manipulate the actual `nodes` array in JSON
- ❌ Duplicates logic from FHSSConfigurationDeriver
- ❌ Maintains state separate from the source of truth (JSON)
- ❌ Has no connection to what GraphExecutor expects

### The Correct Architecture (GraphX as-is)

Looking at actual graph files (`fhss_cpsm_channelized_fixture_500msps.json`):

```json
{
  "name": "fhss_cpsm_channelized_fixture_500msps",
  "nodes": [
    {
      "id": "source",
      "type": "FHSSSyntheticIqSourceNode",
      "node_config": { "active_frequency_indices": [...] }
    },
    {
      "id": "downconverter", 
      "type": "FHSSDownconverterNode",
      "node_config": { ... }
    },
    {
      "id": "detector_24",
      "type": "PerChannelPulseDetectorNode",
      "node_config": { "channel": 24, "frequency_hz": 2450000000 }
    },
    // ... 63 more detector nodes ...
  ]
}
```

**The JSON IS the graph. Period.**

GraphExecutor reads this JSON and instantiates actual nodes. The management plane should:
1. Load the JSON
2. Provide CRUD operations on the `nodes` array
3. Save changes back to JSON
4. That's it.

### What Was Wrong in Original

| Original Concept | Actual GraphX Reality | Fix |
|---|---|---|
| Abstract ReceiverNode struct | Nodes live only in JSON | Remove struct, work with JSON |
| In-memory topology map | JSON is the topology | Load/save JSON directly |
| RegisterReceiver/UnregisterReceiver methods | Add/Remove from JSON nodes array | Rename to AddNodeToGraph/RemoveNodeFromGraph |
| PropagateConfigurationChange | GraphExecutor reads JSON | Remove entirely |
| DetectConflicts | External validator concern | Remove from coordinator |
| GetTopologyAsJson (export only) | JSON loads/saves directly | Make LoadGraphFromFile/SaveGraphToFile primary |

---

## Corrected Specification Specifics

### ReceiverGraphCoordinator (Corrected)

```cpp
class ReceiverGraphCoordinator {
    // The graph JSON (single source of truth)
    nlohmann::json graph_json_;
    
    // File I/O
    bool LoadGraphFromFile(const std::string& path);
    bool SaveGraphToFile(const std::string& path);
    
    // Node CRUD
    bool AddNodeToGraph(const std::string& type, 
                       const std::string& id, 
                       const nlohmann::json& node_config);
    bool RemoveNodeFromGraph(const std::string& id);
    nlohmann::json GetNodeConfig(const std::string& id) const;
    bool UpdateNodeConfig(const std::string& id, 
                         const nlohmann::json& node_config);
    
    // Graph inspection
    nlohmann::json GetGraphJson() const;  // The source of truth
    size_t GetNodeCount() const;
    bool ValidateGraph() const;
};
```

**Key Difference:** Works directly with the JSON graph structure, not an abstraction.

---

## Why This Matters

### Data Flow (Corrected Model)

```
User via CLI/HTTP
    ↓
ReceiverGraphCoordinator
    ├─ LoadGraphFromFile("graph.json")
    ├─ AddNodeToGraph("PerChannelPulseDetectorNode", "rx_48", {...})
    └─ SaveGraphToFile("graph.json")
    ↓
graph.json (updated)
    ↓
GraphExecutor.LoadFromJsonFile("graph.json")
    ↓
for each node in graph.json["nodes"]:
    instantiate(node.type, node.id, node.node_config)
    ↓
Actual running DSP nodes
```

### Data Flow (Original Model - Wrong)

```
User via CLI/HTTP
    ↓
ReceiverGraphCoordinator (in-memory abstraction)
    ├─ RegisterReceiver(new ReceiverNode(...))
    ├─ PropagateConfigurationChange(SourceConfiguration)
    └─ GetTopologyAsJson() [export only]
    ↓
??? How does GraphExecutor know about the new node? ???
    (The JSON file was never updated!)
    ↓
GraphExecutor still reads OLD graph.json
    ↓
New nodes not instantiated
```

---

## Baseline Restoration

**Reset to:** `12fb9690f84ae16d148d941e8e317b51caa97aff` (fhss review baseline - tested)

**What was removed:**
- ReceiverGraphCoordinator.hpp (incorrect version)
- ReceiverGraphCoordinator.cpp (incorrect version)
- ReceiverGraphHttpServer.hpp (incorrect version)
- ReceiverGraphHttpServer.cpp (incorrect version)
- ReceiverGraphCli.hpp (incorrect version)
- ReceiverGraphCli.cpp (incorrect version)
- All incorrect test files (115 tests for wrong problem)

**What remains:**
- Phase 2A configuration system (102/112 passing tests)
- All DSP node implementations
- All graph infrastructure
- GraphExecutor ready to load corrected JSON

---

## Next Steps

1. **Implement ReceiverGraphCoordinator** using corrected specification
   - Load/save graph JSON files
   - CRUD operations on nodes array
   - Thread-safe access with std::mutex

2. **Implement ReceiverGraphHttpServer** 
   - REST endpoints for node management
   - RFC 9457 error responses
   - RFC 7232 ETag headers

3. **Implement ReceiverGraphCli**
   - Commands: load, save, add-node, remove-node, update-node, list-nodes, validate-graph

4. **Write integration tests**
   - Test round-trip file persistence
   - Verify JSON structure matches GraphExecutor expectations
   - Verify RFC compliance

---

## Key Principle

**The JSON graph file is the single source of truth.**

The management layer (Phase 2B) is just a convenient interface for editing it. The execution layer (GraphExecutor) reads it to instantiate real nodes. Nothing more.

No derivation. No abstract topology. No coordination layer inventing state.

Just: Load JSON → User edits → Save JSON → GraphExecutor reads → Nodes run.

