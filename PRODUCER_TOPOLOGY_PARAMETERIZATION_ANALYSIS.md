# Producer-Based Topology Parameterization Analysis

**Date**: May 29, 2026  
**Objective**: Enable TestGraphTopologies to work with DataProducer source nodes (TestIntProducer, TestDoubleProducer) in addition to SourceTestNode

---

## 1. Current Architecture

### Existing Topologies (5 topologies)
| Topology | Structure | Source Node | Data Type | Use Case |
|----------|-----------|------------|-----------|----------|
| Minimal | Source → Sink | SourceTestNode | Message | Basic connectivity |
| Linear Sequential | Source → Interior → Sink | SourceTestNode | Message | Sequential processing |
| Simple Merge | 2×Source → Merge → Sink | SourceTestNode | Message | Multi-input combination |
| Simple Split | Source → Split → 2×Sink | SourceTestNode | Message | Single-to-multiple replication |
| Diamond Complex | Source → Split → 2×Interior → Merge → Sink | SourceTestNode | Message | Complex parallel paths |
| Multi-Path Sequential | Source → Int1 → Int2 → Int3 → Sink | SourceTestNode | Message | Long sequential chains |

### Key Properties
- **SourceTestNode**: Extends `NamedSourceNode<SourceTestNode, Message>`
  - Output port 0: produces `Message` type
  - Single output port

- **TestIntProducer**: Extends `DataProducerWithNotification<TestIntProducer, SimpleIntGenerator, int, int, ...>`
  - Output port 0: produces `int` type
  - Output port 1: produces `CompletionSignal` type
  - Dual-port architecture

- **TestDoubleProducer**: Extends `DataProducerWithNotification<TestDoubleProducer, SimpleDoubleGenerator, double, double, ...>`
  - Output port 0: produces `double` type
  - Output port 1: produces `CompletionSignal` type
  - Dual-port architecture

---

## 2. Parameterization Dimensions

### 2.1 Source Node Type Parameter
```cpp
// Current: Fixed to SourceTestNode
// Parameterized options:
- SourceTestNode           // Message output
- TestIntProducer         // int + CompletionSignal outputs
- TestDoubleProducer      // double + CompletionSignal outputs
```

### 2.2 Data Type Parameter
```cpp
// Current: Fixed to Message
// Parameterized options:
- graph::message::Message  // For SourceTestNode-based topologies
- int                      // For TestIntProducer-based topologies
- double                   // For TestDoubleProducer-based topologies
```

### 2.3 Sink/Interior Node Compatibility
| Source Data Type | Sink Type Needed | Interior Type Needed | Completion Handling |
|------------------|-----------------|-------------------|-------------------|
| Message | SinkTestNode (as-is) | InteriorTestNode (as-is) | Not applicable |
| int | TestIntSinkNode | IntTransformNode (new?) | CompletionNode on port 1 |
| double | TestDoubleSinkNode | DoubleTransformNode (new?) | CompletionNode on port 1 |

---

## 3. Parameterizable Topology Signatures

### 3.1 Template Parameters Required
```cpp
template<typename SourceNodeType, typename DataType>
class ProducerTopologyBuilder {
    // Template parameters:
    // - SourceNodeType: SourceTestNode | TestIntProducer | TestDoubleProducer
    // - DataType: Message | int | double
    
    // Derived types:
    // - SinkNodeType: SinkTestNode | TestIntSinkNode | TestDoubleSinkNode
    // - InteriorNodeType: InteriorTestNode | (future) IntTransformNode | DoubleTransformNode
    // - EdgeSourcePort: 0 (all)
    // - EdgeSinkPort: 0 (all)
    // - DataPortForCompletion: 1 (DataProducer types only)
};
```

### 3.2 Topology Variants
Each topology can be instantiated with:
```
- Variant A (Message-based):     SourceTestNode → Message
- Variant B (Int-based):          TestIntProducer → int
- Variant C (Double-based):       TestDoubleProducer → double
```

Example for Minimal topology:
```cpp
// Variant A
BuildMinimalGraph<SourceTestNode, Message>();
// Creates: SourceTestNode(Message) → SinkTestNode(Message)

// Variant B
BuildMinimalGraph<TestIntProducer, int>();
// Creates: TestIntProducer(int + CompletionSignal) → TestIntSinkNode(int)
//          With CompletionSignal → CompletionNode

// Variant C
BuildMinimalGraph<TestDoubleProducer, double>();
// Creates: TestDoubleProducer(double + CompletionSignal) → TestDoubleSinkNode(double)
//          With CompletionSignal → CompletionNode
```

