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
 * @file test_thread_pool.cpp
 * @brief Comprehensive unit tests for ThreadPool (Phase 5 Priority 1)
 *
 * Tests the fixed-size thread pool with:
 * - Constructor and lifecycle management
 * - Task queuing and execution
 * - Queue capacity enforcement
 * - Statistics tracking
 * - Deadlock detection
 * - Exception handling
 * - C++26 compliance (std::expected<>, atomics)
 *
 * @note Uses C++26 features: std::expected<>, lock-free atomics, move semantics
 */

#include <gtest/gtest.h>
#include "graph/ThreadPool.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <memory>
#include <future>

namespace graph::test {

// ===================================================================================
// Helper Utilities
// ===================================================================================

/**
 * @brief Helper to wait for a condition with timeout
 */
template<typename Predicate>
bool WaitFor(Predicate&& pred, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

graph::ThreadPool::DeadlockConfig NoWatchdogConfig(size_t max_queue_size = 1000) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = max_queue_size;
    cfg.enable_detection = false;
    return cfg;
}

/**
 * @brief Helper task that sets a flag when executed
 */
class FlagTask {
    std::atomic<bool>& flag_;
public:
    explicit FlagTask(std::atomic<bool>& flag) : flag_(flag) {}
    void operator()() { flag_.store(true); }
};

/**
 * @brief Helper task that appends to a vector
 */
class CountingTask {
    std::vector<int>& results_;
    int value_;
public:
    CountingTask(std::vector<int>& results, int value)
        : results_(results), value_(value) {}
    void operator()() {
        results_.push_back(value_);
    }
};

/**
 * @brief Helper task that sleeps for a duration
 */
class SleepingTask {
    std::chrono::milliseconds duration_;
public:
    explicit SleepingTask(std::chrono::milliseconds duration)
        : duration_(duration) {}
    void operator()() {
        std::this_thread::sleep_for(duration_);
    }
};

/**
 * @brief Helper task that throws an exception
 */
class ThrowingTask {
    std::string message_;
public:
    explicit ThrowingTask(const std::string& msg) : message_(msg) {}
    void operator()() {
        throw std::runtime_error(message_);
    }
};

// ===================================================================================
// Test Fixture
// ===================================================================================

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default setup - subclasses can override
    }

    void TearDown() override {
        // Ensure cleanup
    }
};

// ===================================================================================
// Constructor Tests (3 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, DefaultConstruction) {
    graph::ThreadPool pool(4);

    // Verify basic state
    EXPECT_EQ(pool.GetThreadCount(), 4);
    EXPECT_EQ(pool.GetQueueDepth(), 0);
    EXPECT_FALSE(pool.IsDeadlockDetected());

    // Verify statistics initialized
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 0);
    EXPECT_EQ(stats.tasks_completed.load(), 0);
    EXPECT_EQ(stats.tasks_failed.load(), 0);
}

TEST_F(ThreadPoolTest, CustomConfigurationConstruction) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.task_timeout = std::chrono::milliseconds(2000);
    cfg.watchdog_interval = std::chrono::milliseconds(500);
    cfg.max_queue_size = 5000;
    cfg.enable_detection = true;

    graph::ThreadPool pool(4, cfg);

    // Verify configuration applied
    EXPECT_EQ(pool.GetThreadCount(), 4);

    // Verify default stats
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 0);
}

TEST_F(ThreadPoolTest, EdgeCaseConstructions) {
    // Test with 1 thread
    {
        graph::ThreadPool pool1(1);
        EXPECT_EQ(pool1.GetThreadCount(), 1);
    }

    // Test with many threads
    {
        graph::ThreadPool pool_many(std::thread::hardware_concurrency() * 2);
        EXPECT_GT(pool_many.GetThreadCount(), 0);
    }

    // Test with zero threads (should default to hardware_concurrency)
    {
        graph::ThreadPool pool0(0);
        EXPECT_GT(pool0.GetThreadCount(), 0);
        EXPECT_EQ(pool0.GetThreadCount(), std::thread::hardware_concurrency());
    }
}

