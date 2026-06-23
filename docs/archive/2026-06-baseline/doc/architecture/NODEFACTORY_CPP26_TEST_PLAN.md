# GraphX Node Testing - C++26 Enhancement Plan

**Analysis Date**: May 10, 2026  
**Focus**: Test coverage gaps and C++26 feature integration recommendations

---

## Executive Summary

The GraphX NodeFactory and node system have **complete implementations** but suffer from **critical test coverage gaps**:

- ✅ **Implementation**: 100% complete (7 node types, 3 factory implementations)
- ❌ **Test Coverage**: ~20% (only basic plugin lifecycle tested)
- 🎯 **Immediate Priority**: Lifecycle state machine validation tests
- 🎯 **C++26 Opportunity**: Use reflection + concepts for auto-generated test suites

---

## Part 1: Critical Test Gaps

### Gap 1: Lifecycle State Machine (HIGHEST PRIORITY)

**Missing Tests**: Double-start prevention, invalid transitions, error recovery

```cpp
// MISSING TEST: Double-start enforcement
TEST_CASE("SourceNode rejects second Start without Join") {
    auto node = std::make_shared<ConcreteSourceNode>();
    REQUIRE(node->Init());
    REQUIRE(node->Start());           // First start succeeds
    REQUIRE(!node->Start());          // Second start fails (THIS TEST MISSING)
    REQUIRE(node->GetLifecycleState() == LifecycleState::Started);
    node->Stop();
    node->Join();
    REQUIRE(node->Start());           // Succeeds after Join (THIS TEST MISSING)
}

// MISSING TEST: Invalid state transitions
TEST_CASE("Node rejects Start before Init") {
    auto node = std::make_shared<ConcreteSourceNode>();
    REQUIRE(!node->Start());          // Should fail
    REQUIRE(node->GetLifecycleState() == LifecycleState::Uninitialized);
}

TEST_CASE("Node allows multiple Stop calls (idempotent)") {
    auto node = std::make_shared<ConcreteSourceNode>();
    REQUIRE(node->Init());
    REQUIRE(node->Start());
    node->Stop();
    node->Stop();  // Should be safe
    node->Stop();  // Should be safe
    node->Join();
}
```

**Implementation Effort**: 2-3 hours  
**Impact**: Prevents hard-to-debug state machine bugs

---

### Gap 2: JoinWithTimeout Semantics (HIGHEST PRIORITY)

**Missing Tests**: Timeout expiration, hanging threads, partial completion

```cpp
// MISSING TEST: Timeout with slow thread
TEST_CASE("JoinWithTimeout detects slow port thread") {
    auto node = std::make_shared<SlowProducerNode>();
    node->Init();
    node->Start();
    
    // Port thread is deliberately slow (produces every 1 second)
    auto start = std::chrono::high_resolution_clock::now();
    bool joined = node->JoinWithTimeout(std::chrono::milliseconds(100));  // 100ms timeout
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    
    REQUIRE(!joined);                      // Should timeout
    REQUIRE(elapsed >= std::chrono::milliseconds(100));
    REQUIRE(elapsed < std::chrono::milliseconds(200));
    
    node->Stop();
    node->Join();  // Proper cleanup
}

// MISSING TEST: Timeout with completing threads
TEST_CASE("JoinWithTimeout succeeds if threads complete in time") {
    auto node = std::make_shared<QuickProducerNode>();
    node->Init();
    node->Start();
    
    auto start = std::chrono::high_resolution_clock::now();
    bool joined = node->JoinWithTimeout(std::chrono::milliseconds(2000));
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    
    REQUIRE(joined);                       // Should succeed
    REQUIRE(elapsed < std::chrono::milliseconds(1000));
}

// MISSING TEST: Multiple InteriorNode ports with timeout distribution
TEST_CASE("InteriorNode distributes timeout across multiple ports") {
    auto node = std::make_shared<DualPortTransformerNode>();  // 2 input, 2 output
    node->Init();
    node->Start();
    
    // With 2 inputs + 2 outputs = 4 ports, 400ms timeout should be ~100ms per port
    bool joined = node->JoinWithTimeout(std::chrono::milliseconds(400));
    REQUIRE(joined);
}
```

**Implementation Effort**: 3-4 hours  
**Impact**: Critical for real-time applications with strict deadlines