---

## 4. Design Challenges & Solutions

### Challenge 4.1: Dual-Port Architecture of DataProducers
**Problem**: DataProducers have 2 output ports (data + completion), but SourceTestNode has 1  
**Solution Options**:
```
Option A: Always create CompletionNode for port 1
Option B: Make completion port optional (configurable per topology)
Option C: Separate topology templates: BaseTopology vs CompletionAwareTopology

Recommended: Option A (automatic CompletionNode)
- Ensures completion signal is always consumed
- Prevents graph hanging on completion
- Cleaner API (user doesn't manage completion)
```

### Challenge 4.2: Interior/Merge/Split Node Flexibility
**Problem**: Current Interior/Merge/Split nodes expect Message type  
**Solution Options**:
```
Option A: Keep specialized versions for int/double (TestIntProducerTopologies.cpp)
Option B: Create generic templated versions of Interior/Merge/Split
Option C: Use adapter nodes to convert between types

Recommended: Option A + C
- Option A: Specialized test nodes in new file per data type
- Option C: Adapters for cross-type graph experiments
```

### Challenge 4.3: Graph Manager & Edge Type System
**Problem**: AddEdge<SrcNodeType, SrcPort, DstNodeType, DstPort>() requires compile-time type safety  
**Solution**:
```cpp
// Current approach works: template specialization handles different types
// Example:
graph->AddEdge<TestIntProducer, 0, TestIntSinkNode, 0>(...);  // int data
graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(...);   // completion

// Pattern is consistent across all types - no changes needed to GraphManager
```

---

## 5. Implementation Strategy

### Phase 5.1: Create Producer-Specific Test Nodes File
**New File**: `libgraph/test/include/test/ProducerTopologies.hpp`

**Content**:
```cpp
namespace test {

// ====================================================================
// Int-Based Test Nodes (for int data type)
// ====================================================================

template<typename DataType>
class IntTransformNode : public NamedInteriorNode<...> {
    // Processes int data, passes through
};

template<typename DataType>
class IntMergeNode : public NamedInteriorNode<...> {
    // Merges two int streams
};

template<typename DataType>  
class IntSplitNode : public NamedInteriorNode<...> {
    // Splits int stream to two outputs
};

// ====================================================================
// Double-Based Test Nodes
// ====================================================================

template<typename DataType>
class DoubleTransformNode : public NamedInteriorNode<...> {
    // Processes double data
};

// ... similar for double variants

} // namespace test
```

### Phase 5.2: Parameterized Topology Builder
**File**: Modified `libgraph/test/unit/TestGraphTopologies.cpp`

**New Structure**:
```cpp
namespace test {

template<typename SourceNodeType, typename DataType>
class ProducerTopologyBuilder {
public:
    // Topology factories - template specializations per variant
    
    template<>
    static std::shared_ptr<GraphManager> 
    BuildMinimalGraph<SourceTestNode, Message>();
    
    template<>
    static std::shared_ptr<GraphManager> 
    BuildMinimalGraph<TestIntProducer, int>();
    
    template<>
    static std::shared_ptr<GraphManager>
    BuildMinimalGraph<TestDoubleProducer, double>();
    
    // ... similar for other topologies
};

} // namespace test
```

### Phase 5.3: Producer-Based Topology Tests
**New File**: `libgraph/test/unit/test_producer_topologies.cpp`

**Test Structure**:
```cpp
// Parametrized test suite
using ProducerTopologyTypes = ::testing::Types<
    std::pair<test::TestIntProducer, int>,
    std::pair<test::TestDoubleProducer, double>
>;

template<typename T>
class ProducerTopologyTest : public ::testing::Test {};

TYPED_TEST_SUITE(ProducerTopologyTest, ProducerTopologyTypes);

TYPED_TEST(ProducerTopologyTest, Minimal_DataFlow) {
    using SourceType = typename TypeParam::first_type;
    using DataType = typename TypeParam::second_type;
    
    auto graph = ProducerTopologyBuilder<SourceType, DataType>
        ::BuildMinimalGraph();
    
    // Execute and validate
}

TYPED_TEST(ProducerTopologyTest, Linear_SequentialProcessing) { ... }
TYPED_TEST(ProducerTopologyTest, Merge_CombineStreams) { ... }
TYPED_TEST(ProducerTopologyTest, Split_ReplicateData) { ... }
TYPED_TEST(ProducerTopologyTest, Diamond_ComplexPaths) { ... }
TYPED_TEST(ProducerTopologyTest, MultiPath_LongChain) { ... }
```