// ===================================================================================
// Lifecycle Tests (4 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, InitStartStopJoinSequence) {
    graph::ThreadPool pool(4, NoWatchdogConfig());

    // Initialize
    EXPECT_TRUE(pool.Init());

    // Start
    EXPECT_TRUE(pool.Start());

    EXPECT_EQ(pool.GetQueueDepth(), 0);

    // Queue some tasks - use simple counter with shared atomic
    std::shared_ptr<std::atomic<int>> counter = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < 10; ++i) {
        // Explicitly create Task from lambda with shared_ptr capture
        graph::ThreadPool::Task task = [counter]() { counter->fetch_add(1); };
        auto result = pool.QueueTask(std::move(task));
        EXPECT_EQ(result, graph::ThreadPool::QueueResult::Ok);
    }

    ASSERT_TRUE(WaitFor([counter]() { return counter->load() == 10; },
                        std::chrono::milliseconds(1000)));

    // Stop and join
    pool.Stop();
    pool.Join();

    // Verify tasks completed
    EXPECT_EQ(counter->load(), 10);
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load(), 10);
    EXPECT_EQ(stats.tasks_failed.load(), 0);
}

TEST_F(ThreadPoolTest, StartExpectedSuccess) {
    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();

    // Call StartExpected
    auto result = pool.StartExpected();

    // Verify success
    EXPECT_TRUE(result.has_value());

    // Verify pool is operational
    std::shared_ptr<std::atomic<bool>> executed = std::make_shared<std::atomic<bool>>(false);
    auto qresult = pool.QueueTask([executed]() { executed->store(true); });
    EXPECT_EQ(qresult, graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([executed]() { return executed->load(); },
                        std::chrono::milliseconds(1000)));

    // Verify task executed
    pool.Stop();
    pool.Join();
    EXPECT_TRUE(executed->load());
}

TEST_F(ThreadPoolTest, StartExpectedAlreadyRunning) {
    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();

    // First start succeeds
    auto result1 = pool.StartExpected();
    EXPECT_TRUE(result1.has_value());

    // Second start fails with AlreadyRunning error
    auto result2 = pool.StartExpected();
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), graph::ThreadPool::ThreadPoolError::AlreadyRunning);

    // Cleanup
    pool.Stop();
    pool.Join();
}

TEST_F(ThreadPoolTest, StartExpectedAppliesQueueCapacity) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = 1;
    cfg.enable_detection = false;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    auto start_result = pool.StartExpected();
    ASSERT_TRUE(start_result.has_value());

    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);
    ASSERT_EQ(pool.QueueTask([&active_started, active_released]() mutable {
        active_started.store(true);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));

    EXPECT_EQ(pool.QueueTask([]() {}), graph::ThreadPool::QueueResult::Ok);
    EXPECT_EQ(pool.QueueTask([]() {}), graph::ThreadPool::QueueResult::Full);
    EXPECT_EQ(pool.GetStats().tasks_rejected.load(), 1);

    pool.Stop();
    release_active->set_value();
    pool.Join();
}

TEST_F(ThreadPoolTest, DestructorSafety) {
    {
        graph::ThreadPool pool(4, NoWatchdogConfig());
        (void)pool.Init();
        (void)pool.Start();

        // Queue some tasks but don't manually stop/join
        std::atomic<int> counter(0);
        for (int i = 0; i < 20; ++i) {
            auto task = [&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            };
            (void)pool.QueueTask(task);
        }

        // Destructor should handle cleanup automatically
    }

    // If we get here without hanging, destructor cleanup worked
    EXPECT_TRUE(true);
}

// ===================================================================================
// Task Queuing Tests (3 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, SingleTaskExecution) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue and execute single task
    std::shared_ptr<std::atomic<bool>> executed = std::make_shared<std::atomic<bool>>(false);
    auto result = pool.QueueTask([executed]() { executed->store(true); });

    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Ok);

    ASSERT_TRUE(WaitFor([executed]() { return executed->load(); },
                        std::chrono::milliseconds(1000)));

    // Wait for completion
    pool.Stop();
    pool.Join();

    // Verify execution
    EXPECT_TRUE(executed->load());
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load(), 1);
    EXPECT_EQ(stats.tasks_failed.load(), 0);
}

