// SPDX-License-Identifier: MIT

/**
 * @file test_port_metadata_reflection.cpp
 * @brief Isolated tests for port metadata reflection on multi-port nodes
 *
 * This test suite specifically targets Issue #1 from Stage 5.5a analysis:
 * PortMetadata Reflection Failure for multi-port nodes (InteriorTestNode, MergeTestNode, SplitTestNode)
 *
 * The Topology3_LinearSequentialPipeline test crashes during plugin facade
 * initialization when calling GetInputPortMetadata() on InteriorTestNode.
 *
 * This test suite isolates the problem by:
 * 1. Testing port metadata retrieval without plugin loading
 * 2. Testing each node type independently
 * 3. Verifying reflection works for single-port vs multi-port nodes
 * 4. Exposing the exact failure point in the reflection system
 *
 * @author Test Suite
 * @date May 23, 2026
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "test/AdvancedTestNodes.hpp"
#include "graph/Nodes.hpp"
#include "graph/Reflection.hpp"
#include <log4cxx/logger.h>

namespace {

/**
 * @class PortMetadataReflectionTest
 * @brief Test port metadata reflection for all test node types
 *
 * This suite validates that port metadata can be correctly retrieved
 * from each test node type using the reflection system.
 */
class PortMetadataReflectionTest : public ::testing::Test {
protected:
    log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("test.port_metadata");

    void SetUp() override {
        LOG4CXX_INFO(logger, "PortMetadataReflectionTest SetUp");
    }

    void TearDown() override {
        LOG4CXX_INFO(logger, "PortMetadataReflectionTest TearDown");
    }
};

// ============================================================================
// Test: SourceTestNode (Single Output Port)
// ============================================================================

/**
 * @test SourceTestNode Port Metadata
 * @brief Verify that SourceTestNode (1 output port) correctly reports metadata
 *
 * SourceTestNode should have:
 * - 0 input ports
 * - 1 output port (kDataPort = "Data", type Message)
 *
 * This is a baseline test to verify single-output nodes work correctly.
 */
TEST_F(PortMetadataReflectionTest, SourceTestNodeInputPortMetadata) {
    auto node = std::make_shared<test::SourceTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create SourceTestNode";

    LOG4CXX_INFO(logger, "Testing SourceTestNode input port metadata");
    
    try {
        auto input_metadata = node->GetInputPortMetadata();
        EXPECT_EQ(input_metadata.size(), 0) 
            << "SourceTestNode should have 0 input ports";
        LOG4CXX_DEBUG(logger, "SourceTestNode input ports: " << input_metadata.size());
    } catch (const std::exception& e) {
        FAIL() << "GetInputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetInputPortMetadata() threw unknown exception";
    }
}

TEST_F(PortMetadataReflectionTest, SourceTestNodeOutputPortMetadata) {
    auto node = std::make_shared<test::SourceTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create SourceTestNode";

    LOG4CXX_INFO(logger, "Testing SourceTestNode output port metadata");
    
    try {
        auto output_metadata = node->GetOutputPortMetadata();
        EXPECT_EQ(output_metadata.size(), 1) 
            << "SourceTestNode should have 1 output port";
        
        if (output_metadata.size() > 0) {
            EXPECT_FALSE(output_metadata[0].port_name.empty())
                << "Output port name should not be empty";
            LOG4CXX_DEBUG(logger, "SourceTestNode output port 0: " 
                << output_metadata[0].port_name);
        }
    } catch (const std::exception& e) {
        FAIL() << "GetOutputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetOutputPortMetadata() threw unknown exception";
    }
}

// ============================================================================
// Test: SinkTestNode (Single Input Port)
// ============================================================================

/**
 * @test SinkTestNode Port Metadata
 * @brief Verify that SinkTestNode (1 input port) correctly reports metadata
 *
 * SinkTestNode should have:
 * - 1 input port (kStatePort = "State", type Message)
 * - 0 output ports
 *
 * This is a baseline test to verify single-input nodes work correctly.
 */
TEST_F(PortMetadataReflectionTest, SinkTestNodeInputPortMetadata) {
    auto node = std::make_shared<test::SinkTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create SinkTestNode";

    LOG4CXX_INFO(logger, "Testing SinkTestNode input port metadata");
    
    try {
        auto input_metadata = node->GetInputPortMetadata();
        EXPECT_EQ(input_metadata.size(), 1) 
            << "SinkTestNode should have 1 input port";
        
        if (input_metadata.size() > 0) {
            EXPECT_FALSE(input_metadata[0].port_name.empty())
                << "Input port name should not be empty";
            LOG4CXX_DEBUG(logger, "SinkTestNode input port 0: " 
                << input_metadata[0].port_name);
        }
    } catch (const std::exception& e) {
        FAIL() << "GetInputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetInputPortMetadata() threw unknown exception";
    }
}

