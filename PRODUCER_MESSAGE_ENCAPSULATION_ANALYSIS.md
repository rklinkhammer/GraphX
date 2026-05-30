# ProducerTestNodes Message Encapsulation Analysis

**Date**: May 29, 2026  
**Objective**: Analyze feasibility of wrapping ProducerTestNodes int/double data into Message type to match AdvancedTestNodes pattern

---

## 1. Current Architecture Comparison

### AdvancedTestNodes Pattern
```cpp
SourceTestNode (produces Message)
  ├─ Template: NamedSourceNode<SourceTestNode, graph::message::Message>
  ├─ Produces: graph::message::Message msg(count_);
  │           where count_ is type-erased inside Message
  └─ Topology: Message → InteriorTestNode → SinkTestNode
```

### ProducerTestNodes Pattern
```cpp
TestIntProducer (produces int)
  ├─ Template: DataProducerWithNotification<TestIntProducer, ..., int, int, ...>
  ├─ Produces: raw int value (0, 1, 2, 3, 4)
  └─ Topology: int → TestIntSinkNode (or future IntInteriorNode)

TestDoubleProducer (produces double)
  ├─ Template: DataProducerWithNotification<TestDoubleProducer, ..., double, double, ...>
  ├─ Produces: raw double value (0.0, 1.0, 2.0, ...)
  └─ Topology: double → TestDoubleSinkNode
```

---

## 2. Message Type Overview

### What is Message?
```cpp
class Message {
    // Type-erased container using Small String Optimization (SSO)
    
    template<typename T>
    constexpr Message(const T& value) {
        storage_.emplace<T>(value);
    }
    
    template<typename T>
    constexpr const T& get() const {
        // Retrieve type-erased value
    }
};
```

**Key Properties**:
- ✅ Type-erased: Can hold any value (int, double, std::string, custom types)
- ✅ SSO: Small values stored inline (no heap allocation)
- ✅ Copyable/Moveable: Full semantics
- ✅ Used in AdvancedTestNodes by wrapping raw values: `Message msg(count_)`

---

## 3. Encapsulation Approaches

### Approach A: Direct Value Wrapping (Simplest)

**Concept**: Wrap raw data values directly in Message, maintaining current semantics

**Implementation**:
```cpp
// Current TestIntProducer
class TestIntProducer : public DataProducerWithNotification<
    TestIntProducer, SimpleIntGenerator, 
    int, int,                           // Raw int, int
    CompletionSignal, NodeClassification> { ... };

// Proposed MessageProducerInt
class MessageProducerInt : public DataProducerWithNotification<
    MessageProducerInt, SimpleIntGenerator,
    Message,                            // Wrapped in Message
    Message,                            // Notification also Message? Or keep CompletionSignal?
    CompletionSignal, NodeClassification> {
    
    Message CreateData(int value) override {
        return Message(value);  // Type-erase int into Message
    }
};
```

**Pros**:
- ✅ Minimal code changes
- ✅ Matches AdvancedTestNodes producer pattern
- ✅ Single topology builder can handle both Message-based nodes

**Cons**:
- ❌ Loses type safety at compile time (Message is type-erased)
- ❌ Requires `.get<int>()` at sink side to retrieve value
- ❌ Creates two parallel node implementations (TestIntProducer + MessageProducerInt)

---

### Approach B: Message-First Producer Generators (Moderate)

**Concept**: Create generators that produce Message-wrapped data, restructure producers

**Implementation**:
```cpp
// New: Message-producing generators
class SimpleIntMessageGenerator : public DataGeneratorBase<Message> {
private:
    int counter_;
    int max_count_;
    
public:
    std::optional<Message> Produce(size_t) override {
        if (counter_ >= max_count_) {
            return std::nullopt;
        }
        return Message(counter_++);  // Wrap int in Message
    }
};

// Producer using Message-based generator
class TestIntMessageProducer : public DataProducerWithNotification<
    TestIntMessageProducer, SimpleIntMessageGenerator,
    Message, Message,                   // Data type is Message
    CompletionSignal, NodeClassification> { ... };
```

**Pros**:
- ✅ Generators responsible for type-erasing
- ✅ Clean separation: generator handles wrapping, producer handles timing
- ✅ Can coexist with raw-type producers

**Cons**:
- ⚠️ Duplicates generator code (SimpleIntGenerator + SimpleIntMessageGenerator)
- ⚠️ Adds complexity to generator hierarchy
- ⚠️ Message creation overhead per sample

---

### Approach C: Parameterized Producer Nodes (Complex but Flexible)

**Concept**: Create templated producer nodes that accept data type as parameter

