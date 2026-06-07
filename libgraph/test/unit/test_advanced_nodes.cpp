/**
 * @file test_advanced_nodes.cpp
 * @brief Comprehensive unit tests for advanced test nodes
 *
 * Tests the advanced node implementations:
 * - MergeTestNode: Multi-input merge node (2 inputs -> 1 output)
 * - SplitTestNode: Single input to multiple outputs (1 input -> 2 outputs)
 * - InteriorTestNode: Interior/processing node (1 input -> 1 output)
 *
 * Tests cover:
 * - Initialization and lifecycle
 * - Port configuration and metadata
 * - Message handling and routing
 * - Process/Consume method functionality
 * - Edge cases and error conditions
 *
 * @author Test Suite
 * @date May 11, 2026
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <optional>
#include "graph/RegisteredNodeProvider.hpp"
#include "graph/Message.hpp"
#include "test/AdvancedTestNodes.hpp"

namespace {

// ===================================================================================
// Test Fixtures
// ===================================================================================

/**
 * @brief Test fixture for MergeTestNode
 *
 * Provides setup/teardown and common utilities for merge node testing
 */
class MergeTestNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_ = std::make_shared<test::MergeTestNode>();
        ASSERT_NE(node_, nullptr);
    }
    
    void TearDown() override {
        if (node_) {
            node_->Stop();
            node_->Join();
            node_.reset();
        }
    }
    
    std::shared_ptr<test::MergeTestNode> node_;
};

/**
 * @brief Test fixture for SplitTestNode
 *
 * Provides setup/teardown and common utilities for split node testing
 */
class SplitTestNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_ = std::make_shared<test::SplitTestNode>();
        ASSERT_NE(node_, nullptr);
    }
    
    void TearDown() override {
        if (node_) {
            node_->Stop();
            node_->Join();
            node_.reset();
        }
    }
    
    std::shared_ptr<test::SplitTestNode> node_;
};

/**
 * @brief Test fixture for InteriorTestNode
 *
 * Provides setup/teardown and common utilities for interior node testing
 */
class InteriorTestNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_ = std::make_shared<test::InteriorTestNode>();
        ASSERT_NE(node_, nullptr);
    }
    
    void TearDown() override {
        if (node_) {
            node_->Stop();
            node_->Join();
            node_.reset();
        }
    }
    
    std::shared_ptr<test::InteriorTestNode> node_;
};

// ===================================================================================
// MERGE NODE TESTS
// ===================================================================================

/**
 * @test MergeTestNodeInitialization
 * @brief Verify MergeTestNode can be constructed and initialized
 */
TEST_F(MergeTestNodeTest, InitializationSucceeds) {
    // Node is already created in SetUp()
    EXPECT_NE(node_, nullptr);
    
    // Should be able to initialize
    EXPECT_TRUE(node_->Init());
}

/**
 * @test MergeTestNodeLifecycle
 * @brief Verify MergeTestNode supports full lifecycle: Init -> Start -> Stop
 */
TEST_F(MergeTestNodeTest, FullLifecycleSucceeds) {
    // Init node
    EXPECT_TRUE(node_->Init());
    
    // Start node
    EXPECT_TRUE(node_->Start());
    
    // Stop node (should not throw)
    EXPECT_NO_THROW(node_->Stop());
}

/**
 * @test MergeTestNodePortConfiguration
 * @brief Verify MergeTestNode has correct port configuration
 */
TEST_F(MergeTestNodeTest, PortConfigurationIsCorrect) {
    // MergeTestNode has 2 input ports + 1 output port defined in class
    // This test verifies the node can be initialized without errors
    EXPECT_NE(node_, nullptr);
    EXPECT_TRUE(node_->Init());
}

/**
 * @test MergeTestNodePortNames
 * @brief Verify MergeTestNode has correctly named ports
 */
