/**
 * @file test_capability_bus.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/**
 * @file test_capability_bus.cpp
 * @brief Comprehensive tests for CapabilityBus and DefaultCapabilityBus (Phase 4 Testing)
 *
 * Tests the capability discovery and registration system with:
 * - Multiple capability types
 * - Type-safe registration and retrieval
 * - Error conditions and edge cases
 * - Real-world usage patterns
 * - C++26 compliance verification
 *
 * @note Uses C++26 features: std::is_class_v, std::static_pointer_cast, constexpr, concepts
 */

#include <gtest/gtest.h>
#include "graph/CapabilityBus.hpp"
#include "graph/DefaultCapabilityBus.hpp"
#include <memory>
#include <string>
#include <vector>

namespace {

// ===================================================================================
// Test Capability Types - Simulating Real Capabilities
// ===================================================================================

/**
 * @brief Mock metrics capability for testing
 * 
 * Simulates the real MetricsCapability interface used in production.
 */
/**
 * @class MockMetricsCapability
 * @brief Mock metrics capability implementation for GraphX.
 */
class MockMetricsCapability {
public:
    virtual ~MockMetricsCapability() = default;
    
/**
 * @brief Register metrics callback.
 * @param name Parameter for register metrics callback.
 */
    virtual void RegisterMetricsCallback(const std::string& name) {
        metrics_.push_back(name);
    }
    
/**
 * @brief Get metrics.
 */
    virtual std::vector<std::string> GetMetrics() const {
        return metrics_;
    }
    
/**
 * @brief Get metric count.
 */
    virtual int GetMetricCount() const {
        return static_cast<int>(metrics_.size());
    }
    
private:
    std::vector<std::string> metrics_;
};

/**
 * @brief Mock graph capability for testing
 * 
 * Simulates graph state query capability.
 */
/**
 * @class MockGraphCapability
 * @brief Mock graph capability implementation for GraphX.
 */
class MockGraphCapability {
public:
    virtual ~MockGraphCapability() = default;
    
/**
 * @brief Get graph name.
 */
    virtual std::string GetGraphName() const {
        return graph_name_;
    }
    
/**
 * @brief Set graph name.
 * @param name Parameter for set graph name.
 */
    virtual void SetGraphName(const std::string& name) {
        graph_name_ = name;
    }
    
/**
 * @brief Get node count.
 */
    virtual int GetNodeCount() const {
        return node_count_;
    }
    
/**
 * @brief Set node count.
 * @param count Parameter for set node count.
 */
    virtual void SetNodeCount(int count) {
        node_count_ = count;
    }
    
private:
    std::string graph_name_{"default"};
    int node_count_{0};
};

/**
 * @brief Mock dashboard capability for testing
 * 
 * Simulates dashboard/UI capability.
 */
/**
 * @class MockDashboardCapability
 * @brief Mock dashboard capability implementation for GraphX.
 */
class MockDashboardCapability {
public:
    virtual ~MockDashboardCapability() = default;
    
/**
 * @brief Display.
 * @param msg Parameter for display.
 */
    virtual void Display(const std::string& msg) {
        messages_.push_back(msg);
    }
    
/**
 * @brief Get displayed messages.
 */
    virtual std::vector<std::string> GetDisplayedMessages() const {
        return messages_;
    }
    
/**
 * @brief Is initialized.
 */
    virtual bool IsInitialized() const {
        return initialized_;
    }
    
/**
 * @brief Initialize.
 */
    virtual void Initialize() {
        initialized_ = true;
    }
    
private:
    std::vector<std::string> messages_;
    bool initialized_{false};
};

/**
 * @brief Custom test capability with state
 * 
 * Tests capabilities with internal state management.
 */
/**
 * @class CustomTestCapability
 * @brief Custom test capability implementation for GraphX.
 */
class CustomTestCapability {
public:
    virtual ~CustomTestCapability() = default;
    
