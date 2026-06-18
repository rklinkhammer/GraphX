/**
 * @file test_thread_pool_benchmarks.cpp
 * @brief Test Thread Pool Benchmarks Graph runtime support.
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
#include <chrono>
#include <vector>
#include <numeric>
#include <iostream>
#include <iomanip>

namespace graph::test {

// ===================================================================================
// Performance Measurement Helper
// ===================================================================================

struct PerformanceMetrics {
    uint64_t total_tasks{0};
    uint64_t total_time_ns{0};
    double throughput_tasks_per_sec{0.0};
    double avg_latency_us{0.0};
    double min_latency_us{0.0};
    double max_latency_us{0.0};
    std::vector<uint64_t> task_times_ns;

/**
 * @brief Calculate.
 */
    void calculate() {
        if (total_tasks == 0) return;

        // Throughput
        double seconds = static_cast<double>(total_time_ns) / 1e9;
        throughput_tasks_per_sec = static_cast<double>(total_tasks) / seconds;

        // Latency stats
        if (!task_times_ns.empty()) {
            double sum_us = 0.0;
            min_latency_us = static_cast<double>(task_times_ns[0]) / 1e3;
            max_latency_us = min_latency_us;

            for (auto ns : task_times_ns) {
                double us = static_cast<double>(ns) / 1e3;
                sum_us += us;
                min_latency_us = std::min(min_latency_us, us);
                max_latency_us = std::max(max_latency_us, us);
            }
            avg_latency_us = sum_us / task_times_ns.size();
        }
    }

/**
 * @brief Print.
 * @param name Parameter for print.
 */
    void print(const std::string& name) const {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "Performance Metrics: " << name << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Total Tasks:           " << std::setw(12) << total_tasks << std::endl;
        std::cout << "Total Time:            " << std::setw(10) << (total_time_ns / 1e6) << " ms" << std::endl;
        std::cout << "Throughput:            " << std::setw(10) << std::fixed << std::setprecision(2)
                  << throughput_tasks_per_sec << " tasks/sec" << std::endl;
        std::cout << "Avg Latency:           " << std::setw(10) << std::fixed << std::setprecision(3)
                  << avg_latency_us << " μs" << std::endl;
        std::cout << "Min Latency:           " << std::setw(10) << std::fixed << std::setprecision(3)
                  << min_latency_us << " μs" << std::endl;
        std::cout << "Max Latency:           " << std::setw(10) << std::fixed << std::setprecision(3)
                  << max_latency_us << " μs" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }
};

/**
 * @class ThreadPoolBenchmarkTest
 * @brief Thread pool benchmark test implementation for GraphX.
 */
class ThreadPoolBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Synchronize system clock before benchmarks
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

// ===================================================================================
// Throughput Benchmarks
// ===================================================================================

TEST_F(ThreadPoolBenchmarkTest, ThroughputSingleThread_LightTasks) {
    graph::ThreadPool pool(1);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 10000;
    std::atomic<int> completed(0);

    auto start = std::chrono::steady_clock::now();

    // Use QueueTaskWithTimeout to handle queue capacity properly
    for (int i = 0; i < NUM_TASKS; ++i) {
        auto result = pool.QueueTaskWithTimeout([&completed]() {
            // Simulate light work (no sleep)
            volatile int x = 0;
            for (int j = 0; j < 100; ++j) x += j;
            completed.fetch_add(1);
        }, std::chrono::seconds(5));

        // Ensure task was queued
        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task: " << static_cast<int>(result);
        }
    }

    // Wait for completion (with timeout safety)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    pool.Stop();
    pool.Join();

    PerformanceMetrics metrics;
    metrics.total_tasks = NUM_TASKS;
    metrics.total_time_ns = elapsed_ns;
    metrics.calculate();
    metrics.print("SingleThread_LightTasks");

    // Sanity checks
    EXPECT_EQ(completed.load(), NUM_TASKS);
    EXPECT_GT(metrics.throughput_tasks_per_sec, 1000);  // At least 1000 tasks/sec
}

TEST_F(ThreadPoolBenchmarkTest, ThroughputMultiThread_LightTasks) {
    int num_threads = std::thread::hardware_concurrency();
    graph::ThreadPool pool(num_threads);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 50000;
    std::atomic<int> completed(0);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_TASKS; ++i) {
        auto result = pool.QueueTaskWithTimeout([&completed]() {
            // Simulate light work
            volatile int x = 0;
            for (int j = 0; j < 100; ++j) x += j;
            completed.fetch_add(1);
        }, std::chrono::seconds(5));

        if (result != graph::ThreadPool::QueueResult::Ok) {
            FAIL() << "Failed to queue task at index " << i;
        }
    }

    // Wait for completion (with timeout safety)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    pool.Stop();
    pool.Join();

    PerformanceMetrics metrics;
    metrics.total_tasks = NUM_TASKS;
    metrics.total_time_ns = elapsed_ns;
    metrics.calculate();
    metrics.print("MultiThread_LightTasks");

    EXPECT_EQ(completed.load(), NUM_TASKS);
    EXPECT_GT(metrics.throughput_tasks_per_sec, num_threads * 1000);  // Scale linearly
}

// ===================================================================================
// Latency Benchmarks
// ===================================================================================

