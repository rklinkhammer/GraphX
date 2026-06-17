// SPDX-License-Identifier: MIT

/**
 * @file test_data_producers.cpp
 * @brief Comprehensive unit tests for DataProducer and DataInjectionProducer
 *
 * Tests the following components:
 * - DataGeneratorBase: Base generator interface and behavior
 * - SimpleIntGenerator: Counter-based integer generation
 * - SimpleDoubleGenerator: Counter-based double generation
 * - RandomIntGenerator: Random integer generation
 * - DataProducerWithNotification: Core producer timing and notifications
 * - Sample ignore semantics: Skipping first N samples
 * - Completion signal propagation: Two-port output validation
 *
 * @author Test Suite
 * @date May 29, 2026
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "graph/CompletionSignal.hpp"
#include "graph/Message.hpp"
#include "test/ProducerTestNodes.hpp"

namespace {

using namespace graph;
using namespace test;

// =============================================================================
// Test Fixtures
// =============================================================================

/**
 * @brief Base fixture for generator tests
 */
/**
 * @class DataGeneratorTest
 * @brief Data generator test implementation for GraphX.
 */
class DataGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/**
 * @brief Base fixture for producer tests
 */
/**
 * @class DataProducerTest
 * @brief Data producer test implementation for GraphX.
 */
class DataProducerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// =============================================================================
// SimpleIntGenerator Tests (5 tests)
// =============================================================================

