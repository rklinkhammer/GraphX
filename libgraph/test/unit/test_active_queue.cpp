// MIT License
//
// Copyright (c) 2026 graphlib contributors
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

#include <gtest/gtest.h>
#include "core/ActiveQueue.hpp"
#include "graph/Message.hpp"
#include <thread>
#include <chrono>
#include <future>
#include <vector>
#include <mutex>
#include <string>

using namespace core;
using namespace graph::message;

// ============================================================================
// Test Fixture for ActiveQueue Tests
// ============================================================================

class ActiveQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset for each test
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
};

// ============================================================================
// Basic Queue Operations (9 tests)
// ============================================================================

TEST_F(ActiveQueueTest, Constructor_DefaultParameters) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0);
    EXPECT_EQ(queue.Capacity(), 0);
    EXPECT_TRUE(queue.Enabled());
}

TEST_F(ActiveQueueTest, Constructor_WithCapacity) {
    ActiveQueue<int> queue(100, false);
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Capacity(), 100);
    EXPECT_FALSE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, Constructor_WithBlockOnFull) {
    ActiveQueue<int> queue(50, true);
    EXPECT_EQ(queue.Capacity(), 50);
    EXPECT_TRUE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, Enqueue_SingleElement) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(42));
    EXPECT_EQ(queue.Size(), 1);
    EXPECT_FALSE(queue.Empty());
}

TEST_F(ActiveQueueTest, Enqueue_MultipleElements) {
    ActiveQueue<int> queue;
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    EXPECT_EQ(queue.Size(), 10);
}

TEST_F(ActiveQueueTest, DequeueNonBlocking_SingleElement) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(42));
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, DequeueNonBlocking_EmptyQueue) {
    ActiveQueue<int> queue;
    
    int value;
    EXPECT_FALSE(queue.DequeueNonBlocking(value));
}

TEST_F(ActiveQueueTest, Clear_RemovesAllElements) {
    ActiveQueue<int> queue;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    EXPECT_EQ(queue.Size(), 5);
    
    queue.Clear();
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0);
}

TEST_F(ActiveQueueTest, Disable_PreventsEnqueue) {
    ActiveQueue<int> queue;
    queue.Disable();
    
    EXPECT_FALSE(queue.Enqueue(42));
    EXPECT_FALSE(queue.Enabled());
}

// ============================================================================
// Deque-Style Operations (8 tests)
// ============================================================================

TEST_F(ActiveQueueTest, PushFront_AddToFront) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.PushBack(1));
    EXPECT_TRUE(queue.PushBack(2));
    EXPECT_TRUE(queue.PushFront(0));
    
    std::vector<int> values;
    int value;
    while (queue.DequeueNonBlocking(value)) {
        values.push_back(value);
    }
    
    EXPECT_EQ(values, std::vector<int>({0, 1, 2}));
}

TEST_F(ActiveQueueTest, PopBack_RemoveFromBack) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_TRUE(queue.Enqueue(3));
    
    int value;
    EXPECT_TRUE(queue.PopBack(value));
    EXPECT_EQ(value, 3);
    EXPECT_EQ(queue.Size(), 2);
}

TEST_F(ActiveQueueTest, PopFront_SameAsDequeueNonBlocking) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(42));
    
    int value;
    EXPECT_TRUE(queue.PopFront(value));
    EXPECT_EQ(value, 42);
}

TEST_F(ActiveQueueTest, Front_AccessWithoutRemoving) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(42));
    EXPECT_TRUE(queue.Enqueue(43));
    
    int value;
    EXPECT_TRUE(queue.Front(value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(queue.Size(), 2);
}

TEST_F(ActiveQueueTest, Back_AccessLastElement) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_TRUE(queue.Enqueue(3));
    
    int value;
    EXPECT_TRUE(queue.Back(value));
    EXPECT_EQ(value, 3);
    EXPECT_EQ(queue.Size(), 3);
}

TEST_F(ActiveQueueTest, At_RandomAccess) {
    ActiveQueue<int> queue;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(queue.Enqueue(i * 10));
    }
    
    int value;
    EXPECT_TRUE(queue.At(0, value));
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(queue.At(2, value));
    EXPECT_EQ(value, 20);
    EXPECT_TRUE(queue.At(4, value));
    EXPECT_EQ(value, 40);
}

TEST_F(ActiveQueueTest, At_OutOfBounds) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(1));
    
    int value;
    EXPECT_FALSE(queue.At(5, value));
}

TEST_F(ActiveQueueTest, PushBack_Enqueue_Equivalent) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.PushBack(42));
    
    int value;
    EXPECT_TRUE(queue.PopFront(value));
    EXPECT_EQ(value, 42);
}

// ============================================================================
// Boundary Conditions (6 tests)
// ============================================================================

TEST_F(ActiveQueueTest, BoundedQueue_DropWhenFull) {
    ActiveQueue<int> queue(3, false);  // Capacity 3, drop when full
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_TRUE(queue.Enqueue(3));
    EXPECT_FALSE(queue.Enqueue(4));  // Should be dropped
    
    EXPECT_EQ(queue.Size(), 3);
}

TEST_F(ActiveQueueTest, UnboundedQueue_NeverFulls) {
    ActiveQueue<int> queue(0);  // Capacity 0 = unbounded
    
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    EXPECT_EQ(queue.Size(), 100);
}

TEST_F(ActiveQueueTest, SetCapacity_ChangesLimit) {
    ActiveQueue<int> queue(5);
    queue.SetCapacity(10);
    
    EXPECT_EQ(queue.Capacity(), 10);
}

TEST_F(ActiveQueueTest, Enable_AllowsEnqueueAfterDisable) {
    ActiveQueue<int> queue;
    queue.Disable();
    EXPECT_FALSE(queue.Enqueue(42));
    
    queue.Enable();
    EXPECT_TRUE(queue.Enqueue(42));
    EXPECT_EQ(queue.Size(), 1);
}

TEST_F(ActiveQueueTest, SetBlockOnFull_FailsWithData) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.Enqueue(1));
    
    EXPECT_FALSE(queue.SetBlockOnFull(true));
    EXPECT_FALSE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, SetBlockOnFull_SucceedsWhenEmpty) {
    ActiveQueue<int> queue;
    EXPECT_TRUE(queue.SetBlockOnFull(true));
    EXPECT_TRUE(queue.GetBlockOnFull());
}

// ============================================================================
// Metrics Collection (5 tests)
// ============================================================================

TEST_F(ActiveQueueTest, Metrics_DisabledByDefault) {
    ActiveQueue<int> queue;
    const auto& metrics = queue.GetMetrics();
    
    EXPECT_EQ(metrics.enqueued_count.load(), 0);
    EXPECT_EQ(metrics.dequeued_count.load(), 0);
}

TEST_F(ActiveQueueTest, Metrics_TrackEnqueue) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_TRUE(queue.Enqueue(3));
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 3);
}

TEST_F(ActiveQueueTest, Metrics_TrackDequeue) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.dequeued_count.load(), 2);
}

TEST_F(ActiveQueueTest, Metrics_TrackMaxSize) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.max_size_observed.load(), 50);
}

TEST_F(ActiveQueueTest, Metrics_ResetClearsCounters) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    
    queue.ResetMetrics();
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 0);
    EXPECT_EQ(metrics.dequeued_count.load(), 0);
    EXPECT_EQ(metrics.max_size_observed.load(), 0);
}