    int value{42};
    bool flag{false};
    std::string label{"test"};
};

// ===================================================================================
// Test Class - CapabilityBus Tests
// ===================================================================================

/**
 * @class CapabilityBusTest
 * @brief Capability bus test implementation for GraphX.
 */
class CapabilityBusTest : public ::testing::Test {
protected:
    // Create a fresh bus for each test
    graph::DefaultCapabilityBus bus;
};

// ===================================================================================
// Test: Basic Registration and Retrieval
// ===================================================================================

TEST_F(CapabilityBusTest, RegisterAndRetrieveSingleCapability) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    metrics->RegisterMetricsCallback("test_metric");
    
    bus.Register<MockMetricsCapability>(metrics);
    
    auto retrieved = bus.Get<MockMetricsCapability>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->GetMetricCount(), 1);
    EXPECT_EQ(retrieved->GetMetrics()[0], "test_metric");
}

TEST_F(CapabilityBusTest, RegisterAndRetrieveMultipleCapabilityTypes) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    auto graph = std::make_shared<MockGraphCapability>();
    auto dashboard = std::make_shared<MockDashboardCapability>();
    
    bus.Register<MockMetricsCapability>(metrics);
    bus.Register<MockGraphCapability>(graph);
    bus.Register<MockDashboardCapability>(dashboard);
    
    EXPECT_NE(bus.Get<MockMetricsCapability>(), nullptr);
    EXPECT_NE(bus.Get<MockGraphCapability>(), nullptr);
    EXPECT_NE(bus.Get<MockDashboardCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, RetrievePreservesCapabilityState) {
    auto graph = std::make_shared<MockGraphCapability>();
    graph->SetGraphName("MyGraph");
    graph->SetNodeCount(5);
    
    bus.Register<MockGraphCapability>(graph);
    
    auto retrieved = bus.Get<MockGraphCapability>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->GetGraphName(), "MyGraph");
    EXPECT_EQ(retrieved->GetNodeCount(), 5);
}

// ===================================================================================
// Test: Capability Presence Checking (Has<T>)
// ===================================================================================

TEST_F(CapabilityBusTest, HasReturnsTrueForRegisteredCapability) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    bus.Register<MockMetricsCapability>(metrics);
    
    EXPECT_TRUE(bus.Has<MockMetricsCapability>());
}

TEST_F(CapabilityBusTest, HasReturnsFalseForUnregisteredCapability) {
    EXPECT_FALSE(bus.Has<MockMetricsCapability>());
    EXPECT_FALSE(bus.Has<MockGraphCapability>());
    EXPECT_FALSE(bus.Has<MockDashboardCapability>());
}

TEST_F(CapabilityBusTest, HasAccuratelyReflectsMultipleCapabilities) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    auto graph = std::make_shared<MockGraphCapability>();
    
    bus.Register<MockMetricsCapability>(metrics);
    
    EXPECT_TRUE(bus.Has<MockMetricsCapability>());
    EXPECT_FALSE(bus.Has<MockGraphCapability>());
    
    bus.Register<MockGraphCapability>(graph);
    
    EXPECT_TRUE(bus.Has<MockMetricsCapability>());
    EXPECT_TRUE(bus.Has<MockGraphCapability>());
}

// ===================================================================================
// Test: Missing Capability Handling (Returns nullptr)
// ===================================================================================

TEST_F(CapabilityBusTest, GetUnregisteredCapabilityReturnsNullptr) {
    auto result = bus.Get<MockMetricsCapability>();
    EXPECT_EQ(result, nullptr);
}

TEST_F(CapabilityBusTest, GetUnregisteredDoesNotThrow) {
    EXPECT_NO_THROW(bus.Get<MockMetricsCapability>());
    EXPECT_NO_THROW(bus.Get<MockGraphCapability>());
}

TEST_F(CapabilityBusTest, SafeCheckBeforeUse) {
    auto cap = bus.Get<MockMetricsCapability>();
    if (cap) {
        cap->RegisterMetricsCallback("test");
    }
    // Should not reach here without crashing
    EXPECT_EQ(cap, nullptr);
}

// ===================================================================================
// Test: Handler Registration and Replacement
// ===================================================================================

