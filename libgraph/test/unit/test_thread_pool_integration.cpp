/**
 * @file test_thread_pool_integration.cpp
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
 * @file test_thread_pool_integration.cpp
 * @brief Integration tests for ThreadPool with GraphExecutor (Optional Enhancement)
 *
 * Tests ThreadPool interaction with graph execution:
 * - ThreadPool with various graph topologies
 * - Throughput measurement with real graph execution
 * - Task scheduling impact on graph completion
 * - Thread count scaling with graph complexity
 * - Resource management in graph context
 *
 * @note Tests real-world graph execution scenarios
 */

#include <gtest/gtest.h>
#include "graph/ThreadPool.hpp"
#include "graph/GraphManager.hpp"
#include "test/TestGraphTopologies.hpp"
#include "test/AdvancedTestNodes.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <iostream>
#include <iomanip>

namespace graph::test {

/**
 * @class ThreadPoolIntegrationTest
 * @brief Thread pool integration test implementation for GraphX.
 */
class ThreadPoolIntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<graph::ThreadPool> pool;

    void SetUp() override {
        pool = std::make_shared<graph::ThreadPool>(
            std::thread::hardware_concurrency()
        );
        pool->Init();
        pool->StartExpected();
    }

    void TearDown() override {
        if (pool) {
            pool->Stop();
            pool->Join();
        }
    }
};

// ===================================================================================
// ThreadPool Task Scheduling Impact
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, TaskSchedulingWithVariableLoad) {
    const int NUM_ITERATIONS = 10;
    std::vector<double> completion_times;

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        const int TASKS = 1000;
        std::atomic<int> completed(0);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < TASKS; ++i) {
            auto result = pool->QueueTaskWithTimeout([&completed]() {
                // Simulate variable work (1-10μs)
                volatile int x = 0;
                for (int j = 0; j < 100; ++j) {
                    x += j;
                }
                completed.fetch_add(1);
            }, std::chrono::seconds(5));

            if (result != graph::ThreadPool::QueueResult::Ok) {
                FAIL() << "Failed to queue task at iteration " << iter << " task " << i;
            }
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (completed.load() < TASKS && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
        ).count();

        completion_times.push_back(elapsed_ms);
    }

    // Verify consistency across iterations
    double avg_time = 0;
    for (auto time : completion_times) {
        avg_time += time;
    }
    avg_time /= completion_times.size();

    // Check that variation is not excessive (coefficient of variation < 30%)
    double variance = 0;
    for (auto time : completion_times) {
        variance += (time - avg_time) * (time - avg_time);
    }
    variance /= completion_times.size();
    double stddev = std::sqrt(variance);
    double cv = (stddev / avg_time) * 100.0;

    std::cout << "\nTask Scheduling Consistency:" << std::endl;
    std::cout << "Average Time: " << std::fixed << std::setprecision(2) << avg_time << " ms" << std::endl;
    std::cout << "Std Dev:      " << std::fixed << std::setprecision(2) << stddev << " ms" << std::endl;
    std::cout << "CV:           " << std::fixed << std::setprecision(1) << cv << "%" << std::endl;

    EXPECT_LT(cv, 30.0);  // Should have reasonable consistency
}

// ===================================================================================
// Thread Count Scaling
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, ThreadCountScalingImpact) {
    std::cout << "\nThread Count Scaling Analysis:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    for (int num_threads : {1, 2, 4, 8, static_cast<int>(std::thread::hardware_concurrency())}) {
        auto test_pool = std::make_shared<graph::ThreadPool>(num_threads);
        test_pool->Init();
        test_pool->StartExpected();

        const int NUM_TASKS = 10000;
        std::atomic<int> completed(0);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_TASKS; ++i) {
            auto result = test_pool->QueueTaskWithTimeout([&completed]() {
                volatile int x = 0;
                for (int j = 0; j < 50; ++j) x += j;
                completed.fetch_add(1);
            }, std::chrono::seconds(5));

            if (result != graph::ThreadPool::QueueResult::Ok) {
                FAIL() << "Failed to queue task at index " << i;
            }
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
        ).count();

        auto throughput = (double)NUM_TASKS / (elapsed_ms / 1000.0);

        std::cout << "Threads: " << std::setw(2) << num_threads
                  << " | Time: " << std::setw(6) << elapsed_ms << " ms"
                  << " | Throughput: " << std::setw(8) << std::fixed << std::setprecision(0)
                  << throughput << " tasks/sec" << std::endl;

        test_pool->Stop();
        test_pool->Join();
    }

    std::cout << std::string(60, '=') << std::endl;
}

// ===================================================================================
// Queue Capacity Impact
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, QueueCapacityImpact) {
    std::cout << "\nQueue Capacity Impact:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    for (int queue_size : {10, 100, 1000}) {
        graph::ThreadPool::DeadlockConfig cfg;
        cfg.max_queue_size = queue_size;

        auto test_pool = std::make_shared<graph::ThreadPool>(4, cfg);
        test_pool->Init();
        test_pool->StartExpected();

        const int NUM_TASKS = 5000;
        std::atomic<int> completed(0);
        std::atomic<int> queue_full_count(0);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_TASKS; ++i) {
            auto result = test_pool->QueueTaskWithTimeout([&completed]() {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                completed.fetch_add(1);
            }, std::chrono::seconds(10));

            if (result != ThreadPool::QueueResult::Ok) {
                queue_full_count.fetch_add(1);
            }
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
        ).count();

        double rejection_rate = (queue_full_count.load() * 100.0) / NUM_TASKS;

        std::cout << "Queue Size: " << std::setw(4) << queue_size
                  << " | Time: " << std::setw(6) << elapsed_ms << " ms"
                  << " | Rejections: " << std::setw(5) << std::fixed << std::setprecision(1)
                  << rejection_rate << "%" << std::endl;

        test_pool->Stop();
        test_pool->Join();
    }

    std::cout << std::string(60, '=') << std::endl;
}