**Implementation**:
```cpp
// Generic parameterized producer
template<typename GeneratorType, typename DataType, typename OutputType>
class ParameterizedDataProducer : public DataProducerWithNotification<
    ParameterizedDataProducer, GeneratorType, 
    OutputType, OutputType,             // Output type is customizable
    CompletionSignal, NodeClassification> { 
    
    // If OutputType == Message: wrap DataType
    // If OutputType == DataType: use as-is
};

// Specialization: auto-wrapping
template<typename GeneratorType, typename DataType>
class ParameterizedDataProducer<GeneratorType, DataType, Message>
    : public DataProducerWithNotification<...> {
    
    Message CreateNotification() override {
        // Wrap DataType in Message
    }
};

// Usage
auto raw_producer = ParameterizedDataProducer<SimpleIntGenerator, int, int>();
auto message_producer = ParameterizedDataProducer<SimpleIntGenerator, int, Message>();
```

**Pros**:
- ✅ Single producer class handles multiple output types
- ✅ Type safety at instantiation time
- ✅ Highly reusable

**Cons**:
- ❌ Complex template specialization
- ❌ Difficult to maintain
- ❌ Compiler errors hard to debug

---

## 4. Sink Node Compatibility Analysis

### Current Structure
```cpp
// TestIntSinkNode - receives raw int
class TestIntSinkNode : public NamedSinkNode<TestIntSinkNode, int> {
    bool Consume(const int& value, std::integral_constant<std::size_t, 0>) override {
        received_values_.push_back(value);
    }
};

// SinkTestNode - receives Message
class SinkTestNode : public NamedSinkNode<SinkTestNode, Message> {
    bool Consume(const Message& msg, std::integral_constant<std::size_t, 0>) override {
        received_messages_.push_back(msg);
    }
};
```

### Compatibility Challenge

**Current**:
- Producer outputs int → Sink expects int ✅
- Producer outputs Message → Sink expects Message ✅
- Producer outputs int → Sink expects Message ❌ Type mismatch

**Required for Encapsulation**:
```cpp
// Option 1: Dual-interface sink (supports both)
class FlexibleIntSinkNode : public NamedSinkNode<FlexibleIntSinkNode, Message> {
    bool Consume(const Message& msg, ...) override {
        try {
            const int& value = msg.get<int>();
            received_values_.push_back(value);
        } catch (const std::bad_cast&) {
            // Handle type mismatch
        }
    }
};

// Option 2: Type traits for auto-unwrapping
template<typename ValueType>
class UnwrappingSinkNode : public NamedSinkNode<UnwrappingSinkNode, Message> {
    bool Consume(const Message& msg, ...) override {
        const ValueType& value = msg.get<ValueType>();
        received_values_.push_back(value);
    }
};
```

---

## 5. Impact on ProducerGraphTopologies

### If Approach A (Direct Wrapping) Chosen:
```cpp
// Single topology for both patterns
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildMinimal() {
    // Generic: can accept any producer that outputs Message
    
    auto producer = std::make_shared<MessageProducerInt>();
    auto sink = std::make_shared<UnwrappingSinkNode<int>>();
    
    graph->AddEdge<MessageProducerInt, 0, UnwrappingSinkNode<int>, 0>(...);
}

// Advantage: Single topology definition, multiple instantiations
```

### If Approach B (Message-First Generators) Chosen:
```cpp
// Need explicit message variants for each topology

auto producer_int = std::make_shared<TestIntMessageProducer>();
auto producer_double = std::make_shared<TestDoubleMessageProducer>();

// Topologies written to handle Message specifically
```

---

## 6. Data Layout Considerations

### Inside Message Container (Current AdvancedTestNodes)
```cpp
Message msg(count_);  // Wraps int value directly
// Storage: [type_info | count_value (int)]
//                     └─ Where: int as raw value in SSO buffer
```

### Design Decision: What Should Be Wrapped?

**Option 1**: Single value
```cpp
Message msg(value);          // int or double wrapped directly
// Pros: Minimal, matches AdvancedTestNodes
// Cons: No metadata, timing info lost in message
```

**Option 2**: Structured data container
```cpp
struct SensorData {
    int value;
    std::chrono::steady_clock::time_point timestamp;
    int sample_id;
};
Message msg(SensorData{...});  // Richer data
// Pros: Carries metadata through graph
// Cons: More complex, breaks AdvancedTestNodes symmetry
```

**Option 3**: Message wrapper with parallel timing info
```cpp
// Keep timing separate, only wrap value
Message msg(value);
// Timing tracked in producer side-channel (like TestIntSinkNode does)
// Pros: Clean data flow, timing optional
// Cons: Requires parallel infrastructure
```

**Recommended**: Option 1 (matches AdvancedTestNodes pattern exactly)

---

## 7. Recommended Implementation: Approach A

### Phase 1: Create Message-Producing Nodes

**File**: `ProducerTestNodes.hpp` - add new node classes

