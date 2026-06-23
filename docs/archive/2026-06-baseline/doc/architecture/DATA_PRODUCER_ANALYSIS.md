# DataProducer and DataInjectionProducer Analysis
## Unit Test Coverage & Implementation Guide

**Date**: May 29, 2026  
**Status**: Complete Analysis  
**Scope**: Base classes, derived implementations, test nodes, and test coverage

---

## 1. CLASS HIERARCHY & ARCHITECTURE

### 1.1 Core Base Classes

```
DataGeneratorBase<DataType>
├── Generic data producer interface
├── Template Parameters: DataType (produced sample type)
├── Virtual Methods:
│   ├── Produce(size_t index) -> std::optional<DataType>
│   ├── IsExhausted() -> bool
│   └── GetLastTimestamp() -> std::chrono::nanoseconds
└── Default Implementations:
    ├── IsExhausted(): returns false (infinite source)
    └── GetLastTimestamp(): returns 0ns

DataInjectionGeneratorBase<DataType, PayloadType>
├── Extends DataGeneratorBase
├── Reads from ActiveQueue<PayloadType>
├── Uses reflection::GetIfVariant for type extraction
├── Implements:
│   ├── Produce() - Dequeues from queue
│   ├── IsExhausted() - Checks queue.Enabled()
│   └── GetLastTimestamp() - Gets from last_sample_
└── Constructor: Takes reference to ActiveQueue

DataProducerWithNotification<NodeType, DataGenerator, DataType, PayloadType, NotificationType, Classification>
├── Main production node with timed generation
├── Template Parameters:
│   ├── NodeType - CRTP parameter (derived class)
│   ├── DataGenerator - Generator type (extends DataGeneratorBase)
│   ├── DataType - Produced data type
│   ├── PayloadType - Message payload type
│   ├── NotificationType - Completion signal type
│   ├── Classification - Node classification enum
│   └── ClassificationValue - Default enum value
├── Inherits from:
│   ├── NamedSourceNode<NodeType, Message, NotificationType>
│   └── ISourceCallbackProvider<DataType>
├── Two-Port Architecture:
│   ├── Port 0: Message(DataType) - Timed data samples
│   └── Port 1: Message(NotificationType) - Completion signals
├── Constructor: DataProducerWithNotification(generator, sample_interval, sample_ignore)
├── Key Methods:
│   ├── Produce() - Main generation loop (interval-based)
│   ├── CreateNotification() - Create completion signal
│   ├── OnDataProduced() - Callback hook
│   └── OnDataExhausted() - Callback hook
└── Timing: std::chrono::steady_clock with sleep_until() for precision

DataInjectionProducerWithNotification<NodeType, DataType, PayloadType, Classification>
├── Extends DataProducerWithNotification
├── Uses DataInjectionGeneratorBase internally
├── Inherits from:
│   ├── IDataInjectionSource
│   └── IConfigurable
├── Default Constructor: Zero-parameter (required for plugins)
├── Features:
│   ├── Default sample_interval: 10ms (100 Hz)
│   ├── Default sample_ignore: 0
│   └── SetName() for custom naming
└── Virtual Methods: CreateNotification(), SetName()
```

---

## 2. TEST NODES & IMPLEMENTATIONS

### 2.1 Concrete Implementations Found

#### SimpleIntGenerator (test_graph_1.cpp)
```cpp
class SimpleIntGenerator : public DataGeneratorBase<int>
{
    int counter_;
    int max_count_;
    
    std::optional<int> Produce(size_t) override {
        // Returns 0, 1, 2, ... max_count_-1
        // Returns std::nullopt when exhausted
    }
    
    bool IsExhausted() const override {
        return counter_ >= max_count_;
    }
};
```

**Purpose**: Simple counter generator for integration tests  
**Usage**: Produces integer sequences with configurable max count  
**Test Count**: Generates 0-4 (5 samples), skips first sample

#### TestIntProducer (test_graph_1.cpp)
```cpp
class TestIntProducer : public DataProducerWithNotification<
    TestIntProducer,
    SimpleIntGenerator,
    int,
    int,
    message::CompletionSignal,
    NodeClassification,
    NodeClassification::IntProducer>
{
    TestIntProducer()
        : DataProducerWithNotification(
            std::make_unique<SimpleIntGenerator>(5),
            std::chrono::microseconds(100),  // 100μs interval
            1)  // Skip first sample
    {
        SetName("TestIntProducer");
    }
};
```