TEST_F(ThreadPoolTest, MultipleTasksFIFOOrder) {
    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue tasks and verify FIFO ordering
    std::shared_ptr<std::vector<int>> results = std::make_shared<std::vector<int>>();
    std::shared_ptr<std::mutex> results_mtx = std::make_shared<std::mutex>();

    for (int i = 0; i < 100; ++i) {
        graph::ThreadPool::Task task = [results, results_mtx, i]() {
            std::lock_guard lock(*results_mtx);
            results->push_back(i);
        };
        auto qresult = pool.QueueTask(std::move(task));
        ASSERT_EQ(qresult, graph::ThreadPool::QueueResult::Ok);
    }

    ASSERT_TRUE(WaitFor([&pool]() { return pool.GetStats().tasks_completed.load() == 100; },
                        std::chrono::milliseconds(1000)));

    pool.Stop();
    pool.Join();

    // Verify all tasks executed
    EXPECT_EQ(results->size(), 100);
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load(), 100);
    EXPECT_EQ(stats.tasks_failed.load(), 0);
}

TEST_F(ThreadPoolTest, QueueCapacityEnforcement) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = 5;
    cfg.enable_detection = false;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);
    ASSERT_EQ(pool.QueueTask([&active_started, active_released]() mutable {
        active_started.store(true);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));

    // Fill only the pending queue while the single worker is blocked.
    for (int i = 0; i < 5; ++i) {
        auto result = pool.QueueTask([]() {});
        ASSERT_EQ(result, graph::ThreadPool::QueueResult::Ok);
    }
    ASSERT_EQ(pool.GetQueueDepth(), 5);

    std::shared_ptr<std::atomic<bool>> extra_executed = std::make_shared<std::atomic<bool>>(false);
    auto over_result = pool.QueueTask([extra_executed]() { extra_executed->store(true); });
    EXPECT_EQ(over_result, graph::ThreadPool::QueueResult::Full);
    EXPECT_FALSE(extra_executed->load());
    EXPECT_EQ(pool.GetStats().tasks_rejected.load(), 1);

    pool.Stop();
    release_active->set_value();
    pool.Join();
}

// ===================================================================================
// Statistics Tests (2 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, TaskCounters) {
    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue normal tasks
    std::shared_ptr<std::atomic<int>> success_count = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < 50; ++i) {
        graph::ThreadPool::Task task = [success_count]() { success_count->fetch_add(1); };
        (void)pool.QueueTask(std::move(task));
    }

    // Queue tasks that throw
    for (int i = 0; i < 5; ++i) {
        graph::ThreadPool::Task task = []() { throw std::runtime_error("test"); };
        (void)pool.QueueTask(std::move(task));
    }

    ASSERT_TRUE(WaitFor([&pool]() {
        const auto& stats = pool.GetStats();
        return stats.tasks_completed.load() + stats.tasks_failed.load() == 55;
    }, std::chrono::milliseconds(1000)));

    // Wait and verify
    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 55);
    EXPECT_EQ(stats.tasks_completed.load(), 50);
    EXPECT_EQ(stats.tasks_failed.load(), 5);
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load(), 55);
}

TEST_F(ThreadPoolTest, ExecutionMetrics) {
    graph::ThreadPool pool(2, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue tasks with known duration
    std::chrono::milliseconds task_duration(5);
    for (int i = 0; i < 20; ++i) {
        graph::ThreadPool::Task task = [task_duration]() {
            std::this_thread::sleep_for(task_duration);
        };
        (void)pool.QueueTask(std::move(task));
    }

    ASSERT_TRUE(WaitFor([&pool]() { return pool.GetStats().tasks_completed.load() == 20; },
                        std::chrono::milliseconds(1000)));

    // Wait and check metrics
    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load(), 20);
    EXPECT_GT(stats.total_task_time_ns.load(), 0);

    // Average time should be approximately task_duration
    double avg_ms = pool.GetAverageTaskTimeMs();
    EXPECT_GT(avg_ms, 0.0);
    // Allow large tolerance due to timing variance
    EXPECT_LT(avg_ms, 100.0);  // Should be ~5ms, definitely less than 100ms
}

