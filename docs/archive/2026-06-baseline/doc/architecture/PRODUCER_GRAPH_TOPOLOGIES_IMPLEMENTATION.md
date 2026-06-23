# Producer Graph Topologies Implementation Analysis

**Date**: May 29, 2026  
**Objective**: Create producer-based graph topologies following the TestGraphTopologies pattern without parameterization

---

## 1. Design Principle: Parallel Topology Sets

Rather than parameterizing existing topologies, create a **parallel set** of production-ready topology builders:

```
Existing:
  TestGraphTopologies.cpp (Message-based, plugin-based nodes)
  └─ SourceTestNode (source)
  └─ SinkTestNode (sink)
  └─ InteriorTestNode (transform)
  └─ MergeTestNode (combiner)
  └─ SplitTestNode (replicator)

New:
  ProducerGraphTopologies.cpp (Producer-based, direct instantiation)
  ├─ TestIntProducer (source with completion)
  ├─ TestIntSinkNode (sink for int)
  ├─ TestDoubleSinkNode (sink for double)
  ├─ CompletionNode (completion signal consumer)
  └─ [Future] IntProducerInteriorNode, DoubleProducerInteriorNode
```

---

## 2. ProducerTestNodes Role Mapping

### From TestGraphTopologies Patterns

| TestGraphTopologies | ProducerGraphTopologies | Data Type | Ports |
|-------------------|------------------------|-----------|-------|
| SourceTestNode | TestIntProducer | int + CompletionSignal | 2 (data + completion) |
| SourceTestNode | TestDoubleProducer | double + CompletionSignal | 2 (data + completion) |
| SinkTestNode | TestIntSinkNode | int | 1 (data in) |
| SinkTestNode | TestDoubleSinkNode | double | 1 (data in) |
| InteriorTestNode | (need to create) | int | 2 (passthrough) |
| InteriorTestNode | (need to create) | double | 2 (passthrough) |
| MergeTestNode | (need to create) | int + int → int | 3 inputs, 1 output |
| MergeTestNode | (need to create) | double + double → double | 3 inputs, 1 output |
| SplitTestNode | (need to create) | int → int + int | 1 input, 2 outputs |
| SplitTestNode | (need to create) | double → double + double | 1 input, 2 outputs |

**Key Difference**: Producer nodes require explicit CompletionNode connections on port 1

---

## 3. Topology Implementations

### 3.1 Minimal Producer Topology

**Structure**:
```
TestIntProducer
  ├─ Port 0 (int) → TestIntSinkNode
  └─ Port 1 (CompletionSignal) → CompletionNode
```

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildMinimalInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<TestIntProducer>();
    auto sink = std::make_shared<TestIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(sink);
    graph->AddNode(completion);
    
    // Data flow: producer port 0 → sink port 0
    graph->AddEdge<TestIntProducer, 0, TestIntSinkNode, 0>(
        producer, sink, buffer_size);
    
    // Completion flow: producer port 1 → completion port 0
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

**Differences from TestGraphTopologies::BuildMinimalGraph**:
1. ✅ Two edges required (data + completion)
2. ✅ Different node types (producer vs. generic source)
3. ✅ Implicit CompletionNode creation (required for producer graphs)
4. ✅ Timing semantics (producer samples at fixed intervals)

---

### 3.2 Linear Sequential Producer Topology

**Structure**:
```
TestIntProducer
  ├─ Port 0 (int) → IntProducerInteriorNode → TestIntSinkNode
  └─ Port 1 (CompletionSignal) → CompletionNode
```

**Implementation Issues**:
1. **Interior node problem**: Need `IntProducerInteriorNode` that:
   - Has input port 0 (int) and output port 0 (int)
   - Simple passthrough for now
   - Future: data transformations