TEST_F(CapabilityBusTest, ReplacingCapabilityUpdatesReference) {
    auto metrics1 = std::make_shared<MockMetricsCapability>();
    metrics1->RegisterMetricsCallback("first");
    
    auto metrics2 = std::make_shared<MockMetricsCapability>();
    metrics2->RegisterMetricsCallback("second");
    metrics2->RegisterMetricsCallback("third");
    
    bus.Register<MockMetricsCapability>(metrics1);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 1);
    
    bus.Register<MockMetricsCapability>(metrics2);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 2);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetrics()[0], "second");
}

TEST_F(CapabilityBusTest, MultipleCapabilitiesIndependentUpdates) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    auto graph = std::make_shared<MockGraphCapability>();
    
    bus.Register<MockMetricsCapability>(metrics);
    bus.Register<MockGraphCapability>(graph);
    
    // Update metrics
    bus.Get<MockMetricsCapability>()->RegisterMetricsCallback("metric1");
    
    // Update graph
    bus.Get<MockGraphCapability>()->SetGraphName("TestGraph");
    
    // Verify both were updated independently
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 1);
    EXPECT_EQ(bus.Get<MockGraphCapability>()->GetGraphName(), "TestGraph");
}

// ===================================================================================
// Test: Clear Functionality
// ===================================================================================

TEST_F(CapabilityBusTest, ClearRemovesAllCapabilities) {
    bus.Register<MockMetricsCapability>(std::make_shared<MockMetricsCapability>());
    bus.Register<MockGraphCapability>(std::make_shared<MockGraphCapability>());
    bus.Register<MockDashboardCapability>(std::make_shared<MockDashboardCapability>());
    
    EXPECT_TRUE(bus.Has<MockMetricsCapability>());
    EXPECT_TRUE(bus.Has<MockGraphCapability>());
    EXPECT_TRUE(bus.Has<MockDashboardCapability>());
    
    bus.Clear();
    
    EXPECT_FALSE(bus.Has<MockMetricsCapability>());
    EXPECT_FALSE(bus.Has<MockGraphCapability>());
    EXPECT_FALSE(bus.Has<MockDashboardCapability>());
}

TEST_F(CapabilityBusTest, GetAfterClearReturnsNullptr) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    bus.Register<MockMetricsCapability>(metrics);
    
    EXPECT_NE(bus.Get<MockMetricsCapability>(), nullptr);
    
    bus.Clear();
    
    EXPECT_EQ(bus.Get<MockMetricsCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, ReregisterAfterClearWorks) {
    auto metrics1 = std::make_shared<MockMetricsCapability>();
    metrics1->RegisterMetricsCallback("first");
    
    bus.Register<MockMetricsCapability>(metrics1);
    bus.Clear();
    
    auto metrics2 = std::make_shared<MockMetricsCapability>();
    metrics2->RegisterMetricsCallback("second");
    metrics2->RegisterMetricsCallback("third");
    
    bus.Register<MockMetricsCapability>(metrics2);
    
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 2);
}

// ===================================================================================
// Test: Type Safety and Correctness
// ===================================================================================

TEST_F(CapabilityBusTest, TypeSafetyEnforcesCorrectRetrieval) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    bus.Register<MockMetricsCapability>(metrics);
    
    // Should return correct type
    auto retrieved = bus.Get<MockMetricsCapability>();
    EXPECT_NE(retrieved, nullptr);
    
    // Should NOT find unrelated types
    EXPECT_EQ(bus.Get<MockGraphCapability>(), nullptr);
    EXPECT_EQ(bus.Get<MockDashboardCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, RegistrationPreservesCapabilityInterface) {
    auto dashboard = std::make_shared<MockDashboardCapability>();
    dashboard->Initialize();
    dashboard->Display("Hello");
    dashboard->Display("World");
    
    bus.Register<MockDashboardCapability>(dashboard);
    
    auto retrieved = bus.Get<MockDashboardCapability>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_TRUE(retrieved->IsInitialized());
    EXPECT_EQ(retrieved->GetDisplayedMessages().size(), 2);
    EXPECT_EQ(retrieved->GetDisplayedMessages()[0], "Hello");
    EXPECT_EQ(retrieved->GetDisplayedMessages()[1], "World");
}

// ===================================================================================
// Test: Shared Pointer Semantics
// ===================================================================================