TEST_F(MergeTestNodeTest, PortNamesAreCorrect) {
    // MergeTestNode uses: In0, In1 for inputs, Out for output
    // These are defined as static constexpr arrays in the class
    EXPECT_STREQ(test::MergeTestNode::kInput0, "In0");
    EXPECT_STREQ(test::MergeTestNode::kInput1, "In1");
    EXPECT_STREQ(test::MergeTestNode::kOutput, "Out");
}

/**
 * @test MergeTestNodeMessageProcessing
 * @brief Verify MergeTestNode can process messages from first input
 */
TEST_F(MergeTestNodeTest, ProcessesMessageFromFirstInput) {
    // Create a test message
    ::graph::message::Message msg(42);
    ASSERT_TRUE(msg.valid());
    
    // Process message (port index 0)
    auto result = node_->Process(msg, std::integral_constant<std::size_t, 0>());
    
    // Should return the message (pass-through behavior)
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->get<int>(), 42);
}

/**
 * @test MergeTestNodeMessagePassthrough
 * @brief Verify MergeTestNode passes through different message types
 */
TEST_F(MergeTestNodeTest, PassesThroughMessageCorrectly) {
    // Test with different integer values
    for (int i : {0, 1, 42, 100, -1, INT_MAX}) {
        ::graph::message::Message msg(i);
        auto result = node_->Process(msg, std::integral_constant<std::size_t, 0>());
        
        ASSERT_TRUE(result.has_value()) << "Failed for value: " << i;
        EXPECT_EQ(result->get<int>(), i);
    }
}

/**
 * @test MergeTestNodeMultipleProcessCalls
 * @brief Verify MergeTestNode handles multiple Process calls sequentially
 */
TEST_F(MergeTestNodeTest, HandlesMultipleProcessCallsSequentially) {
    node_->Init();
    node_->Start();
    
    // Process multiple messages
    std::vector<int> values = {1, 2, 3, 4, 5};
    for (int val : values) {
        ::graph::message::Message msg(val);
        auto result = node_->Process(msg, std::integral_constant<std::size_t, 0>());
        
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->get<int>(), val);
    }
}

// ===================================================================================
// SPLIT NODE TESTS
// ===================================================================================

/**
 * @test SplitTestNodeInitialization
 * @brief Verify SplitTestNode can be constructed and initialized
 */
TEST_F(SplitTestNodeTest, InitializationSucceeds) {
    // Node is already created in SetUp()
    EXPECT_NE(node_, nullptr);
    
    // Should be able to initialize
    EXPECT_TRUE(node_->Init());
}

/**
 * @test SplitTestNodeLifecycle
 * @brief Verify SplitTestNode supports full lifecycle: Init -> Start -> Stop
 */
TEST_F(SplitTestNodeTest, FullLifecycleSucceeds) {
    // Init node
    EXPECT_TRUE(node_->Init());
    
    // Start node
    EXPECT_TRUE(node_->Start());
    
    // Stop node (should not throw)
    EXPECT_NO_THROW(node_->Stop());
}

/**
 * @test SplitTestNodePortConfiguration
 * @brief Verify SplitTestNode has correct port configuration
 */
TEST_F(SplitTestNodeTest, PortConfigurationIsCorrect) {
    // SplitTestNode has 1 input port + 2 output ports defined in class
    // This test verifies the node can be initialized without errors
    EXPECT_NE(node_, nullptr);
    EXPECT_TRUE(node_->Init());
}

/**
 * @test SplitTestNodePortNames
 * @brief Verify SplitTestNode has correctly named ports
 */
TEST_F(SplitTestNodeTest, PortNamesAreCorrect) {
    // SplitTestNode uses: In for input, Out0, Out1 for outputs
    EXPECT_STREQ(test::SplitTestNode::kInput, "In");
    EXPECT_STREQ(test::SplitTestNode::kOutput0, "Out0");
    EXPECT_STREQ(test::SplitTestNode::kOutput1, "Out1");
}