TEST_F(PortMetadataReflectionTest, SinkTestNodeOutputPortMetadata) {
    auto node = std::make_shared<test::SinkTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create SinkTestNode";

    LOG4CXX_INFO(logger, "Testing SinkTestNode output port metadata");
    
    try {
        auto output_metadata = node->GetOutputPortMetadata();
        EXPECT_EQ(output_metadata.size(), 0) 
            << "SinkTestNode should have 0 output ports";
        LOG4CXX_DEBUG(logger, "SinkTestNode output ports: " << output_metadata.size());
    } catch (const std::exception& e) {
        FAIL() << "GetOutputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetOutputPortMetadata() threw unknown exception";
    }
}

// ============================================================================
// Test: InteriorTestNode (Multi-Port: 1 Input + 1 Output) - **CRITICAL**
// ============================================================================

/**
 * @test InteriorTestNode Input Port Metadata
 * @brief Verify that InteriorTestNode (1 input + 1 output) correctly reports input metadata
 *
 * **CRITICAL TEST**: This is the exact point where Topology3_LinearSequentialPipeline
 * crashes during plugin facade initialization.
 *
 * InteriorTestNode should have:
 * - 1 input port (kInput = "Input", type Message)
 * - 1 output port (kOutput = "Output", type Message)
 *
 * This test runs the reflection WITHOUT plugin loading to isolate the problem.
 */
TEST_F(PortMetadataReflectionTest, InteriorTestNodeInputPortMetadata) {
    LOG4CXX_INFO(logger, "=== CRITICAL TEST: InteriorTestNode input port metadata ===");
    
    auto node = std::make_shared<test::InteriorTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create InteriorTestNode";

    LOG4CXX_INFO(logger, "InteriorTestNode instance created successfully");
    
    try {
        LOG4CXX_INFO(logger, "Calling GetInputPortMetadata()...");
        auto input_metadata = node->GetInputPortMetadata();
        
        LOG4CXX_INFO(logger, "GetInputPortMetadata() succeeded, port count: " 
            << input_metadata.size());
        
        EXPECT_EQ(input_metadata.size(), 1) 
            << "InteriorTestNode should have 1 input port";
        
        if (input_metadata.size() > 0) {
            EXPECT_FALSE(input_metadata[0].port_name.empty())
                << "Input port name should not be empty";
            EXPECT_EQ(input_metadata[0].port_name, test::g_interior_input)
                << "Input port should be named 'Input'";
            LOG4CXX_DEBUG(logger, "InteriorTestNode input port 0: " 
                << input_metadata[0].port_name);
        }
    } catch (const std::exception& e) {
        FAIL() << "GetInputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetInputPortMetadata() threw unknown exception";
    }
}

/**
 * @test InteriorTestNode Output Port Metadata
 * @brief Verify that InteriorTestNode correctly reports output metadata
 *
 * **CRITICAL TEST**: If GetInputPortMetadata() passes, test output metadata.
 * This further isolates whether the issue is with input or output reflection.
 */
TEST_F(PortMetadataReflectionTest, InteriorTestNodeOutputPortMetadata) {
    LOG4CXX_INFO(logger, "=== CRITICAL TEST: InteriorTestNode output port metadata ===");
    
    auto node = std::make_shared<test::InteriorTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create InteriorTestNode";

    LOG4CXX_INFO(logger, "InteriorTestNode instance created successfully");
    
    try {
        LOG4CXX_INFO(logger, "Calling GetOutputPortMetadata()...");
        auto output_metadata = node->GetOutputPortMetadata();
        
        LOG4CXX_INFO(logger, "GetOutputPortMetadata() succeeded, port count: " 
            << output_metadata.size());
        
        EXPECT_EQ(output_metadata.size(), 1) 
            << "InteriorTestNode should have 1 output port";
        
        if (output_metadata.size() > 0) {
            EXPECT_FALSE(output_metadata[0].port_name.empty())
                << "Output port name should not be empty";
            EXPECT_EQ(output_metadata[0].port_name, test::g_interior_output)
                << "Output port should be named 'Output'";
            LOG4CXX_DEBUG(logger, "InteriorTestNode output port 0: " 
                << output_metadata[0].port_name);
        }
    } catch (const std::exception& e) {
        FAIL() << "GetOutputPortMetadata() threw exception: " << e.what();
    } catch (...) {
        FAIL() << "GetOutputPortMetadata() threw unknown exception";
    }
}