TEST_F(ActiveQueueTest, Metrics_TrackDequeStyleOperations) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();

    EXPECT_TRUE(queue.Emplace(1));
    EXPECT_TRUE(queue.PushFront(0));

    int value = 0;
    EXPECT_TRUE(queue.PopBack(value));
    EXPECT_EQ(value, 1);

    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 2);
    EXPECT_EQ(metrics.dequeued_count.load(), 1);
    EXPECT_EQ(metrics.current_size.load(), 1);
    EXPECT_EQ(metrics.max_size_observed.load(), 2);
}

TEST_F(ActiveQueueTest, Metrics_TrackRejectionsAndEmptyDequeStyleOperations) {
    ActiveQueue<int> queue(1, false);
    queue.EnableMetrics();

    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_FALSE(queue.Emplace(2));
    EXPECT_FALSE(queue.PushFront(3));

    int value = 0;
    EXPECT_TRUE(queue.PopBack(value));
    EXPECT_FALSE(queue.PopBack(value));

    queue.Disable();
    EXPECT_FALSE(queue.Enqueue(4));

    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 1);
    EXPECT_EQ(metrics.dequeued_count.load(), 1);
    EXPECT_EQ(metrics.enqueue_rejections.load(), 3);
    EXPECT_EQ(metrics.dequeue_empty.load(), 1);
    EXPECT_EQ(metrics.current_size.load(), 0);
}

TEST_F(ActiveQueueTest, Metrics_ClearUpdatesCurrentSize) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();

    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    ASSERT_EQ(queue.GetMetrics().current_size.load(), 2);

    queue.Clear();

    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.current_size.load(), 0);
    EXPECT_EQ(metrics.max_size_observed.load(), 2);
}

// ============================================================================
// Thread Safety (2 tests - simplified for Phase 1)
// ============================================================================

TEST_F(ActiveQueueTest, ThreadSafety_ConcurrentEnqueue) {
    ActiveQueue<int> queue;
    const int num_threads = 4;
    const int items_per_thread = 25;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
        threads.emplace_back([&queue, t, items_per_thread]() {
            for (int i = 0; i < items_per_thread; ++i) {
                EXPECT_TRUE(queue.Enqueue(t * 1000 + i));
            }
        });
#pragma clang diagnostic pop
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(queue.Size(), num_threads * items_per_thread);
}