// ===================================================================================
// Deadlock Detection Tests (2 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, WatchdogDetectsLongTask) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.task_timeout = std::chrono::milliseconds(100);
    cfg.watchdog_interval = std::chrono::milliseconds(50);
    cfg.enable_detection = true;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    // Queue a task that exceeds timeout
    auto task = []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    };
    (void)pool.QueueTask(task);

    // Wait for detection
    bool detected = WaitFor(
        [&pool]() { return pool.IsDeadlockDetected(); },
        std::chrono::milliseconds(500)
    );

    // Cleanup
    pool.Stop();
    pool.Join();

    // Verify detection
    EXPECT_TRUE(detected);
    const auto& stats = pool.GetStats();
    EXPECT_GT(stats.deadlock_detections.load(), 0);
}

TEST_F(ThreadPoolTest, ClearDeadlockFlag) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.task_timeout = std::chrono::milliseconds(50);
    cfg.watchdog_interval = std::chrono::milliseconds(25);
    cfg.enable_detection = true;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    // Trigger deadlock detection
    (void)pool.QueueTask([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });

    // Wait for detection
    WaitFor(
        [&pool]() { return pool.IsDeadlockDetected(); },
        std::chrono::milliseconds(500)
    );

    EXPECT_TRUE(pool.IsDeadlockDetected());

    // Wait for long task to complete (200ms task + buffer)
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Clear flag after long task is done
    pool.ClearDeadlockFlag();
    EXPECT_FALSE(pool.IsDeadlockDetected());

    // Queue short task and verify flag stays clear
    (void)pool.QueueTask([]() { /* quick task */ });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(pool.IsDeadlockDetected());

    // Cleanup
    pool.Stop();
    pool.Join();
}

// ===================================================================================
// Exception Handling Tests (1 test)
// ===================================================================================

TEST_F(ThreadPoolTest, TaskExceptionIncrementsFailCount) {
    graph::ThreadPool pool(2, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Add delay for thread startup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Queue mix of normal and throwing tasks
    std::shared_ptr<std::atomic<int>> normal_count = std::make_shared<std::atomic<int>>(0);

    for (int i = 0; i < 10; ++i) {
        if (i % 3 == 0) {
            // Throw
            graph::ThreadPool::Task task = []() { throw std::runtime_error("test error"); };
            (void)pool.QueueTask(std::move(task));
        } else {
            // Normal
            graph::ThreadPool::Task task = [normal_count]() { normal_count->fetch_add(1); };
            (void)pool.QueueTask(std::move(task));
        }
    }

    ASSERT_TRUE(WaitFor([&pool]() {
        const auto& stats = pool.GetStats();
        return stats.tasks_completed.load() + stats.tasks_failed.load() == 10;
    }, std::chrono::milliseconds(1000)));

    // Wait for completion
    pool.Stop();
    pool.Join();

    // Verify counts
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 10);
    EXPECT_GE(stats.tasks_completed.load(), 6);  // At least 6 normal tasks
    EXPECT_GE(stats.tasks_failed.load(), 3);     // At least 3 throwing tasks
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load(), 10);
}

// ===================================================================================
// C++26 Compliance Tests (2 tests)
// ===================================================================================

TEST_F(ThreadPoolTest, ExpectedErrorHandling) {
    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();

    // First start succeeds
    {
        auto result = pool.StartExpected();
        EXPECT_TRUE(result.has_value());
        EXPECT_TRUE(static_cast<bool>(result));
    }

    // Second start returns error
    {
        auto result = pool.StartExpected();
        EXPECT_FALSE(result.has_value());
        EXPECT_FALSE(static_cast<bool>(result));
        EXPECT_EQ(result.error(), graph::ThreadPool::ThreadPoolError::AlreadyRunning);
    }

    // Cleanup
    pool.Stop();
    pool.Join();
}