```cpp
/**
 * @brief Producer nodes that emit Message instead of raw types
 * Allows integration with topologies designed for AdvancedTestNodes
 */

// MessageProducerInt - int data wrapped in Message
class MessageProducerInt : public DataProducerWithNotification<
    MessageProducerInt,
    SimpleIntGenerator,                 // Raw generator still produces int
    Message,                             // But output is wrapped in Message
    Message,                             // DataType template param = Message
    CompletionSignal, NodeClassification,
    NodeClassification::IntProducer> {
    
    // Constructor same as TestIntProducer
    MessageProducerInt()
        : DataProducerWithNotification(...) {
        SetName("MessageProducerInt");
    }
    
    // Override to wrap int in Message
    Message ProcessGeneratedData(int raw_value) {
        return Message(raw_value);  // Type-erase into Message
    }
};

// MessageProducerDouble - double data wrapped in Message
class MessageProducerDouble : public DataProducerWithNotification<
    MessageProducerDouble,
    SimpleDoubleGenerator,
    Message, Message,
    CompletionSignal, NodeClassification,
    NodeClassification::DoubleProducer> { ... };
```

### Phase 2: Create Unwrapping Sinks

**File**: `ProducerTestNodes.hpp` - add sink variants

```cpp
// MessageIntSinkNode - receives Message, unwraps int
class MessageIntSinkNode : public NamedSinkNode<MessageIntSinkNode, Message> {
public:
    bool Consume(const Message& msg, std::integral_constant<std::size_t, 0>) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        try {
            const int& value = msg.get<int>();
            received_values_.push_back(value);
            // Timing tracking same as TestIntSinkNode
        } catch (const std::bad_cast&) {
            error_ = true;
            return false;
        }
        return true;
    }
    
    // Same interface as TestIntSinkNode but works with Message
    size_t GetReceivedCount() const { return received_values_.size(); }
    const std::vector<int>& GetReceivedValues() const { return received_values_; }
    // ...
};

// MessageDoubleSinkNode - receives Message, unwraps double
class MessageDoubleSinkNode : public NamedSinkNode<MessageDoubleSinkNode, Message> { ... };
```

### Phase 3: Unified Topologies

**File**: `ProducerGraphTopologies.cpp` - simplify topology builders

```cpp
// Can now use Message nodes directly
std::shared_ptr<GraphManager> ProducerTopologyBuilder::BuildMinimalMessage() {
    auto graph = std::make_shared<GraphManager>();
    
    auto producer = std::make_shared<MessageProducerInt>();
    auto sink = std::make_shared<MessageIntSinkNode>();
    auto completion = std::make_shared<CompletionNode>();
    
    graph->AddNode(producer);
    graph->AddNode(sink);
    graph->AddNode(completion);
    
    // Type-safe edges with Message
    graph->AddEdge<MessageProducerInt, 0, MessageIntSinkNode, 0>(
        producer, sink, buffer_size);
    graph->AddEdge<MessageProducerInt, 1, CompletionNode, 0>(
        producer, completion, buffer_size);
    
    return graph;
}
```

---

## 8. Coexistence Strategy

### Keep Both Parallel Implementations

**ProducerTestNodes.hpp** would contain:

```
Generators (remain unchanged):
  ✅ SimpleIntGenerator → int
  ✅ SimpleDoubleGenerator → double
  ✅ RandomIntGenerator → int

Raw-Type Producers (keep for unit tests):
  ✅ TestIntProducer → int
  ✅ TestDoubleProducer → double
  ✅ FailingProducerNode → int

Raw-Type Sinks (keep for unit tests):
  ✅ TestIntSinkNode ← int
  ✅ TestDoubleSinkNode ← double

NEW Message-Based Producers:
  🆕 MessageProducerInt → Message (wraps int)
  🆕 MessageProducerDouble → Message (wraps double)

NEW Message-Based Sinks:
  🆕 MessageIntSinkNode ← Message (unwraps int)
  🆕 MessageDoubleSinkNode ← Message (unwraps double)

NEW Interior/Merge/Split (for producer topologies):
  🆕 IntProducerInteriorNode
  🆕 DoubleProducerInteriorNode
  🆕 IntMergeNode, DoubleMergeNode
  🆕 IntSplitNode, DoubleSplitNode
```

**Benefits**:
- ✅ Raw-type producers for unit testing (already working, 38 tests)
- ✅ Message producers for topology testing (matches AdvancedTestNodes)
- ✅ No breaking changes to existing tests
- ✅ Maximum flexibility for users

---

## 9. Migration Path: Gradual Adoption

### Step 1: Add Message Producers (No changes to existing)
- Create MessageProducerInt, MessageProducerDouble
- Create MessageIntSinkNode, MessageDoubleSinkNode
- Existing test_data_producers.cpp continues unchanged