TEST_F(ActiveQueueTest, ThreadSafety_ConcurrentDequeue) {
    ActiveQueue<int> queue;
    const int total_items = 100;
    
    for (int i = 0; i < total_items; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    
    std::vector<int> results;
    std::mutex results_mutex;
    const int num_threads = 4;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            int value;
            while (queue.DequeueNonBlocking(value)) {
                {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    results.push_back(value);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(results.size(), total_items);
    EXPECT_TRUE(queue.Empty());
}

// ============================================================================
// PHASE 1: Message<T> Integration Tests (20 tests)
// ============================================================================

TEST_F(ActiveQueueTest, MessageQueue_EnqueueDequeueInt) {
    ActiveQueue<Message> queue;
    Message msg(42);
    EXPECT_TRUE(queue.Enqueue(std::move(msg)));
    
    Message retrieved;
    EXPECT_TRUE(queue.DequeueNonBlocking(retrieved));
    EXPECT_EQ(retrieved.get<int>(), 42);
}

TEST_F(ActiveQueueTest, MessageQueue_EnqueueDequeueDouble) {
    ActiveQueue<Message> queue;
    Message msg(3.14159);
    EXPECT_TRUE(queue.Enqueue(std::move(msg)));
    
    Message retrieved;
    EXPECT_TRUE(queue.DequeueNonBlocking(retrieved));
    EXPECT_DOUBLE_EQ(retrieved.get<double>(), 3.14159);
}

TEST_F(ActiveQueueTest, MessageQueue_EnqueueDequeueString) {
    ActiveQueue<Message> queue;
    Message msg(std::string("Hello, Queue!"));
    EXPECT_TRUE(queue.Enqueue(std::move(msg)));
    
    Message retrieved;
    EXPECT_TRUE(queue.DequeueNonBlocking(retrieved));
    EXPECT_EQ(retrieved.get<std::string>(), "Hello, Queue!");
}

TEST_F(ActiveQueueTest, MessageQueue_MultipleMessageTypes) {
    ActiveQueue<Message> queue;
    
    Message msg_int(42);
    Message msg_double(3.14);
    Message msg_string(std::string("test"));
    
    EXPECT_TRUE(queue.Enqueue(std::move(msg_int)));
    EXPECT_TRUE(queue.Enqueue(std::move(msg_double)));
    EXPECT_TRUE(queue.Enqueue(std::move(msg_string)));
    
    EXPECT_EQ(queue.Size(), 3);
}

TEST_F(ActiveQueueTest, MessageQueue_TypePreservation) {
    ActiveQueue<Message> queue;
    
    Message msg1(100);
    Message msg2(2.71828);
    
    (void)queue.Enqueue(std::move(msg1));
    (void)queue.Enqueue(std::move(msg2));
    
    Message r1, r2;
    (void)queue.DequeueNonBlocking(r1);
    (void)queue.DequeueNonBlocking(r2);
    
    EXPECT_EQ(r1.get<int>(), 100);
    EXPECT_DOUBLE_EQ(r2.get<double>(), 2.71828);
}

TEST_F(ActiveQueueTest, MessageQueue_CopySemantics) {
    ActiveQueue<Message> queue;
    
    Message original(999);
    Message copy = original;  // Independent copy
    
    (void)queue.Enqueue(std::move(copy));
    EXPECT_TRUE(queue.Enqueue(std::move(original)));
    
    Message r1, r2;
    (void)queue.DequeueNonBlocking(r1);
    (void)queue.DequeueNonBlocking(r2);
    
    // Both should contain 999
    EXPECT_EQ(r1.get<int>(), 999);
    EXPECT_EQ(r2.get<int>(), 999);
}

TEST_F(ActiveQueueTest, MessageQueue_MoveSemantics) {
    ActiveQueue<Message> queue;
    
    Message msg(777);
    EXPECT_TRUE(queue.Enqueue(std::move(msg)));
    
    Message retrieved;
    EXPECT_TRUE(queue.DequeueNonBlocking(retrieved));
    EXPECT_EQ(retrieved.get<int>(), 777);
    
    // Original message should be invalid after move
    // (We can't assert this directly, but operations should be safe)
}

TEST_F(ActiveQueueTest, MessageQueue_LargePayload) {
    ActiveQueue<Message> queue;
    
    // Create a message with a string (heap allocation)
    std::string large_data(1000, 'a');
    Message msg(large_data);
    
    EXPECT_TRUE(queue.Enqueue(std::move(msg)));
    
    Message retrieved;
    EXPECT_TRUE(queue.DequeueNonBlocking(retrieved));
    EXPECT_EQ(retrieved.get<std::string>().size(), 1000);
}

TEST_F(ActiveQueueTest, MessageQueue_EmptyQueueDequeue) {
    ActiveQueue<Message> queue;
    
    Message msg;
    EXPECT_FALSE(queue.DequeueNonBlocking(msg));
}

TEST_F(ActiveQueueTest, MessageQueue_DisablePreventsMsgEnqueue) {
    ActiveQueue<Message> queue;
    queue.Disable();
    
    Message msg(42);
    EXPECT_FALSE(queue.Enqueue(std::move(msg)));
}

TEST_F(ActiveQueueTest, MessageQueue_ClearRemovesMsgElements) {
    ActiveQueue<Message> queue;
    
    for (int i = 0; i < 5; ++i) {
        Message msg(i * 100);
        (void)queue.Enqueue(std::move(msg));
    }
    
    EXPECT_EQ(queue.Size(), 5);
    queue.Clear();
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, MessageQueue_MetricsWithMessages) {
    ActiveQueue<Message> queue;
    queue.EnableMetrics();
    
    for (int i = 0; i < 10; ++i) {
        Message msg(i);
        (void)queue.Enqueue(std::move(msg));
    }
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 10);
}

TEST_F(ActiveQueueTest, MessageQueue_MetricsMessageDequeue) {
    ActiveQueue<Message> queue;
    queue.EnableMetrics();
    
    for (int i = 0; i < 5; ++i) {
        Message msg(i);
        (void)queue.Enqueue(std::move(msg));
    }
    
    Message retrieved;
    for (int i = 0; i < 3; ++i) {
        (void)queue.DequeueNonBlocking(retrieved);
    }
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.dequeued_count.load(), 3);
    EXPECT_EQ(metrics.current_size.load(), 2);
}

TEST_F(ActiveQueueTest, MessageQueue_BoundedQueueDropMessages) {
    ActiveQueue<Message> queue(3, false);  // Bounded, drop when full
    
    EXPECT_TRUE(queue.Enqueue(Message(1)));
    EXPECT_TRUE(queue.Enqueue(Message(2)));
    EXPECT_TRUE(queue.Enqueue(Message(3)));
    EXPECT_FALSE(queue.Enqueue(Message(4)));  // Should be dropped
    
    EXPECT_EQ(queue.Size(), 3);
}

TEST_F(ActiveQueueTest, MessageQueue_FrontBackMessages) {
    ActiveQueue<Message> queue;
    
    (void)queue.Enqueue(Message(10));
    (void)queue.Enqueue(Message(20));
    (void)queue.Enqueue(Message(30));
    
    Message front_msg, back_msg;
    EXPECT_TRUE(queue.Front(front_msg));
    EXPECT_TRUE(queue.Back(back_msg));
    
    EXPECT_EQ(front_msg.get<int>(), 10);
    EXPECT_EQ(back_msg.get<int>(), 30);
}

TEST_F(ActiveQueueTest, MessageQueue_AtRandomAccess) {
    ActiveQueue<Message> queue;
    
    for (int i = 0; i < 5; ++i) {
        (void)queue.Enqueue(Message(i * 100));
    }
    
    Message msg;
    EXPECT_TRUE(queue.At(0, msg));
    EXPECT_EQ(msg.get<int>(), 0);
    
    EXPECT_TRUE(queue.At(2, msg));
    EXPECT_EQ(msg.get<int>(), 200);
    
    EXPECT_TRUE(queue.At(4, msg));
    EXPECT_EQ(msg.get<int>(), 400);
}

TEST_F(ActiveQueueTest, MessageQueue_PushFrontMessages) {
    ActiveQueue<Message> queue;
    
    (void)queue.PushBack(Message(1));
    (void)queue.PushBack(Message(2));
    (void)queue.PushFront(Message(0));
    
    std::vector<int> values;
    Message msg;
    while (queue.DequeueNonBlocking(msg)) {
        values.push_back(msg.get<int>());
    }
    
    EXPECT_EQ(values, std::vector<int>({0, 1, 2}));
}

TEST_F(ActiveQueueTest, MessageQueue_ConcurrentMessageEnqueue) {
    ActiveQueue<Message> queue;
    const int num_threads = 4;
    const int items_per_thread = 25;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
        threads.emplace_back([&queue, t, items_per_thread]() {
            for (int i = 0; i < items_per_thread; ++i) {
                Message msg(t * 1000 + i);
                EXPECT_TRUE(queue.Enqueue(std::move(msg)));
            }
        });
#pragma clang diagnostic pop
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(queue.Size(), num_threads * items_per_thread);
}

// ============================================================================
// PHASE 1: Blocking Queue Operation Tests (20 tests)
// ============================================================================

TEST_F(ActiveQueueTest, BlockingDequeue_WaitsForElement) {
    ActiveQueue<int> queue;
    int value = 0;
    std::promise<void> dequeue_started;
    auto dequeue_started_future = dequeue_started.get_future();
    std::promise<bool> dequeue_completed;
    auto dequeue_completed_future = dequeue_completed.get_future();

    std::thread dequeuer([&queue, &value, &dequeue_started, &dequeue_completed]() {
        dequeue_started.set_value();
        dequeue_completed.set_value(queue.Dequeue(value));
    });

    ASSERT_EQ(dequeue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(dequeue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    EXPECT_TRUE(queue.Enqueue(42));

    ASSERT_EQ(dequeue_completed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_TRUE(dequeue_completed_future.get());

    dequeuer.join();
    EXPECT_EQ(value, 42);
}

TEST_F(ActiveQueueTest, BlockingDequeue_WakesOnDisable) {
    ActiveQueue<int> queue;
    int value = 0;
    std::promise<void> dequeue_started;
    auto dequeue_started_future = dequeue_started.get_future();
    std::promise<bool> dequeue_completed;
    auto dequeue_completed_future = dequeue_completed.get_future();

    std::thread dequeuer([&queue, &value, &dequeue_started, &dequeue_completed]() {
        dequeue_started.set_value();
        dequeue_completed.set_value(queue.Dequeue(value));
    });

    ASSERT_EQ(dequeue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(dequeue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    queue.Disable();

    ASSERT_EQ(dequeue_completed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_FALSE(dequeue_completed_future.get());

    dequeuer.join();
}

TEST_F(ActiveQueueTest, BlockingDequeue_ImmediateIfElementAvailable) {
    ActiveQueue<int> queue;
    (void)queue.Enqueue(42);
    
    int value = 0;
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 42);
}

TEST_F(ActiveQueueTest, BlockingEnqueue_WaitsWhenFull) {
    ActiveQueue<int> queue(2, true);  // Capacity 2, block on full
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    
    std::promise<void> enqueue_started;
    auto enqueue_started_future = enqueue_started.get_future();
    std::promise<bool> enqueue_completed;
    auto enqueue_completed_future = enqueue_completed.get_future();

    std::thread enqueuer([&queue, &enqueue_started, &enqueue_completed]() {
        enqueue_started.set_value();
        enqueue_completed.set_value(queue.Enqueue(3));
    });

    ASSERT_EQ(enqueue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(enqueue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);
    EXPECT_EQ(queue.Size(), 2);  // Still full
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));  // Make space

    ASSERT_EQ(enqueue_completed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_TRUE(enqueue_completed_future.get());

    enqueuer.join();
    EXPECT_EQ(queue.Size(), 2);
}

TEST_F(ActiveQueueTest, BlockingEnqueue_WakesOnDisable) {
    ActiveQueue<int> queue(1, true);  // Capacity 1, block on full
    
    ASSERT_TRUE(queue.Enqueue(1));  // Fill it

    std::promise<void> enqueue_started;
    auto enqueue_started_future = enqueue_started.get_future();
    std::promise<bool> enqueue_completed;
    auto enqueue_completed_future = enqueue_completed.get_future();

    std::thread enqueuer([&queue, &enqueue_started, &enqueue_completed]() {
        enqueue_started.set_value();
        enqueue_completed.set_value(queue.Enqueue(2));
    });

    ASSERT_EQ(enqueue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(enqueue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    queue.Disable();  // Unblock the enqueuer

    ASSERT_EQ(enqueue_completed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_FALSE(enqueue_completed_future.get());

    enqueuer.join();
}

TEST_F(ActiveQueueTest, BlockingEnqueue_WakesWhenQueueCleared) {
    ActiveQueue<int> queue(1, true);
    ASSERT_TRUE(queue.Enqueue(1));

    std::promise<void> enqueue_started;
    auto enqueue_started_future = enqueue_started.get_future();
    std::promise<bool> enqueue_completed;
    auto enqueue_completed_future = enqueue_completed.get_future();

    std::thread enqueuer([&queue, &enqueue_started, &enqueue_completed]() {
        enqueue_started.set_value();
        enqueue_completed.set_value(queue.Enqueue(2));
    });

    ASSERT_EQ(enqueue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(enqueue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    queue.Clear();

    if (enqueue_completed_future.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
        queue.Disable();
        enqueuer.join();
        FAIL() << "Blocked enqueue did not wake after Clear()";
    }
    EXPECT_TRUE(enqueue_completed_future.get());

    enqueuer.join();

    int value = 0;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, BlockingEnqueue_WakesWhenCapacityIncreased) {
    ActiveQueue<int> queue(1, true);
    ASSERT_TRUE(queue.Enqueue(1));

    std::promise<void> enqueue_started;
    auto enqueue_started_future = enqueue_started.get_future();
    std::promise<bool> enqueue_completed;
    auto enqueue_completed_future = enqueue_completed.get_future();

    std::thread enqueuer([&queue, &enqueue_started, &enqueue_completed]() {
        enqueue_started.set_value();
        enqueue_completed.set_value(queue.Enqueue(2));
    });

    ASSERT_EQ(enqueue_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(enqueue_completed_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    queue.SetCapacity(2);

    if (enqueue_completed_future.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
        queue.Disable();
        enqueuer.join();
        FAIL() << "Blocked enqueue did not wake after SetCapacity() made space";
    }
    EXPECT_TRUE(enqueue_completed_future.get());

    enqueuer.join();

    EXPECT_EQ(queue.Size(), 2);

    int value = 0;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 2);
}

TEST_F(ActiveQueueTest, BlockingEnqueue_ImmediateIfSpace) {
    ActiveQueue<int> queue(5, true);
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_EQ(queue.Size(), 2);
}

TEST_F(ActiveQueueTest, MultipleBlockedDequeuers_OneWakesPerEnqueue) {
    ActiveQueue<int> queue;
    std::vector<int> results;
    std::mutex results_mutex;
    std::atomic<int> completed{0};
    const int num_waiters = 3;

    std::vector<std::promise<void>> started(num_waiters);
    std::vector<std::future<void>> started_futures;
    started_futures.reserve(num_waiters);
    for (auto& promise : started) {
        started_futures.push_back(promise.get_future());
    }

    std::vector<std::thread> dequeuers;
    
    for (int i = 0; i < num_waiters; ++i) {
        dequeuers.emplace_back([&queue, &results, &results_mutex, &completed, &started, i]() {
            int value;
            started[i].set_value();
            if (queue.Dequeue(value)) {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(value);
            }
            completed.fetch_add(1, std::memory_order_release);
        });
    }

    for (auto& future : started_futures) {
        ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    }
    EXPECT_EQ(completed.load(std::memory_order_acquire), 0);

    for (int i = 0; i < num_waiters; ++i) {
        EXPECT_TRUE(queue.Enqueue(i * 100));
    }
    
    for (auto& t : dequeuers) {
        t.join();
    }
    
    EXPECT_EQ(results.size(), num_waiters);
    EXPECT_EQ(completed.load(std::memory_order_acquire), num_waiters);
}

TEST_F(ActiveQueueTest, MultipleBlockedEnqueuers_OneWakesPerDequeue) {
    ActiveQueue<int> queue(1, true);  // Capacity 1
    ASSERT_TRUE(queue.Enqueue(0));  // Fill it
    
    std::vector<bool> results;
    std::mutex results_mutex;
    std::atomic<int> completed{0};
    const int num_waiters = 3;

    std::vector<std::promise<void>> started(num_waiters);
    std::vector<std::future<void>> started_futures;
    started_futures.reserve(num_waiters);
    for (auto& promise : started) {
        started_futures.push_back(promise.get_future());
    }

    std::vector<std::thread> enqueuers;
    
    for (int i = 0; i < num_waiters; ++i) {
        enqueuers.emplace_back([&queue, &results, &results_mutex, &completed, &started, i]() {
            started[i].set_value();
            bool success = queue.Enqueue(i + 1);
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(success);
            completed.fetch_add(1, std::memory_order_release);
        });
    }

    for (auto& future : started_futures) {
        ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    }
    EXPECT_EQ(completed.load(std::memory_order_acquire), 0);

    int value;
    bool all_progressed = true;
    for (int i = 0; i < num_waiters; ++i) {
        EXPECT_TRUE(queue.DequeueNonBlocking(value));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (completed.load(std::memory_order_acquire) < i + 1 &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (completed.load(std::memory_order_acquire) < i + 1) {
            all_progressed = false;
            queue.Disable();
            break;
        }
    }
    
    for (auto& t : enqueuers) {
        t.join();
    }
    
    EXPECT_TRUE(all_progressed);
    EXPECT_EQ(completed.load(std::memory_order_acquire), num_waiters);
    for (bool result : results) {
        EXPECT_TRUE(result);
    }
}

TEST_F(ActiveQueueTest, BlockingDequeue_EmptyAndDisabled) {
    ActiveQueue<int> queue;
    queue.Disable();
    
    int value;
    EXPECT_FALSE(queue.Dequeue(value));  // Should return false, not block
}

TEST_F(ActiveQueueTest, BlockingDequeue_DisableTakesPriorityOverQueuedData) {
    ActiveQueue<int> queue;
    ASSERT_TRUE(queue.Enqueue(42));
    queue.Disable();

    int value = 0;
    EXPECT_FALSE(queue.Dequeue(value));
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 42);
}

TEST_F(ActiveQueueTest, BlockingDequeue_WithMessages) {
    ActiveQueue<Message> queue;
    Message result;
    bool dequeue_succeeded = false;
    
    std::thread enqueuer([&queue]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (void)queue.Enqueue(Message(42));
    });
    
    std::thread dequeuer([&queue, &result, &dequeue_succeeded]() {
        dequeue_succeeded = queue.Dequeue(result);
    });
    
    dequeuer.join();
    enqueuer.join();
    
    EXPECT_TRUE(dequeue_succeeded);
    EXPECT_EQ(result.get<int>(), 42);
}

TEST_F(ActiveQueueTest, BlockingEnqueue_WithMessages) {
    ActiveQueue<Message> queue(2, true);
    
    (void)queue.Enqueue(Message(1));
    (void)queue.Enqueue(Message(2));
    
    bool enqueue_succeeded = false;
    
    std::thread enqueuer([&queue, &enqueue_succeeded]() {
        Message msg(3);
        enqueue_succeeded = queue.Enqueue(std::move(msg));
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    Message msg;
    (void)queue.DequeueNonBlocking(msg);
    
    enqueuer.join();
    
    EXPECT_TRUE(enqueue_succeeded);
}

TEST_F(ActiveQueueTest, ProducerConsumer_SingleThread) {
    ActiveQueue<int> queue;
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.Enqueue(i));
    }
    
    for (int i = 0; i < 10; ++i) {
        int value;
        EXPECT_TRUE(queue.DequeueNonBlocking(value));
        EXPECT_EQ(value, i);
    }
}

TEST_F(ActiveQueueTest, ProducerConsumer_OneProducerMultipleConsumers) {
    ActiveQueue<int> queue;
    std::vector<int> results;
    std::mutex results_mutex;
    const int total_items = 40;
    const int num_consumers = 4;
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&queue, &producer_done]() {
        for (int i = 0; i < total_items; ++i) {
            (void)queue.Enqueue(i);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        producer_done = true;
    });
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&queue, &results, &results_mutex, &producer_done]() {
            int value;
            while (queue.DequeueNonBlocking(value)) {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(value);
            }
            // Keep trying until producer is done and queue is empty
            while (!producer_done || !queue.Empty()) {
                if (queue.DequeueNonBlocking(value)) {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    results.push_back(value);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
    
    producer.join();
    
    // Wait a bit for consumers to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    for (auto& c : consumers) {
        c.join();
    }
    
    EXPECT_EQ(results.size(), total_items);
}

TEST_F(ActiveQueueTest, BlockingQueueCapacity_Enforced) {
    ActiveQueue<int> queue(3, false);  // Bounded, drop when full
    
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_TRUE(queue.Enqueue(3));
    EXPECT_FALSE(queue.Enqueue(4));  // Exceeds capacity
    
    EXPECT_EQ(queue.Size(), 3);
}

TEST_F(ActiveQueueTest, Emplace_EnqueuesConstructedElement) {
    ActiveQueue<std::string> queue;
    
    EXPECT_TRUE(queue.Emplace("Hello"));
    EXPECT_TRUE(queue.Emplace("World"));
    
    std::string value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, "Hello");
}

// ============================================================================
// PHASE 1: Comparator/Sorted Insertion Tests (15 tests)
// ============================================================================

TEST_F(ActiveQueueTest, Comparator_SortedInsertionAscending) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;  // Min heap
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(30);
    (void)queue.Enqueue(10);
    (void)queue.Enqueue(20);
#pragma clang diagnostic pop
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 10);
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 20);
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 30);
}

TEST_F(ActiveQueueTest, Comparator_SortedInsertionDescending) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a > b;  // Max heap
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(10);
    (void)queue.Enqueue(30);
    (void)queue.Enqueue(20);
#pragma clang diagnostic pop
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 30);
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 20);
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 10);
}

TEST_F(ActiveQueueTest, Comparator_StringsSorted) {
    ActiveQueue<std::string> queue;
    queue.SetComparator([](const std::string& a, const std::string& b) {
        return a < b;  // Alphabetical
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(std::string("zebra"));
    (void)queue.Enqueue(std::string("apple"));
    (void)queue.Enqueue(std::string("mango"));
#pragma clang diagnostic pop
    
    std::string value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, "apple");
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, "mango");
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, "zebra");
}

TEST_F(ActiveQueueTest, Comparator_MessagesSorted) {
    struct IntWrapper {
        int value;
        explicit IntWrapper(int v = 0) : value(v) {}
    };
    
    ActiveQueue<IntWrapper> queue;
    queue.SetComparator([](const IntWrapper& a, const IntWrapper& b) {
        return a.value < b.value;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(IntWrapper(50));
    (void)queue.Enqueue(IntWrapper(20));
    (void)queue.Enqueue(IntWrapper(40));
#pragma clang diagnostic pop
    
    IntWrapper result;
    EXPECT_TRUE(queue.DequeueNonBlocking(result));
    EXPECT_EQ(result.value, 20);
}

TEST_F(ActiveQueueTest, Comparator_PreservesOrderAfterDequeue) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(30);
    (void)queue.Enqueue(10);
#pragma clang diagnostic pop
    
    int value;
    (void)queue.DequeueNonBlocking(value);  // Remove 10
    EXPECT_EQ(value, 10);
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(20);
    (void)queue.Enqueue(5);
#pragma clang diagnostic pop
    
    (void)queue.DequeueNonBlocking(value);  // Should get 5
    EXPECT_EQ(value, 5);
    
    (void)queue.DequeueNonBlocking(value);  // Should get 20
    EXPECT_EQ(value, 20);
    
    (void)queue.DequeueNonBlocking(value);  // Should get 30
    EXPECT_EQ(value, 30);
}

TEST_F(ActiveQueueTest, Comparator_CustomComparatorComplex) {
    struct Person {
        std::string name;
        int age;
        Person(const std::string& n = "", int a = 0) : name(n), age(a) {}
    };
    
    ActiveQueue<Person> queue;
    // Sort by age (ascending)
    queue.SetComparator([](const Person& a, const Person& b) {
        return a.age < b.age;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(Person("Alice", 30));
    (void)queue.Enqueue(Person("Bob", 20));
    (void)queue.Enqueue(Person("Charlie", 25));
#pragma clang diagnostic pop
    
    Person p;
    EXPECT_TRUE(queue.DequeueNonBlocking(p));
    EXPECT_EQ(p.age, 20);
    EXPECT_EQ(p.name, "Bob");
}

TEST_F(ActiveQueueTest, Comparator_EmptyQueue) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
    EXPECT_TRUE(queue.Empty());
    
    int value;
    EXPECT_FALSE(queue.DequeueNonBlocking(value));
}

TEST_F(ActiveQueueTest, Comparator_SingleElement) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(42);
#pragma clang diagnostic pop
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 42);
}

TEST_F(ActiveQueueTest, Comparator_LargeDataset) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
    // Insert in random order
    std::vector<int> input = {50, 10, 30, 20, 40, 5, 15, 25, 35, 45};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    for (int val : input) {
        (void)queue.Enqueue(val);
    }
#pragma clang diagnostic pop
    
    // Verify sorted order
    std::vector<int> result;
    int value;
    while (queue.DequeueNonBlocking(value)) {
        result.push_back(value);
    }
    
    std::vector<int> expected = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    EXPECT_EQ(result, expected);
}