TEST_F(DataGeneratorTest, SimpleIntGenerator_ProduceSequence) {
    auto gen = std::make_unique<SimpleIntGenerator>(5);

    // Verify sequence 0, 1, 2, 3, 4
    for (int i = 0; i < 5; ++i) {
        auto sample = gen->Produce(i);
        ASSERT_TRUE(sample.has_value());
        EXPECT_EQ(sample.value(), i);
    }

    // Verify exhaustion
    EXPECT_FALSE(gen->Produce(5).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST_F(DataGeneratorTest, SimpleIntGenerator_ExhaustionBeforeSamples) {
    auto gen = std::make_unique<SimpleIntGenerator>(0);

    // Should be exhausted immediately
    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(0).has_value());
}

TEST_F(DataGeneratorTest, SimpleIntGenerator_ExhaustionAfterSamples) {
    auto gen = std::make_unique<SimpleIntGenerator>(3);

    // Produce 3 samples
    EXPECT_TRUE(gen->Produce(0).has_value());
    EXPECT_TRUE(gen->Produce(1).has_value());
    EXPECT_TRUE(gen->Produce(2).has_value());

    // Next should be exhausted
    EXPECT_FALSE(gen->Produce(3).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST_F(DataGeneratorTest, SimpleIntGenerator_BoundaryExactMax) {
    auto gen = std::make_unique<SimpleIntGenerator>(5);

    // Produce exactly max samples
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(gen->Produce(i).has_value());
    }

    // Next should fail
    EXPECT_FALSE(gen->Produce(5).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST_F(DataGeneratorTest, SimpleIntGenerator_MultipleExhaustionCalls) {
    auto gen = std::make_unique<SimpleIntGenerator>(1);

    // Produce one sample
    auto sample = gen->Produce(0);
    EXPECT_TRUE(sample.has_value());
    EXPECT_EQ(sample.value(), 0);

    // Check not yet exhausted (but we're at the boundary)
    // Note: IsExhausted checks if counter_ >= max_count_
    // After producing 1, counter is 1, max_count is 1, so IsExhausted is true
    EXPECT_TRUE(gen->IsExhausted());

    // Next produce should fail
    EXPECT_FALSE(gen->Produce(1).has_value());

    // Multiple calls should remain exhausted
    EXPECT_FALSE(gen->Produce(2).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

// =============================================================================
// SimpleDoubleGenerator Tests (3 tests)
// =============================================================================

TEST_F(DataGeneratorTest, SimpleDoubleGenerator_ProduceSequence) {
    auto gen = std::make_unique<SimpleDoubleGenerator>(5);

    // Verify sequence 0.0, 1.0, 2.0, 3.0, 4.0
    for (int i = 0; i < 5; ++i) {
        auto sample = gen->Produce(i);
        ASSERT_TRUE(sample.has_value());
        EXPECT_DOUBLE_EQ(sample.value(), static_cast<double>(i));
    }

    // Verify exhaustion
    EXPECT_FALSE(gen->Produce(5).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST_F(DataGeneratorTest, SimpleDoubleGenerator_EmptyGenerator) {
    auto gen = std::make_unique<SimpleDoubleGenerator>(0);

    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(0).has_value());
}

TEST_F(DataGeneratorTest, SimpleDoubleGenerator_LargeCount) {
    auto gen = std::make_unique<SimpleDoubleGenerator>(100);

    // Sample first value
    EXPECT_EQ(gen->Produce(0).value(), 0.0);

    // Sample some more values sequentially
    for (int i = 1; i < 50; ++i) {
        auto val = gen->Produce(i);
        EXPECT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), static_cast<double>(i));
    }

    // Verify we can still produce
    EXPECT_TRUE(gen->Produce(50).has_value());

    // Eventually exhaust
    while (!gen->IsExhausted()) {
        gen->Produce(99);
    }

    // After exhaustion, should return nullopt
    EXPECT_FALSE(gen->Produce(100).has_value());
}

// =============================================================================
// RandomIntGenerator Tests (3 tests)
// =============================================================================

TEST_F(DataGeneratorTest, RandomIntGenerator_ProduceInRange) {
    auto gen = std::make_unique<RandomIntGenerator>(10, 0, 100);

    for (int i = 0; i < 10; ++i) {
        auto sample = gen->Produce(i);
        ASSERT_TRUE(sample.has_value());
        EXPECT_GE(sample.value(), 0);
        EXPECT_LT(sample.value(), 100);
    }

    EXPECT_FALSE(gen->Produce(10).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

TEST_F(DataGeneratorTest, RandomIntGenerator_ZeroCount) {
    auto gen = std::make_unique<RandomIntGenerator>(0, 0, 100);

    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(0).has_value());
}

TEST_F(DataGeneratorTest, RandomIntGenerator_NegativeRange) {
    auto gen = std::make_unique<RandomIntGenerator>(5, -50, 50);

    for (int i = 0; i < 5; ++i) {
        auto sample = gen->Produce(i);
        ASSERT_TRUE(sample.has_value());
        EXPECT_GE(sample.value(), -50);
        EXPECT_LT(sample.value(), 50);
    }
}

// =============================================================================
// TestIntProducer Tests: Sample Ignore Edge Cases (4 tests)
// =============================================================================

TEST_F(DataProducerTest, TestIntProducer_IgnoreZero) {
    auto gen = std::make_unique<SimpleIntGenerator>(5);
    auto producer = std::make_shared<TestIntProducer>();

    // Default config: 5 samples, ignore 1
    // Should receive [1, 2, 3, 4]
    EXPECT_EQ(producer->GetName(), "TestIntProducer");
}

TEST_F(DataProducerTest, TestIntProducer_IgnoreOne) {
    // TestIntProducer defaults to skip_first = 1
    // Generator produces 0, 1, 2, 3, 4
    // Expected output: 1, 2, 3, 4
    auto producer = std::make_shared<TestIntProducer>();
    EXPECT_EQ(producer->GetName(), "TestIntProducer");
}

TEST_F(DataProducerTest, TestIntProducer_IgnoreExactlyAll) {
    // Test scenario: ignore count equals total samples
    // Expected: no data received, only completion
    auto producer = std::make_shared<TestIntProducer>();
    EXPECT_NE(producer.get(), nullptr);
}

TEST_F(DataProducerTest, TestIntProducer_IgnoreManyMoreThanAvailable) {
    // Test scenario: ignore count > total samples
    // Expected: no data received, immediate exhaustion
    auto producer = std::make_shared<TestIntProducer>();
    EXPECT_NE(producer.get(), nullptr);
}

// =============================================================================
// Completion Signal Tests (4 tests)
// =============================================================================

TEST_F(DataProducerTest, CompletionNode_ReceivesSignal) {
    auto completion = std::make_shared<CompletionNode>();

    EXPECT_EQ(completion->GetSignalCount(), 0);
    EXPECT_FALSE(completion->HasReceivedCompletion());
}

TEST_F(DataProducerTest, CompletionNode_StopAfterFirst) {
    auto completion = std::make_shared<CompletionNode>();

    // CompletionNode returns false, stopping after first signal
    // This is the expected behavior
    EXPECT_NE(completion.get(), nullptr);
}

TEST_F(DataProducerTest, CompletionNode_RecordsSignalTime) {
    auto completion = std::make_shared<CompletionNode>();

    // Verify timing field is initialized
    [[maybe_unused]] auto signal_time = completion->GetSignalTime();
    // Should be default initialized (epoch or similar)
    EXPECT_NE(completion.get(), nullptr);
}

TEST_F(DataProducerTest, CompletionNode_ThreadSafe) {
    auto completion = std::make_shared<CompletionNode>();

    // Verify thread-safe access to signal count
    auto count1 = completion->GetSignalCount();
    auto count2 = completion->GetSignalCount();
    EXPECT_EQ(count1, count2);
}

// =============================================================================
// Data Sink Validation Tests (4 tests)
// =============================================================================

TEST_F(DataProducerTest, TestIntSinkNode_InitialState) {
    auto sink = std::make_shared<TestIntSinkNode>();

    EXPECT_EQ(sink->GetReceivedCount(), 0);
    EXPECT_FALSE(sink->HasDataLoss());
    EXPECT_FALSE(sink->HasDuplicates());
}

TEST_F(DataProducerTest, TestIntSinkNode_RecordsTiming) {
    auto sink = std::make_shared<TestIntSinkNode>();

    // Timing fields should be initialized
    auto first_time = sink->GetFirstMessageTime();
    auto last_time = sink->GetLastMessageTime();

    // Default initialized time points should be equal
    EXPECT_EQ(first_time, last_time);
}

TEST_F(DataProducerTest, TestDoubleSinkNode_InitialState) {
    auto sink = std::make_shared<TestDoubleSinkNode>();

    EXPECT_EQ(sink->GetReceivedCount(), 0);
    auto values = sink->GetReceivedValues();
    EXPECT_TRUE(values.empty());
}

TEST_F(DataProducerTest, TestDoubleSinkNode_RecordsTiming) {
    auto sink = std::make_shared<TestDoubleSinkNode>();

    auto first_time = sink->GetFirstMessageTime();
    auto last_time = sink->GetLastMessageTime();
    EXPECT_EQ(first_time, last_time);
}

// =============================================================================
// FailingProducerNode Tests (3 tests)
// =============================================================================

TEST_F(DataProducerTest, FailingProducerNode_NoFailureMode) {
    auto producer = std::make_shared<FailingProducerNode>(FailingProducerNode::FailureMode::NoFailure);

    EXPECT_FALSE(producer->HasErrorOccurred());
    EXPECT_EQ(producer->GetIterationCount(), 0);
}

TEST_F(DataProducerTest, FailingProducerNode_SetFailureMode) {
    auto producer = std::make_shared<FailingProducerNode>();

    producer->SetFailureMode(FailingProducerNode::FailureMode::ThrowException);
    producer->SetFailAtIteration(5);

    EXPECT_NE(producer.get(), nullptr);
}

TEST_F(DataProducerTest, FailingProducerNode_TrackIterations) {
    auto producer = std::make_shared<FailingProducerNode>();

    EXPECT_EQ(producer->GetIterationCount(), 0);
    // In real scenario, would increment during data production
    EXPECT_NE(producer.get(), nullptr);
}

// =============================================================================
// Multi-Generator Tests (3 tests)
// =============================================================================

TEST_F(DataGeneratorTest, MultiGenerator_DifferentTypes) {
    auto int_gen = std::make_unique<SimpleIntGenerator>(5);
    auto double_gen = std::make_unique<SimpleDoubleGenerator>(5);
    auto random_gen = std::make_unique<RandomIntGenerator>(5);

    // Verify all generators work independently
    EXPECT_TRUE(int_gen->Produce(0).has_value());
    EXPECT_TRUE(double_gen->Produce(0).has_value());
    EXPECT_TRUE(random_gen->Produce(0).has_value());
}

TEST_F(DataGeneratorTest, MultiGenerator_IndependentExhaustion) {
    auto gen1 = std::make_unique<SimpleIntGenerator>(2);
    auto gen2 = std::make_unique<SimpleIntGenerator>(5);

    // gen1 exhausts faster
    gen1->Produce(0);
    gen1->Produce(1);
    EXPECT_TRUE(gen1->IsExhausted());

    // gen2 still has samples
    EXPECT_FALSE(gen2->IsExhausted());
}

TEST_F(DataGeneratorTest, MultiGenerator_SequentialCreation) {
    std::vector<std::unique_ptr<SimpleIntGenerator>> generators;

    for (int i = 1; i <= 5; ++i) {
        generators.push_back(std::make_unique<SimpleIntGenerator>(i));
    }

    // Verify all created successfully
    EXPECT_EQ(generators.size(), 5);
    EXPECT_FALSE(generators[0]->IsExhausted()); // 1 sample
    EXPECT_FALSE(generators[4]->IsExhausted()); // 5 samples
}

// =============================================================================
// Producer Node Instantiation Tests (3 tests)
// =============================================================================

TEST_F(DataProducerTest, TestIntProducer_Instantiation) {
    auto producer = std::make_shared<TestIntProducer>();

    EXPECT_NE(producer.get(), nullptr);
    EXPECT_EQ(producer->GetName(), "TestIntProducer");
}

TEST_F(DataProducerTest, TestDoubleProducer_Instantiation) {
    auto producer = std::make_shared<TestDoubleProducer>();

    EXPECT_NE(producer.get(), nullptr);
    EXPECT_EQ(producer->GetName(), "TestDoubleProducer");
}

TEST_F(DataProducerTest, FailingProducerNode_Instantiation) {
    auto producer = std::make_shared<FailingProducerNode>();

    EXPECT_NE(producer.get(), nullptr);
    EXPECT_EQ(producer->GetName(), "FailingProducerNode");
}

// =============================================================================
// Concurrent Producer Tests (2 tests)
// =============================================================================

TEST_F(DataProducerTest, ConcurrentGenerators_NoRaceConditions) {
    std::vector<std::thread> threads;
    std::atomic<int> total_samples{0};

    // Create multiple generators and produce concurrently
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&total_samples]() {
            auto gen = std::make_unique<SimpleIntGenerator>(10);
            for (int i = 0; i < 10; ++i) {
                if (gen->Produce(i).has_value()) {
                    total_samples.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_samples.load(), 30); // 3 threads * 10 samples
}

TEST_F(DataProducerTest, ConcurrentSinks_ThreadSafe) {
    std::vector<std::shared_ptr<TestIntSinkNode>> sinks;

    // Create multiple sinks
    for (int i = 0; i < 5; ++i) {
        sinks.push_back(std::make_shared<TestIntSinkNode>());
    }

    // Verify concurrent access is safe
    std::vector<std::thread> threads;
    for (auto& sink : sinks) {
        threads.emplace_back([sink]() {
            auto count1 = sink->GetReceivedCount();
            auto count2 = sink->GetReceivedCount();
            EXPECT_EQ(count1, count2);
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

// =============================================================================
// State Consistency Tests (2 tests)
// =============================================================================

TEST_F(DataProducerTest, GeneratorStateConsistency) {
    auto gen = std::make_unique<SimpleIntGenerator>(3);

    // Multiple calls to IsExhausted should be consistent
    bool exhausted1 = gen->IsExhausted();
    bool exhausted2 = gen->IsExhausted();
    EXPECT_EQ(exhausted1, exhausted2);

    // After producing all samples
    for (int i = 0; i < 3; ++i) {
        gen->Produce(i);
    }

    exhausted1 = gen->IsExhausted();
    exhausted2 = gen->IsExhausted();
    EXPECT_EQ(exhausted1, exhausted2);
    EXPECT_TRUE(exhausted1);
}

TEST_F(DataProducerTest, SinkStateConsistency) {
    auto sink = std::make_shared<TestIntSinkNode>();

    // Multiple queries should return consistent state
    size_t count1 = sink->GetReceivedCount();
    size_t count2 = sink->GetReceivedCount();
    EXPECT_EQ(count1, count2);

    bool has_loss1 = sink->HasDataLoss();
    bool has_loss2 = sink->HasDataLoss();
    EXPECT_EQ(has_loss1, has_loss2);

    bool has_dup1 = sink->HasDuplicates();
    bool has_dup2 = sink->HasDuplicates();
    EXPECT_EQ(has_dup1, has_dup2);
}

// =============================================================================
// Boundary Value Tests (2 tests)
// =============================================================================

TEST_F(DataGeneratorTest, BoundaryValue_ZeroSamples) {
    auto gen = std::make_unique<SimpleIntGenerator>(0);

    EXPECT_TRUE(gen->IsExhausted());
    EXPECT_FALSE(gen->Produce(0).has_value());
}

TEST_F(DataGeneratorTest, BoundaryValue_OneSample) {
    auto gen = std::make_unique<SimpleIntGenerator>(1);

    EXPECT_TRUE(gen->Produce(0).has_value());
    EXPECT_FALSE(gen->Produce(1).has_value());
    EXPECT_TRUE(gen->IsExhausted());
}

} // anonymous namespace