---

### Gap 3: Port Metrics Accuracy (HIGH PRIORITY)

**Missing Tests**: Queue metrics, thread metrics per-port correctness

```cpp
// MISSING TEST: Queue metrics accuracy
TEST_CASE("SourceNode reports accurate output queue metrics") {
    auto node = std::make_shared<TestSourceNode>();
    node->Init();
    node->Start();
    
    // Produce 10 items on port 0
    for (int i = 0; i < 10; ++i) {
        node->Produce<0>(i);
    }
    
    // Query metrics
    auto metrics = node->GetOutputQueueMetrics(0);
    REQUIRE(metrics != nullptr);
    REQUIRE(metrics->total_enqueued >= 10);
    REQUIRE(metrics->current_size >= 0);
    REQUIRE(metrics->peak_size >= 10);
    
    node->Stop();
    node->Join();
}

// MISSING TEST: Thread metrics collection
TEST_CASE("SourceNode collects accurate thread metrics") {
    auto node = std::make_shared<TestSourceNode>();
    node->Init();
    node->Start();
    
    // Run for 100ms producing items
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto metrics = node->GetOutputPortThreadMetrics(0);
    REQUIRE(metrics != nullptr);
    REQUIRE(metrics->total_iterations > 0);        // Thread ran
    REQUIRE(metrics->total_produce_time_ns > 0);   // Produced something
    REQUIRE(metrics->thread_active);
    
    node->Stop();
    node->Join();
}

// MISSING TEST: Metrics persistence across Stop/Join
TEST_CASE("Metrics remain valid after node stops") {
    auto node = std::make_shared<TestSourceNode>();
    node->Init();
    node->Start();
    
    for (int i = 0; i < 5; ++i) {
        node->Produce<0>(i);
    }
    
    auto metrics_before = node->GetOutputQueueMetrics(0);
    size_t peak_before = metrics_before->peak_size;
    
    node->Stop();
    node->Join();
    
    auto metrics_after = node->GetOutputQueueMetrics(0);
    REQUIRE(metrics_after != nullptr);
    REQUIRE(metrics_after->peak_size == peak_before);  // Not lost
}

// MISSING TEST: ResetMetrics effectiveness
TEST_CASE("ResetMetrics clears counters") {
    auto node = std::make_shared<TestSourceNode>();
    node->Init();
    node->Start();
    
    for (int i = 0; i < 10; ++i) {
        node->Produce<0>(i);
    }
    
    auto before = node->GetOutputQueueMetrics(0);
    REQUIRE(before->total_enqueued >= 10);
    
    node->ResetMetrics();
    
    auto after = node->GetOutputQueueMetrics(0);
    REQUIRE(after->total_enqueued == 0);
}
```

**Implementation Effort**: 4-5 hours  
**Impact**: Enables performance monitoring and debugging

---

### Gap 4: NodeFactory Integration (MEDIUM PRIORITY)

**Missing Tests**: Plugin registry failures, unified registry fallback, type availability

```cpp
// MISSING TEST: Factory creation with null plugin registry
TEST_CASE("NodeFactory handles missing plugin registry gracefully") {
    auto factory = std::make_shared<graph::NodeFactory>(nullptr);  // No plugins
    
    // Initialize should complete
    factory->Initialize();
    
    // Dynamic node creation should fail with clear error
    REQUIRE_THROWS_AS(
        factory->CreateDynamicNode("NonexistentPlugin"),
        std::runtime_error
    );
}

// MISSING TEST: Unified registry fallback
TEST_CASE("CreateNode falls back to CreateDynamicNode if unified registry unavailable") {
    auto mock_registry = std::make_shared<MockPluginRegistry>();
    mock_registry->RegisterNodeType("TestNode");
    
    auto factory = std::make_shared<graph::NodeFactory>(mock_registry);
    
    // Don't call Initialize() - unified registry should not be used
    auto node = factory->CreateNode("TestNode");
    REQUIRE(node != nullptr);
}

// MISSING TEST: IsNodeTypeAvailable accuracy
TEST_CASE("IsNodeTypeAvailable reflects actual availability") {
    auto mock_registry = std::make_shared<MockPluginRegistry>();
    mock_registry->RegisterNodeType("ExistingNode");
    
    auto factory = std::make_shared<graph::NodeFactory>(mock_registry);
    
    REQUIRE(factory->IsNodeTypeAvailable("ExistingNode"));
    REQUIRE(!factory->IsNodeTypeAvailable("NonexistentNode"));
}

// MISSING TEST: GetAvailableNodeTypes returns all types
TEST_CASE("GetAvailableNodeTypes lists all registered plugins") {
    auto mock_registry = std::make_shared<MockPluginRegistry>();
    mock_registry->RegisterNodeType("Node1");
    mock_registry->RegisterNodeType("Node2");
    mock_registry->RegisterNodeType("Node3");
    
    auto factory = std::make_shared<graph::NodeFactory>(mock_registry);
    auto types = factory->GetAvailableNodeTypes();
    
    REQUIRE(types.size() == 3);
    REQUIRE(std::find(types.begin(), types.end(), "Node1") != types.end());
}
```

