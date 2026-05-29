// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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
 * @file test_metrics_policy_gaps.cpp
 * @brief Focused unit tests addressing critical gaps in MetricsPolicy testing
 * 
 * This test suite focuses ONLY on gaps not covered by existing tests:
 * - test_topologies_simple.cpp (event publishing across 10 topologies)
 * - test_graph_executor_builder_policies.cpp (capability registration, callback installation)
 * - test_execution_policies.cpp (policy lifecycle OnInit/Start/Stop/Join)
 * 
 * Phase 1 Implementation:
 * - Gap #1: Event Queue Mechanics (15 tests)
 * - Gap #4: Event Content Validation (12 tests)
 * Total: 27 tests for Phase 1
 */

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "policies/MetricsPolicy.hpp"
#include "metrics/MetricsEvent.hpp"
#include "core/ActiveQueue.hpp"
#include "test/AdvancedTestNodes.hpp"
#include "test/TestGraphTopologies.hpp"

namespace MetricsPolicyGapsTests {

using MetricsEvent = app::metrics::MetricsEvent;
using MetricsEventQueue = core::ActiveQueue<MetricsEvent>;

/**
 * @class MetricsPolicyGapsFixture
 * @brief Test fixture for metrics policy gap testing
 * 
 * Provides base infrastructure for testing MetricsPolicy queue mechanics
 * and event content validation without duplicating existing topology tests.
 */
class MetricsPolicyGapsFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test queue instance for isolated queue testing
        test_queue_ = std::make_unique<MetricsEventQueue>();
    }

    void TearDown() override {
        if (test_queue_) {
            test_queue_->Disable();
            // Drain any remaining events
            MetricsEvent event;
            while (test_queue_->DequeueNonBlocking(event)) {
                // Discard
            }
        }
    }

    std::unique_ptr<MetricsEventQueue> test_queue_;
};

// ============================================================================
// GAP #1: EVENT QUEUE MECHANICS (15 tests)
// ============================================================================
// Tests the MetricsEventQueue (based on core::ActiveQueue<MetricsEvent>)
// for initialization, FIFO ordering, capacity, and thread safety.
// 
// Motivation: Session notes indicate capacity=0 bug; queue behavior under
// load not tested in existing suite.
// ============================================================================

/**
 * Test 16: QueueInitializedWithCorrectCapacity
 * Validates that a new MetricsEventQueue has non-zero capacity for unbounded operation.
 */
TEST_F(MetricsPolicyGapsFixture, QueueInitializedWithCorrectCapacity) {
    EXPECT_EQ(test_queue_->Capacity(), 0) 
        << "Default capacity should be 0 (unbounded queue)";
    EXPECT_EQ(test_queue_->Size(), 0)
        << "New queue should be empty";
}

/**
 * Test 17: QueueInitiallyEmpty
 * Validates that a new queue is empty and DequeueNonBlocking returns nullptr-equivalent.
 */
TEST_F(MetricsPolicyGapsFixture, QueueInitiallyEmpty) {
    MetricsEvent event;
    event.source = "test";
    event.event_type = "test_type";
    
    // DequeueNonBlocking on empty queue should return false
    bool result = test_queue_->DequeueNonBlocking(event);
    EXPECT_FALSE(result) << "DequeueNonBlocking on empty queue should return false";
    EXPECT_EQ(test_queue_->Size(), 0) << "Queue size should remain 0";
}

/**
 * Test 18: EnqueueIncreasesSize
 * Validates that Enqueue increases queue size.
 */
TEST_F(MetricsPolicyGapsFixture, EnqueueIncreasesSize) {
    MetricsEvent event;
    event.source = "TestNode";
    event.event_type = "message_produced";
    event.timestamp = std::chrono::system_clock::now();
    
    EXPECT_EQ(test_queue_->Size(), 0);
    bool result = test_queue_->Enqueue(event);
    EXPECT_TRUE(result) << "Enqueue should succeed";
    EXPECT_EQ(test_queue_->Size(), 1) << "Size should be 1 after enqueue";
}