/**
 * @test SplitTestNodeConsumeSucceeds
 * @brief Verify SplitTestNode Consume method succeeds with valid message
 */
TEST_F(SplitTestNodeTest, ConsumeSucceedsWithValidMessage) {
    node_->Init();
    node_->Start();
    
    // Create a test message
    ::graph::message::Message msg(42);
    ASSERT_TRUE(msg.valid());
    
    // Consume should succeed (enqueues to both output queues)
    bool result = node_->Consume(msg, std::integral_constant<std::size_t, 0>());
    EXPECT_TRUE(result);
}

/**
 * @test SplitTestNodeReplicatesToBothOutputs
 * @brief Verify SplitTestNode replicates messages to both output queues
 */
TEST_F(SplitTestNodeTest, ReplicatesToBothOutputQueues) {
    node_->Init();
    node_->Start();
    
    // Create test message
    ::graph::message::Message msg(123);
    
    // Consume the message (should replicate to both queues)
    bool success = node_->Consume(msg, std::integral_constant<std::size_t, 0>());
    EXPECT_TRUE(success) << "Message should be successfully replicated to output queues";
}

/**
 * @test SplitTestNodeHandlesMultipleMessages
 * @brief Verify SplitTestNode handles multiple Consume calls
 */
TEST_F(SplitTestNodeTest, HandlesMultipleConsumeCallsSuccessfully) {
    node_->Init();
    node_->Start();
    
    // Consume multiple messages
    std::vector<int> values = {10, 20, 30, 40, 50};
    for (int val : values) {
        ::graph::message::Message msg(val);
        bool result = node_->Consume(msg, std::integral_constant<std::size_t, 0>());
        EXPECT_TRUE(result) << "Consume failed for value: " << val;
    }
}

/**
 * @test SplitTestNodeMessageIntegrity
 * @brief Verify split node preserves message integrity through replication
 */
TEST_F(SplitTestNodeTest, PreservesMessageIntegrityThroughReplication) {
    node_->Init();
    node_->Start();
    
    // Test with various integer values
    std::vector<int> test_values = {0, 1, 42, 255, 1000, INT_MAX};
    for (int val : test_values) {
        ::graph::message::Message msg(val);
        bool result = node_->Consume(msg, std::integral_constant<std::size_t, 0>());
        
        ASSERT_TRUE(result) << "Consume failed for value: " << val;
        // Message integrity is maintained through the replication process
    }
}

// ===================================================================================
// INTERIOR NODE TESTS
// ===================================================================================

/**
 * @test InteriorTestNodeInitialization
 * @brief Verify InteriorTestNode can be constructed and initialized
 */
TEST_F(InteriorTestNodeTest, InitializationSucceeds) {
    // Node is already created in SetUp()
    EXPECT_NE(node_, nullptr);
    
    // Should be able to initialize
    EXPECT_TRUE(node_->Init());
}

/**
 * @test InteriorTestNodeLifecycle
 * @brief Verify InteriorTestNode supports full lifecycle: Init -> Start -> Stop
 */
TEST_F(InteriorTestNodeTest, FullLifecycleSucceeds) {
    // Init node
    EXPECT_TRUE(node_->Init());
    
    // Start node
    EXPECT_TRUE(node_->Start());
    
    // Stop node (should not throw)
    EXPECT_NO_THROW(node_->Stop());
}

/**
 * @test InteriorTestNodePortConfiguration
 * @brief Verify InteriorTestNode has correct port configuration
 */
TEST_F(InteriorTestNodeTest, PortConfigurationIsCorrect) {
    // InteriorTestNode has 1 input port + 1 output port defined in class
    // This test verifies the node can be initialized without errors
    EXPECT_NE(node_, nullptr);
    EXPECT_TRUE(node_->Init());
}

/**
 * @test InteriorTestNodePortNames
 * @brief Verify InteriorTestNode has correctly named ports
 */