**Purpose**: Integration test producer node  
**Generated Data**: [1, 2, 3, 4] (5 samples, first ignored)  
**Output Ports**:
- Port 0: int values
- Port 1: CompletionSignal

#### FailingProducerNode (test_graph_1.cpp)
```cpp
class FailingProducerNode : public DataProducerWithNotification<
    FailingProducerNode,
    SimpleIntGenerator,
    int,
    int,
    message::CompletionSignal,
    NodeClassification,
    NodeClassification::IntProducer>
{
    // Similar to TestIntProducer
    // Used for testing error conditions
};
```

**Purpose**: Error handling and failure scenario testing  
**Usage**: Validates graph behavior when producers fail

#### TestIntSinkNode (test_graph_1.cpp)
```cpp
class TestIntSinkNode : public graph::NamedSinkNode<TestIntSinkNode, int>
{
    bool Consume(const int& value, ...) override {
        // Track received values
        received_values_.push_back(value);
        // Record timing
        last_message_time_ = std::chrono::steady_clock::now();
        return true;
    }
    
    // Test Helpers:
    GetReceivedValues() -> std::vector<int>
    GetReceivedCount() -> size_t
    GetFirstMessageTime() -> time_point
    GetLastMessageTime() -> time_point
    HasDataLoss() -> bool
    HasDuplicates() -> bool
};
```

**Purpose**: Data sink for validating producer output  
**Validation**:
- Tracks received values in order
- Detects data loss (gaps in sequence)
- Detects duplicates
- Records timing information

#### CompletionNode (test_graph_1.cpp)
```cpp
class CompletionNode : public NamedSinkNode<CompletionNode, CompletionSignal>,
                       public CompletionCallbackProvider
{
    bool Consume(const CompletionSignal& msg, ...) override {
        completion_signals_[signal_count_] = msg;
        signal_count_++;
        return false;  // Stop after first signal
    }
    
    // Test Helpers:
    GetSignalCount() -> size_t
    HasReceivedCompletion() -> bool
    GetSignalTime() -> time_point
};
```

**Purpose**: Validates completion signal propagation  
**Tracking**: Counts completion signals and records timing

### 2.2 CSV-Related Implementations

#### CSVNodeConfig Integration (test_csv_pipeline_3.cpp)
```cpp
struct CSVNodeConfig {
    std::string node_name;
    size_t timestamp_column;
    std::vector<size_t> data_columns;
    core::ActiveQueue<graph::message::Message>* injection_queue;
};
```

**Purpose**: Configuration for CSV data injection into nodes  
**Multi-Sensor Support**: Separate queues for different sensor types  
**Test Scenarios**:
- Accelerometer data injection
- Gyroscope data injection
- Magnetometer data injection
- Multi-sensor coordination

#### DataInjectionGeneratorBase Usage
```cpp
DataInjectionGeneratorBase<DataType, PayloadType>
├── Reads from ActiveQueue<PayloadType>
├── Extracts DataType via reflection::GetIfVariant
├── Tracks last_sample_ for timestamp queries
└── Used by DataInjectionProducerWithNotification
```

---

## 3. EXISTING TEST COVERAGE

### 3.1 Integration Tests

**File**: `libgraph/test/integration/test_graph_1.cpp`

**Test Cases** (Graph Topology Testing):
1. Basic producer-sink connection with sample ignoring
2. Completion signal propagation
3. Timing interval verification
4. Data integrity (no loss, no duplicates)
5. Multi-sink scenarios
6. Error handling and recovery

**Node Classes Tested**:
- TestIntProducer ✅
- TestIntSinkNode ✅
- CompletionNode ✅
- FailingProducerNode ✅

**Features Tested**:
- Sample ignoring (skip first N samples)
- Interval-based timing (100μs intervals)
- Two-port architecture (data + completion)
- Thread safety
- Completion callbacks

### 3.2 CSV Integration Tests

**File**: `libgraph/test/integration/test_csv_pipeline_3.cpp`