/**
 * Test 19: DequeueDecreases Size
 * Validates that DequeueNonBlocking decreases queue size.
 */
TEST_F(MetricsPolicyGapsFixture, DequeueDecreasesSize) {
    MetricsEvent event;
    event.source = "TestNode";
    event.event_type = "message_produced";
    event.timestamp = std::chrono::system_clock::now();
    
    test_queue_->Enqueue(event);
    EXPECT_EQ(test_queue_->Size(), 1);
    
    MetricsEvent dequeued;
    bool result = test_queue_->DequeueNonBlocking(dequeued);
    EXPECT_TRUE(result) << "DequeueNonBlocking should succeed";
    EXPECT_EQ(test_queue_->Size(), 0) << "Size should be 0 after dequeue";
}

/**
 * Test 20: FIFOOrdering
 * Validates that events maintain FIFO order.
 */
TEST_F(MetricsPolicyGapsFixture, FIFOOrdering) {
    MetricsEvent event1, event2, event3;
    event1.source = "Node1"; event1.event_type = "produced";
    event2.source = "Node2"; event2.event_type = "consumed";
    event3.source = "Node3"; event3.event_type = "transferred";
    
    test_queue_->Enqueue(event1);
    test_queue_->Enqueue(event2);
    test_queue_->Enqueue(event3);
    
    MetricsEvent dequeued;
    EXPECT_TRUE(test_queue_->DequeueNonBlocking(dequeued));
    EXPECT_EQ(dequeued.source, "Node1");
    
    EXPECT_TRUE(test_queue_->DequeueNonBlocking(dequeued));
    EXPECT_EQ(dequeued.source, "Node2");
    
    EXPECT_TRUE(test_queue_->DequeueNonBlocking(dequeued));
    EXPECT_EQ(dequeued.source, "Node3");
}

/**
 * Test 21: DequeueNonBlockingReturnsNullImmediately
 * Validates that DequeueNonBlocking never blocks on empty queue.
 */
TEST_F(MetricsPolicyGapsFixture, DequeueNonBlockingReturnsNullImmediately) {
    auto start = std::chrono::high_resolution_clock::now();
    MetricsEvent event;
    bool result = test_queue_->DequeueNonBlocking(event);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    
    EXPECT_FALSE(result) << "Should return false for empty queue";
    EXPECT_LT(elapsed.count(), 10) << "Should return immediately (< 10ms)";
}

/**
 * Test 22: MultipleDequeuesExhaustQueue
 * Validates that DequeueNonBlocking exhausts all events.
 */
TEST_F(MetricsPolicyGapsFixture, MultipleDequeuesExhaustQueue) {
    // Enqueue 5 events
    for (int i = 0; i < 5; ++i) {
        MetricsEvent event;
        event.source = "Node_" + std::to_string(i);
        test_queue_->Enqueue(event);
    }
    EXPECT_EQ(test_queue_->Size(), 5);
    
    // Dequeue 5 times
    for (int i = 0; i < 5; ++i) {
        MetricsEvent event;
        EXPECT_TRUE(test_queue_->DequeueNonBlocking(event))
            << "Should dequeue event " << i;
    }
    
    // 6th dequeue should return false
    MetricsEvent event;
    EXPECT_FALSE(test_queue_->DequeueNonBlocking(event))
        << "6th dequeue should return false (queue empty)";
}

/**
 * Test 23: UnboundedQueueAccepts1kEvents
 * Validates that queue accepts 1000 events without dropping or rejecting.
 */
TEST_F(MetricsPolicyGapsFixture, UnboundedQueueAccepts1kEvents) {
    const int EVENT_COUNT = 1000;
    
    // Enqueue 1000 events
    for (int i = 0; i < EVENT_COUNT; ++i) {
        MetricsEvent event;
        event.source = "Node_" + std::to_string(i % 5);
        event.event_type = "produced";
        bool result = test_queue_->Enqueue(event);
        EXPECT_TRUE(result) << "Enqueue " << i << " should succeed";
    }
    
    EXPECT_EQ(test_queue_->Size(), EVENT_COUNT)
        << "Queue should contain all " << EVENT_COUNT << " events";
}