2. **Completion signal routing**: Does CompletionSignal propagate through interior chain?
   - Option A: Interior node strips completion, sink handles it
   - Option B: Interior node has completion input/output ports
   - **Recommended**: Option A (simpler, completion handled separately)

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildLinearSequentialInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<TestIntProducer>();
    auto interior = std::make_shared<IntProducerInteriorNode>();
    auto sink = std::make_shared<TestIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(interior);
    graph->AddNode(sink);
    graph->AddNode(completion);
    
    // Data flow chain
    graph->AddEdge<TestIntProducer, 0, IntProducerInteriorNode, 0>(
        producer, interior, buffer_size);
    graph->AddEdge<IntProducerInteriorNode, 0, TestIntSinkNode, 0>(
        interior, sink, buffer_size);
    
    // Completion flow (directly from producer to completion sink)
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

---

### 3.3 Simple Merge Producer Topology

**Structure**:
```
TestIntProducer (producer1)
  ├─ Port 0 → IntMergeNode (input 0)
  └─ Port 1 → CompletionNode

TestIntProducer (producer2)
  ├─ Port 0 → IntMergeNode (input 1)
  └─ Port 1 → CompletionNode
  
IntMergeNode → TestIntSinkNode
```

**Complexity**: Two producers, two completion nodes (or shared?)
- **Option A**: Each producer gets its own CompletionNode
- **Option B**: Single CompletionNode aggregates signals from both
- **Recommended**: Option A (simpler, tests independent completion handling)

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildMergeSimpleInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer1 = std::make_shared<TestIntProducer>();
    auto producer2 = std::make_shared<TestIntProducer>();
    auto merge = std::make_shared<IntMergeNode>();
    auto sink = std::make_shared<TestIntSinkNode>();
    auto completion1 = std::make_shared<CompletionNode>();
    auto completion2 = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer1);
    graph->AddNode(producer2);
    graph->AddNode(merge);
    graph->AddNode(sink);
    graph->AddNode(completion1);
    graph->AddNode(completion2);
    
    // Data flow
    graph->AddEdge<TestIntProducer, 0, IntMergeNode, 0>(
        producer1, merge, buffer_size);
    graph->AddEdge<TestIntProducer, 0, IntMergeNode, 1>(
        producer2, merge, buffer_size);
    graph->AddEdge<IntMergeNode, 0, TestIntSinkNode, 0>(
        merge, sink, buffer_size);
    
    // Completion flows (independent)
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer1, completion1, buffer_size);
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer2, completion2, buffer_size);
    
    return graph;
}
```

---

### 3.4 Simple Split Producer Topology

**Structure**:
```
TestIntProducer
  ├─ Port 0 → IntSplitNode → TestIntSinkNode (sink1)
  │                       → TestIntSinkNode (sink2)
  └─ Port 1 → CompletionNode
```

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildSplitSimpleInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<TestIntProducer>();
    auto split = std::make_shared<IntSplitNode>();
    auto sink1 = std::make_shared<TestIntSinkNode>();
    auto sink2 = std::make_shared<TestIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(split);
    graph->AddNode(sink1);
    graph->AddNode(sink2);
    graph->AddNode(completion);
    
    // Data flow
    graph->AddEdge<TestIntProducer, 0, IntSplitNode, 0>(
        producer, split, buffer_size);
    graph->AddEdge<IntSplitNode, 0, TestIntSinkNode, 0>(
        split, sink1, buffer_size);
    graph->AddEdge<IntSplitNode, 1, TestIntSinkNode, 0>(
        split, sink2, buffer_size);
    
    // Completion flow
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

**Validation**: Both sinks should receive identical data (replicated)

---

### 3.5 Diamond Complex Producer Topology

**Structure**:
```
TestIntProducer
  ├─ Port 0 → IntSplitNode
  │            ├─ out(0) → IntProducerInteriorNode1 ─┐
  │            └─ out(1) → IntProducerInteriorNode2 ─┼→ IntMergeNode → TestIntSinkNode
  │                                                   ┘
  └─ Port 1 → CompletionNode