TEST_F(ActiveQueueTest, Comparator_DuplicateValues) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(5);
    (void)queue.Enqueue(5);
    (void)queue.Enqueue(5);
    (void)queue.Enqueue(10);
    (void)queue.Enqueue(10);
#pragma clang diagnostic pop
    
    std::vector<int> result;
    int value;
    while (queue.DequeueNonBlocking(value)) {
        result.push_back(value);
    }
    
    std::vector<int> expected = {5, 5, 5, 10, 10};
    EXPECT_EQ(result, expected);
}

TEST_F(ActiveQueueTest, Comparator_ClearRemovesSortedElements) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(30);
    (void)queue.Enqueue(10);
    (void)queue.Enqueue(20);
#pragma clang diagnostic pop
    
    queue.Clear();
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, Comparator_FIFOWhenNoComparator) {
    ActiveQueue<int> queue;
    // No comparator set - should be FIFO
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(3);
    (void)queue.Enqueue(1);
    (void)queue.Enqueue(2);
#pragma clang diagnostic pop
    
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 3);  // FIFO order
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 2);
}

TEST_F(ActiveQueueTest, Comparator_DoesNotAffectPeek) {
    ActiveQueue<int> queue;
    queue.SetComparator([](const int& a, const int& b) {
        return a < b;
    });
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"
    (void)queue.Enqueue(30);
    (void)queue.Enqueue(10);
    (void)queue.Enqueue(20);
#pragma clang diagnostic pop
    
    int front;
    EXPECT_TRUE(queue.Front(front));
    EXPECT_EQ(front, 10);  // Sorted order
    
    // Peek doesn't change the queue
    EXPECT_TRUE(queue.Front(front));
    EXPECT_EQ(front, 10);
}