### Step 2: Create Message-Based Topologies
- New ProducerGraphTopologies using Message nodes
- New test_producer_graph_topologies.cpp

### Step 3: Optional Future Consolidation
- If Message-based approach proves superior, gradually phase out raw-type producers
- But keep as long as unit tests need them

---

## 10. Comparison Table

| Aspect | Raw Types (Current) | Message-Wrapped (Proposed) |
|--------|-------------------|--------------------------|
| **Produced Type** | int, double | Message (type-erased) |
| **Type Safety** | Compile-time checked | Runtime (`.get<T>()`) |
| **Sink Compatibility** | Requires matching type | Flexible (unwrapping) |
| **Memory Overhead** | None | Message SSO (~64B) |
| **Topology Integration** | Specialized builders | Reusable Message builders |
| **AdvancedTestNodes Pattern Match** | ❌ No | ✅ Yes |
| **Unit Test Effort** | ✅ Existing 38 tests | + new tests for unwrapping |
| **Total Sink Types Needed** | 3 (Int, Double, Completion) | 5 (+ Message unwrappers) |

---

## 11. Code Example: Full Encapsulation

### Before (Raw Types)
```cpp
// Unit test
TEST_F(DataProducerTest, TestIntProducer_Produces_Sequence) {
    auto producer = std::make_shared<TestIntProducer>();
    auto sink = std::make_shared<TestIntSinkNode>();
    
    // Connected: int → sink expects int ✅
}

// Topology using ProducerGraphTopologies
auto graph = ProducerTopologyBuilder::BuildMinimalInt();
// Problem: Can't reuse TestGraphTopologies pattern for Message nodes
```

### After (Message-Wrapped)
```cpp
// Unit test - raw types still work
TEST_F(DataProducerTest, TestIntProducer_Produces_Sequence) {
    auto producer = std::make_shared<TestIntProducer>();
    auto sink = std::make_shared<TestIntSinkNode>();
    
    // Connected: int → sink expects int ✅ (unchanged)
}

// NEW: Unit test for Message-wrapped
TEST_F(DataProducerTest, MessageProducerInt_Wraps_Values) {
    auto producer = std::make_shared<MessageProducerInt>();
    auto sink = std::make_shared<MessageIntSinkNode>();
    
    // Connected: Message(int) → sink unwraps int ✅
    
    // Verify: msg.get<int>() == expected_value
}

// Topology using ProducerGraphTopologies
auto graph = ProducerTopologyBuilder::BuildMinimalMessage();
// Benefit: Symmetric with AdvancedTestNodes pattern
```

---

## 12. Summary: Feasibility Assessment

### Is Encapsulation Feasible? ✅ **YES**

**Recommended Approach**: **Approach A (Direct Value Wrapping)**
- Simplest to implement (minimal code changes)
- Matches AdvancedTestNodes pattern exactly
- Maintains backward compatibility
- Clear separation: raw-type tests + message-type topologies

**Implementation Effort**:
1. Phase 1: 4 new node classes (Message producers + unwrapping sinks) = ~200 LOC
2. Phase 2: Simplified topology builders = ~100 LOC net (reused from raw version)
3. Phase 3: 12 new topology tests = ~300 LOC

**Total**: ~600 LOC (vs. 900-1300 without message wrapping)

**Key Advantage**: Enables **unified topology builders** that work with both:
- AdvancedTestNodes (produces Message with generic source)
- ProducerTestNodes (produces Message via wrapper, tracking timing/completion)

This creates architectural symmetry: **both test node systems emit Message, graphs are agnostic to source producer type**.

---

## 13. Recommended Next Steps

1. ✅ Create MessageProducerInt, MessageProducerDouble in ProducerTestNodes.hpp
2. ✅ Create MessageIntSinkNode, MessageDoubleSinkNode in ProducerTestNodes.hpp
3. ✅ Create unified ProducerGraphTopologies.cpp using Message nodes
4. ✅ Create test_producer_graph_topologies.cpp with 12 message-based topology tests
5. ✅ Verify all 38 existing unit tests still pass
6. ⚠️ Consider: create generic Message-based adapter for existing interior/merge/split nodes

---

## Appendix: Type-Erasure Cost Analysis

**Memory**:
- Message with SSO: ~64 bytes overhead (type info + storage)
- int value: 4 bytes (fits in SSO, no heap allocation)
- double value: 8 bytes (fits in SSO, no heap allocation)
- Total per sample: ~68 bytes (negligible for test purposes)

**Performance**:
- Type erasure: minimal (~3-5 CPU cycles for construction)
- Type recovery (`.get<int>()`): minimal (~1-2 CPU cycles with likely cache hit)
- Negligible impact for timing-based producer (samples at 100μs intervals)

**Conclusion**: Message wrapping introduces **no meaningful performance overhead** for test scenarios.