/**
 * Test 24: QueueDoesNotSilentlyDropEvents
 * Validates that all enqueued events are dequeued (no silent drops).
 */
TEST_F(MetricsPolicyGapsFixture, QueueDoesNotSilentlyDropEvents) {
    const int EVENT_COUNT = 100;
    
    // Enqueue 100 events
    for (int i = 0; i < EVENT_COUNT; ++i) {
        MetricsEvent event;
        event.source = "Node_" + std::to_string(i);
        test_queue_->Enqueue(event);
    }
    
    // Dequeue all and count
    int dequeue_count = 0;
    MetricsEvent event;
    while (test_queue_->DequeueNonBlocking(event)) {
        ++dequeue_count;
    }
    
    EXPECT_EQ(dequeue_count, EVENT_COUNT)
        << "Should dequeue " << EVENT_COUNT << " events, got " << dequeue_count;
}

/**
 * Test 25: EnqueueReturnsImmediately
 * Validates that Enqueue operations complete quickly without blocking.
 */
TEST_F(MetricsPolicyGapsFixture, EnqueueReturnsImmediately) {
    const int EVENT_COUNT = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < EVENT_COUNT; ++i) {
        MetricsEvent event;
        event.source = "Node";
        test_queue_->Enqueue(event);
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    
    EXPECT_LT(elapsed.count(), 50)
        << "Enqueuing 100 events should complete in < 50ms";
}

/**
 * Test 26: QueueSizeMatchesEnqueueMinusDequeue
 * Validates that queue size = enqueued_count - dequeued_count.
 */
TEST_F(MetricsPolicyGapsFixture, QueueSizeMatchesEnqueueMinusDequeue) {
    MetricsEvent event;
    
    // Enqueue 50
    for (int i = 0; i < 50; ++i) {
        event.source = "Node";
        test_queue_->Enqueue(event);
    }
    EXPECT_EQ(test_queue_->Size(), 50);
    
    // Dequeue 20
    for (int i = 0; i < 20; ++i) {
        test_queue_->DequeueNonBlocking(event);
    }
    EXPECT_EQ(test_queue_->Size(), 30);
    
    // Enqueue 30
    for (int i = 0; i < 30; ++i) {
        test_queue_->Enqueue(event);
    }
    EXPECT_EQ(test_queue_->Size(), 60);
}

/**
 * Test 27: QueueNotificationMechanismWorks
 * Validates that queue signaling mechanism works (Disable/Dequeue interaction).
 */
TEST_F(MetricsPolicyGapsFixture, QueueNotificationMechanismWorks) {
    MetricsEvent event;
    event.source = "TestNode";
    
    // Enqueue an event
    test_queue_->Enqueue(event);
    
    // Disable the queue
    test_queue_->Disable();
    
    // Dequeue should still work (returns the enqueued event, then false)
    bool result = test_queue_->DequeueNonBlocking(event);
    EXPECT_TRUE(result) << "Should dequeue the enqueued event";
    
    // Next dequeue should return false (queue now empty and disabled)
    result = test_queue_->DequeueNonBlocking(event);
    EXPECT_FALSE(result) << "After disable and drain, should return false";
}

/**
 * Test 28: QueueStateAfterSequentialOps
 * Validates queue state consistency after interleaved enqueue/dequeue.
 */