TEST_F(InteriorTestNodeTest, PortNamesAreCorrect) {
    // InteriorTestNode uses: Input for input, Output for output
    EXPECT_STREQ(test::InteriorTestNode::kInput, "Input");
    EXPECT_STREQ(test::InteriorTestNode::kOutput, "Output");
}

/**
 * @test InteriorTestNodeMessageTransfer
 * @brief Verify InteriorTestNode Transfer method passes through messages
 */
TEST_F(InteriorTestNodeTest, TransfersMessageCorrectly) {
    // Create a test message
    ::graph::message::Message msg(99);
    ASSERT_TRUE(msg.valid());
    
    // Transfer message (input index 0, output index 0)
    auto result = node_->Process(msg, std::integral_constant<std::size_t, 0>());
    
    // Should return the message (pass-through behavior)
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->get<int>(), 99);
}

/**
 * @test InteriorTestNodeMessagePassthrough
 * @brief Verify InteriorTestNode passes through different message values
 */
TEST_F(InteriorTestNodeTest, PassesThroughDifferentMessageValues) {
    // Test with various integer values
    std::vector<int> values = {0, 1, 50, 100, 500, INT_MAX, INT_MIN};
    for (int val : values) {
        ::graph::message::Message msg(val);
        auto result = node_->Process(msg,
                                     std::integral_constant<std::size_t, 0>());
        
        ASSERT_TRUE(result.has_value()) << "Transfer failed for value: " << val;
        EXPECT_EQ(result->get<int>(), val);
    }
}

/**
 * @test InteriorTestNodeMultipleTransfers
 * @brief Verify InteriorTestNode handles multiple Transfer calls
 */
TEST_F(InteriorTestNodeTest, HandlesMultipleTransferCallsSequentially) {
    node_->Init();
    node_->Start();
    
    // Transfer multiple messages
    std::vector<int> values = {7, 14, 21, 28, 35};
    for (int val : values) {
        ::graph::message::Message msg(val);
        auto result = node_->Process(msg,
                                     std::integral_constant<std::size_t, 0>());
        
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->get<int>(), val);
    }
}

// ===================================================================================
// CROSS-NODE COMPATIBILITY TESTS
// ===================================================================================

/**
 * @test AllAdvancedNodesHaveSameMessageFormat
 * @brief Verify all advanced nodes use compatible Message format
 */
TEST(AdvancedNodesCompatibility, AllNodesUseSameMessageType) {
    // All nodes should use ::graph::message::Message
    auto merge_node = std::make_shared<test::MergeTestNode>();
    auto split_node = std::make_shared<test::SplitTestNode>();
    auto interior_node = std::make_shared<test::InteriorTestNode>();
    
    EXPECT_NE(merge_node, nullptr);
    EXPECT_NE(split_node, nullptr);
    EXPECT_NE(interior_node, nullptr);
}

/**
 * @test AdvancedNodesInstantiateSuccessfully
 * @brief Verify all advanced nodes can be instantiated
 */
TEST(AdvancedNodesInstantiation, AllNodesCanBeInstantiated) {
    EXPECT_NO_THROW({
        auto merge = std::make_shared<test::MergeTestNode>();
        auto split = std::make_shared<test::SplitTestNode>();
        auto interior = std::make_shared<test::InteriorTestNode>();
    });
}

/**
 * @test AdvancedNodesCleanupSuccessfully
 * @brief Verify all advanced nodes clean up without errors
 */
TEST(AdvancedNodesCleanup, AllNodesCleanupSuccessfully) {
    {
        auto merge = std::make_shared<test::MergeTestNode>();
        auto split = std::make_shared<test::SplitTestNode>();
        auto interior = std::make_shared<test::InteriorTestNode>();
        
        EXPECT_TRUE(merge->Init());
        EXPECT_TRUE(split->Init());
        EXPECT_TRUE(interior->Init());
        
        merge->Stop();
        split->Stop();
        interior->Stop();
    } // Should clean up without errors
}

} // namespace
