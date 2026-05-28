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
 * @file test_variant_router.cpp
 * @brief Comprehensive tests for generic VariantRouter template (Phase 4)
 *
 * Tests the VariantRouter<Variant> template with multiple variant types
 * including both simple test types and real sensor data types.
 */

#include <gtest/gtest.h>
#include "core/VariantRouter.hpp"
#include "config/DataTypes.hpp"
#include <string>
#include <vector>

namespace {

// ===================================================================================
// Test Type Definitions - Simple Custom Types
// ===================================================================================

struct EventA {
    int value{0};
    std::string label{"EventA"};
};

struct EventB {
    double x{0.0};
    double y{0.0};
};

struct EventC {
    std::string message;
    int count{0};
};

using SimpleVariant = std::variant<EventA, EventB, EventC>;

// ===================================================================================
// Test Class
// ===================================================================================

class VariantRouterTest : public ::testing::Test {
protected:
    graph::VariantRouter<SimpleVariant> router;
    
    // Track handler calls
    struct Calls {
        bool event_a_called{false};
        bool event_b_called{false};
        bool event_c_called{false};
        int event_a_count{0};
        int event_b_count{0};
        int event_c_count{0};
        EventA last_a;
        EventB last_b;
        EventC last_c;
    } calls;
};

// ===================================================================================
// Test: Basic Handler Registration and Dispatch
// ===================================================================================

TEST_F(VariantRouterTest, RegisterAndDispatchEventA) {
    router.RegisterHandler<EventA>([this](const auto& event) {
        calls.event_a_called = true;
        calls.event_a_count++;
        calls.last_a = event;
    });

    EXPECT_EQ(router.GetHandlerCount(), 1);
    EXPECT_TRUE(router.HasHandler<EventA>());
    EXPECT_FALSE(router.HasHandler<EventB>());

    EventA event{42, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    
    router.Dispatch(payload);
    
    EXPECT_TRUE(calls.event_a_called);
    EXPECT_EQ(calls.event_a_count, 1);
    EXPECT_EQ(calls.last_a.value, 42);
    EXPECT_EQ(calls.last_a.label, "test");
}

TEST_F(VariantRouterTest, RegisterAndDispatchEventB) {
    router.RegisterHandler<EventB>([this](const auto& event) {
        calls.event_b_called = true;
        calls.event_b_count++;
        calls.last_b = event;
    });

    EXPECT_EQ(router.GetHandlerCount(), 1);
    EXPECT_TRUE(router.HasHandler<EventB>());
    EXPECT_FALSE(router.HasHandler<EventA>());

    EventB event{3.14, 2.71};
    SimpleVariant payload(std::in_place_type<EventB>, event);
    
    router.Dispatch(payload);
    
    EXPECT_TRUE(calls.event_b_called);
    EXPECT_EQ(calls.event_b_count, 1);
    EXPECT_DOUBLE_EQ(calls.last_b.x, 3.14);
    EXPECT_DOUBLE_EQ(calls.last_b.y, 2.71);
}

TEST_F(VariantRouterTest, RegisterAndDispatchEventC) {
    router.RegisterHandler<EventC>([this](const auto& event) {
        calls.event_c_called = true;
        calls.event_c_count++;
        calls.last_c = event;
    });

    EXPECT_EQ(router.GetHandlerCount(), 1);
    EXPECT_TRUE(router.HasHandler<EventC>());

    EventC event{"hello world", 99};
    SimpleVariant payload(std::in_place_type<EventC>, event);
    
    router.Dispatch(payload);
    
    EXPECT_TRUE(calls.event_c_called);
    EXPECT_EQ(calls.event_c_count, 1);
    EXPECT_EQ(calls.last_c.message, "hello world");
    EXPECT_EQ(calls.last_c.count, 99);
}

// ===================================================================================
// Test: Multiple Handler Registration
// ===================================================================================

TEST_F(VariantRouterTest, MultipleHandlersRegistered) {
    router.RegisterHandler<EventA>([this](const auto& event) {
        calls.event_a_called = true;
        calls.last_a = event;
    });
    
    router.RegisterHandler<EventB>([this](const auto& event) {
        calls.event_b_called = true;
        calls.last_b = event;
    });
    
    router.RegisterHandler<EventC>([this](const auto& event) {
        calls.event_c_called = true;
        calls.last_c = event;
    });

    EXPECT_EQ(router.GetHandlerCount(), 3);
    EXPECT_TRUE(router.HasHandler<EventA>());
    EXPECT_TRUE(router.HasHandler<EventB>());
    EXPECT_TRUE(router.HasHandler<EventC>());
}

TEST_F(VariantRouterTest, DispatchMultipleTypesSelectsCorrect) {
    router.RegisterHandler<EventA>([this](const auto&) {
        calls.event_a_count++;
    });
    
    router.RegisterHandler<EventB>([this](const auto&) {
        calls.event_b_count++;
    });
    
    router.RegisterHandler<EventC>([this](const auto&) {
        calls.event_c_count++;
    });

    // Dispatch EventA
    SimpleVariant payload_a(std::in_place_type<EventA>, EventA{1, "a"});
    router.Dispatch(payload_a);
    
    // Dispatch EventB
    SimpleVariant payload_b(std::in_place_type<EventB>, EventB{1.0, 2.0});
    router.Dispatch(payload_b);
    
    // Dispatch EventC
    SimpleVariant payload_c(std::in_place_type<EventC>, EventC{"msg", 3});
    router.Dispatch(payload_c);

    EXPECT_EQ(calls.event_a_count, 1);
    EXPECT_EQ(calls.event_b_count, 1);
    EXPECT_EQ(calls.event_c_count, 1);
}

// ===================================================================================
// Test: Handler Replacement
// ===================================================================================

TEST_F(VariantRouterTest, ReplacingHandlerOverwritesPrevious) {
    int first_count = 0, second_count = 0;
    
    router.RegisterHandler<EventA>([&first_count](const auto&) {
        first_count++;
    });
    
    EventA event{1, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    
    router.Dispatch(payload);
    EXPECT_EQ(first_count, 1);
    EXPECT_EQ(second_count, 0);

    // Replace handler
    router.RegisterHandler<EventA>([&second_count](const auto&) {
        second_count++;
    });
    
    router.Dispatch(payload);
    EXPECT_EQ(first_count, 1);  // Not called again
    EXPECT_EQ(second_count, 1);  // New handler called
}

// ===================================================================================
// Test: Unregistered Handler (Graceful Ignore)
// ===================================================================================

TEST_F(VariantRouterTest, DispatchWithoutRegisteredHandlerIsIgnored) {
    router.RegisterHandler<EventA>([this](const auto&) {
        calls.event_a_called = true;
    });

    // Dispatch EventB without registering handler
    EventB event{1.0, 2.0};
    SimpleVariant payload(std::in_place_type<EventB>, event);
    
    // Should not throw or crash
    router.Dispatch(payload);
    
    // EventA handler should not be called
    EXPECT_FALSE(calls.event_a_called);
}

// ===================================================================================
// Test: Handler Counting and Querying
// ===================================================================================

TEST_F(VariantRouterTest, HandlerCountStartsAtZero) {
    EXPECT_EQ(router.GetHandlerCount(), 0);
    EXPECT_FALSE(router.HasHandler<EventA>());
    EXPECT_FALSE(router.HasHandler<EventB>());
}

TEST_F(VariantRouterTest, HandlerCountIncrementsWithRegistration) {
    EXPECT_EQ(router.GetHandlerCount(), 0);
    
    router.RegisterHandler<EventA>([](const auto&) {});
    EXPECT_EQ(router.GetHandlerCount(), 1);
    
    router.RegisterHandler<EventB>([](const auto&) {});
    EXPECT_EQ(router.GetHandlerCount(), 2);
    
    router.RegisterHandler<EventC>([](const auto&) {});
    EXPECT_EQ(router.GetHandlerCount(), 3);
}

TEST_F(VariantRouterTest, ReplacementDoesNotIncreaseCount) {
    router.RegisterHandler<EventA>([](const auto&) {});
    EXPECT_EQ(router.GetHandlerCount(), 1);
    
    router.RegisterHandler<EventA>([](const auto&) {});
    EXPECT_EQ(router.GetHandlerCount(), 1);  // Still 1
}

TEST_F(VariantRouterTest, HasHandlerAccuratelyReflectsRegistration) {
    EXPECT_FALSE(router.HasHandler<EventA>());
    router.RegisterHandler<EventA>([](const auto&) {});
    EXPECT_TRUE(router.HasHandler<EventA>());
    
    EXPECT_FALSE(router.HasHandler<EventB>());
    router.RegisterHandler<EventB>([](const auto&) {});
    EXPECT_TRUE(router.HasHandler<EventB>());
}

// ===================================================================================
// Test: Clear Handlers
// ===================================================================================

TEST_F(VariantRouterTest, ClearHandlersRemovesAll) {
    router.RegisterHandler<EventA>([](const auto&) {});
    router.RegisterHandler<EventB>([](const auto&) {});
    router.RegisterHandler<EventC>([](const auto&) {});
    
    EXPECT_EQ(router.GetHandlerCount(), 3);
    
    router.ClearHandlers();
    
    EXPECT_EQ(router.GetHandlerCount(), 0);
    EXPECT_FALSE(router.HasHandler<EventA>());
    EXPECT_FALSE(router.HasHandler<EventB>());
    EXPECT_FALSE(router.HasHandler<EventC>());
}

TEST_F(VariantRouterTest, ClearHandlersStopsDispatch) {
    int call_count = 0;
    router.RegisterHandler<EventA>([&call_count](const auto&) {
        call_count++;
    });
    
    EventA event{1, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    
    router.Dispatch(payload);
    EXPECT_EQ(call_count, 1);
    
    router.ClearHandlers();
    
    router.Dispatch(payload);
    EXPECT_EQ(call_count, 1);  // Not called again
}

// ===================================================================================
// Test: Move Semantics
// ===================================================================================

TEST_F(VariantRouterTest, MoveConstructorTransfersHandlers) {
    router.RegisterHandler<EventA>([this](const auto&) {
        calls.event_a_count++;
    });
    
    EXPECT_EQ(router.GetHandlerCount(), 1);
    EXPECT_TRUE(router.HasHandler<EventA>());
    
    graph::VariantRouter<SimpleVariant> moved_router(std::move(router));
    
    EXPECT_EQ(moved_router.GetHandlerCount(), 1);
    EXPECT_TRUE(moved_router.HasHandler<EventA>());
    
    EventA event{1, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    moved_router.Dispatch(payload);
    
    EXPECT_EQ(calls.event_a_count, 1);
}

TEST_F(VariantRouterTest, MoveAssignmentTransfersHandlers) {
    auto create_router = []() {
        graph::VariantRouter<SimpleVariant> temp;
        temp.RegisterHandler<EventB>([](const auto&) {});
        return temp;
    };
    
    router = create_router();
    
    EXPECT_EQ(router.GetHandlerCount(), 1);
    EXPECT_TRUE(router.HasHandler<EventB>());
}

// ===================================================================================
// Test: Multiple Dispatches
// ===================================================================================

TEST_F(VariantRouterTest, MultipleDispatchesCallHandlerEachTime) {
    int call_count = 0;
    router.RegisterHandler<EventA>([&call_count](const auto&) {
        call_count++;
    });
    
    EventA event{1, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    
    for (int i = 0; i < 5; ++i) {
        router.Dispatch(payload);
    }
    
    EXPECT_EQ(call_count, 5);
}

TEST_F(VariantRouterTest, DispatchPreservesPayloadData) {
    std::vector<EventB> received_events;
    router.RegisterHandler<EventB>([&received_events](const auto& event) {
        received_events.push_back(event);
    });
    
    for (int i = 0; i < 3; ++i) {
        EventB event{static_cast<double>(i), static_cast<double>(i * 2)};
        SimpleVariant payload(std::in_place_type<EventB>, event);
        router.Dispatch(payload);
    }
    
    ASSERT_EQ(received_events.size(), 3);
    EXPECT_DOUBLE_EQ(received_events[0].x, 0.0);
    EXPECT_DOUBLE_EQ(received_events[1].x, 1.0);
    EXPECT_DOUBLE_EQ(received_events[2].x, 2.0);
}

// ===================================================================================
// Test: Copy Deletion (Verify Correct Behavior)
// ===================================================================================

TEST_F(VariantRouterTest, CopyConstructorIsDeleted) {
    // This test verifies that copy is deleted (compile-time check in real code)
    // At runtime, we verify via static assertions in the header
    static_assert(!std::is_copy_constructible_v<graph::VariantRouter<SimpleVariant>>);
}

TEST_F(VariantRouterTest, CopyAssignmentIsDeleted) {
    static_assert(!std::is_copy_assignable_v<graph::VariantRouter<SimpleVariant>>);
}

// ===================================================================================
// Test: With Real Sensor Types
// ===================================================================================

TEST_F(VariantRouterTest, WorksWithSensorPayloadVariant) {
    using SensorPayload = std::variant<sensors::AccelerometerData, sensors::GyroscopeData>;
    graph::VariantRouter<SensorPayload> sensor_router;
    
    int accel_count = 0, gyro_count = 0;
    
    sensor_router.RegisterHandler<sensors::AccelerometerData>([&accel_count](const auto&) {
        accel_count++;
    });
    
    sensor_router.RegisterHandler<sensors::GyroscopeData>([&gyro_count](const auto&) {
        gyro_count++;
    });
    
    EXPECT_EQ(sensor_router.GetHandlerCount(), 2);
    
    // Dispatch AccelerometerData
    sensors::AccelerometerData accel{sensors::Vector3D{1, 2, 3}};
    SensorPayload accel_payload(std::in_place_type<sensors::AccelerometerData>, accel);
    sensor_router.Dispatch(accel_payload);
    
    // Dispatch GyroscopeData
    sensors::GyroscopeData gyro{sensors::Vector3D{0.1f, 0.2f, 0.3f}};
    SensorPayload gyro_payload(std::in_place_type<sensors::GyroscopeData>, gyro);
    sensor_router.Dispatch(gyro_payload);
    
    EXPECT_EQ(accel_count, 1);
    EXPECT_EQ(gyro_count, 1);
}

// ===================================================================================
// Test: Handler Exception Safety (Basic)
// ===================================================================================

TEST_F(VariantRouterTest, NoThrowGuaranteeForEmptyDispatch) {
    EventA event{1, "test"};
    SimpleVariant payload(std::in_place_type<EventA>, event);
    
    // Should not throw even without registered handler
    EXPECT_NO_THROW(router.Dispatch(payload));
}

}  // namespace