// ============================================================================
// PHASE 2: State Enumeration Tests (18 tests)
// ============================================================================

TEST_F(ActiveQueueTest, State_EmptyEnabledUnbounded) {
    ActiveQueue<int> queue(0, false);  // Unbounded, drop mode
    EXPECT_TRUE(queue.Empty());
    EXPECT_TRUE(queue.Enabled());
    EXPECT_EQ(queue.Capacity(), 0);
    EXPECT_FALSE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, State_EmptyEnabledBounded) {
    ActiveQueue<int> queue(10, false);  // Bounded, drop mode
    EXPECT_TRUE(queue.Empty());
    EXPECT_TRUE(queue.Enabled());
    EXPECT_EQ(queue.Capacity(), 10);
    EXPECT_FALSE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, State_EmptyEnabledBlockingBounded) {
    ActiveQueue<int> queue(10, true);  // Bounded, block mode
    EXPECT_TRUE(queue.Empty());
    EXPECT_TRUE(queue.Enabled());
    EXPECT_EQ(queue.Capacity(), 10);
    EXPECT_TRUE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, State_PartialUnbounded) {
    ActiveQueue<int> queue(0, false);
    for (int i = 0; i < 50; ++i) {
        (void)queue.Enqueue(i);
    }
    EXPECT_FALSE(queue.Empty());
    EXPECT_EQ(queue.Size(), 50);
    EXPECT_TRUE(queue.Enabled());
}