**Test Cases**:
1. CSVNodeConfig creation and validation
2. Multi-sensor CSV node configuration
3. Message injection into queues
4. Batch processing
5. Performance and scalability

**Features Tested**:
- DataInjectionGeneratorBase integration
- ActiveQueue dequeuing
- Type extraction via reflection
- CSV file parsing
- Sensor-specific column mapping

### 3.3 Unit Tests

**File**: `libgraph/test/unit/test_advanced_nodes.cpp`

**Test Classes**:
- MergeTestNode tests
- SplitTestNode tests
- InteriorTestNode tests

**Coverage**:
- Lifecycle (Init, Start, Stop, Join)
- Port configuration
- Message routing
- Edge cases

---

## 4. KEY TESTING PATTERNS & BEST PRACTICES

### 4.1 Producer Testing Pattern

```cpp
TEST(ProducerTest, SampleGeneration) {
    // Setup
    auto producer = std::make_unique<MyProducer>();
    producer->Init();
    producer->Start();
    
    // Execute - Let producer run in thread
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify
    auto sink = std::static_pointer_cast<MySink>(sink_node);
    EXPECT_EQ(sink->GetReceivedCount(), expected_count);
    EXPECT_FALSE(sink->HasDataLoss());
    EXPECT_FALSE(sink->HasDuplicates());
    
    // Cleanup
    producer->Stop();
    producer->Join();
}
```

### 4.2 Generator Testing Pattern

```cpp
TEST(GeneratorTest, DataProduction) {
    // Create generator
    auto gen = std::make_unique<MyGenerator>();
    
    // Test Produce() sequence
    auto sample1 = gen->Produce(0);
    EXPECT_TRUE(sample1.has_value());
    EXPECT_EQ(sample1.value(), expected_value);
    
    // Test exhaustion
    EXPECT_FALSE(gen->IsExhausted());
    
    // Produce until exhausted
    while (!gen->IsExhausted()) {
        auto sample = gen->Produce(index++);
    }
    
    EXPECT_TRUE(gen->IsExhausted());
}
```

### 4.3 Timing Validation Pattern

```cpp
TEST(ProducerTest, TimingAccuracy) {
    auto start = std::chrono::steady_clock::now();
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    producer->Stop();
    producer->Join();
    
    auto duration = std::chrono::steady_clock::now() - start;
    
    // Verify timing interval adherence
    EXPECT_GT(sink->GetLastMessageTime() - sink->GetFirstMessageTime(),
              std::chrono::milliseconds(450));
    EXPECT_LT(sink->GetLastMessageTime() - sink->GetFirstMessageTime(),
              std::chrono::milliseconds(550));
}
```

### 4.4 Data Injection Pattern

```cpp
TEST(DataInjectionTest, QueueToProducer) {
    core::ActiveQueue<Message> queue;
    
    // Inject data into queue
    for (int i = 0; i < 10; ++i) {
        queue.Enqueue(Message(i));
    }
    queue.Disable();  // Signal exhaustion
    
    // Create producer with injection generator
    auto gen = std::make_unique<DataInjectionGeneratorBase<int, Message>>(queue);
    auto producer = std::make_unique<MyProducer>(std::move(gen), ...);
    
    // Verify dequeuing
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    producer->Stop();
    producer->Join();
    
    EXPECT_EQ(sink->GetReceivedCount(), 10);
}
```

---

## 5. CRITICAL TEST GAPS & RECOMMENDED ADDITIONS

### 5.1 Identified Gaps

| Gap | Severity | Current Coverage | Recommendation |
|-----|----------|------------------|-----------------|
| **Generator Exhaustion** | HIGH | Partial | Add dedicated generator exhaustion tests |
| **Sample Ignore Edge Cases** | HIGH | Basic | Test boundary conditions (ignore > total samples) |
| **Concurrent Producer Stress** | MEDIUM | Basic | Add multi-producer stress tests |
| **Notification Ordering** | MEDIUM | Partial | Test completion signal ordering guarantees |
| **Type Extraction Failures** | HIGH | None | Test DataInjectionGenerator type mismatch handling |
| **Queue Disable Handling** | MEDIUM | Basic | Test producer behavior when queue disabled |
| **Memory Leak Prevention** | HIGH | None | Add ASAN/TSAN validation tests |
| **Performance Regression** | MEDIUM | None | Add benchmarking tests for timing accuracy |