**Implementation Effort**: 3-4 hours  
**Impact**: Prevents integration issues in production

---

### Gap 5: Advanced Node Types (MEDIUM PRIORITY)

**Missing Tests**: SplitNode specializations, MergeNode variants, complex InteriorNodes

```cpp
// MISSING TEST: SplitNode broadcast correctness
TEST_CASE("SplitNode3 broadcasts to all 3 output ports") {
    auto splitter = std::make_shared<graph::SplitNode3<int>>();
    splitter->Init();
    splitter->Start();
    
    // Consume from splitter
    for (int i = 0; i < 5; ++i) {
        splitter->Consume(i * 100, std::integral_constant<size_t, 0>());
    }
    
    // Verify all 3 outputs received the values
    for (size_t port = 0; port < 3; ++port) {
        auto metrics = splitter->GetOutputQueueMetrics(port);
        REQUIRE(metrics->total_enqueued == 5);
    }
    
    splitter->Stop();
    splitter->Join();
}

// MISSING TEST: MergeNode with 5 inputs
TEST_CASE("MergeNode5 merges 5 inputs into unified queue") {
    auto merger = std::make_shared<TestMergeNode5>();
    merger->Init();
    merger->Start();
    
    // Send data on 5 input ports
    for (size_t port = 0; port < 5; ++port) {
        for (int i = 0; i < 3; ++i) {
            merger->Consume(i, std::integral_constant<size_t, port>());
        }
    }
    
    // All 15 items should be in unified queue
    auto unified_metrics = merger->GetInputQueueMetrics(0);  // Unified queue
    REQUIRE(unified_metrics->total_enqueued >= 15);
    
    merger->Stop();
    merger->Join();
}

// MISSING TEST: InteriorNode Transfer from multiple inputs
TEST_CASE("InteriorNode correctly routes Transfer outputs to different ports") {
    auto transformer = std::make_shared<TestInteriorNode>();
    transformer->Init();
    transformer->Start();
    
    // Consume on input ports 0 and 1
    transformer->Consume(10, std::integral_constant<size_t, 0>());
    transformer->Consume(20, std::integral_constant<size_t, 1>());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify Transfer routing
    auto output0_metrics = transformer->GetOutputQueueMetrics(0);
    auto output1_metrics = transformer->GetOutputQueueMetrics(1);
    REQUIRE(output0_metrics->total_enqueued > 0);
    REQUIRE(output1_metrics->total_enqueued > 0);
    
    transformer->Stop();
    transformer->Join();
}
```

**Implementation Effort**: 5-6 hours  
**Impact**: Validates advanced dataflow patterns

---

## Part 2: C++26 Feature Integration Plan

### Strategy 1: Reflection-Based Auto-Generated Tests

**Feature**: C++26 Reflection (P1240R8)

**Implementation**:
```cpp
// File: test/unit/test_node_reflection.cpp
#include <experimental/reflection>

// Compile-time node validation
template<typename NodeType>
concept ValidNodeType = requires {
    typename NodeType::InputPorts;
    typename NodeType::OutputPorts;
    { NodeType::NInputs } -> std::convertible_to<std::size_t>;
    { NodeType::NOutputs } -> std::convertible_to<std::size_t>;
};

// Auto-generate tests for all node types
TEST_CASE("All nodes satisfy GraphNode contract") {
    // Uses reflection to validate at compile-time
    static_assert(ValidNodeType<TestSourceNode>);
    static_assert(ValidNodeType<TestSinkNode>);
    static_assert(ValidNodeType<TestInteriorNode>);
}

// Reflection-based port introspection testing
TEST_CASE("Reflection reveals correct port structure") {
    using Info = std::meta::type_name(^TestInteriorNode);
    // Compile-time verification of inheritance hierarchy
    // Compile-time verification of port counts
}
```