TEST_F(ActiveQueueTest, State_FullBounded) {
    ActiveQueue<int> queue(5, false);
    for (int i = 0; i < 5; ++i) {
        (void)queue.Enqueue(i);
    }
    EXPECT_EQ(queue.Size(), 5);
    EXPECT_EQ(queue.Capacity(), 5);
    EXPECT_FALSE(queue.Enqueue(5));  // Drops when full
}

TEST_F(ActiveQueueTest, State_DisabledEmpty) {
    ActiveQueue<int> queue;
    queue.Disable();
    EXPECT_FALSE(queue.Enabled());
    EXPECT_TRUE(queue.Empty());
    EXPECT_FALSE(queue.Enqueue(1));
}

TEST_F(ActiveQueueTest, State_DisabledWithData) {
    ActiveQueue<int> queue;
    (void)queue.Enqueue(1);
    (void)queue.Enqueue(2);
    queue.Disable();
    
    EXPECT_FALSE(queue.Enabled());
    EXPECT_EQ(queue.Size(), 2);
    EXPECT_FALSE(queue.Enqueue(3));  // Cannot enqueue after disable
    
    // Can still dequeue non-blocking
    int value;
    EXPECT_TRUE(queue.DequeueNonBlocking(value));
    EXPECT_EQ(value, 1);
}

TEST_F(ActiveQueueTest, State_ReenableAfterDisable) {
    ActiveQueue<int> queue;
    (void)queue.Enqueue(1);
    queue.Disable();
    EXPECT_FALSE(queue.Enabled());
    
    queue.Enable();
    EXPECT_TRUE(queue.Enabled());
    EXPECT_TRUE(queue.Enqueue(2));
    EXPECT_EQ(queue.Size(), 2);
}

TEST_F(ActiveQueueTest, State_CapacityTransition) {
    ActiveQueue<int> queue(5);
    for (int i = 0; i < 5; ++i) {
        (void)queue.Enqueue(i);
    }
    EXPECT_EQ(queue.Size(), 5);
    
    queue.SetCapacity(10);
    EXPECT_EQ(queue.Capacity(), 10);
    
    for (int i = 5; i < 10; ++i) {
        (void)queue.Enqueue(i);
    }
    EXPECT_EQ(queue.Size(), 10);
}

TEST_F(ActiveQueueTest, State_CapacityOne) {
    ActiveQueue<int> queue(1, false);
    EXPECT_TRUE(queue.Enqueue(1));
    EXPECT_FALSE(queue.Enqueue(2));  // Exceeds capacity 1
    EXPECT_EQ(queue.Size(), 1);
}

TEST_F(ActiveQueueTest, State_MetricsEnabled) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    
    for (int i = 0; i < 10; ++i) {
        (void)queue.Enqueue(i);
    }
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 10);
    EXPECT_EQ(metrics.max_size_observed.load(), 10);
}

TEST_F(ActiveQueueTest, State_MetricsDisabledByDefault) {
    ActiveQueue<int> queue;
    for (int i = 0; i < 10; ++i) {
        (void)queue.Enqueue(i);
    }
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 0);  // Not counted
}

TEST_F(ActiveQueueTest, State_TransitionToFullFromEmpty) {
    ActiveQueue<int> queue(3, false);
    EXPECT_TRUE(queue.Empty());
    
    (void)queue.Enqueue(1);
    EXPECT_FALSE(queue.Empty());
    EXPECT_EQ(queue.Size(), 1);
    
    (void)queue.Enqueue(2);
    (void)queue.Enqueue(3);
    EXPECT_EQ(queue.Size(), 3);
    EXPECT_FALSE(queue.Enqueue(4));  // Now full
}

TEST_F(ActiveQueueTest, State_BlockModeTransition) {
    ActiveQueue<int> queue;
    EXPECT_FALSE(queue.GetBlockOnFull());
    
    EXPECT_TRUE(queue.SetBlockOnFull(true));
    EXPECT_TRUE(queue.GetBlockOnFull());
    
    // Cannot change with data
    (void)queue.Enqueue(1);
    EXPECT_FALSE(queue.SetBlockOnFull(false));
    EXPECT_TRUE(queue.GetBlockOnFull());  // Unchanged
    
    queue.Clear();
    EXPECT_TRUE(queue.SetBlockOnFull(false));
    EXPECT_FALSE(queue.GetBlockOnFull());
}

TEST_F(ActiveQueueTest, State_MultipleDisableEnable) {
    ActiveQueue<int> queue;
    
    queue.Disable();
    EXPECT_FALSE(queue.Enabled());
    
    queue.Enable();
    EXPECT_TRUE(queue.Enabled());
    
    queue.Disable();
    EXPECT_FALSE(queue.Enabled());
    
    queue.Enable();
    EXPECT_TRUE(queue.Enabled());
    EXPECT_TRUE(queue.Enqueue(1));
}

TEST_F(ActiveQueueTest, State_MetricsToggle) {
    ActiveQueue<int> queue;
    
    (void)queue.Enqueue(1);
    auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), 0);  // Not enabled yet
    
    queue.EnableMetrics();
    (void)queue.Enqueue(2);
    EXPECT_EQ(metrics.enqueued_count.load(), 1);  // Now tracking
    
    queue.EnableMetrics(false);
    (void)queue.Enqueue(3);
    EXPECT_EQ(metrics.enqueued_count.load(), 1);  // Stopped tracking
}