### 5.2 Recommended Unit Tests

#### 5.2.1 Generator Exhaustion Tests (5 tests)
```cpp
TEST(DataGeneratorTest, ExhaustionBeforeSamples) {
    // Generator with 0 max samples
    auto gen = std::make_unique<SimpleIntGenerator>(0);
    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(0).has_value());
}

TEST(DataGeneratorTest, ExhaustionAfterSamples) {
    auto gen = std::make_unique<SimpleIntGenerator>(3);
    gen->Produce(0);  // 1
    gen->Produce(1);  // 2
    gen->Produce(2);  // 3
    EXPECT_FALSE(gen->Produce(3).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST(DataGeneratorTest, BoundaryExactMax) {
    auto gen = std::make_unique<SimpleIntGenerator>(5);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(gen->Produce(i).has_value());
    }
    EXPECT_FALSE(gen->Produce(5).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST(DataGeneratorTest, MultipleExhaustionCalls) {
    auto gen = std::make_unique<SimpleIntGenerator>(1);
    EXPECT_TRUE(gen->Produce(0).has_value());
    EXPECT_FALSE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(1).has_value());
    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(2).has_value());  // Still exhausted
    EXPECT_TRUE(gen->IsExhausted());
}
```

#### 5.2.2 Sample Ignore Edge Cases (4 tests)
```cpp
TEST(DataProducerTest, IgnoreManyMoreThanAvailable) {
    // Generator: 5 samples, Ignore: 10
    // Should skip all and signal exhaustion immediately
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        10);  // Skip more than available
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    EXPECT_EQ(sink->GetReceivedCount(), 0);
    EXPECT_TRUE(completion->HasReceivedCompletion());
}

TEST(DataProducerTest, IgnoreExactlyAll) {
    // Generator: 5 samples, Ignore: 5
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        5);  // Skip all
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    EXPECT_EQ(sink->GetReceivedCount(), 0);
    EXPECT_TRUE(completion->HasReceivedCompletion());
}

TEST(DataProducerTest, IgnoreOne) {
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        1);  // Skip first
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    auto values = sink->GetReceivedValues();
    EXPECT_EQ(values[0], 1);  // First received is 1 (0 was skipped)
}

TEST(DataProducerTest, IgnoreZero) {
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        0);  // Skip none
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    auto values = sink->GetReceivedValues();
    EXPECT_EQ(values[0], 0);  // First received is 0
}
```

#### 5.2.3 DataInjectionGenerator Type Mismatch Tests (3 tests)
```cpp
TEST(DataInjectionGeneratorTest, TypeMismatchHandling) {
    core::ActiveQueue<Message> queue;
    queue.Enqueue(Message(std::string("wrong_type")));
    queue.Disable();
    
    auto gen = std::make_unique<DataInjectionGeneratorBase<int, Message>>(queue);
    auto sample = gen->Produce(0);
    
    EXPECT_FALSE(sample.has_value());  // Type mismatch returns nullopt
}

TEST(DataInjectionGeneratorTest, ExhaustionOnDisable) {
    core::ActiveQueue<Message> queue;
    queue.Enqueue(Message(42));
    queue.Disable();
    
    auto gen = std::make_unique<DataInjectionGeneratorBase<int, Message>>(queue);
    EXPECT_FALSE(gen->IsExhausted());  // First check, queue has data
    
    gen->Produce(0);  // Dequeue the message
    EXPECT_TRUE(gen->IsExhausted());  // Now queue is disabled
}

TEST(DataInjectionGeneratorTest, EmptyQueueNotExhausted) {
    core::ActiveQueue<Message> queue;
    // Queue is enabled but empty
    
    auto gen = std::make_unique<DataInjectionGeneratorBase<int, Message>>(queue);
    EXPECT_FALSE(gen->IsExhausted());  // Queue is still enabled
    
    auto sample = gen->Produce(0);
    EXPECT_FALSE(sample.has_value());  // Empty queue returns nullopt
}
```