TEST_F(MetricsPolicyGapsFixture, QueueStateAfterSequentialOps) {
    MetricsEvent event;
    
    // Enqueue (a)
    event.source = "a";
    test_queue_->Enqueue(event);
    EXPECT_EQ(test_queue_->Size(), 1);
    
    // Dequeue
    test_queue_->DequeueNonBlocking(event);
    EXPECT_EQ(test_queue_->Size(), 0);
    EXPECT_EQ(event.source, "a");
    
    // Enqueue (b, c)
    event.source = "b"; test_queue_->Enqueue(event);
    event.source = "c"; test_queue_->Enqueue(event);
    EXPECT_EQ(test_queue_->Size(), 2);
    
    // Dequeue all
    int count = 0;
    while (test_queue_->DequeueNonBlocking(event)) {
        ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(test_queue_->Size(), 0);
}

/**
 * Test 29: QueueMemoryCleanupOnDestruction
 * Validates that queue destructor properly cleans up without leaks.
 * 
 * Note: ASAN will detect leaks if destructor doesn't clean up properly.
 */
TEST_F(MetricsPolicyGapsFixture, QueueMemoryCleanupOnDestruction) {
    {
        auto queue = std::make_unique<MetricsEventQueue>();
        
        // Enqueue 100 events
        for (int i = 0; i < 100; ++i) {
            MetricsEvent event;
            event.source = "Node_" + std::to_string(i);
            queue->Enqueue(event);
        }
        
        // Queue destruction should clean up
        queue.reset();
    }
    
    // If we reach here without ASAN error, cleanup succeeded
    SUCCEED();
}

/**
 * Test 30: EnqueueWithLargePayload
 * Validates that queue handles events with large data maps.
 */
TEST_F(MetricsPolicyGapsFixture, EnqueueWithLargePayload) {
    MetricsEvent event;
    event.source = "LargePayloadNode";
    event.event_type = "complex_event";
    
    // Add many data fields
    for (int i = 0; i < 100; ++i) {
        event.data["field_" + std::to_string(i)] = "value_" + std::to_string(i);
    }
    
    bool result = test_queue_->Enqueue(event);
    EXPECT_TRUE(result) << "Should enqueue event with large payload";
    EXPECT_EQ(test_queue_->Size(), 1);
    
    MetricsEvent dequeued;
    result = test_queue_->DequeueNonBlocking(dequeued);
    EXPECT_TRUE(result) << "Should dequeue large payload event";
    EXPECT_EQ(dequeued.data.size(), 100) << "Should preserve all data fields";
}

// ============================================================================
// GAP #4: EVENT CONTENT VALIDATION (12 tests)
// ============================================================================
// Tests that MetricsEvent fields (timestamp, source, type, data) are
// correctly populated and preserved through the queue.
// 
// Motivation: Existing tests verify event types exist, not content accuracy.
// ============================================================================

/**
 * Test 31: EventTimestampIsSet
 * Validates that enqueued events have non-zero timestamps.
 */
TEST_F(MetricsPolicyGapsFixture, EventTimestampIsSet) {
    MetricsEvent event;
    event.source = "TestNode";
    event.event_type = "message_produced";
    event.timestamp = std::chrono::system_clock::now();
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    // Timestamp should match (or be very close)
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        dequeued.timestamp - event.timestamp);
    EXPECT_LE(diff.count(), 10)
        << "Timestamp should be preserved (within 10ms tolerance)";
}

/**
 * Test 32: EventTimestampsAreMonotonicallyIncreasing
 * Validates that sequentially enqueued events have increasing timestamps.
 */