// ============================================================================
// PHASE 2: Advanced Concurrent Patterns (14 tests)
// ============================================================================

TEST_F(ActiveQueueTest, Concurrent_MultipleProducersMultipleConsumers) {
    ActiveQueue<int> queue;
    std::vector<int> results;
    std::mutex results_mutex;
    const int total_items = 100;
    const int num_producers = 2;
    const int num_consumers = 3;
    std::atomic<int> produced{0};
    std::atomic<int> producers_done{0};
    
    // Producer threads
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&queue, &produced, &producers_done, p]() {
            for (int i = 0; i < total_items / num_producers; ++i) {
                (void)queue.Enqueue(p * 1000 + i);
                produced++;
            }
            // Signal when this producer is done
            if (++producers_done == num_producers) {
                // Last producer to finish - signal the queue
                queue.Disable();
            }
        });
    }
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&queue, &results, &results_mutex, &producers_done]() {
            int value;
            while (producers_done.load() < num_producers || !queue.Empty()) {
                if (queue.DequeueNonBlocking(value)) {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    results.push_back(value);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }
    
    EXPECT_EQ(produced.load(), total_items);
    EXPECT_EQ(results.size(), total_items);
}

TEST_F(ActiveQueueTest, Concurrent_HighThreadCount_Enqueue) {
    ActiveQueue<int> queue;
    const int num_threads = 20;
    const int items_per_thread = 10;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&queue, t]() {
            for (int i = 0; i < items_per_thread; ++i) {
                EXPECT_TRUE(queue.Enqueue(t * 100 + i));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(queue.Size(), num_threads * items_per_thread);
}

TEST_F(ActiveQueueTest, Concurrent_HighThreadCount_Dequeue) {
    ActiveQueue<int> queue;
    const int num_threads = 20;
    const int items_per_thread = 10;
    
    for (int i = 0; i < num_threads * items_per_thread; ++i) {
        (void)queue.Enqueue(i);
    }
    
    std::vector<int> results;
    std::mutex results_mutex;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&queue, &results, &results_mutex]() {
            int value;
            for (int i = 0; i < items_per_thread; ++i) {
                if (queue.DequeueNonBlocking(value)) {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    results.push_back(value);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(results.size(), num_threads * items_per_thread);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, Concurrent_RapidEnableDisableCycles) {
    ActiveQueue<int> queue;
    std::atomic<int> enqueued{0};
    
    std::thread disabler([&queue]() {
        for (int i = 0; i < 10; ++i) {
            queue.Disable();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            queue.Enable();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    
    std::thread enqueuer([&queue, &enqueued]() {
        for (int i = 0; i < 50; ++i) {
            if (queue.Enqueue(i)) {
                enqueued++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    
    disabler.join();
    enqueuer.join();
    
    // Some items should have been enqueued successfully
    EXPECT_GT(enqueued.load(), 0);
}

TEST_F(ActiveQueueTest, Concurrent_SustainedLoad_10kOperations) {
    ActiveQueue<int> queue;
    const int total = 10000;
    std::atomic<int> enqueued{0};
    std::atomic<int> dequeued{0};
    std::atomic<bool> producer_done{false};
    
    std::thread producer([&queue, &enqueued, &producer_done]() {
        for (int i = 0; i < total; ++i) {
            if (queue.Enqueue(i)) {
                enqueued++;
            }
        }
        producer_done = true;
    });
    
    std::thread consumer([&queue, &dequeued, &producer_done]() {
        int value;
        while (dequeued.load() < total) {
            if (queue.DequeueNonBlocking(value)) {
                dequeued++;
            } else if (!producer_done) {
                // Producer still running, wait a bit
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else if (!queue.Empty()) {
                // Producer done, but queue might have items - try once more
                continue;
            } else {
                // Producer done and queue empty, give it a moment to ensure
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(enqueued.load(), total);
    EXPECT_EQ(dequeued.load(), total);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, Concurrent_BoundedQueue_BlockingBackpressure) {
    ActiveQueue<int> queue(10, true);  // Bounded, blocking
    std::vector<int> enqueued;
    std::vector<int> dequeued;
    std::mutex enqueue_mutex, dequeue_mutex;
    
    std::thread producer([&queue, &enqueued, &enqueue_mutex]() {
        for (int i = 0; i < 50; ++i) {
            if (queue.Enqueue(i)) {
                std::lock_guard<std::mutex> lock(enqueue_mutex);
                enqueued.push_back(i);
            }
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::thread consumer([&queue, &dequeued, &dequeue_mutex]() {
        int value;
        int count = 0;
        while (count < 50) {
            if (queue.DequeueNonBlocking(value)) {
                std::lock_guard<std::mutex> lock(dequeue_mutex);
                dequeued.push_back(value);
                count++;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(enqueued.size(), 50);
    EXPECT_EQ(dequeued.size(), 50);
}

TEST_F(ActiveQueueTest, Concurrent_MetricsAccuracy_HighFrequency) {
    ActiveQueue<int> queue;
    queue.EnableMetrics();
    const int total = 1000;
    std::atomic<bool> producer_done{false};
    
    std::thread producer([&queue, &producer_done]() {
        for (int i = 0; i < total; ++i) {
            (void)queue.Enqueue(i);
        }
        producer_done = true;
    });
    
    std::thread consumer([&queue, &producer_done]() {
        int value;
        while (true) {
            if (queue.DequeueNonBlocking(value)) {
                // Successfully dequeued
                continue;
            } else if (producer_done && queue.Empty()) {
                // Producer is done and queue is empty, exit
                break;
            } else {
                // Keep trying
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), total);
}

TEST_F(ActiveQueueTest, Concurrent_VariableProducerRate) {
    ActiveQueue<int> queue;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    std::thread fast_producer([&queue, &produced]() {
        for (int i = 0; i < 100; ++i) {
            (void)queue.Enqueue(i);
            produced++;
        }
    });
    
    std::thread slow_producer([&queue, &produced]() {
        for (int i = 0; i < 100; ++i) {
            (void)queue.Enqueue(1000 + i);
            produced++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::thread consumer([&queue, &consumed, &produced]() {
        int value;
        while (consumed.load() < 200) {
            if (queue.DequeueNonBlocking(value)) {
                consumed++;
            } else if (produced.load() < 200) {
                // Producers still going
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // All producers done, try once more
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    
    fast_producer.join();
    slow_producer.join();
    consumer.join();
    
    EXPECT_EQ(produced.load(), 200);
    EXPECT_EQ(consumed.load(), 200);
}

TEST_F(ActiveQueueTest, Concurrent_Fairness_AllThreadsProgress) {
    ActiveQueue<int> queue(50);  // Moderate capacity
    std::vector<int> thread_items[4];
    std::mutex mutexes[4];
    const int items_per_thread = 50;
    const int num_threads = 4;
    const int total_items = num_threads * items_per_thread;
    std::atomic<int> total_dequeued{0};
    std::atomic<int> enqueuers_done{0};
    
    // 4 threads enqueuing
    std::vector<std::thread> enqueuers;
    for (int t = 0; t < num_threads; ++t) {
        enqueuers.emplace_back([&queue, t, &enqueuers_done]() {
            for (int i = 0; i < items_per_thread; ++i) {
                (void)queue.Enqueue(t * 1000 + i);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            // Unblock if all enqueuers done
            enqueuers_done++;
        });
    }
    
    // 4 threads dequeueing
    std::vector<std::thread> dequeuers;
    for (int t = 0; t < num_threads; ++t) {
        dequeuers.emplace_back([&queue, t, &thread_items, &mutexes, &total_dequeued, &enqueuers_done]() {
            int value;
            while (total_dequeued.load() < total_items) {
                if (queue.DequeueNonBlocking(value)) {
                    std::lock_guard<std::mutex> lock(mutexes[t]);
                    thread_items[t].push_back(value);
                    total_dequeued++;
                } else if (enqueuers_done.load() < num_threads) {
                    // Enqueuers still going
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    // All enqueuers done, no more items coming
                    break;
                }
            }
        });
    }
    
    for (auto& t : enqueuers) {
        t.join();
    }
    for (auto& t : dequeuers) {
        t.join();
    }
    
    int total = 0;
    for (int t = 0; t < num_threads; ++t) {
        total += thread_items[t].size();
        EXPECT_GT(thread_items[t].size(), 0);  // All threads got some work
    }
    EXPECT_EQ(total, total_items);
}

// ============================================================================
// PHASE 2: Message Concurrent Advanced Tests (8 tests)
// ============================================================================

TEST_F(ActiveQueueTest, MessageConcurrent_BlockingDequeueMessages_MultiThread) {
    ActiveQueue<Message> queue;
    std::vector<int> results;
    std::mutex results_mutex;
    const int total = 30;
    
    std::thread producer([&queue]() {
        for (int i = 0; i < total; ++i) {
            (void)queue.Enqueue(Message(i * 10));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    
    std::vector<std::thread> consumers;
    for (int c = 0; c < 3; ++c) {
        consumers.emplace_back([&queue, &results, &results_mutex]() {
            Message msg;
            while (queue.Dequeue(msg)) {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(msg.get<int>());
            }
        });
    }
    
    producer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.Disable();
    
    for (auto& c : consumers) {
        c.join();
    }
    
    EXPECT_EQ(results.size(), total);
}

TEST_F(ActiveQueueTest, MessageConcurrent_HighFrequencyEnqueueDequeue) {
    ActiveQueue<Message> queue;
    queue.EnableMetrics();
    const int total = 500;
    std::atomic<int> operations{0};
    
    std::thread producer([&queue, &operations]() {
        for (int i = 0; i < total; ++i) {
            (void)queue.Enqueue(Message(i));
            operations++;
        }
    });
    
    std::thread consumer([&queue, &operations]() {
        Message msg;
        int count = 0;
        while (count < total) {
            if (queue.DequeueNonBlocking(msg)) {
                count++;
                operations++;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), total);
    EXPECT_EQ(metrics.dequeued_count.load(), total);
}

TEST_F(ActiveQueueTest, MessageConcurrent_TypeSafetyUnderConcurrency) {
    ActiveQueue<Message> queue;
    std::vector<int> int_results;
    std::vector<double> double_results;
    std::mutex results_mutex;
    
    std::thread int_producer([&queue]() {
        for (int i = 0; i < 50; ++i) {
            (void)queue.Enqueue(Message(i));
        }
    });
    
    std::thread double_producer([&queue]() {
        for (int i = 0; i < 50; ++i) {
            (void)queue.Enqueue(Message(static_cast<double>(i) * 1.5));
        }
    });
    
    Message msg;
    int dequeued = 0;
    while (dequeued < 100) {
        if (queue.DequeueNonBlocking(msg)) {
            // Try to get as int
            auto int_ptr = msg.try_get<int>();
            if (int_ptr) {
                std::lock_guard<std::mutex> lock(results_mutex);
                int_results.push_back(*int_ptr);
            } else {
                // Must be double
                auto double_ptr = msg.try_get<double>();
                if (double_ptr) {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    double_results.push_back(*double_ptr);
                }
            }
            dequeued++;
        } else {
            // Producers still working, wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    int_producer.join();
    double_producer.join();
    
    EXPECT_EQ(int_results.size() + double_results.size(), 100);
    EXPECT_EQ(int_results.size(), 50);
    EXPECT_EQ(double_results.size(), 50);
}

TEST_F(ActiveQueueTest, MessageConcurrent_MetricsWithAllocationTracking) {
    ActiveQueue<Message> queue;
    queue.EnableMetrics();
    const int small = 20;   // SSO messages
    const int large = 20;   // Heap messages
    
    std::thread producer([&queue]() {
        // Enqueue small messages (SSO)
        for (int i = 0; i < small; ++i) {
            (void)queue.Enqueue(Message(i));
        }
        
        // Enqueue large messages (heap)
        for (int i = 0; i < large; ++i) {
            (void)queue.Enqueue(Message(std::string(1000, 'a')));
        }
    });
    
    Message msg;
    int dequeued = 0;
    while (dequeued < small + large) {
        if (queue.DequeueNonBlocking(msg)) {
            dequeued++;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    producer.join();
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_EQ(metrics.enqueued_count.load(), small + large);
    EXPECT_EQ(metrics.dequeued_count.load(), small + large);
}

TEST_F(ActiveQueueTest, MessageConcurrent_MixedOperationsWithMetrics) {
    ActiveQueue<Message> queue;
    queue.EnableMetrics();
    std::atomic<bool> running{true};
    
    std::thread producer([&queue, &running]() {
        int i = 0;
        while (running) {
            (void)queue.Enqueue(Message(i++));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::thread consumer([&queue, &running]() {
        Message msg;
        while (running) {
            (void)queue.DequeueNonBlocking(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    
    producer.join();
    consumer.join();
    
    const auto& metrics = queue.GetMetrics();
    EXPECT_GT(metrics.enqueued_count.load(), 50);
    EXPECT_GT(metrics.dequeued_count.load(), 0);
    EXPECT_LE(metrics.dequeued_count.load(), metrics.enqueued_count.load());
}

TEST_F(ActiveQueueTest, MessageConcurrent_BoundedBlockingWithMessages) {
    ActiveQueue<Message> queue(20, true);  // Bounded, blocking
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    std::thread producer([&queue, &produced]() {
        for (int i = 0; i < 100; ++i) {
            (void)queue.Enqueue(Message(i));
            produced++;
        }
    });
    
    std::thread consumer([&queue, &consumed]() {
        Message msg;
        while (consumed.load() < 100) {
            if (queue.DequeueNonBlocking(msg)) {
                consumed++;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(produced.load(), 100);
    EXPECT_EQ(consumed.load(), 100);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(ActiveQueueTest, MessageConcurrent_ComparatorWithMessages) {
    ActiveQueue<Message> queue;
    queue.SetComparator([](const Message& a, const Message& b) {
        return a.get<int>() < b.get<int>();
    });
    
    std::thread producer([&queue]() {
        for (int i : {50, 20, 80, 10, 60, 30}) {
            (void)queue.Enqueue(Message(i));
        }
    });
    
    std::vector<int> results;
    Message msg;
    
    producer.join();
    
    while (queue.DequeueNonBlocking(msg)) {
        results.push_back(msg.get<int>());
    }
    
    std::vector<int> expected = {10, 20, 30, 50, 60, 80};
    EXPECT_EQ(results, expected);
}