**Benefits**:
- Zero runtime overhead
- Exhaustive structure validation
- Auto-generates tests for new node types

**Effort**: 2-3 hours

---

### Strategy 2: Concept-Based Generic Tests

**Feature**: C++20/C++26 Concepts

**Implementation**:
```cpp
// File: test/unit/test_node_concepts.hpp

// Define refined concepts for node types
template<typename NodeType>
concept SourceNodeLike = requires(NodeType node) {
    typename NodeType::OutputPorts;
    { node.GetOutputPortCount() } -> std::convertible_to<int>;
    { node.GetInputPortCount() } -> std::convertible_to<int>;
    requires node.GetInputPortCount() == 0;
};

template<typename NodeType>
concept SinkNodeLike = requires(NodeType node) {
    typename NodeType::InputPorts;
    { node.GetInputPortCount() } -> std::convertible_to<int>;
    { node.GetOutputPortCount() } -> std::convertible_to<int>;
    requires node.GetOutputPortCount() == 0;
};

// Generic test for any SourceNode
template<SourceNodeLike NodeType>
void test_source_node_lifecycle() {
    auto node = std::make_shared<NodeType>();
    REQUIRE(node->GetInputPortCount() == 0);
    REQUIRE(node->GetOutputPortCount() > 0);
    REQUIRE(node->Init());
    REQUIRE(node->Start());
    node->Stop();
    node->Join();
}

// Instantiate for all source nodes
TEST_CASE("All SourceNode types pass lifecycle test") {
    test_source_node_lifecycle<TestSourceNode>();
    test_source_node_lifecycle<TestSourceNode2>();
    test_source_node_lifecycle<TestSourceNode3>();
    // ... auto-scales to all conforming types
}
```

**Benefits**:
- Single test code for all node types
- Reduces duplication by ~70%
- New nodes automatically tested

**Effort**: 3-4 hours

---

### Strategy 3: std::expected Error Testing

**Feature**: std::expected<T, E> (P0323R12)

**Implementation**:
```cpp
// File: test/unit/test_node_errors.cpp

using InitError = std::expected<void, std::string>;

// Enhanced error testing with context
InitError try_init(graph::INode& node) {
    if (!node.Init()) {
        return std::unexpected("Node initialization failed");
    }
    return {};
}

TEST_CASE("Factory reports detailed errors") {
    auto factory = std::make_shared<graph::NodeFactory>(nullptr);
    
    auto result = std::invoke([&]() -> std::expected<NodeFacadeAdapter, std::string> {
        try {
            return factory->CreateDynamicNode("Nonexistent");
        } catch (const std::exception& e) {
            return std::unexpected(e.what());
        }
    });
    
    REQUIRE(!result);
    REQUIRE(result.error().contains("PluginRegistry"));
}

// Error recovery testing
TEST_CASE("Node recovers from failed Init") {
    auto node = std::make_shared<FaultyInitNode>();
    
    auto init1 = try_init(*node);
    REQUIRE(!init1);
    REQUIRE(init1.error().contains("resource"));
    
    // Fix and retry
    node->ResetForRetry();
    auto init2 = try_init(*node);
    REQUIRE(init2);
}
```

**Benefits**:
- Rich error diagnostics without exceptions
- Cleaner error handling syntax
- Better test failure messages

**Effort**: 2-3 hours

---

### Strategy 4: std::move_only_function for Factory Tests

**Feature**: std::move_only_function (P0288R9)