---

## 6. Topology Parameterization Mapping

### Minimal Graph (Simplest)
```
┌─────────────────────────────────┐
│ Minimal Graph Variants          │
└─────────────────────────────────┘

Variant A (Message):
  SourceTestNode
        ↓ (Message, port 0)
    SinkTestNode

Variant B (int):
  TestIntProducer
        ↓ (int, port 0)
    TestIntSinkNode
        
  TestIntProducer
        ↓ (CompletionSignal, port 1)
    CompletionNode

Variant C (double):
  TestDoubleProducer
        ↓ (double, port 0)
    TestDoubleSinkNode
        
  TestDoubleProducer
        ↓ (CompletionSignal, port 1)
    CompletionNode
```

### Linear Sequential (Medium Complexity)
```
┌──────────────────────────────────────────┐
│ Linear Sequential Graph Variants         │
└──────────────────────────────────────────┘

Variant A (Message):
  SourceTestNode
        ↓ (Message, port 0)
    InteriorTestNode
        ↓ (Message, port 0)
    SinkTestNode

Variant B (int):
  TestIntProducer
        ↓ (int, port 0)
    IntTransformNode
        ↓ (int, port 0)
    TestIntSinkNode
        
  TestIntProducer
        ↓ (CompletionSignal, port 1)
    CompletionNode (feeds IntTransformNode completion port?)

Variant C (double):
  TestDoubleProducer
        ↓ (double, port 0)
    DoubleTransformNode
        ↓ (double, port 0)
    TestDoubleSinkNode
        
  TestDoubleProducer
        ↓ (CompletionSignal, port 1)
    CompletionNode
```

### Diamond Complex (High Complexity)
```
┌──────────────────────────────────────────────────────┐
│ Diamond Complex Graph Variants                       │
└──────────────────────────────────────────────────────┘

Variant B (int):
  TestIntProducer(port 0: int, port 1: CompletionSignal)
        ↓
    IntSplitNode
      ↙     ↘
  IntTransform1  IntTransform2
      ↘     ↙
    IntMergeNode
        ↓
    TestIntSinkNode
        
  TestIntProducer(port 1: CompletionSignal)
        ↓
    CompletionNode
```

---

## 7. Type-Safe Edge Addition Strategy

### Current Pattern (Works for all types)
```cpp
// For Message-based graphs
graph->AddEdge<SourceTestNode, 0, SinkTestNode, 0>(
    src_wrapper, sink_wrapper, buffer_size);

// For int-based graphs
graph->AddEdge<TestIntProducer, 0, TestIntSinkNode, 0>(
    src_wrapper, sink_wrapper, buffer_size);

// For completion signal
graph->AddEdge<TestIntProducer, 1, CompletionNode, 0>(
    producer_wrapper, completion_wrapper, buffer_size);
```

**Key Insight**: Graph type system is generic enough - no changes needed to AddEdge template

---

## 8. Data Flow Comparison

### Message-Based (Current)
```
SourceTestNode (deterministic message generation)
    ↓
[Message: {id, timestamp, data}]
    ↓
InteriorTestNode (message pass-through/transform)
    ↓
SinkTestNode (consumes and validates messages)
```

### Int-Based Producer (New)
```
TestIntProducer (timed sample generation from SimpleIntGenerator)
    ├─ Port 0: int value
    └─ Port 1: CompletionSignal
    
Port 0 (int): 0 → 1 → 2 → 3 → 4 (sequentially with timing)
    ↓
[IntTransformNode or TestIntSinkNode]
    
Port 1 (CompletionSignal): [one signal at end]
    ↓
[CompletionNode]
```

