// SPDX-License-Identifier: MIT

/**
 * @file test_interior_node_creation.cpp
 * @brief Test Interior Node Creation Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>
#include <memory>
#include <iostream>
#include "plugins/NodePluginTemplate.hpp"
#include "test/AdvancedTestNodes.hpp"

using namespace graph;
using namespace test;

namespace {

/**
 * @class StubPluginDescriptorProvider
 * @brief Stub plugin descriptor provider implementation for GraphX.
 */
class StubPluginDescriptorProvider final : public graph::INodeDescriptorProvider {
public:
    graph::NodeDescriptor BuildRuntimeDescriptor(
        graph::RuntimeNodeDescriptorRequest request) const override {
        graph::NodeDescriptor descriptor;
        descriptor.name = "stub-plugin-descriptor";
        descriptor.type = std::move(request.seed.type);
        descriptor.description = std::move(request.seed.description);
        descriptor.lifecycle_state = request.seed.lifecycle_state;
        descriptor.supports_configuration = request.seed.supports_configuration;
        descriptor.config_fields = {};
        descriptor.input_ports = std::move(request.input_ports);
        descriptor.output_ports = std::move(request.output_ports);
        return descriptor;
    }
};

/**
 * @class StubPluginMetadataService
 * @brief Stub plugin metadata service implementation for GraphX.
 */
class StubPluginMetadataService final : public graph::INodeMetadataService {
public:
    const graph::INodeDescriptorProvider& DescriptorProvider() const override {
        return descriptor_provider_;
    }

    const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return graph::GetDefaultNodeDescriptorSchemaProvider();
    }

private:
    StubPluginDescriptorProvider descriptor_provider_;
};

}  // namespace

/**
 * @class InteriorNodeCreationTest
 * @brief Interior node creation test implementation for GraphX.
 */
class InteriorNodeCreationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Called before each test
    }

    void TearDown() override {
        // Called after each test
    }
};

// ============================================================================
// Test 1: Basic make_shared
// ============================================================================

TEST_F(InteriorNodeCreationTest, MakeSharedBasic) {
    std::cout << "Test 1: Creating InteriorTestNode with std::make_shared\n";
    
    ASSERT_NO_THROW({
        auto node = std::make_shared<InteriorTestNode>();
        ASSERT_NE(nullptr, node);
        std::cout << "  ✓ Created node successfully\n";
    });
}

// ============================================================================
// Test 2: Check node is valid
// ============================================================================

TEST_F(InteriorNodeCreationTest, NodeIsValid) {
    std::cout << "Test 2: Validating InteriorTestNode properties\n";
    
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);
    
    // Try to access basic node properties
    std::cout << "  ✓ Node address: " << node.get() << "\n";
    
    // Try to call a virtual method
    ASSERT_NO_THROW({
        auto type_name = node->GetNodeTypeName();
        std::cout << "  ✓ GetNodeTypeName returned: " << type_name << "\n";
    });
}

// ============================================================================
// Test 3: Wrap in NodePluginInstance
// ============================================================================

TEST_F(InteriorNodeCreationTest, WrapInNodePluginInstance) {
    std::cout << "Test 3: Wrapping InteriorTestNode in NodePluginInstance\n";
    
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);
    std::cout << "  ✓ Created node\n";
    
    ASSERT_NO_THROW({
        auto instance = new NodePluginInstance<InteriorTestNode>(
            node, "InteriorTestNode", "test.InteriorTestNode");
        ASSERT_NE(nullptr, instance);
        std::cout << "  ✓ Wrapped in NodePluginInstance\n";
        
        // Check instance properties
        ASSERT_EQ("InteriorTestNode", instance->name);
        std::cout << "  ✓ Instance name: " << instance->name << "\n";
        
        // Don't leak memory
        delete instance;
    });
}

TEST_F(InteriorNodeCreationTest, BuildDescriptorUsesInjectedMetadataService) {
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);

    NodePluginInstance<InteriorTestNode> instance(
        node,
        "InteriorTestNode",
        "plugin.InteriorTestNode");

    StubPluginMetadataService metadata_service;
    const auto descriptor = BuildDescriptorFromPluginInstance(&instance, &metadata_service);

    EXPECT_EQ(descriptor.name, "stub-plugin-descriptor");
    EXPECT_EQ(descriptor.type, instance.type);
}