/**
 * @test InteriorTestNode Port Count
 * @brief Verify that InteriorTestNode correctly reports port counts
 *
 * This tests the GetInputPortCount() and GetOutputPortCount() methods
 * which may be called before GetInputPortMetadata()/GetOutputPortMetadata().
 */
TEST_F(PortMetadataReflectionTest, InteriorTestNodePortCounts) {
    LOG4CXX_INFO(logger, "Testing InteriorTestNode port counts");
    
    auto node = std::make_shared<test::InteriorTestNode>();
    ASSERT_NE(node, nullptr) << "Failed to create InteriorTestNode";

    try {
        auto input_count = node->GetInputPortCount();
        EXPECT_EQ(input_count, 1) 
            << "InteriorTestNode should have 1 input port";
        LOG4CXX_DEBUG(logger, "InteriorTestNode input ports: " << input_count);
    } catch (const std::exception& e) {
        FAIL() << "GetInputPortCount() threw exception: " << e.what();
    }

    try {
        auto output_count = node->GetOutputPortCount();
        EXPECT_EQ(output_count, 1) 
            << "InteriorTestNode should have 1 output port";
        LOG4CXX_DEBUG(logger, "InteriorTestNode output ports: " << output_count);
    } catch (const std::exception& e) {
        FAIL() << "GetOutputPortCount() threw exception: " << e.what();
    }
}

// ============================================================================
// Diagnostic Test: Detailed Reflection Analysis
// ============================================================================

/**
 * @test Detailed Reflection Analysis for All Test Nodes
 * @brief Comprehensive diagnostic test showing exactly which nodes have reflection issues
 *
 * This test provides detailed logging of the reflection process for debugging.
 */