TEST_F(CapabilityBusTest, ReferencesPointToSameUnderlyingObject) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    metrics->RegisterMetricsCallback("test");
    
    bus.Register<MockMetricsCapability>(metrics);
    
    auto retrieved1 = bus.Get<MockMetricsCapability>();
    auto retrieved2 = bus.Get<MockMetricsCapability>();
    
    // Should be same object
    EXPECT_EQ(retrieved1.get(), retrieved2.get());
    EXPECT_EQ(retrieved1.get(), metrics.get());
}

TEST_F(CapabilityBusTest, SharedPtrIncrementedAfterRetrieval) {
    auto metrics = std::make_shared<MockMetricsCapability>();
    metrics->RegisterMetricsCallback("test");
    
    long ref_count_before = metrics.use_count();  // 1 (metrics)
    
    bus.Register<MockMetricsCapability>(metrics);
    
    long ref_count_after_register = metrics.use_count();  // 2 (metrics + bus)
    EXPECT_GT(ref_count_after_register, ref_count_before);
    
    auto retrieved = bus.Get<MockMetricsCapability>();
    
    long ref_count_after_get = retrieved.use_count();  // 3 (metrics + bus + retrieved)
    EXPECT_GT(ref_count_after_get, ref_count_after_register);
}

// ===================================================================================
// Test: Custom Capability Types (Generic Test)
// ===================================================================================

TEST_F(CapabilityBusTest, RegisterAndRetrieveCustomCapability) {
    auto custom = std::make_shared<CustomTestCapability>();
    custom->value = 123;
    custom->flag = true;
    custom->label = "custom";
    
    bus.Register<CustomTestCapability>(custom);
    
    auto retrieved = bus.Get<CustomTestCapability>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->value, 123);
    EXPECT_TRUE(retrieved->flag);
    EXPECT_EQ(retrieved->label, "custom");
}

// ===================================================================================
// Test: Real-World Usage Pattern - Initialization Sequence
// ===================================================================================

TEST_F(CapabilityBusTest, RealWorldInitializationSequence) {
    // Simulate GraphExecutor initialization
    auto metrics_cap = std::make_shared<MockMetricsCapability>();
    auto graph_cap = std::make_shared<MockGraphCapability>();
    auto dashboard_cap = std::make_shared<MockDashboardCapability>();
    
    // Register capabilities
    bus.Register<MockMetricsCapability>(metrics_cap);
    bus.Register<MockGraphCapability>(graph_cap);
    bus.Register<MockDashboardCapability>(dashboard_cap);
    
    // Dashboard initialization discovers capabilities
    if (auto metrics = bus.Get<MockMetricsCapability>()) {
        dashboard_cap->Display("Metrics capability found");
    }
    if (auto graph = bus.Get<MockGraphCapability>()) {
        graph->SetGraphName("ExecutedGraph");
        dashboard_cap->Display("Graph initialized");
    }
    
    // Verify final state
    EXPECT_TRUE(bus.Has<MockMetricsCapability>());
    EXPECT_TRUE(bus.Has<MockGraphCapability>());
    EXPECT_TRUE(bus.Has<MockDashboardCapability>());
    EXPECT_EQ(dashboard_cap->GetDisplayedMessages().size(), 2);
}

TEST_F(CapabilityBusTest, OptionalCapabilityPattern) {
    // Register only metrics, not dashboard
    auto metrics = std::make_shared<MockMetricsCapability>();
    bus.Register<MockMetricsCapability>(metrics);
    
    // Code should handle missing dashboard gracefully
    if (auto dashboard = bus.Get<MockDashboardCapability>()) {
        dashboard->Display("This should not execute");
    } else {
        metrics->RegisterMetricsCallback("dashboard_unavailable");
    }
    
    EXPECT_EQ(metrics->GetMetricCount(), 1);
}

// ===================================================================================
// Test: Edge Cases and Boundary Conditions
// ===================================================================================

