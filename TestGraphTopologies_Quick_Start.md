# TestGraphTopologies - Quick Start Guide

**For**: GraphExecutorBuilder and Graph Execution Testing  
**Date**: May 11, 2026

---

## 5-Minute Quick Start

### 1. Include the Header

```cpp
#include "test/TestGraphTopologies.hpp"
```

### 2. Build a Topology

```cpp
// Build a simple sequential graph
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::LinearSequential
);

// Or build a complex diamond pattern
auto complex = test::TopologyBuilder::BuildTopology(
    test::TopologyType::DiamondComplex
);
```

### 3. Use with GraphExecutor

```cpp
auto executor = graph::GraphExecutorBuilder()
    .WithGraphManager(graph)
    .Build();
```

### 4. Test with the Graph

```cpp
// Initialize
EXPECT_TRUE(graph->Initialize());

// Execute
// ... inject messages ...

// Stop
EXPECT_TRUE(graph->Stop());
```

---

## Topology Selection Guide

### For Sequential Processing Tests
Use: **LinearSequential** or **MultiPathSequential**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::LinearSequential  // Simple 3-node chain
);
```

### For Merge/Multi-Input Tests
Use: **MergeSimple** or **InteriorToMerge**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::MergeSimple  // Two sources → Merge → Sink
);
```

### For Split/Fan-Out Tests
Use: **SplitSimple** or **DiamondComplex**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::SplitSimple  // Source → Split → Two Sinks
);
```

### For Complex Routing Tests
Use: **ComplexNetwork**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::ComplexNetwork  // 9-node multi-stage network
);
```

### For Baseline Tests
Use: **MinimalGraph**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::MinimalGraph  // Simplest: Source → Sink
);
```

### For Edge Cases
Use: **SourceOnly**
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::SourceOnly  // Single disconnected node
);
```

---

## Common Testing Patterns

### Pattern 1: Test All Topologies

```cpp
TEST(MyGraphTest, WorksWithAllTopologies) {
    auto all_types = test::TopologyBuilder::GetAllTopologyTypes();
    
    for (auto type : all_types) {
        auto graph = test::TopologyBuilder::BuildTopology(type);
        EXPECT_NE(graph, nullptr);
        
        // Your test logic here
        EXPECT_TRUE(graph->Initialize());
        EXPECT_TRUE(graph->Stop());
    }
}
```

### Pattern 2: Verify Graph Structure

```cpp
TEST(GraphStructureTest, NodeEdgeCounts) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::DiamondComplex
    );
    
    auto metadata = test::TopologyBuilder::GetTopologyMetadata(
        test::TopologyType::DiamondComplex
    );
    
    EXPECT_EQ(graph->GetNodeCount(), metadata.expected_node_count);
    EXPECT_EQ(graph->GetEdgeCount(), metadata.expected_edge_count);
}
```

### Pattern 3: Get Topology Information

```cpp
auto metadata = test::TopologyBuilder::GetTopologyMetadata(
    test::TopologyType::LinearSequential
);

// Access metadata
std::cout << "Name: " << metadata.name << "\n";
std::cout << "Nodes: " << metadata.expected_node_count << "\n";
std::cout << "Edges: " << metadata.expected_edge_count << "\n";
std::cout << "Description: " << metadata.description << "\n";

// Access node names
for (const auto& name : metadata.node_names) {
    std::cout << "  - " << name << "\n";
}
```

### Pattern 4: Get Visual Diagram

```cpp
auto diagram = test::GetTopologyDiagram(
    test::TopologyType::DiamondComplex
);
std::cout << diagram << "\n";
// Output:
// [Source] → [Split] ┬→ [Interior1] ┐
//                    └→ [Interior2] ┴→ [Merge] → [Sink]
```

---

## Topology Descriptions

### LinearSequential (Type 0)
- **Diagram**: `[Source] → [Interior] → [Sink]`
- **Nodes**: 3, **Edges**: 2
- **Use**: Test sequential message transformation
- **Data Flow**: Source → transforms via Interior → Sink

### MergeSimple (Type 1)
- **Diagram**: Two sources merged to one output
- **Nodes**: 4, **Edges**: 3
- **Use**: Test multi-input merge operation
- **Data Flow**: Source1 & Source2 → Merge → Sink

### SplitSimple (Type 2)
- **Diagram**: One source split to two outputs
- **Nodes**: 4, **Edges**: 3
- **Use**: Test message replication/fan-out
- **Data Flow**: Source → Split → Sink1, Sink2

### DiamondComplex (Type 3)
- **Diagram**: Diamond pattern with parallel processing
- **Nodes**: 6, **Edges**: 5
- **Use**: Test parallel paths that merge
- **Data Flow**: Split into 2 paths → process separately → merge

### MultiPathSequential (Type 4)
- **Diagram**: Long sequential chain
- **Nodes**: 5, **Edges**: 4
- **Use**: Test transformation through multiple stages
- **Data Flow**: Source → Interior1 → Interior2 → Interior3 → Sink

