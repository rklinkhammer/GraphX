/**
 * @file test_thread_pool_chaos.cpp
 * @brief Test Thread Pool Chaos Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include <gtest/gtest.h>
#include "graph/ThreadPool.hpp"
#include <atomic>
#include <thread>
#include <random>
#include <vector>
#include <memory>

namespace graph::test {

/**
 * @class ThreadPoolChaosTest
 * @brief Thread pool chaos test implementation for GraphX.
 */
class ThreadPoolChaosTest : public ::testing::Test {
protected:
    static constexpr uint32_t kSeed = 0xC0FFEEu;
    std::mt19937 rng{kSeed};

    void SetUp() override {
        RecordProperty("seed", kSeed);
    }
};

// ===================================================================================
// Random Failure Injection
// ===================================================================================

TEST_F(ThreadPoolChaosTest, RandomTaskFailures) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 1000;
    std::atomic<int> completed(0);
    std::atomic<int> failures(0);

    std::uniform_int_distribution<int> fail_dist(0, 9);  // 10% failure rate

    for (int i = 0; i < NUM_TASKS; ++i) {
        int task_id = i;
        auto result = pool.QueueTaskWithTimeout([task_id, &completed, &failures, fail_dist, this]() mutable {
            if (fail_dist(rng) == 0) {
                // Simulate random failure
                failures.fetch_add(1);
                throw std::runtime_error("Random failure in task " + std::to_string(task_id));
            }
            completed.fetch_add(1);
        }, std::chrono::seconds(5));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    // Wait for completion (with timeout safety)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (completed.load() + failures.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load(), NUM_TASKS);
    EXPECT_GT(stats.tasks_failed.load(), 0);  // Should have some failures
    EXPECT_LT(stats.tasks_failed.load(), NUM_TASKS / 5);  // But not too many
}

TEST_F(ThreadPoolChaosTest, ExceptionCascade) {
    // Test that exceptions in one task don't affect others
    graph::ThreadPool pool(4);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 100;
    std::vector<int> completed_tasks;
    std::mutex mutex;

    for (int i = 0; i < NUM_TASKS; ++i) {
        int task_id = i;
        auto result = pool.QueueTaskWithTimeout([task_id, &completed_tasks, &mutex]() {
            if (task_id % 10 == 0) {
                throw std::runtime_error("Task " + std::to_string(task_id) + " failed");
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed_tasks.push_back(task_id);
            }
        }, std::chrono::seconds(5));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    // All 100 tasks should be accounted for
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load(), NUM_TASKS);
    // Failed tasks should be multiples of 10
    EXPECT_EQ(stats.tasks_failed.load(), 10);
    // Other tasks should complete
    EXPECT_EQ(completed_tasks.size(), 90);
}

// ===================================================================================
// Variable Execution Times
// ===================================================================================

TEST_F(ThreadPoolChaosTest, RandomizedExecutionTimes) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 500;
    std::atomic<int> completed(0);

    std::uniform_int_distribution<int> sleep_dist(0, 10);  // 0-10ms random sleep

    for (int i = 0; i < NUM_TASKS; ++i) {
        auto result = pool.QueueTaskWithTimeout([&completed, sleep_dist, this]() mutable {
            // Random work duration
            int sleep_ms = sleep_dist(rng);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            completed.fetch_add(1);
        }, std::chrono::seconds(5));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    // Wait with reasonable timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed.load(), NUM_TASKS);
}

// ===================================================================================
// Rapid Queue/Stop Cycles
// ===================================================================================

TEST_F(ThreadPoolChaosTest, RapidQueueStopCycles) {
    const int NUM_CYCLES = 10;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        graph::ThreadPool pool(2);
        pool.Init();
        pool.StartExpected();

        // Queue tasks rapidly
        const int TASKS_PER_CYCLE = 100;
        std::atomic<int> completed(0);

        for (int i = 0; i < TASKS_PER_CYCLE; ++i) {
            auto result = pool.QueueTask([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                completed.fetch_add(1);
            });

            // Stop immediately if we hit queue full (chaos condition)
            if (result == graph::ThreadPool::QueueResult::Full) {
                break;
            }
        }

        // Random delay before stopping (0-50ms)
        std::uniform_int_distribution<int> delay_dist(0, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(rng)));

        pool.Stop();
        pool.Join();

        const auto& stats = pool.GetStats();
        // Verify consistency even under rapid cycles
        EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
                  stats.tasks_cancelled.load(),
                  stats.tasks_queued.load());
    }
}

// ===================================================================================
// Concurrent Stop Requests
// ===================================================================================

TEST_F(ThreadPoolChaosTest, ConcurrentStopRequests) {
    graph::ThreadPool pool(4);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 500;
    std::atomic<int> completed(0);

    // Queue long-running tasks
    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.QueueTask([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            completed.fetch_add(1);
        });
    }

    // Let some tasks start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Multiple threads trying to stop simultaneously (chaos condition)
    std::vector<std::thread> stop_threads;
    for (int i = 0; i < 5; ++i) {
        stop_threads.emplace_back([&pool]() {
            pool.Stop();
        });
    }

    // Wait for stop requests
    for (auto& t : stop_threads) t.join();

    // Join should be safe even after multiple stops
    pool.Join();

    const auto& stats = pool.GetStats();
    // Verify counter consistency
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load(),
              stats.tasks_queued.load());
}