### Double-Based Producer (New)
```
TestDoubleProducer (timed sample generation from SimpleDoubleGenerator)
    ├─ Port 0: double value  
    └─ Port 1: CompletionSignal
    
Port 0 (double): 0.0 → 1.0 → 2.0 → ... with timing
    ↓
[DoubleTransformNode or TestDoubleSinkNode]
    
Port 1 (CompletionSignal): [one signal at end]
    ↓
[CompletionNode]
```

---

## 9. Implementation Priority

### Priority 1 (Minimum for Functionality)
1. ✅ ProducerTestNodes.hpp exists (generators, producers, sinks, completion)
2. Create ProducerTopologies.hpp with:
   - Parameterized topology builder templates
   - Type trait specializations for int/double
3. Create test_producer_topologies.cpp with:
   - Typed tests for each topology variant
   - Completion signal validation

### Priority 2 (Extended Functionality)
1. IntTransformNode, DoubleTransformNode for interior processing
2. IntMergeNode, DoubleMergeNode for merge operations
3. IntSplitNode, DoubleSplitNode for split operations
4. Cross-type adapters (Message↔int, Message↔double)

### Priority 3 (Advanced)
1. Performance benchmarks comparing Message vs int/double topologies
2. Stress tests with large data volumes
3. Timing compliance tests for producer sampling intervals

---

## 10. Summary of Changes Required

| Component | Current | Parameterized | Effort |
|-----------|---------|---------------|--------|
| ProducerTestNodes.hpp | ✅ Exists | No change | - |
| TestGraphTopologies.cpp | Fixed to Message | Add templates | Medium |
| ProducerTopologies.hpp | N/A | Create new | Medium |
| test_producer_topologies.cpp | N/A | Create new | High |
| Interior/Merge/Split nodes | Message-only | Add int/double variants | Medium |

---

## 11. Example Implementation Sketch

```cpp
// ProducerTopologies.hpp

namespace test {

// Type traits for node/data type mapping
template<typename DataType>
struct TopologyTraits {};

template<>
struct TopologyTraits<graph::message::Message> {
    using SourceNode = SourceTestNode;
    using SinkNode = SinkTestNode;
    using InteriorNode = InteriorTestNode;
    using MergeNode = MergeTestNode;
    using SplitNode = SplitTestNode;
};

template<>
struct TopologyTraits<int> {
    using SourceNode = TestIntProducer;
    using SinkNode = TestIntSinkNode;
    using InteriorNode = IntTransformNode;
    using MergeNode = IntMergeNode;
    using SplitNode = IntSplitNode;
    using CompletionSink = CompletionNode;
};

// Topology builder with specializations
template<typename DataType>
class ProducerTopologyBuilder {
    using Traits = TopologyTraits<DataType>;
    
public:
    static std::shared_ptr<GraphManager> BuildMinimalGraph() {
        // Implementation specializes on DataType
        // Creates appropriate source → sink with completion handling if needed
    }
    
    static std::shared_ptr<GraphManager> BuildLinearSequential() { ... }
    static std::shared_ptr<GraphManager> BuildDiamond() { ... }
    // ... other topologies
};

} // namespace test
```

---

## 12. Validation Strategy

### Unit Test Coverage
- ✅ Generator tests (test_data_producers.cpp - 16 tests)
- ✅ Producer tests (test_data_producers.cpp - 22 tests)  
- 🔄 Topology integration tests (test_producer_topologies.cpp - ~30 tests planned)
  - Minimal topology: 3 variants
  - Linear: 3 variants
  - Merge: 3 variants
  - Split: 3 variants
  - Diamond: 3 variants
  - Multi-Path: 3 variants
  - **Total: ~18 topology tests × 2 variations (Message vs Producer) = ~36 tests**

### Test Scenarios per Topology
1. ✅ Data flows correctly through all nodes
2. ✅ Completion signals are delivered (for producer variants)
3. ✅ No data loss or duplication
4. ✅ Correct ordering (FIFO) maintained
5. ✅ Timing compliance (sampling intervals respected for producers)

---

## Conclusion

**Parameterization enables**:
- Reusing topology patterns across multiple data types
- Testing DataProducer framework in realistic graph scenarios
- Validating completion signal propagation end-to-end
- Comparing Message-based vs Producer-based performance

**Key architectural insight**: The existing GraphManager's type system is flexible enough to support this parameterization without modification. Only test node creation and topology builder logic need updates.