**Implementation**:
```cpp
// File: test/unit/test_factory_ownership.cpp

// Enhanced factory with exclusive ownership
using ExclusiveFactory = std::move_only_function<graph::NodeFacadeAdapter()>;

TEST_CASE("Factory enforces exclusive ownership of resources") {
    std::unique_ptr<int> resource = std::make_unique<int>(42);
    ExclusiveFactory factory = [res = std::move(resource)]() {
        return graph::NodeFacadeAdapter{...};
    };
    
    // This would not compile (good!):
    // ExclusiveFactory factory2 = factory;  // ERROR: non-copyable
    
    // But move semantics work:
    ExclusiveFactory factory2 = std::move(factory);
    
    // Test factory functionality
    auto node = factory2();
    REQUIRE(node != nullptr);
}

TEST_CASE("Move-only factories prevent accidental copies") {
    auto registry = std::make_shared<graph::config::NodeFactoryRegistry>();
    
    std::unique_ptr<TestResource> resource = std::make_unique<TestResource>();
    ExclusiveFactory factory = [res = std::move(resource)]() {
        return graph::NodeFacadeAdapter{...};
    };
    
    // Registry takes ownership via move
    registry->Register("TestNode", std::move(factory));
    
    // Original factory is now empty - we can't use it again
    // This catches bugs at compile-time
}
```

**Benefits**:
- Zero-overhead abstraction
- Compile-time prevention of resource leaks
- Better semantics for factory ownership

**Effort**: 2-3 hours

---

### Strategy 5: Designated Initializers for Test Data

**Feature**: Designated Initializers (C++20, enhanced in C++26)

**Implementation**:
```cpp
// File: test/unit/test_port_metadata.cpp

TEST_CASE("Port metadata matches expected structure") {
    // Before (unclear which field is which)
    graph::PortInfo expected_old{"MyPort", 0, graph::PortDirection::Output, "int", 100};
    
    // After (self-documenting)
    graph::PortInfo expected{
        .name = "MyPort",
        .index = 0,
        .direction = graph::PortDirection::Output,
        .type_name = "int",
        .queue_capacity = 100
    };
    
    auto node = std::make_shared<TestSourceNode>();
    auto ports = node->OutputPorts();
    
    REQUIRE(ports[0].name == expected.name);
    REQUIRE(ports[0].index == expected.index);
    REQUIRE(ports[0].direction == expected.direction);
    REQUIRE(ports[0].type_name == expected.type_name);
}

// Test configuration clarity
TEST_CASE("MergeNode respects configuration") {
    struct MergeConfig {
        std::size_t input_count;
        std::size_t queue_capacity;
        bool enable_metrics;
    };
    
    MergeConfig config{
        .input_count = 5,
        .queue_capacity = 1000,
        .enable_metrics = true
    };
    
    auto node = std::make_shared<TestMergeNode>(config);
    REQUIRE(node->GetInputPortCount() == config.input_count);
}
```

**Benefits**:
- More readable test data setup
- Self-documenting field assignments
- Less prone to wrong-order errors

**Effort**: 1-2 hours

---

### Strategy 6: Ranges for Port Iteration

**Feature**: Ranges Library Enhancements (C++26)

**Implementation**:
```cpp
// File: test/unit/test_port_iteration.cpp

TEST_CASE("Port metrics tested across all ports") {
    auto node = std::make_shared<ComplexInteriorNode>();  // 3 inputs, 3 outputs
    node->Init();
    node->Start();
    
    // Ranges-based iteration with filtering and transformation
    namespace views = std::views;
    
    auto input_metrics = std::ranges::iota_view(0, node->GetInputPortCount())
        | views::transform([node](int i) {
            return std::make_pair(i, node->GetInputQueueMetrics(i));
        })
        | views::filter([](const auto& p) {
            return p.second != nullptr;
        });
    
    for (auto [port_id, metrics] : input_metrics) {
        REQUIRE(metrics->total_enqueued >= 0);
        REQUIRE(metrics->current_size >= 0);
    }
    
    node->Stop();
    node->Join();
}

// Functional composition of test assertions
TEST_CASE("All output ports contain expected data") {
    auto node = std::make_shared<TestSourceNode>();
    
    namespace views = std::views;
    
    std::vector<std::pair<int, size_t>> port_sizes =
        std::ranges::iota_view(0, node->GetOutputPortCount())
        | views::transform([node](int i) {
            return std::make_pair(i, node->GetOutputPortQueueSize(i));
        })
        | std::ranges::to<std::vector>();
    
    for (auto [port_id, size] : port_sizes) {
        REQUIRE(size > 0);
    }
}
```

**Benefits**:
- Expressive data pipelines in tests
- Eliminates intermediate variables
- More functional/declarative style

**Effort**: 2-3 hours

---

## Part 3: Recommended Implementation Timeline