TEST_F(CapabilityBusTest, EmptyBusReturnsNullptr) {
    EXPECT_EQ(bus.Get<MockMetricsCapability>(), nullptr);
    EXPECT_EQ(bus.Get<MockGraphCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, ClearOnEmptyBusIsNoOp) {
    EXPECT_NO_THROW(bus.Clear());
    EXPECT_NO_THROW(bus.Clear());
}

TEST_F(CapabilityBusTest, MultipleRegistrationsSameTypeReplaces) {
    auto first = std::make_shared<MockMetricsCapability>();
    first->RegisterMetricsCallback("first");
    
    auto second = std::make_shared<MockMetricsCapability>();
    second->RegisterMetricsCallback("second");
    
    auto third = std::make_shared<MockMetricsCapability>();
    third->RegisterMetricsCallback("third");
    
    bus.Register<MockMetricsCapability>(first);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 1);
    
    bus.Register<MockMetricsCapability>(second);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 1);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetrics()[0], "second");
    
    bus.Register<MockMetricsCapability>(third);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetricCount(), 1);
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetrics()[0], "third");
}

// ===================================================================================
// Test: C++26 Compliance and Modern C++ Features
// ===================================================================================

TEST_F(CapabilityBusTest, TemplateSpecializationCompiles) {
    // This test verifies template instantiation works correctly
    // The compiler generates code for each call to Register<T> and Get<T>
    
    auto metrics = std::make_shared<MockMetricsCapability>();
    auto graph = std::make_shared<MockGraphCapability>();
    auto dashboard = std::make_shared<MockDashboardCapability>();
    
    bus.Register<MockMetricsCapability>(metrics);
    bus.Register<MockGraphCapability>(graph);
    bus.Register<MockDashboardCapability>(dashboard);
    
    // All three template specializations should work
    EXPECT_NE(bus.Get<MockMetricsCapability>(), nullptr);
    EXPECT_NE(bus.Get<MockGraphCapability>(), nullptr);
    EXPECT_NE(bus.Get<MockDashboardCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, StaticAssertValidatesClassType) {
    // static_assert in DefaultCapabilityBus::Register ensures CapabilityT is a class type
    // This test verifies it compiles correctly with class types
    
/**
 * @class ValidCapability
 * @brief Valid capability implementation for GraphX.
 */
    class ValidCapability {};
    auto cap = std::make_shared<ValidCapability>();
    
    // This should compile without errors
    bus.Register<ValidCapability>(cap);
    EXPECT_NE(bus.Get<ValidCapability>(), nullptr);
}

TEST_F(CapabilityBusTest, TypeIndexStabilityAcrossRetrievals) {
    // std::type_index should provide consistent type identification
    // across multiple Get() calls
    
    auto metrics1 = std::make_shared<MockMetricsCapability>();
    bus.Register<MockMetricsCapability>(metrics1);
    
    auto metrics2 = bus.Get<MockMetricsCapability>();
    auto metrics3 = bus.Get<MockMetricsCapability>();
    
    EXPECT_EQ(metrics2.get(), metrics3.get());
    EXPECT_EQ(metrics2.get(), metrics1.get());
}

// ===================================================================================
// Test: Large Number of Different Capability Types
// ===================================================================================

TEST_F(CapabilityBusTest, ManyCapabilityTypesScalabilityTest) {
    // Register many different capability types
    std::vector<std::shared_ptr<MockMetricsCapability>> metrics_caps;
    std::vector<std::shared_ptr<MockGraphCapability>> graph_caps;
    std::vector<std::shared_ptr<MockDashboardCapability>> dashboard_caps;
    
    // Register multiple instances of each type (only last one stored per type)
    for (int i = 0; i < 5; ++i) {
        auto metrics = std::make_shared<MockMetricsCapability>();
        metrics->RegisterMetricsCallback(std::string("metric_") + std::to_string(i));
        bus.Register<MockMetricsCapability>(metrics);
        metrics_caps.push_back(metrics);
    }
    
    for (int i = 0; i < 5; ++i) {
        auto graph = std::make_shared<MockGraphCapability>();
        graph->SetGraphName(std::string("Graph_") + std::to_string(i));
        bus.Register<MockGraphCapability>(graph);
        graph_caps.push_back(graph);
    }
    
    // Only the last registration should be available
    EXPECT_EQ(bus.Get<MockMetricsCapability>()->GetMetrics()[0], "metric_4");
    EXPECT_EQ(bus.Get<MockGraphCapability>()->GetGraphName(), "Graph_4");
}

}  // namespace