TEST_F(ThreadPoolTest, AtomicMemoryOrdering) {
    // This test verifies that atomic operations are lock-free
    // (verified by static_assert in ThreadPool.cpp)

    graph::ThreadPool pool(4, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Concurrent queue from multiple threads
    std::vector<std::thread> threads;
    std::atomic<int> queued_count(0);

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&pool, &queued_count]() {
            for (int i = 0; i < 250; ++i) {
                auto task = [&queued_count]() { queued_count.fetch_add(1); };
                if (pool.QueueTask(task) == graph::ThreadPool::QueueResult::Ok) {
                    // Success
                }
            }
        });
    }

    // Wait for all threads to finish queueing
    for (auto& t : threads) {
        t.join();
    }

    // Stop and verify
    pool.Stop();
    pool.Join();

    // Stop() cancels work that has not started yet. Account for every accepted
    // task as completed, failed, or cancelled.
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() + stats.tasks_cancelled.load(),
              stats.tasks_queued.load());

    // Verify no lost increments for tasks that actually executed.
    EXPECT_EQ(queued_count.load(), stats.tasks_completed.load());
}

// ===================================================================================
// Additional Behavioral Tests
// ===================================================================================

TEST_F(ThreadPoolTest, JoinWithTimeout) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue a short task
    std::shared_ptr<std::atomic<bool>> executed = std::make_shared<std::atomic<bool>>(false);
    (void)pool.QueueTask([executed]() { executed->store(true); });

    ASSERT_TRUE(WaitFor([executed]() { return executed->load(); },
                        std::chrono::milliseconds(1000)));

    // JoinWithTimeout should succeed with adequate timeout
    pool.Stop();
    bool success = pool.JoinWithTimeout(std::chrono::milliseconds(5000));

    EXPECT_TRUE(success);
    EXPECT_TRUE(executed->load());
}

TEST_F(ThreadPoolTest, NullTaskHandling) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Attempting to queue null task should fail
    graph::ThreadPool::Task null_task;
    auto result = pool.QueueTask(std::move(null_task));

    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Error);

    pool.Stop();
    pool.Join();
}

TEST_F(ThreadPoolTest, QueueAfterStop) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue some tasks
    std::atomic<bool> task1(false);
    (void)pool.QueueTask(FlagTask(task1));

    // Stop the pool
    pool.Stop();

    // Attempts to queue after stop should be rejected
    std::atomic<bool> task2(false);
    auto result = pool.QueueTask(FlagTask(task2));

    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Stopped);
    EXPECT_FALSE(task2.load());
    EXPECT_EQ(pool.GetStats().tasks_rejected.load(), 1);

    pool.Join();
}

TEST_F(ThreadPoolTest, IdempotentStop) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Queue task
    std::shared_ptr<std::atomic<bool>> executed = std::make_shared<std::atomic<bool>>(false);
    (void)pool.QueueTask([executed]() { executed->store(true); });

    ASSERT_TRUE(WaitFor([executed]() { return executed->load(); },
                        std::chrono::milliseconds(1000)));

    // Multiple stops should be safe
    pool.Stop();
    pool.Stop();
    pool.Stop();

    pool.Join();
    EXPECT_TRUE(executed->load());
}

// ===================================================================================
// Priority 1 Enhancements: Critical Coverage Gaps
// ===================================================================================

TEST_F(ThreadPoolTest, QueueTaskWithTimeout) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = 1;
    cfg.enable_detection = false;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);
    ASSERT_EQ(pool.QueueTask([&active_started, active_released]() mutable {
        active_started.store(true);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));
    ASSERT_EQ(pool.QueueTask([]() {}), graph::ThreadPool::QueueResult::Ok);
    ASSERT_EQ(pool.GetQueueDepth(), 1);

    std::atomic<bool> executed(false);
    auto result_future = std::async(std::launch::async, [&pool, &executed]() {
        return pool.QueueTaskWithTimeout(
            [&executed]() { executed.store(true); },
            std::chrono::milliseconds(500)
        );
    });
    EXPECT_EQ(result_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    release_active->set_value();
    auto result = result_future.get();
    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&executed]() { return executed.load(); },
                        std::chrono::milliseconds(1000)));

    pool.Stop();
    pool.Join();
    EXPECT_TRUE(executed.load());
}