```

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildDiamondComplexInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<TestIntProducer>();
    auto split = std::make_shared<IntSplitNode>();
    auto interior1 = std::make_shared<IntProducerInteriorNode>();
    auto interior2 = std::make_shared<IntProducerInteriorNode>();
    auto merge = std::make_shared<IntMergeNode>();
    auto sink = std::make_shared<TestIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(split);
    graph->AddNode(interior1);
    graph->AddNode(interior2);
    graph->AddNode(merge);
    graph->AddNode(sink);
    graph->AddNode(completion);
    
    // Diamond data flow
    graph->AddEdge<TestIntProducer, 0, IntSplitNode, 0>(
        producer, split, buffer_size);
    
    graph->AddEdge<IntSplitNode, 0, IntProducerInteriorNode, 0>(
        split, interior1, buffer_size);
    graph->AddEdge<IntSplitNode, 1, IntProducerInteriorNode, 0>(
        split, interior2, buffer_size);
    
    graph->AddEdge<IntProducerInteriorNode, 0, IntMergeNode, 0>(
        interior1, merge, buffer_size);
    graph->AddEdge<IntProducerInteriorNode, 0, IntMergeNode, 1>(
        interior2, merge, buffer_size);
    
    graph->AddEdge<IntMergeNode, 0, TestIntSinkNode, 0>(
        merge, sink, buffer_size);
    
    // Completion flow
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

---

### 3.6 Multi-Path Sequential Producer Topology

**Structure**:
```
TestIntProducer
  ├─ Port 0 → Interior1 → Interior2 → Interior3 → TestIntSinkNode
  └─ Port 1 → CompletionNode
```

**Implementation**:
```cpp
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildMultiPathSequentialInt() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<TestIntProducer>();
    auto interior1 = std::make_shared<IntProducerInteriorNode>();
    auto interior2 = std::make_shared<IntProducerInteriorNode>();
    auto interior3 = std::make_shared<IntProducerInteriorNode>();
    auto sink = std::make_shared<TestIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(interior1);
    graph->AddNode(interior2);
    graph->AddNode(interior3);
    graph->AddNode(sink);
    graph->AddNode(completion);
    
    // Long sequential chain
    graph->AddEdge<TestIntProducer, 0, IntProducerInteriorNode, 0>(
        producer, interior1, buffer_size);
    graph->AddEdge<IntProducerInteriorNode, 0, IntProducerInteriorNode, 0>(
        interior1, interior2, buffer_size);
    graph->AddEdge<IntProducerInteriorNode, 0, IntProducerInteriorNode, 0>(
        interior2, interior3, buffer_size);
    graph->AddEdge<IntProducerInteriorNode, 0, TestIntSinkNode, 0>(
        interior3, sink, buffer_size);
    
    // Completion flow
    graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

---

## 4. New Nodes Required

### 4.1 IntProducerInteriorNode

**Purpose**: Passthrough node for int data in producer-based topologies

**Definition** (in ProducerTestNodes.hpp):
```cpp
class IntProducerInteriorNode : public NamedInteriorNode<
    IntProducerInteriorNode, int> {
public:
    IntProducerInteriorNode() : NamedInteriorNode("IntProducerInteriorNode") {}
    
    bool Consume(const int& value) override {
        // Passthrough: immediately emit
        this->Emit(0, value);
        return true;
    }
};
```

### 4.2 DoubleProducerInteriorNode

**Purpose**: Passthrough node for double data in producer-based topologies

**Definition** (in ProducerTestNodes.hpp):
```cpp
class DoubleProducerInteriorNode : public NamedInteriorNode<
    DoubleProducerInteriorNode, double> {
public:
    DoubleProducerInteriorNode() : NamedInteriorNode("DoubleProducerInteriorNode") {}
    
    bool Consume(const double& value) override {
        // Passthrough: immediately emit
        this->Emit(0, value);
        return true;
    }
};
```

### 4.3 IntMergeNode

**Purpose**: Merge two int streams into one