TEST_F(PortMetadataReflectionTest, DetailedReflectionAnalysisAllNodes) {
    LOG4CXX_INFO(logger, "=== DETAILED REFLECTION ANALYSIS ===");

    // Test SourceTestNode
    {
        LOG4CXX_INFO(logger, "\n--- SourceTestNode ---");
        auto node = std::make_shared<test::SourceTestNode>();
        
        try {
            LOG4CXX_INFO(logger, "Input ports: " << node->GetInputPortCount());
            auto in_metadata = node->GetInputPortMetadata();
            LOG4CXX_INFO(logger, "  Input metadata retrieved: " << in_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Input metadata failed: " << e.what());
        }
        
        try {
            LOG4CXX_INFO(logger, "Output ports: " << node->GetOutputPortCount());
            auto out_metadata = node->GetOutputPortMetadata();
            LOG4CXX_INFO(logger, "  Output metadata retrieved: " << out_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Output metadata failed: " << e.what());
        }
    }

    // Test SinkTestNode
    {
        LOG4CXX_INFO(logger, "\n--- SinkTestNode ---");
        auto node = std::make_shared<test::SinkTestNode>();
        
        try {
            LOG4CXX_INFO(logger, "Input ports: " << node->GetInputPortCount());
            auto in_metadata = node->GetInputPortMetadata();
            LOG4CXX_INFO(logger, "  Input metadata retrieved: " << in_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Input metadata failed: " << e.what());
        }
        
        try {
            LOG4CXX_INFO(logger, "Output ports: " << node->GetOutputPortCount());
            auto out_metadata = node->GetOutputPortMetadata();
            LOG4CXX_INFO(logger, "  Output metadata retrieved: " << out_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Output metadata failed: " << e.what());
        }
    }

    // Test InteriorTestNode
    {
        LOG4CXX_INFO(logger, "\n--- InteriorTestNode ---");
        auto node = std::make_shared<test::InteriorTestNode>();
        
        try {
            LOG4CXX_INFO(logger, "Input ports: " << node->GetInputPortCount());
            auto in_metadata = node->GetInputPortMetadata();
            LOG4CXX_INFO(logger, "  ✓ Input metadata retrieved: " << in_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Input metadata failed: " << e.what());
        }
        
        try {
            LOG4CXX_INFO(logger, "Output ports: " << node->GetOutputPortCount());
            auto out_metadata = node->GetOutputPortMetadata();
            LOG4CXX_INFO(logger, "  ✓ Output metadata retrieved: " << out_metadata.size() << " ports");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "  ✗ Output metadata failed: " << e.what());
        }
    }

    LOG4CXX_INFO(logger, "\n=== END DETAILED REFLECTION ANALYSIS ===");
}

template <typename NodeType>
/**
 * @class PortMetadataConformanceTest
 * @brief Port metadata conformance test implementation for GraphX.
 */
class PortMetadataConformanceTest : public ::testing::Test {};

using ConformanceNodeTypes = ::testing::Types<
    test::SourceTestNode,
    test::SinkTestNode,
    test::InteriorTestNode,
    test::MergeTestNode,
    test::SplitTestNode,
    test::FailingTestNode>;

TYPED_TEST_SUITE(PortMetadataConformanceTest, ConformanceNodeTypes);

TYPED_TEST(PortMetadataConformanceTest, RuntimeMetadataMatchesCompileTimeDescriptor) {
    using NodeType = TypeParam;
    auto node = std::make_shared<NodeType>();
    ASSERT_NE(node, nullptr);

    auto input_metadata = node->GetInputPortMetadata();
    auto output_metadata = node->GetOutputPortMetadata();

    EXPECT_EQ(graph::NodePortDescriptor<NodeType>::input_count, input_metadata.size());
    EXPECT_EQ(graph::NodePortDescriptor<NodeType>::output_count, output_metadata.size());

    for (const auto& port : input_metadata) {
        EXPECT_FALSE(port.port_name.empty());
        EXPECT_FALSE(port.payload_type.empty());
    }

    for (const auto& port : output_metadata) {
        EXPECT_FALSE(port.port_name.empty());
        EXPECT_FALSE(port.payload_type.empty());
    }
}

struct ReflectionHelperNode {
    static constexpr std::size_t NInputs = 2;
    static constexpr std::size_t NOutputs = 3;
};

struct ReflectionHelperNoPortCountNode {};

struct ReflectionHelperCustomPayload {};

TEST(PortMetadataReflectionHelpersTest, SplitNodeMetadataUsesDirectionalIndexedNames) {
    using SplitType = graph::SplitNode2<::graph::message::Message>;
    auto node = std::make_shared<SplitType>();
    ASSERT_NE(node, nullptr);

    const auto input_metadata = node->GetInputPortMetadata();
    const auto output_metadata = node->GetOutputPortMetadata();

    ASSERT_EQ(input_metadata.size(), 1u);
    ASSERT_EQ(output_metadata.size(), 2u);

    EXPECT_EQ(input_metadata[0].port_name, "Input0");
    EXPECT_EQ(output_metadata[0].port_name, "Output0");
    EXPECT_EQ(output_metadata[1].port_name, "Output1");
}

TEST(PortMetadataReflectionHelpersTest, ReflectOutputPortsUsesNodeOutputCount) {
    constexpr auto metadata = graph::reflection::reflect_output_ports<ReflectionHelperNode>();

    static_assert(metadata.size() == ReflectionHelperNode::NOutputs);
    EXPECT_EQ(metadata.size(), ReflectionHelperNode::NOutputs);
    EXPECT_EQ(metadata.front().id, 0);
    EXPECT_EQ(metadata.back().id, ReflectionHelperNode::NOutputs - 1);
    EXPECT_EQ(metadata.front().name, "Output0");
    EXPECT_EQ(metadata.back().name, "Output2");
    EXPECT_EQ(metadata.front().direction, graph::reflection::PortDirection::Output);
}

TEST(PortMetadataReflectionHelpersTest, ReflectInputPortsUsesNodeInputCount) {
    constexpr auto metadata = graph::reflection::reflect_input_ports<ReflectionHelperNode>();

    static_assert(metadata.size() == ReflectionHelperNode::NInputs);
    EXPECT_EQ(metadata.size(), ReflectionHelperNode::NInputs);
    EXPECT_EQ(metadata.front().id, 0);
    EXPECT_EQ(metadata.back().id, ReflectionHelperNode::NInputs - 1);
    EXPECT_EQ(metadata.front().name, "Input0");
    EXPECT_EQ(metadata.back().name, "Input1");
    EXPECT_EQ(metadata.front().direction, graph::reflection::PortDirection::Input);
}

TEST(PortMetadataReflectionHelpersTest, ReflectPortsWithoutCountsReturnsEmptyArray) {
    constexpr auto input_metadata = graph::reflection::reflect_input_ports<ReflectionHelperNoPortCountNode>();
    constexpr auto output_metadata = graph::reflection::reflect_output_ports<ReflectionHelperNoPortCountNode>();

    static_assert(input_metadata.empty());
    static_assert(output_metadata.empty());
    EXPECT_TRUE(input_metadata.empty());
    EXPECT_TRUE(output_metadata.empty());
}

TEST(PortMetadataReflectionHelpersTest, GetTypeNameFallbackDoesNotCollapseCustomType) {
    constexpr auto custom_name = graph::reflection::get_type_name<ReflectionHelperCustomPayload>();

    EXPECT_FALSE(custom_name.empty());
    EXPECT_NE(custom_name, "custom_type");
}

}  // namespace