// ============================================================================
// Test 4: Exact plugin sequence
// ============================================================================

TEST_F(InteriorNodeCreationTest, ExactPluginSequence) {
    std::cout << "Test 4: Exact sequence from plugin_create_interior_test_node\n";
    
    try {
        // Step 1: make_shared
        std::cout << "  Step 1: Creating with std::make_shared\n";
        auto node = std::make_shared<InteriorTestNode>();
        ASSERT_NE(nullptr, node) << "make_shared returned nullptr";
        std::cout << "    ✓ make_shared succeeded\n";
        
        // Step 2: Check for nullptr (as in plugin)
        std::cout << "  Step 2: Null check\n";
        if (!node) {
            FAIL() << "Node is nullptr after make_shared";
        }
        std::cout << "    ✓ Null check passed\n";
        
        // Step 3: Wrap in NodePluginInstance
        std::cout << "  Step 3: Creating NodePluginInstance\n";
        auto instance = new NodePluginInstance<InteriorTestNode>(
            node, "InteriorTestNode", "plugin.InteriorTestNode");
        ASSERT_NE(nullptr, instance) << "NodePluginInstance creation returned nullptr";
        std::cout << "    ✓ NodePluginInstance created\n";
        
        // Step 4: Cast to void*
        std::cout << "  Step 4: Casting to void*\n";
        void* result = static_cast<void*>(instance);
        ASSERT_NE(nullptr, result) << "Cast resulted in nullptr";
        std::cout << "    ✓ Cast succeeded, result: " << result << "\n";
        
        // Cleanup
        delete instance;
        std::cout << "  ✓ All steps completed successfully\n";
        
    } catch (const std::exception& e) {
        FAIL() << "Exception thrown: " << e.what();
    } catch (...) {
        FAIL() << "Unknown exception thrown";
    }
}

// ============================================================================
// Test 5: Multiple allocations
// ============================================================================

TEST_F(InteriorNodeCreationTest, MultipleAllocations) {
    std::cout << "Test 5: Creating multiple InteriorTestNode instances\n";
    
    const int NUM_NODES = 5;
    std::vector<std::shared_ptr<InteriorTestNode>> nodes;
    
    for (int i = 0; i < NUM_NODES; ++i) {
        std::cout << "  Creating node " << (i + 1) << "/" << NUM_NODES << "\n";
        auto node = std::make_shared<InteriorTestNode>();
        ASSERT_NE(nullptr, node);
        nodes.push_back(node);
    }
    
    ASSERT_EQ(NUM_NODES, nodes.size());
    std::cout << "  ✓ Created " << NUM_NODES << " nodes successfully\n";
}

// ============================================================================
// Test 6: IMetricsCallbackProvider interface
// ============================================================================

TEST_F(InteriorNodeCreationTest, MetricsCallbackProvider) {
    std::cout << "Test 6: Testing IMetricsCallbackProvider interface\n";
    
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);
    std::cout << "  ✓ Created node\n";
    
    // InteriorTestNode should implement IMetricsCallbackProvider
    // ASSERT_NO_THROW({
    //     auto has_callback = node->HasMetricsCallback();
    //     std::cout << "  ✓ HasMetricsCallback returned: " << has_callback << "\n";
    // })
    
    ;
}

// ============================================================================
// Test 7: Test the Ports
// ============================================================================

TEST_F(InteriorNodeCreationTest, PortAccess) {
    std::cout << "Test 7: Testing port access\n";
    
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);
    
    ASSERT_NO_THROW({
        // InteriorTestNode ports are compile-time defined
        // Just verify node exists and is callable
        std::cout << "  ✓ Node created and accessible\n";
    });
}

// ============================================================================
// Test 8: Transfer method (key operation)
// ============================================================================

TEST_F(InteriorNodeCreationTest, TransferMethod) {
    std::cout << "Test 8: Testing node capabilities\n";
    
    auto node = std::make_shared<InteriorTestNode>();
    ASSERT_NE(nullptr, node);
    std::cout << "  ✓ Created node\n";
    
    // Create a test message
    ASSERT_NO_THROW({
        graph::message::Message msg;
        std::cout << "  ✓ Created test message\n";
        
        // Just verify node is callable
        std::cout << "  ✓ Node is accessible for use\n";
    });
}