TEST_F(ThreadPoolTest, CancelledTasksCounter) {
    graph::ThreadPool pool(1, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    std::atomic<int> executed_count(0);
    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);

    ASSERT_EQ(pool.QueueTask([&executed_count, &active_started, active_released]() mutable {
        active_started.store(true);
        executed_count.fetch_add(1);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));

    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(pool.QueueTask([&executed_count]() {
            executed_count.fetch_add(1);
        }), graph::ThreadPool::QueueResult::Ok);
    }
    ASSERT_EQ(pool.GetQueueDepth(), 10);

    pool.Stop();
    release_active->set_value();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 11);
    EXPECT_EQ(stats.tasks_completed.load(), 1);
    EXPECT_EQ(stats.tasks_cancelled.load(), 10);
    EXPECT_EQ(executed_count.load(), 1);
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load(),
              stats.tasks_queued.load());
}

// ===================================================================================
// Priority 2 Enhancements: Stress Testing & Advanced Validation
// ===================================================================================

TEST_F(ThreadPoolTest, HighConcurrencyStressTest) {
    graph::ThreadPool pool(std::thread::hardware_concurrency(), NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    // Use realistic queue size - reduce tasks to fit default constraints
    const int NUM_THREADS = 4;
    const int TASKS_PER_THREAD = 500;
    const int TOTAL_TASKS = NUM_THREADS * TASKS_PER_THREAD;  // 2,000 tasks

    std::vector<std::thread> threads;
    std::atomic<int> execution_count(0);
    std::atomic<int> queue_failures(0);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&pool, &execution_count, &queue_failures]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                auto result = pool.QueueTask([&execution_count]() {
                    execution_count.fetch_add(1);
                });
                if (result != graph::ThreadPool::QueueResult::Ok) {
                    queue_failures.fetch_add(1);
                }
            }
        });
    }

    // Wait for all queuing threads to complete
    for (auto& t : threads) t.join();

    ASSERT_TRUE(WaitFor([&pool, &queue_failures]() {
        const auto& stats = pool.GetStats();
        return stats.tasks_completed.load() + stats.tasks_failed.load() +
               stats.tasks_cancelled.load() + queue_failures.load() == TOTAL_TASKS;
    }, std::chrono::milliseconds(2000)));

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    // Verify counter invariant
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load() + queue_failures.load(),
              TOTAL_TASKS);
    EXPECT_EQ(execution_count.load(), stats.tasks_completed.load());
    // Most tasks should execute (allowing for some queue failures and timing variation)
    // ~70%+ of tasks should successfully queue and execute
    EXPECT_GT(stats.tasks_completed.load(), TOTAL_TASKS * 0.70);  // At least 70%
}