**Definition** (in ProducerTestNodes.hpp):
```cpp
class IntMergeNode : public NamedInteriorNode<
    IntMergeNode, int, int> {
    // Multiple input ports: port 0 and 1
    // Single output port: port 0
    // Implementation: reads from both inputs, interleaves output
};
```

### 4.4 DoubleMergeNode

**Purpose**: Merge two double streams into one

### 4.5 IntSplitNode

**Purpose**: Split int stream to two outputs

**Definition** (in ProducerTestNodes.hpp):
```cpp
class IntSplitNode : public NamedInteriorNode<
    IntSplitNode, int> {
    // Single input port: port 0
    // Multiple output ports: port 0 and 1
    // Implementation: emits same value to both outputs
};
```

### 4.6 DoubleSplitNode

**Purpose**: Split double stream to two outputs

---

## 5. File Structure

### 5.1 New File: ProducerGraphTopologies.hpp

```cpp
#pragma once

#include <memory>
#include "graph/GraphManager.hpp"
#include "test/ProducerTestNodes.hpp"

namespace test {

class ProducerTopologyBuilder {
public:
    // Int-based topologies (6 variants)
    static std::shared_ptr<graph::GraphManager> BuildMinimalInt();
    static std::shared_ptr<graph::GraphManager> BuildLinearSequentialInt();
    static std::shared_ptr<graph::GraphManager> BuildMergeSimpleInt();
    static std::shared_ptr<graph::GraphManager> BuildSplitSimpleInt();
    static std::shared_ptr<graph::GraphManager> BuildDiamondComplexInt();
    static std::shared_ptr<graph::GraphManager> BuildMultiPathSequentialInt();
    
    // Double-based topologies (6 variants)
    static std::shared_ptr<graph::GraphManager> BuildMinimalDouble();
    static std::shared_ptr<graph::GraphManager> BuildLinearSequentialDouble();
    static std::shared_ptr<graph::GraphManager> BuildMergeSimpleDouble();
    static std::shared_ptr<graph::GraphManager> BuildSplitSimpleDouble();
    static std::shared_ptr<graph::GraphManager> BuildDiamondComplexDouble();
    static std::shared_ptr<graph::GraphManager> BuildMultiPathSequentialDouble();
};

} // namespace test
```

### 5.2 New File: ProducerGraphTopologies.cpp

Implementation of all 12 topology builders (6 int + 6 double)

### 5.3 New File: test_producer_graph_topologies.cpp

```cpp
#include <gtest/gtest.h>
#include "test/ProducerGraphTopologies.hpp"

class ProducerGraphTopologyTest : public ::testing::Test {};

// Int-based topology tests
TEST_F(ProducerGraphTopologyTest, MinimalInt_DataFlow) {
    auto graph = ProducerTopologyBuilder::BuildMinimalInt();
    // Execute and validate data reaches sink
    // Validate completion signal is delivered
}

TEST_F(ProducerGraphTopologyTest, LinearSequentialInt_PreservesOrder) {
    auto graph = ProducerTopologyBuilder::BuildLinearSequentialInt();
    // Verify int values pass through interior unchanged
    // Verify order preserved
}

TEST_F(ProducerGraphTopologyTest, MergeSimpleInt_CombinesStreams) {
    auto graph = ProducerTopologyBuilder::BuildMergeSimpleInt();
    // Verify both producer streams reach merge
    // Verify merged data reaches sink
}

TEST_F(ProducerGraphTopologyTest, SplitSimpleInt_ReplicatesData) {
    auto graph = ProducerTopologyBuilder::BuildSplitSimpleInt();
    // Verify both sinks receive same data
    // Verify count doubles (each value to both sinks)
}

TEST_F(ProducerGraphTopologyTest, DiamondComplexInt_ComplexPaths) {
    auto graph = ProducerTopologyBuilder::BuildDiamondComplexInt();
    // Verify data flows through both interior nodes
    // Verify merge combines correctly
    // Verify final sink receives merged data
}

TEST_F(ProducerGraphTopologyTest, MultiPathSequentialInt_LongChain) {
    auto graph = ProducerTopologyBuilder::BuildMultiPathSequentialInt();
    // Verify data passes through all 3 interior nodes
    // Verify no data loss
    // Verify completion delivered at end
}

// Double-based topology tests (mirror int tests)
TEST_F(ProducerGraphTopologyTest, MinimalDouble_DataFlow) { ... }
TEST_F(ProducerGraphTopologyTest, LinearSequentialDouble_PreservesOrder) { ... }
TEST_F(ProducerGraphTopologyTest, MergeSimpleDouble_CombinesStreams) { ... }
TEST_F(ProducerGraphTopologyTest, SplitSimpleDouble_ReplicatesData) { ... }
TEST_F(ProducerGraphTopologyTest, DiamondComplexDouble_ComplexPaths) { ... }
TEST_F(ProducerGraphTopologyTest, MultiPathSequentialDouble_LongChain) { ... }

// Cross-type validation tests
TEST_F(ProducerGraphTopologyTest, CompletionSignalDelivery) {
    // For all topologies: verify CompletionNode receives signal
}

TEST_F(ProducerGraphTopologyTest, TimingCompliance) {
    // For all topologies: verify producer timing intervals respected
}

TEST_F(ProducerGraphTopologyTest, NoDataLoss) {
    // For all topologies: all produced values reach sink
}
```