// ===================================================================================
// Memory Pressure Scenarios
// ===================================================================================

TEST_F(ThreadPoolChaosTest, LargeTaskPayloads) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 100;
    std::atomic<int> completed(0);

    for (int i = 0; i < NUM_TASKS; ++i) {
        // Create large payload per task
        std::shared_ptr<std::vector<uint8_t>> payload =
            std::make_shared<std::vector<uint8_t>>(1024 * 100);  // 100KB each

        auto result = pool.QueueTaskWithTimeout([payload, &completed]() {
            // Use the payload
            (*payload)[0] = 42;
            completed.fetch_add(1);
        }, std::chrono::seconds(10));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.Stop();
    pool.Join();

    EXPECT_EQ(completed.load(), NUM_TASKS);
}

// ===================================================================================
// Rapid Queue Depth Changes
// ===================================================================================

TEST_F(ThreadPoolChaosTest, RapidQueueDepthFluctuations) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int NUM_BATCHES = 20;
    const int BATCH_SIZE = 100;
    std::atomic<int> completed(0);

    std::uniform_int_distribution<int> delay_dist(1, 10);

    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        // Queue a batch
        for (int i = 0; i < BATCH_SIZE; ++i) {
            auto result = pool.QueueTaskWithTimeout([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                completed.fetch_add(1);
            }, std::chrono::seconds(5));

            if (result != graph::ThreadPool::QueueResult::Ok) {
                FAIL() << "Failed to queue task in batch " << batch;
            }
        }

        // Random delay before next batch
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(rng)));

        // Check queue depth occasionally
        if (batch % 5 == 0) {
            int depth = pool.GetQueueDepth();
            // Queue depth should be reasonable (not negative or infinite)
            // With timeouts and rapid batching, queue can temporarily grow large
            EXPECT_GE(depth, 0);
            EXPECT_LT(depth, BATCH_SIZE * 10);  // More lenient limit
        }
    }

    // Wait for all to complete (with timeout safety)
    // Each task sleeps 2ms, so 2000 tasks * 2ms / num_threads should be minimum
    // Using generous 120 second timeout to account for system load
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (completed.load() < NUM_BATCHES * BATCH_SIZE && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.Stop();
    pool.Join();

    EXPECT_EQ(completed.load(), NUM_BATCHES * BATCH_SIZE)
        << "Expected " << NUM_BATCHES * BATCH_SIZE << " tasks, but only "
        << completed.load() << " completed";
}

// ===================================================================================
// Realistic Failure Scenarios
// ===================================================================================

TEST_F(ThreadPoolChaosTest, RealisticWorkloadMix) {
    // Simulate realistic workload: mix of fast tasks, slow tasks, and failing tasks
    graph::ThreadPool pool(4);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 200;
    std::atomic<int> fast_count(0);
    std::atomic<int> slow_count(0);
    std::atomic<int> failed_count(0);

    std::uniform_int_distribution<int> task_type_dist(0, 99);

    for (int i = 0; i < NUM_TASKS; ++i) {
        int task_type = task_type_dist(rng);
        int task_id = i;

        auto result = pool.QueueTaskWithTimeout([task_type, task_id, &fast_count, &slow_count, &failed_count]() {
            if (task_type < 10) {
                // 10% failure rate
                failed_count.fetch_add(1);
                throw std::runtime_error("Task " + std::to_string(task_id) + " failure");
            } else if (task_type < 50) {
                // 40% slow tasks (5ms)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                slow_count.fetch_add(1);
            } else {
                // 50% fast tasks (immediate)
                fast_count.fetch_add(1);
            }
        }, std::chrono::seconds(10));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    // Wait with generous timeout for slow tasks
    std::this_thread::sleep_for(std::chrono::seconds(2));

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();

    // Verify all tasks accounted for
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load(),
              NUM_TASKS);

    // Verify task counts make sense
    int total_completed = fast_count.load() + slow_count.load();
    EXPECT_EQ(total_completed, stats.tasks_completed.load());
    EXPECT_GT(stats.tasks_failed.load(), 0);  // Should have failures
}

// ===================================================================================
// Stability Under Sustained Load
// ===================================================================================

TEST_F(ThreadPoolChaosTest, SustainedLoadStability) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int DURATION_MS = 1000;  // 1 second of sustained load
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(DURATION_MS);

    std::atomic<int> total_queued(0);
    std::atomic<int> total_completed(0);

    std::uniform_int_distribution<int> sleep_dist(0, 2);

    // Queue tasks as fast as possible for duration
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = pool.QueueTaskWithTimeout([&total_completed, sleep_dist, this]() mutable {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleep_dist(rng))
            );
            total_completed.fetch_add(1);
        }, std::chrono::milliseconds(100));  // Short timeout for sustained load

        if (result == graph::ThreadPool::QueueResult::Ok) {
            total_queued.fetch_add(1);
        }
    }

    // Wait for remaining tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    pool.Stop();
    pool.Join();

    const auto& stats = pool.GetStats();

    // Verify stability: most tasks complete
    int completion_rate = (stats.tasks_completed.load() * 100) / total_queued.load();
    EXPECT_GT(completion_rate, 50);  // At least 50% completion

    // Verify no hangs or deadlocks
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load(),
              stats.tasks_queued.load());
}

} // namespace graph::test