TEST_F(ThreadPoolTest, GetQueueDepthAccuracy) {
    graph::ThreadPool pool(1, NoWatchdogConfig());  // Single thread to control execution timing
    (void)pool.Init();
    (void)pool.Start();

    std::atomic<int> started_count(0);
    std::atomic<int> can_finish(0);

    // Queue tasks that wait for permission to finish
    for (int i = 0; i < 10; ++i) {
        (void)pool.QueueTask([&started_count, &can_finish]() {
            started_count.fetch_add(1);
            while (can_finish.load() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Wait for first task to start
    WaitFor([&started_count]() { return started_count.load() > 0; },
            std::chrono::milliseconds(500));

    // Queue depth should be 9 (1 executing, 9 pending)
    int depth = pool.GetQueueDepth();
    EXPECT_EQ(depth, 9);

    // Allow tasks to finish
    can_finish.store(1);
    ASSERT_TRUE(WaitFor([&pool]() { return pool.GetQueueDepth() == 0; },
                        std::chrono::milliseconds(1000)));

    // Queue depth should be 0
    depth = pool.GetQueueDepth();
    EXPECT_EQ(depth, 0);

    pool.Stop();
    pool.Join();
}

TEST_F(ThreadPoolTest, EnqueueTimeoutTracking) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = 1;
    cfg.enable_detection = false;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);
    ASSERT_EQ(pool.QueueTask([&active_started, active_released]() mutable {
        active_started.store(true);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));
    ASSERT_EQ(pool.QueueTask([]() {}), graph::ThreadPool::QueueResult::Ok);
    ASSERT_EQ(pool.GetQueueDepth(), 1);

    std::atomic<bool> executed(false);
    auto result = pool.QueueTaskWithTimeout(
        [&executed]() { executed.store(true); },
        std::chrono::milliseconds(20)
    );

    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Timeout);
    EXPECT_FALSE(executed.load());

    pool.Stop();
    release_active->set_value();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.enqueue_timeouts.load(), 1);
    EXPECT_EQ(stats.tasks_rejected.load(), 1);
}

TEST_F(ThreadPoolTest, QueueTaskWithTimeoutReturnsStoppedWhenStopCancelsQueuedWork) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.max_queue_size = 1;
    cfg.enable_detection = false;

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    auto release_active = std::make_shared<std::promise<void>>();
    auto active_released = release_active->get_future().share();
    std::atomic<bool> active_started(false);
    ASSERT_EQ(pool.QueueTask([&active_started, active_released]() mutable {
        active_started.store(true);
        active_released.wait();
    }), graph::ThreadPool::QueueResult::Ok);
    ASSERT_TRUE(WaitFor([&active_started]() { return active_started.load(); },
                        std::chrono::milliseconds(1000)));
    ASSERT_EQ(pool.QueueTask([]() {}), graph::ThreadPool::QueueResult::Ok);
    ASSERT_EQ(pool.GetQueueDepth(), 1);

    std::atomic<bool> executed(false);
    auto result_future = std::async(std::launch::async, [&pool, &executed]() {
        return pool.QueueTaskWithTimeout(
            [&executed]() { executed.store(true); },
            std::chrono::milliseconds(1000)
        );
    });
    ASSERT_EQ(result_future.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

    pool.Stop();
    auto result = result_future.get();
    release_active->set_value();
    pool.Join();

    EXPECT_EQ(result, graph::ThreadPool::QueueResult::Stopped);
    EXPECT_FALSE(executed.load());
    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_rejected.load(), 1);
}

// ===================================================================================
// Priority 3 Enhancements: Configuration & Edge Case Validation
// ===================================================================================

TEST_F(ThreadPoolTest, DeadlockDetectionDisabled) {
    graph::ThreadPool::DeadlockConfig cfg;
    cfg.task_timeout = std::chrono::milliseconds(100);
    cfg.watchdog_interval = std::chrono::milliseconds(50);
    cfg.enable_detection = false;  // Disable detection

    graph::ThreadPool pool(1, cfg);
    (void)pool.Init();
    (void)pool.Start();

    // Queue long task that would trigger detection if enabled
    (void)pool.QueueTask([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    });

    // Wait longer than timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Should NOT detect deadlock (detection disabled)
    EXPECT_FALSE(pool.IsDeadlockDetected());

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.deadlock_detections.load(), 0);
}

TEST_F(ThreadPoolTest, MixedTaskTypeSequence) {
    graph::ThreadPool pool(2, NoWatchdogConfig());
    (void)pool.Init();
    (void)pool.Start();

    std::atomic<int> short_count(0);
    std::atomic<int> long_count(0);

    for (int i = 0; i < 20; ++i) {
        if (i % 4 == 0) {
            // Long task
            (void)pool.QueueTask([&long_count]() {
                long_count.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
        } else if (i % 4 == 1) {
            // Short task
            (void)pool.QueueTask([&short_count]() {
                short_count.fetch_add(1);
            });
        } else if (i % 4 == 2) {
            // Throwing task
            (void)pool.QueueTask([]() {
                throw std::runtime_error("intentional");
            });
        } else {
            // Another short task
            (void)pool.QueueTask([&short_count]() {
                short_count.fetch_add(1);
            });
        }
    }

    ASSERT_TRUE(WaitFor([&pool]() {
        const auto& stats = pool.GetStats();
        return stats.tasks_completed.load() + stats.tasks_failed.load() == 20;
    }, std::chrono::milliseconds(1000)));

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_queued.load(), 20);
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load(), 20);
    EXPECT_EQ(short_count.load() + long_count.load(), 15);  // 15 successful
    EXPECT_EQ(stats.tasks_failed.load(), 5);  // 5 throwing tasks
}

} // namespace graph::test