---

## 6. Integration with Existing ProducerTestNodes

**Update ProducerTestNodes.hpp** to add:

1. ✅ **SimpleIntGenerator** (exists)
2. ✅ **SimpleDoubleGenerator** (exists)
3. ✅ **TestIntProducer** (exists)
4. ✅ **TestDoubleProducer** (exists)
5. ✅ **TestIntSinkNode** (exists)
6. ✅ **TestDoubleSinkNode** (exists)
7. ✅ **CompletionNode** (exists)
8. 🆕 **IntProducerInteriorNode** (NEW)
9. 🆕 **DoubleProducerInteriorNode** (NEW)
10. 🆕 **IntMergeNode** (NEW)
11. 🆕 **DoubleMergeNode** (NEW)
12. 🆕 **IntSplitNode** (NEW)
13. 🆕 **DoubleSplitNode** (NEW)

---

## 7. Key Differences from TestGraphTopologies Pattern

| Aspect | TestGraphTopologies | ProducerGraphTopologies |
|--------|-------------------|------------------------|
| Node Creation | Plugin-based (factory) | Direct instantiation |
| Data Type | Message (universal) | int or double (specialized) |
| Port Count | Producer: 1 port | Producer: 2 ports (data + completion) |
| Completion Handling | N/A | Explicit CompletionNode per graph |
| Variants | 6 topologies | 12 topologies (6×2 data types) |
| Dependencies | GraphExecutorBuilder, plugins | ProducerTestNodes only |
| Test Count Target | 20+ tests | 12 topologies × 3 scenarios = 36+ tests |

---

## 8. Implementation Roadmap

### Phase 1: Extend ProducerTestNodes.hpp
1. Add IntProducerInteriorNode
2. Add DoubleProducerInteriorNode
3. Add IntMergeNode, DoubleMergeNode
4. Add IntSplitNode, DoubleSplitNode

**Effort**: 200-300 LOC (6 new classes, each 30-50 LOC)

### Phase 2: Create ProducerGraphTopologies.hpp/cpp
1. Create topology builder class
2. Implement 12 topology factory methods (6 int + 6 double)
3. Each topology: ~30-50 LOC

**Effort**: 400-600 LOC (12 methods × 35-50 LOC each)

### Phase 3: Create test_producer_graph_topologies.cpp
1. Test all 12 topologies
2. Validation scenarios: data flow, ordering, completion, timing

**Effort**: 300-400 LOC (12 tests × 25-35 LOC each)

---