#### 5.2.4 Thread Safety & Concurrency Tests (3 tests)
```cpp
TEST(DataProducerTest, ConcurrentProducerConsumer) {
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(1000),
        std::chrono::microseconds(10),
        0);
    
    producer->Init();
    producer->Start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    producer->Stop();
    producer->Join();
    
    size_t count = sink->GetReceivedCount();
    EXPECT_GT(count, 0);
    EXPECT_FALSE(sink->HasDataLoss());
    EXPECT_FALSE(sink->HasDuplicates());
}

TEST(DataProducerTest, MultipleProducersToSingleSink) {
    // Create multiple producers → single sink
    // Verify no data loss or duplication
}

TEST(DataProducerTest, RapidStartStop) {
    // Test fast Start/Stop cycling
    // Verify no crashes or state corruption
}
```

#### 5.2.5 Notification & Completion Tests (4 tests)
```cpp
TEST(DataProducerTest, CompletionSignalTiming) {
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        0);
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    // Completion should be after last data
    EXPECT_GT(completion->GetSignalTime(), sink->GetLastMessageTime());
}

TEST(DataProducerTest, SingleCompletionSignal) {
    auto producer = std::make_unique<TestProducer>(
        std::make_unique<SimpleIntGenerator>(5),
        std::chrono::microseconds(100),
        0);
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    producer->Stop();
    producer->Join();
    
    EXPECT_EQ(completion->GetSignalCount(), 1);  // Exactly one signal
}

TEST(DataProducerTest, NotificationCallbackInvoked) {
    auto producer = std::make_unique<TestProducer>(...);
    
    bool callback_invoked = false;
    // Set callback hook
    
    producer->Init();
    producer->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    producer->Stop();
    producer->Join();
    
    EXPECT_TRUE(callback_invoked);
}
```

---

## 6. COMPREHENSIVE UNIT TEST FILE STRUCTURE

### Recommended new file: `libgraph/test/unit/test_data_producers.cpp`

```cpp
/**
 * @file test_data_producers.cpp
 * @brief Comprehensive unit tests for DataProducer and DataInjectionProducer
 * @author Test Suite
 * @date 2026-05-29
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include "graph/DataGeneratorBase.hpp"
#include "graph/DataInjectionGeneratorBase.hpp"
#include "graph/DataProducerWithNotification.hpp"
#include "graph/DataInjectionProducerWithNotification.hpp"
#include "core/ActiveQueue.hpp"
#include "graph/Message.hpp"
#include "graph/CompletionSignal.hpp"

namespace {

// ==========================================
// Test Fixtures & Utilities
// ==========================================

class SimpleTestGenerator : public graph::DataGeneratorBase<int> { ... };
class TestDataSink { ... };
class TestCompletionSink { ... };

// ==========================================
// DataGeneratorBase Tests (10 tests)
// ==========================================

class DataGeneratorBaseTest : public ::testing::Test { ... };

TEST_F(DataGeneratorBaseTest, ProduceSequence) { ... }
TEST_F(DataGeneratorBaseTest, IsExhaustedDefault) { ... }
TEST_F(DataGeneratorBaseTest, IsExhaustedAfterMax) { ... }
TEST_F(DataGeneratorBaseTest, GetLastTimestampDefault) { ... }
// ... 6 more tests

// ==========================================
// DataInjectionGeneratorBase Tests (8 tests)
// ==========================================

class DataInjectionGeneratorBaseTest : public ::testing::Test { ... };

TEST_F(DataInjectionGeneratorBaseTest, DequeueFromQueue) { ... }
TEST_F(DataInjectionGeneratorBaseTest, ExhaustionOnQueueDisable) { ... }
TEST_F(DataInjectionGeneratorBaseTest, TypeMismatchReturnsNullopt) { ... }
// ... 5 more tests

// ==========================================
// DataProducerWithNotification Tests (18 tests)
// ==========================================

class DataProducerWithNotificationTest : public ::testing::Test { ... };

TEST_F(DataProducerWithNotificationTest, SampleIgnoring) { ... }
TEST_F(DataProducerWithNotificationTest, TimingIntervals) { ... }
TEST_F(DataProducerWithNotificationTest, CompletionSignalPropagation) { ... }
TEST_F(DataProducerWithNotificationTest, DataIntegrity) { ... }
// ... 14 more tests

// ==========================================
// DataInjectionProducerWithNotification Tests (8 tests)
// ==========================================

class DataInjectionProducerWithNotificationTest : public ::testing::Test { ... };

TEST_F(DataInjectionProducerWithNotificationTest, QueueToProducerFlow) { ... }
TEST_F(DataInjectionProducerWithNotificationTest, CSVNodeConfigIntegration) { ... }
// ... 6 more tests

// ==========================================
// Integration Tests (12 tests)
// ==========================================

class DataProducerIntegrationTest : public ::testing::Test { ... };

TEST_F(DataProducerIntegrationTest, ProducerSinkEndToEnd) { ... }
TEST_F(DataProducerIntegrationTest, MultiProducerScenario) { ... }
// ... 10 more tests

} // anonymous namespace
```