### InteriorToMerge (Type 5)
- **Diagram**: Interior output + direct source → Merge
- **Nodes**: 5, **Edges**: 4
- **Use**: Test mixed input types to merge
- **Data Flow**: (Source1 → Interior) + Source2 → Merge → Sink

### ParallelMergeWithInterior (Type 6)
- **Diagram**: Parallel paths to merge
- **Nodes**: 5, **Edges**: 4
- **Use**: Test combining direct and processed inputs
- **Data Flow**: Source1 + (Source2 → Interior) → Merge → Sink

### ComplexNetwork (Type 7)
- **Diagram**: Multi-stage merge/split network
- **Nodes**: 9, **Edges**: 8
- **Use**: Test complex routing scenarios
- **Data Flow**: Sources → Merge → Split → Process → Merge → Sink

### MinimalGraph (Type 8)
- **Diagram**: `[Source] → [Sink]`
- **Nodes**: 2, **Edges**: 1
- **Use**: Baseline test (simplest valid graph)
- **Data Flow**: Direct source to sink

### SourceOnly (Type 9)
- **Diagram**: `[Source]` (disconnected)
- **Nodes**: 1, **Edges**: 0
- **Use**: Edge case validation
- **Data Flow**: No connections (may fail strict validation)

---

## Reference: Topology Enum Values

```cpp
enum class TopologyType {
    LinearSequential = 0,      // Source → Interior → Sink
    MergeSimple = 1,           // Two sources → Merge → Sink
    SplitSimple = 2,           // Source → Split → Two sinks
    DiamondComplex = 3,        // Diamond split→process→merge
    MultiPathSequential = 4,   // Source → 3 Interiors → Sink
    InteriorToMerge = 5,       // Interior+Source → Merge → Sink
    ParallelMergeWithInterior = 6,  // Parallel paths → Merge
    ComplexNetwork = 7,        // Multi-stage complex network
    MinimalGraph = 8,          // Source → Sink (minimal)
    SourceOnly = 9             // Single disconnected source
};
```

---

## Common Use Cases

### Use Case 1: GraphExecutorBuilder Testing
```cpp
for (auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
    auto graph = test::TopologyBuilder::BuildTopology(type);
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .Build();
    EXPECT_NE(executor, nullptr);
}
```

### Use Case 2: Message Flow Verification
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::DiamondComplex
);
// Inject messages and verify they flow through all paths
```

### Use Case 3: Performance Benchmarking
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::ComplexNetwork
);
// Measure construction time, execution throughput, etc.
```

### Use Case 4: Graph Validation
```cpp
auto graph = test::TopologyBuilder::BuildTopology(
    test::TopologyType::LinearSequential
);
auto metadata = test::TopologyBuilder::GetTopologyMetadata(
    test::TopologyType::LinearSequential
);
// Verify graph structure matches metadata
```

---

## Troubleshooting

### Issue: Can't find TestGraphTopologies header
**Solution**: Make sure you're including:
```cpp
#include "test/TestGraphTopologies.hpp"
```

### Issue: Graph build returns nullptr
**Solution**: Check that all nodes were added before connecting:
```cpp
auto graph = test::TopologyBuilder::BuildTopology(type);
EXPECT_NE(graph, nullptr);  // Should never be null if topology is valid
```

### Issue: Connection fails with "Edge type not registered"
**Solution**: This means the edge type needs to be registered in EdgeRegistry. All test topologies use standard Message types which should already be registered.

### Issue: Graph Initialize() fails
**Solution**: Check that all nodes are properly instantiated and in a valid state. Use metadata to verify node count.

---

## API Reference

### TopologyBuilder (Static Factory)

```cpp
// Build a topology
static std::shared_ptr<graph::GraphManager> BuildTopology(TopologyType type);

// Get metadata
static TopologyMetadata GetTopologyMetadata(TopologyType type);

// Get all topology types
static std::vector<TopologyType> GetAllTopologyTypes();
```

### Helper Functions

```cpp
// Get ASCII diagram
std::string GetTopologyDiagram(TopologyType type);

// Get documentation
std::string GetTopologyDocumentation(TopologyType type);
```

### TopologyMetadata Structure

```cpp
struct TopologyMetadata {
    std::string name;                      // Topology name
    std::string description;               // Description
    size_t expected_node_count;            // Expected node count
    size_t expected_edge_count;            // Expected edge count
    std::vector<std::string> node_names;   // Node names
};
```

---

## Tips & Best Practices

1. **Use MinimalGraph for baseline tests** - Simplest graph to verify basic functionality

2. **Use ComplexNetwork for stress tests** - Most complex scenario (9 nodes, 8 edges)

3. **Iterate through all topologies** - Ensures your code works with various patterns

4. **Query metadata for assertions** - Use expected counts to validate graph structure

5. **Combine with performance monitoring** - Measure timing for each topology type

6. **Document your topology choice** - Add comments explaining why you chose each topology

---

**For More Information**: See [TestGraphTopologies_Implementation_Report.md](TestGraphTopologies_Implementation_Report.md)