TEST_F(ThreadPoolBenchmarkTest, LatencyMeasurement_SingleThread) {
    graph::ThreadPool pool(1);
    pool.Init();
    pool.StartExpected();

    const int NUM_TASKS = 1000;
    std::vector<uint64_t> task_times;
    std::mutex mutex;

    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.QueueTask([&task_times, &mutex]() {
            auto start = std::chrono::steady_clock::now();
            // Simulate work
            volatile int x = 0;
            for (int j = 0; j < 1000; ++j) x += j;
            auto end = std::chrono::steady_clock::now();

            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            {
                std::lock_guard<std::mutex> lock(mutex);
                task_times.push_back(elapsed);
            }
        });
    }

    // Wait for all tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    pool.Stop();
    pool.Join();

    PerformanceMetrics metrics;
    metrics.total_tasks = NUM_TASKS;
    metrics.task_times_ns = task_times;
    metrics.calculate();
    metrics.print("LatencyMeasurement_SingleThread");

    EXPECT_EQ(task_times.size(), NUM_TASKS);
    EXPECT_LT(metrics.avg_latency_us, 100);  // Average latency < 100 μs
}

TEST_F(ThreadPoolBenchmarkTest, LatencyScaling_ThreadCount) {
    // Test how latency scales with thread count
    for (int num_threads : {1, 2, 4, 8}) {
        graph::ThreadPool pool(num_threads);
        pool.Init();
        pool.StartExpected();

        const int NUM_TASKS = 1000;
        std::vector<uint64_t> task_times;
        std::mutex mutex;

        auto overall_start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_TASKS; ++i) {
            pool.QueueTask([&task_times, &mutex]() {
                auto start = std::chrono::steady_clock::now();
                volatile int x = 0;
                for (int j = 0; j < 100; ++j) x += j;
                auto end = std::chrono::steady_clock::now();

                auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    task_times.push_back(elapsed);
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto overall_end = std::chrono::steady_clock::now();
        auto overall_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(overall_end - overall_start).count();

        pool.Stop();
        pool.Join();

        PerformanceMetrics metrics;
        metrics.total_tasks = NUM_TASKS;
        metrics.total_time_ns = overall_time_ns;
        metrics.task_times_ns = task_times;
        metrics.calculate();
        metrics.print("LatencyScaling_" + std::to_string(num_threads) + "threads");
    }
}

// ===================================================================================
// Queue Overhead Benchmarks
// ===================================================================================

TEST_F(ThreadPoolBenchmarkTest, QueueOverhead_WithBatching) {
    graph::ThreadPool pool(std::thread::hardware_concurrency());
    pool.Init();
    pool.StartExpected();

    const int BATCH_SIZE = 100;
    const int NUM_BATCHES = 100;
    std::atomic<int> completed(0);

    auto start = std::chrono::steady_clock::now();

    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        for (int i = 0; i < BATCH_SIZE; ++i) {
            auto result = pool.QueueTaskWithTimeout([&completed]() {
                volatile int x = 0;
                for (int j = 0; j < 50; ++j) x += j;
                completed.fetch_add(1);
            }, std::chrono::seconds(5));

            if (result != graph::ThreadPool::QueueResult::Ok) {
                FAIL() << "Failed to queue task in batch " << batch;
            }
        }
        // Small delay between batches (simulate pipelined work)
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (completed.load() < BATCH_SIZE * NUM_BATCHES && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    pool.Stop();
    pool.Join();

    PerformanceMetrics metrics;
    metrics.total_tasks = BATCH_SIZE * NUM_BATCHES;
    metrics.total_time_ns = elapsed_ns;
    metrics.calculate();
    metrics.print("QueueOverhead_WithBatching");

    EXPECT_EQ(completed.load(), BATCH_SIZE * NUM_BATCHES);
}

// ===================================================================================
// Memory Efficiency Benchmarks
// ===================================================================================

TEST_F(ThreadPoolBenchmarkTest, MemoryScaling_QueueSize) {
    // Test memory impact of different queue sizes
    for (int queue_size : {10, 100, 1000}) {
        graph::ThreadPool::DeadlockConfig cfg;
        cfg.max_queue_size = queue_size;

        graph::ThreadPool pool(4, cfg);
        pool.Init();
        pool.StartExpected();

        const int NUM_TASKS = 5000;
        std::atomic<int> completed(0);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_TASKS; ++i) {
            auto result = pool.QueueTaskWithTimeout([&completed]() {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                completed.fetch_add(1);
            }, std::chrono::seconds(5));

            if (result != graph::ThreadPool::QueueResult::Ok) {
                FAIL() << "Failed to queue task at index " << i;
            }
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (completed.load() < NUM_TASKS && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        pool.Stop();
        pool.Join();

        PerformanceMetrics metrics;
        metrics.total_tasks = NUM_TASKS;
        metrics.total_time_ns = elapsed_ns;
        metrics.calculate();
        metrics.print("MemoryScaling_QueueSize" + std::to_string(queue_size));
    }
}

// ===================================================================================
// Startup/Shutdown Overhead
// ===================================================================================

TEST_F(ThreadPoolBenchmarkTest, StartupShutdownOverhead) {
    const int NUM_POOLS = 100;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_POOLS; ++i) {
        graph::ThreadPool pool(2);
        pool.Init();
        pool.StartExpected();

        pool.QueueTask([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pool.Stop();
        pool.Join();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto per_pool_ms = elapsed_ns / NUM_POOLS / 1e6;

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Startup/Shutdown Overhead" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Pools Created:   " << std::setw(12) << NUM_POOLS << std::endl;
    std::cout << "Total Time:            " << std::setw(10) << std::fixed << std::setprecision(2)
              << (elapsed_ns / 1e6) << " ms" << std::endl;
    std::cout << "Per-Pool Avg:          " << std::setw(10) << std::fixed << std::setprecision(2)
              << per_pool_ms << " ms" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

} // namespace graph::test