### Week 1: Critical Gaps (40 hours)
- [ ] Lifecycle state machine tests (8h)
- [ ] JoinWithTimeout tests (8h)
- [ ] Port metrics tests (8h)
- [ ] NodeFactory integration tests (8h)
- [ ] Review and refinement (8h)

### Week 2: C++26 Features (32 hours)
- [ ] Reflection-based tests (6h)
- [ ] Concept-based generic tests (8h)
- [ ] std::expected error tests (6h)
- [ ] std::move_only_function tests (4h)
- [ ] Ranges-based iteration tests (4h)
- [ ] Designated initializer refactoring (4h)

### Week 3: Advanced Types + Integration (24 hours)
- [ ] SplitNode specialization tests (6h)
- [ ] MergeNode variant tests (6h)
- [ ] ComplexInteriorNode tests (6h)
- [ ] End-to-end integration tests (4h)
- [ ] Performance benchmarks (2h)

**Total Effort**: ~96 hours (~2-3 weeks)  
**Improvement**: From 20% → 95% test coverage

---

## Part 4: Testing Infrastructure Needed

### 4.1 Test Node Implementations

```cpp
// File: test/include/test/TestNodes.hpp

namespace test {
    // Basic test nodes
    class TestSourceNode : public graph::SourceNode<int, double>;
    class TestSinkNode : public graph::SinkNode<int, std::string>;
    class TestInteriorNode : public graph::InteriorNode<
        graph::TypeList<int, double>,
        graph::TypeList<std::string, bool>>;
    
    // Slow/quick nodes for timeout testing
    class SlowProducerNode : public graph::SourceNode<int>;
    class QuickProducerNode : public graph::SourceNode<int>;
    
    // Faulty nodes for error testing
    class FaultyInitNode : public graph::SourceNode<int>;
    class FaultyStartNode : public graph::SourceNode<int>;
    
    // Advanced nodes
    class TestMergeNode5 : public graph::MergeNodeBase<5, int, int>;
    class ComplexInteriorNode : public graph::InteriorNode<
        graph::TypeList<int, int, int>,
        graph::TypeList<int, int, int>>;
}
```

### 4.2 Mock Fixtures

```cpp
// File: test/include/test/MockFixtures.hpp

class MockPluginRegistry : public graph::PluginRegistry {
    // ... implementation for testing
};

class MetricsValidator {
    // Helper to validate queue/thread metrics
};

class TimeoutValidator {
    // Helper to validate timeout behavior within tolerances
};
```

### 4.3 Test Utilities

```cpp
// File: test/include/test/TestUtilities.hpp

// Timing utilities
std::chrono::nanoseconds measure_duration(std::function<void()> fn);

// Assertion helpers
void assert_lifecycle_state(graph::INode& node, graph::LifecycleState expected);
void assert_port_metrics(const core::QueueMetrics* metrics, size_t expected_count);
void assert_thread_metrics_nonzero(const ThreadMetrics* metrics);
```

---

## Part 5: Success Criteria

### Coverage Targets

- ✅ Lifecycle methods: 100% coverage
- ✅ All node types: 100% per-type lifecycle
- ✅ Timeout semantics: 100% path coverage
- ✅ Metrics operations: 100% API coverage
- ✅ Factory methods: 100% API coverage
- ✅ Error conditions: 90% branch coverage

### Quality Gates

- ✅ All tests pass on C++26 compiler
- ✅ Zero sanitizer warnings (ASAN, UBSAN)
- ✅ Zero thread-safety warnings (ThreadSanitizer)
- ✅ Code coverage ≥ 95%
- ✅ All C++26 feature tests compile

### Performance Targets

- ✅ Full test suite runs in < 30 seconds
- ✅ Timeout tests complete within ±10ms tolerance
- ✅ No memory leaks in test execution

---

## Conclusion

GraphX has a **production-quality implementation** with **critical test gaps**. By following this plan:

1. **Weeks 1-2**: Eliminate critical gaps (state machine, timeouts, metrics)
2. **Week 3**: Modernize with C++26 features (reflection, concepts, ranges)
3. **Result**: 95%+ test coverage with zero test duplication

The C++26 enhancements will enable **auto-generated test suites** that scale effortlessly to future node types, turning a 96-hour investment into long-term maintainability gains.