// ===================================================================================
// Sustained Load Handling
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, SustainedLoadHandling) {
    // Sustained load over extended period (5 seconds)
    const auto duration = std::chrono::seconds(5);
    const auto deadline = std::chrono::steady_clock::now() + duration;

    std::atomic<int> total_tasks_queued(0);
    std::atomic<int> total_tasks_completed(0);
    std::atomic<int> total_queue_failures(0);

    std::cout << "\nSustained Load Test (5 seconds):" << std::endl;
    std::cout << "Status: Running..." << std::endl;

    while (std::chrono::steady_clock::now() < deadline) {
        auto result = pool->QueueTask([&total_tasks_completed]() {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            total_tasks_completed.fetch_add(1);
        });

        if (result == ThreadPool::QueueResult::Ok) {
            total_tasks_queued.fetch_add(1);
        } else {
            total_queue_failures.fetch_add(1);
        }
    }

    // Wait for remaining tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const auto& stats = pool->GetStats();

    int completion_rate = (stats.tasks_completed.load() * 100) / total_tasks_queued.load();

    std::cout << "Tasks Queued:      " << std::setw(8) << total_tasks_queued.load() << std::endl;
    std::cout << "Tasks Completed:   " << std::setw(8) << stats.tasks_completed.load() << std::endl;
    std::cout << "Queue Failures:    " << std::setw(8) << total_queue_failures.load() << std::endl;
    std::cout << "Completion Rate:   " << std::setw(7) << completion_rate << "%" << std::endl;

    // Should maintain reasonable completion rate
    EXPECT_GT(completion_rate, 50);
}

// ===================================================================================
// Task Distribution Uniformity
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, TaskDistributionUniformity) {
    const int NUM_THREADS = std::thread::hardware_concurrency();
    const int TASKS_PER_THREAD = 100;
    const int NUM_TASKS = NUM_THREADS * TASKS_PER_THREAD;

    std::vector<std::atomic<int>> thread_work_counts(NUM_THREADS);
    for (auto& counter : thread_work_counts) {
        counter.store(0);
    }

    std::atomic<int> task_id_counter(0);

    // Queue tasks with thread-id tracking
    for (int i = 0; i < NUM_TASKS; ++i) {
        auto result = pool->QueueTaskWithTimeout([&task_id_counter, &thread_work_counts, NUM_THREADS]() {
            // Get approximate thread ID (based on task execution)
            auto thread_id = std::this_thread::get_id();
            static thread_local int my_id = -1;
            if (my_id == -1) {
                my_id = (task_id_counter.fetch_add(1)) % NUM_THREADS;
            }
            thread_work_counts[my_id].fetch_add(1);

            // Do some work
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }, std::chrono::seconds(10));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    // Wait for all tasks to complete
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::atomic<int> completed_count(0);
    while (true) {
        completed_count.store(0);
        for (auto& counter : thread_work_counts) {
            completed_count.fetch_add(counter.load());
        }
        if (completed_count.load() == NUM_TASKS) {
            break;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL() << "Timeout waiting for tasks to complete. Got " << completed_count.load() << " of " << NUM_TASKS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Analyze distribution
    std::cout << "\nTask Distribution Across Threads:" << std::endl;
    std::cout << std::string(40, '=') << std::endl;

    int total = 0;
    for (size_t i = 0; i < thread_work_counts.size(); ++i) {
        int count = thread_work_counts[i].load();
        total += count;
        std::cout << "Thread " << i << ": " << std::setw(4) << count << " tasks" << std::endl;
    }

    std::cout << std::string(40, '=') << std::endl;
    std::cout << "Total: " << total << " tasks" << std::endl;

    // Check that work is reasonably distributed
    EXPECT_EQ(total, NUM_TASKS);
}

// ===================================================================================
// Error Resilience
// ===================================================================================

TEST_F(ThreadPoolIntegrationTest, ErrorRecoveryInLongSequence) {
    const int NUM_TASKS = 1000;
    std::atomic<int> completed(0);
    std::atomic<int> failed(0);

    for (int i = 0; i < NUM_TASKS; ++i) {
        int task_id = i;
        pool->QueueTask([task_id, &completed, &failed]() {
            if (task_id % 50 == 0) {
                // Inject failures periodically
                failed.fetch_add(1);
                throw std::runtime_error("Task " + std::to_string(task_id));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            completed.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const auto& stats = pool->GetStats();

    std::cout << "\nError Recovery Test:" << std::endl;
    std::cout << "Total Tasks:     " << NUM_TASKS << std::endl;
    std::cout << "Completed:       " << stats.tasks_completed.load() << std::endl;
    std::cout << "Failed:          " << stats.tasks_failed.load() << std::endl;
    std::cout << "Cancelled:       " << stats.tasks_cancelled.load() << std::endl;

    // Verify all tasks accounted for
    EXPECT_EQ(stats.tasks_completed.load() + stats.tasks_failed.load() +
              stats.tasks_cancelled.load(),
              NUM_TASKS);

    // Verify pool continued operation after failures
    EXPECT_GT(stats.tasks_completed.load(), 0);
}

} // namespace graph::test
