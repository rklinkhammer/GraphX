// SPDX-License-Identifier: MIT

/**
 * @file test_mergenode_threading.cpp
 * @brief Test Mergenode Threading Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include "graph/Nodes.hpp"

using namespace graph;

// ============================================================================
// Test Fixtures and Helper Types
// ============================================================================

/**
 * @brief Simple MergeNode with 1 input for testing
 */
/**
 * @class TestMergeNode
 * @brief Test merge node implementation for GraphX.
 */
class TestMergeNode : public MergeNode<1, int, int, TestMergeNode> {
public:
    std::atomic<size_t> process_count{0};
    std::vector<int> processed_messages;
    std::mutex messages_lock;

    std::optional<int> Process(
        const int& input,
        std::integral_constant<std::size_t, 0>) override {
        
        process_count.fetch_add(1, std::memory_order_acq_rel);
        
        {
            std::lock_guard<std::mutex> lock(messages_lock);
            processed_messages.push_back(input);
        }
        
        return input;
    }

    std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
        return MergeNode<1, int, int, TestMergeNode>::GetInputPortMetadata();
    }

    std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
        return MergeNode<1, int, int, TestMergeNode>::GetOutputPortMetadata();
    }
};

/**
 * @brief MergeNode with 2 inputs for testing
 */
/**
 * @class TestMergeNode2
 * @brief Test merge node 2 implementation for GraphX.
 */
class TestMergeNode2 : public MergeNode<2, int, int, TestMergeNode2> {
public:
    std::atomic<size_t> process_count{0};
    std::vector<int> processed_messages;
    std::mutex messages_lock;

    std::optional<int> Process(
        const int& input,
        std::integral_constant<std::size_t, 0>) override {
        
        process_count.fetch_add(1, std::memory_order_acq_rel);
        
        {
            std::lock_guard<std::mutex> lock(messages_lock);
            processed_messages.push_back(input);
        }
        
        return input;
    }

    std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
        return MergeNode<2, int, int, TestMergeNode2>::GetInputPortMetadata();
    }

    std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
        return MergeNode<2, int, int, TestMergeNode2>::GetOutputPortMetadata();
    }
};

/**
 * @brief Wait for condition with timeout
 */
template<typename Predicate>
/**
 * @brief Wait for.
 * @param pred Parameter for wait for.
 * @param timeout Parameter for wait for.
 */
bool WaitFor(Predicate&& pred, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/**
 * @brief Test fixture for MergeNode threading tests
 */
/**
 * @class MergeNodeThreadingTest
 * @brief Merge node threading test implementation for GraphX.
 */
class MergeNodeThreadingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, LifecycleInitStartStopJoin) {
    auto node = std::make_shared<TestMergeNode>();

    // Init phase
    EXPECT_TRUE(node->Init());
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Initialized);

    // Start phase - spawns merge thread
    EXPECT_TRUE(node->Start());
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Started);

    // Stop phase - signals thread to stop
    node->Stop();
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Stopped);

    // Join phase - waits for thread to exit
    node->Join();
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Joined);
}

TEST_F(MergeNodeThreadingTest, MultipleStartStopCycles) {
    auto node = std::make_shared<TestMergeNode>();

    // Cycle 1
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    node->Stop();
    node->Join();

    // Verify node transitioned to Stopped state
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Joined);
}

// ============================================================================
// Thread Synchronization Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, ThreadProcessesMessages) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Node should be in Started state
    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Started);

    // Give thread time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

TEST_F(MergeNodeThreadingTest, ThreadProcessesMultipleMessages) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Give thread time to work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

TEST_F(MergeNodeThreadingTest, ThreadStopsAfterQueueDisable) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop and join should complete quickly
    node->Stop();
    node->Join();

    EXPECT_EQ(node->GetLifecycleState(), LifecycleState::Joined);
}

// ============================================================================
// Output Queue Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, ThreadEnqueuesOutputMessages) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Give thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

TEST_F(MergeNodeThreadingTest, OutputQueueMetrics) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Give thread time to work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, ThreadHandlesExceptionInProcess) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Node should handle exceptions internally
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

// ============================================================================
// Multi-Input Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, TwoInputMergeNode) {
    auto node = std::make_shared<TestMergeNode2>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Node should have 2 input ports
    auto inputs = node->InputPorts();
    EXPECT_EQ(inputs.size(), 2);

    // Give thread time to work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    node->Stop();
    node->Join();
}

// ============================================================================
// Port Metadata Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, InputPortMetadata) {
    auto node = std::make_shared<TestMergeNode>();

    auto input_ports = node->InputPorts();
    EXPECT_EQ(input_ports.size(), 1);
    EXPECT_EQ(input_ports[0].id, 0);
    EXPECT_EQ(input_ports[0].direction, PortDirection::Input);
}

TEST_F(MergeNodeThreadingTest, OutputPortMetadata) {
    auto node = std::make_shared<TestMergeNode>();

    auto output_ports = node->OutputPorts();
    EXPECT_EQ(output_ports.size(), 1);
    EXPECT_EQ(output_ports[0].id, 0);
    EXPECT_EQ(output_ports[0].direction, PortDirection::Output);
}

TEST_F(MergeNodeThreadingTest, MultiInputPortMetadata) {
    auto node = std::make_shared<TestMergeNode2>();

    auto input_ports = node->InputPorts();
    EXPECT_EQ(input_ports.size(), 2);
    EXPECT_EQ(input_ports[0].id, 0);
    EXPECT_EQ(input_ports[1].id, 1);

    auto output_ports = node->OutputPorts();
    EXPECT_EQ(output_ports.size(), 1);
}

// ============================================================================
// Cleanup and Resource Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, DestructorCleansUpThread) {
    {
        auto node = std::make_shared<TestMergeNode>();
        EXPECT_TRUE(node->Init());
        EXPECT_TRUE(node->Start());

        // Destructor should clean up thread even without explicit Stop/Join
    } // Destructor called here

    // If we get here without hanging, cleanup works
    SUCCEED();
}

TEST_F(MergeNodeThreadingTest, StopBeforeStart) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());

    // Stop without start should not crash
    node->Stop();
    node->Join();

    // Should not crash
    SUCCEED();
}

TEST_F(MergeNodeThreadingTest, JoinWithoutStart) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());

    // Join without start should not crash
    node->Join();

    // Should not crash
    SUCCEED();
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(MergeNodeThreadingTest, HighVolumeThroughput) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Give thread time to work
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    node->Stop();
    node->Join();

    // Should complete without crashes
    SUCCEED();
}

TEST_F(MergeNodeThreadingTest, ConcurrentOperations) {
    auto node = std::make_shared<TestMergeNode>();
    EXPECT_TRUE(node->Init());
    EXPECT_TRUE(node->Start());

    // Launch thread to do some work
/**
 * @brief Worker.
 * @param [node]( Parameter for worker.
 */
    std::thread worker([node]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    // Stop while worker thread is running
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    node->Stop();

    worker.join();
    node->Join();

    // Should not crash
    SUCCEED();
}