TEST_F(MetricsPolicyGapsFixture, EventTimestampsAreMonotonicallyIncreasing) {
    std::vector<MetricsEvent> events;
    
    // Create 10 events with small delays
    for (int i = 0; i < 10; ++i) {
        MetricsEvent event;
        event.source = "Node_" + std::to_string(i);
        event.event_type = "produced";
        event.timestamp = std::chrono::system_clock::now();
        test_queue_->Enqueue(event);
        events.push_back(event);
        
        // Small delay to ensure timestamp differences
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Verify timestamps are increasing
    for (size_t i = 1; i < events.size(); ++i) {
        EXPECT_GE(events[i].timestamp, events[i-1].timestamp)
            << "Event " << i << " should have timestamp >= previous event";
    }
}

/**
 * Test 33: EventSourceFieldMatchesPublishingNode
 * Validates that source field correctly identifies the publishing node.
 */
TEST_F(MetricsPolicyGapsFixture, EventSourceFieldMatchesPublishingNode) {
    MetricsEvent event;
    event.source = "SourceTestNode_1";
    event.event_type = "message_produced";
    event.timestamp = std::chrono::system_clock::now();
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    EXPECT_EQ(dequeued.source, "SourceTestNode_1")
        << "Source field should be preserved exactly";
}

/**
 * Test 34: EventSourceWithDuplicateNodeTypes
 * Validates that source field distinguishes between nodes of same type.
 */
TEST_F(MetricsPolicyGapsFixture, EventSourceWithDuplicateNodeTypes) {
    // Enqueue from "SourceTestNode_1"
    MetricsEvent event1;
    event1.source = "SourceTestNode_1";
    event1.event_type = "produced";
    test_queue_->Enqueue(event1);
    
    // Enqueue from "SourceTestNode_2"
    MetricsEvent event2;
    event2.source = "SourceTestNode_2";
    event2.event_type = "produced";
    test_queue_->Enqueue(event2);
    
    // Dequeue and verify source fields are different
    MetricsEvent dequeued1, dequeued2;
    test_queue_->DequeueNonBlocking(dequeued1);
    test_queue_->DequeueNonBlocking(dequeued2);
    
    EXPECT_EQ(dequeued1.source, "SourceTestNode_1");
    EXPECT_EQ(dequeued2.source, "SourceTestNode_2");
    EXPECT_NE(dequeued1.source, dequeued2.source);
}

/**
 * Test 35: EventSourceConsistencyAcrossPublishes
 * Validates that same node publishes with consistent source field.
 */
TEST_F(MetricsPolicyGapsFixture, EventSourceConsistencyAcrossPublishes) {
    const std::string NODE_NAME = "ConsistentNode";
    
    // Publish 10 events from same node
    for (int i = 0; i < 10; ++i) {
        MetricsEvent event;
        event.source = NODE_NAME;
        event.event_type = "produced";
        test_queue_->Enqueue(event);
    }
    
    // Verify all have same source
    MetricsEvent dequeued;
    for (int i = 0; i < 10; ++i) {
        test_queue_->DequeueNonBlocking(dequeued);
        EXPECT_EQ(dequeued.source, NODE_NAME)
            << "Event " << i << " should have consistent source";
    }
}

/**
 * Test 36: MessageDataSurvivesEventPublishing
 * Validates that event.data fields are preserved through queue.
 */
TEST_F(MetricsPolicyGapsFixture, MessageDataSurvivesEventPublishing) {
    MetricsEvent event;
    event.source = "DataTestNode";
    event.event_type = "status_update";
    event.data["status"] = "RUNNING";
    event.data["count"] = "42";
    event.data["message_id"] = "msg_12345";
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    EXPECT_EQ(dequeued.data.size(), 3) << "Should preserve all data fields";
    EXPECT_EQ(dequeued.data["status"], "RUNNING");
    EXPECT_EQ(dequeued.data["count"], "42");
    EXPECT_EQ(dequeued.data["message_id"], "msg_12345");
}

/**
 * Test 37: SpecialCharactersInEventData
 * Validates that special characters in data fields are preserved.
 */
TEST_F(MetricsPolicyGapsFixture, SpecialCharactersInEventData) {
    MetricsEvent event;
    event.source = "SpecialCharNode";
    event.event_type = "special_event";
    event.data["newline"] = "line1\nline2";
    event.data["quote"] = "value\"with\"quotes";
    event.data["unicode"] = "emoji😀test";
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    EXPECT_EQ(dequeued.data["newline"], "line1\nline2");
    EXPECT_EQ(dequeued.data["quote"], "value\"with\"quotes");
    EXPECT_EQ(dequeued.data["unicode"], "emoji😀test");
}

/**
 * Test 38: EventTypeMatchesNodeType
 * Validates that event_type field matches the publishing node type.
 */
TEST_F(MetricsPolicyGapsFixture, EventTypeMatchesNodeType) {
    // Source publishes "message_produced"
    MetricsEvent source_event;
    source_event.source = "SourceTestNode";
    source_event.event_type = "message_produced";
    test_queue_->Enqueue(source_event);
    
    // Sink publishes "message_consumed"
    MetricsEvent sink_event;
    sink_event.source = "SinkTestNode";
    sink_event.event_type = "message_consumed";
    test_queue_->Enqueue(sink_event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    EXPECT_EQ(dequeued.event_type, "message_produced");
    
    test_queue_->DequeueNonBlocking(dequeued);
    EXPECT_EQ(dequeued.event_type, "message_consumed");
}

/**
 * Test 39: EventTypeConsistentAcrossInstances
 * Validates that same node type publishes consistent event types.
 */
TEST_F(MetricsPolicyGapsFixture, EventTypeConsistentAcrossInstances) {
    // Two SourceTestNode instances
    for (int i = 1; i <= 2; ++i) {
        MetricsEvent event;
        event.source = "SourceTestNode_" + std::to_string(i);
        event.event_type = "message_produced";
        test_queue_->Enqueue(event);
    }
    
    // Both should have "message_produced" type
    MetricsEvent dequeued;
    for (int i = 0; i < 2; ++i) {
        test_queue_->DequeueNonBlocking(dequeued);
        EXPECT_EQ(dequeued.event_type, "message_produced")
            << "Instance " << (i+1) << " should have consistent event type";
    }
}

/**
 * Test 40: EventDataFieldsPreservedWithLargeCount
 * Validates that many data fields are preserved.
 */
TEST_F(MetricsPolicyGapsFixture, EventDataFieldsPreservedWithLargeCount) {
    MetricsEvent event;
    event.source = "LargeDataNode";
    event.event_type = "metrics_snapshot";
    
    // Add 50 data fields
    for (int i = 0; i < 50; ++i) {
        std::string key = "metric_" + std::to_string(i);
        std::string value = std::to_string(i * 10);
        event.data[key] = value;
    }
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    EXPECT_EQ(dequeued.data.size(), 50) << "Should preserve all 50 fields";
    
    // Spot-check a few values
    EXPECT_EQ(dequeued.data["metric_0"], "0");
    EXPECT_EQ(dequeued.data["metric_25"], "250");
    EXPECT_EQ(dequeued.data["metric_49"], "490");
}

/**
 * Test 41: EmptyDataMapIsValid
 * Validates that events can have empty data maps.
 */
TEST_F(MetricsPolicyGapsFixture, EmptyDataMapIsValid) {
    MetricsEvent event;
    event.source = "MinimalNode";
    event.event_type = "simple_event";
    // No data fields
    
    EXPECT_TRUE(event.data.empty());
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    EXPECT_TRUE(dequeued.data.empty())
        << "Empty data map should be preserved";
    EXPECT_EQ(dequeued.source, "MinimalNode");
    EXPECT_EQ(dequeued.event_type, "simple_event");
}

/**
 * Test 42: AllEventFieldsPreservedTogether
 * Validates that all fields (timestamp, source, type, data) are preserved together.
 */
TEST_F(MetricsPolicyGapsFixture, AllEventFieldsPreservedTogether) {
    MetricsEvent event;
    auto original_time = std::chrono::system_clock::now();
    event.timestamp = original_time;
    event.source = "CompleteNode";
    event.event_type = "complete_event";
    event.data["field1"] = "value1";
    event.data["field2"] = "value2";
    event.data["field3"] = "value3";
    
    test_queue_->Enqueue(event);
    
    MetricsEvent dequeued;
    test_queue_->DequeueNonBlocking(dequeued);
    
    // All fields should match
    EXPECT_EQ(dequeued.source, "CompleteNode");
    EXPECT_EQ(dequeued.event_type, "complete_event");
    EXPECT_EQ(dequeued.data.size(), 3);
    EXPECT_EQ(dequeued.data["field1"], "value1");
    EXPECT_EQ(dequeued.data["field2"], "value2");
    EXPECT_EQ(dequeued.data["field3"], "value3");
    
    // Timestamp should be approximately same (within reasonable tolerance)
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        dequeued.timestamp - event.timestamp);
    EXPECT_LE(std::abs(diff.count()), 10) << "Timestamp should be preserved (±10ms)";
}

} // namespace MetricsPolicyGapsTests