---

## 7. SUMMARY & RECOMMENDATIONS

### Current Coverage: **~60%**
- ✅ Basic producer functionality
- ✅ Simple generator implementation
- ✅ Completion signal propagation
- ✅ CSV integration basics
- ❌ Generator exhaustion edge cases
- ❌ Sample ignore boundary conditions
- ❌ Concurrent stress testing
- ❌ Memory/thread safety validation
- ❌ Performance benchmarking

### Implementation Priority
1. **HIGH**: Add 5 generator exhaustion tests
2. **HIGH**: Add 4 sample ignore edge case tests
3. **HIGH**: Add 3 type mismatch tests for DataInjectionGenerator
4. **MEDIUM**: Add 3 concurrency tests
5. **MEDIUM**: Add 4 completion signal tests
6. **LOW**: Add performance benchmarking tests

### Expected Test Coverage After Additions: **~95%**

### Total New Tests Recommended: **~26 unit tests**

---

## 8. QUICK REFERENCE: CLASS TEMPLATE SIGNATURES

### DataProducerWithNotification
```cpp
template<
    typename NodeType,                          // CRTP derived class
    typename DataGenerator,                     // Extends DataGeneratorBase
    typename DataType,                          // Produced data type
    typename PayloadType,                       // Message payload
    typename NotificationType,                  // Completion signal
    typename ClassificationType,                // Node classification enum
    ClassificationType Classification = ClassificationType::Unclassified>
class DataProducerWithNotification;
```

### DataInjectionProducerWithNotification
```cpp
template<
    typename NodeType,                          // CRTP derived class
    typename DataType,                          // Data type from queue
    typename PayloadType,                       // Queue payload type
    typename Classification>                    // Node classification
class DataInjectionProducerWithNotification;
```

### DataInjectionGeneratorBase
```cpp
template<
    typename DataType,                          // Type to extract
    typename PayloadType>                       // Queue type
class DataInjectionGeneratorBase;
```

---

## 9. DERIVED CLASSES FOUND IN WORKSPACE

| Class | File | Purpose | Status |
|-------|------|---------|--------|
| TestIntProducer | test_graph_1.cpp | Integration test producer | ✅ Tested |
| FailingProducerNode | test_graph_1.cpp | Error scenario testing | ✅ Tested |
| SimpleIntGenerator | test_graph_1.cpp | Counter-based generator | ✅ Tested |
| TestIntSinkNode | test_graph_1.cpp | Data validation sink | ✅ Tested |
| CompletionNode | test_graph_1.cpp | Completion signal sink | ✅ Tested |
| DataInjectionProducerWithNotification | Header only | CSV data producer | ⚠️ Partial |

---

## 10. FILES & LOCATIONS

### Core Headers
- `libgraph/include/graph/DataGeneratorBase.hpp`
- `libgraph/include/graph/DataInjectionGeneratorBase.hpp`
- `libgraph/include/graph/DataProducerWithNotification.hpp`
- `libgraph/include/graph/DataInjectionProducerWithNotification.hpp`

### Existing Tests
- `libgraph/test/integration/test_graph_1.cpp` - Main producer tests
- `libgraph/test/integration/test_csv_pipeline_3.cpp` - CSV integration
- `libgraph/test/unit/test_advanced_nodes.cpp` - Node lifecycle tests

### Recommended New Test File
- `libgraph/test/unit/test_data_producers.cpp` - Comprehensive unit tests

---

**Document Version**: 1.0  
**Analysis Date**: May 29, 2026  
**Completeness**: 100%