## 9. Validation Criteria

### Per Topology
- ✅ All nodes added to graph without errors
- ✅ All edges connected without type errors
- ✅ Data flows from producer(s) to sink(s)
- ✅ Completion signal reaches CompletionNode
- ✅ No data loss or duplication
- ✅ FIFO ordering preserved
- ✅ Timing intervals respected (producer sampling rate)

### Cross-Topology
- ✅ Int and double variants produce identical behavior (modulo data type)
- ✅ Complex topologies (merge, split, diamond) handle multiple flows correctly
- ✅ Long sequential chains preserve data integrity

---

## 10. Comparison: Single Topology Implementation

### TestGraphTopologies Pattern (Minimal)
```cpp
// 6 topologies × 1 variant (Message) = 6 implementations
BuildMinimalGraph()         // Message only
BuildLinearSequential()     // Message only
BuildMergeSimple()          // Message only
BuildSplitSimple()          // Message only
BuildDiamondComplex()       // Message only
BuildMultiPathSequential()  // Message only
```

### ProducerGraphTopologies Pattern (Proposed)
```cpp
// 6 topologies × 2 variants (int + double) = 12 implementations
BuildMinimalInt()           // int variant
BuildMinimalDouble()        // double variant
BuildLinearSequentialInt()  // int variant
BuildLinearSequentialDouble() // double variant
// ... etc (12 total)
```

---

## 11. Example Test Scenarios

### Scenario 1: Minimal Topology Validation
```cpp
TEST_F(ProducerGraphTopologyTest, MinimalInt_DataFlow) {
    auto graph = ProducerTopologyBuilder::BuildMinimalInt();
    auto executor = CreateExecutor(graph);
    
    // Execute graph
    executor->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    executor->Stop();
    executor->Join();
    
    // Validate: TestIntSinkNode received all 5 values (0,1,2,3,4)
    // Validate: CompletionNode received completion signal
    // Validate: No data loss or duplicates
}
```

### Scenario 2: Diamond Topology Validation
```cpp
TEST_F(ProducerGraphTopologyTest, DiamondComplexInt_ComplexPaths) {
    auto graph = ProducerTopologyBuilder::BuildDiamondComplexInt();
    auto executor = CreateExecutor(graph);
    
    executor->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    executor->Stop();
    executor->Join();
    
    // Validate: Both interior nodes processed data
    // Validate: Merge combined data from both paths
    // Validate: Sink received combined data (count = 10, not 5)
    // Validate: Ordering preserved despite merge
}
```

### Scenario 3: Multi-Path Sequential Validation
```cpp
TEST_F(ProducerGraphTopologyTest, MultiPathSequentialInt_LongChain) {
    auto graph = ProducerTopologyBuilder::BuildMultiPathSequentialInt();
    auto executor = CreateExecutor(graph);
    
    executor->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    executor->Stop();
    executor->Join();
    
    // Validate: All 3 interior nodes processed sequentially
    // Validate: Output identical to input (passthrough chain)
    // Validate: Timing respected at each hop
}
```

---

## 12. Summary

**Approach**: Create a **parallel** set of producer-based topologies (ProducerGraphTopologies) that:
- Follow the same structural patterns as TestGraphTopologies
- Use ProducerTestNodes (int/double producers, sinks, interiors)
- Include explicit CompletionNode connections (requirement of dual-port producers)
- Provide 12 topology variants (6 patterns × 2 data types)
- Enable comprehensive testing of producer-based graph execution

**New Components Required**:
1. 6 new test nodes (interior, merge, split variants for int/double)
2. ProducerGraphTopologies.hpp/cpp with 12 topology builders
3. test_producer_graph_topologies.cpp with 36+ tests

**Total Estimated Code**: 900-1300 LOC across 3 files

**Key Benefit**: ProducerTestNodes (already unit tested with 38 tests) can now be validated in realistic graph execution contexts, confirming end-to-end data flow, timing, and completion signal propagation.
