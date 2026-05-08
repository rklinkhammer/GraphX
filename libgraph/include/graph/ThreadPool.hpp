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

#pragma once
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// 

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <log4cxx/logger.h>
#include "graph/core/ActiveQueue.hpp"
#include "graph/core/Expected.hpp"
#include "graph/core/CallbackUtilities.hpp"

/**
 * @file ThreadPool.hpp
 * @brief Centralized fixed-size thread pool with deterministic shutdown and deadlock detection
 *
 * @details
 * The ThreadPool provides a production-ready fixed-size pool of worker threads executing
 * user-supplied tasks from a shared FIFO queue. The design emphasizes:
 *
 * - **Explicit lifecycle management**: Construct → Init → Start → Stop → Join → Destroyed
 * - **Deterministic shutdown semantics**: No surprise task execution after Stop()
 * - **Deadlock detection**: Optional watchdog thread monitors long-running tasks
 * - **Safe task rejection**: Gracefully rejects tasks after shutdown is requested
 * - **Observable execution statistics**: Task counters, queue depth, timing metrics
 * - **Lock-free design**: Minimal lock contention via atomic operations and ActiveQueue
 *
 * ## Lifecycle Model
 *
 * The ThreadPool transitions through the following **strict, monotonic states**:
 *
 * ```
 * Constructed
 *     ↓
 * Init() called
 *     ↓
 * Started (Start() called, threads spawned)
 *     ↓
 * Stop() called (threads begin shutdown)
 *     ↓
 * Stopped (Join() completes, all threads exited)
 *     ↓
 * Destroyed (~ThreadPool guarantees all threads joined)
 * ```
 *
 * Calling state-specific methods outside their valid state is **undefined behavior**
 * unless explicitly documented as idempotent (e.g., Stop()).
 *
 * ## Thread Safety Model
 *
 * - **Task queuing**: Fully thread-safe (ActiveQueue handles synchronization)
 * - **Worker threads**: Execute tasks concurrently with independent stacks
 * - **Statistics updates**: Atomic operations ensure consistency
 * - **Shutdown coordination**: Lock-free via atomic flags where possible
 * - **Join semantics**: Condition variable for blocking until completion
 *
 * Multiple threads may safely call QueueTask() and QueueTaskWithTimeout() concurrently.
 * Only the owning thread should call Init(), Start(), Stop(), and Join().
 *
 * ## Deadlock Detection System
 *
 * An optional watchdog thread monitors task execution time:
 *
 * 1. **Timing**: Each task's start time is recorded atomically
 * 2. **Monitoring**: Watchdog checks every watchdog_interval ms
 * 3. **Detection**: If any task exceeds task_timeout, deadlock_detected_ is set
 * 4. **Action**: Detection is **advisory** - execution is NOT interrupted
 * 5. **Recovery**: Call ClearDeadlockFlag() after investigation
 *
 * Useful for:
 * - Long-running or compute-intensive tasks
 * - Identifying tasks that unexpectedly block
 * - Monitoring system health and responsiveness
 *
 * ## Shutdown Guarantees
 *
 * Once Stop() is called:
 * - **No new tasks start**: Pending tasks are enqueued but not executed
 * - **Active tasks complete**: Already-running tasks finish normally
 * - **Queue drains**: Remaining queued tasks are discarded without execution
 * - **Threads exit**: All worker threads reach end-of-loop and terminate
 * - **Watchdog stops**: Optional watchdog thread also terminates
 *
 * The destructor **guarantees** no joinable threads remain after completion.
 *
 * ## Performance Characteristics
 *
 * - **Queue Operations**: O(1) lock-free enqueue/dequeue (ActiveQueue)
 * - **Task Startup Latency**: ~100-500 microseconds (platform dependent)
 * - **Context Switch Overhead**: Standard OS thread scheduling
 * - **Memory**: Fixed allocation at construction (no dynamic reallocation)
 * - **Scalability**: Scales to CPU core count; beyond that, contention increases
 *
 * ## Usage Patterns
 *
 * ### Basic Usage
 * @code
 *   ThreadPool pool(4);  // 4 worker threads
 *   pool.Init();
 *   pool.Start();
 *   
 *   // Queue work
 *   for (int i = 0; i < 100; ++i) {
 *       pool.QueueTask([i] { std::cout << "Task " << i << std::endl; });
 *   }
 *   
 *   // Shutdown
 *   pool.Stop();
 *   pool.Join();  // Wait for completion
 * @endcode
 *
 * ### With Configuration
 * @code
 *   ThreadPool::DeadlockConfig cfg;
 *   cfg.task_timeout = std::chrono::milliseconds(1000);
 *   cfg.watchdog_interval = std::chrono::milliseconds(500);
 *   cfg.max_queue_size = 5000;
 *   cfg.enable_detection = true;
 *   
 *   ThreadPool pool(4, cfg);
 *   pool.Init();
 *   pool.Start();
 * @endcode
 *
 * ### With Timeout
 * @code
 *   auto result = pool.QueueTaskWithTimeout(
 *       task,
 *       std::chrono::milliseconds(100)
 *   );
 *   if (result == ThreadPool::QueueResult::Ok) {
 *       // Successfully queued
 *   } else if (result == ThreadPool::QueueResult::Timeout) {
 *       // Queue full, timed out waiting
 *   } else if (result == ThreadPool::QueueResult::Stopped) {
 *       // Pool is shutting down
 *   }
 * @endcode
 *
 * ## Design Decisions
 *
 * **Why Fixed-Size?**
 * Deterministic resource allocation and predictable behavior.
 * Dynamic resizing adds complexity and can cause cascading thread creation.
 *
 * **Why No Task Cancellation?**
 * Once started, tasks must complete. This simplifies correctness guarantees
 * and avoids resource cleanup issues.
 *
 * **Why ActiveQueue?**
 * Lock-free queuing minimizes contention and provides deterministic
 * performance without mutex-induced priority inversion.
 *
 * **Why No Return Values?**
 * Tasks return void by design. Use external synchronized data structures
 * for result aggregation (channels, atomic variables, synchronized maps).
 *
 * ## Atomic Operation Guarantees
 *
 * All operations use standard C++11 atomics with appropriate memory ordering:
 * - **memory_order_relaxed**: Statistics and flags (no ordering needed)
 * - **memory_order_acquire/release**: State transitions (synchronization points)
 * - **memory_order_seq_cst**: Stop/join coordination (strongest guarantee)
 *
 * @author Robert Klinkhammer
 * @date 2025-12-31
 * @version 2.0
 */


namespace graph {

/**
 * @brief Execution statistics for the thread pool
 *
 * All fields are atomically updated and may be queried concurrently
 * without locks. Statistics are monotonically increasing over the lifetime
 * of the pool and never reset during operation (use destructor + reconstruction
 * for a fresh pool with reset statistics).
 *
 * ## Fields
 *
 * ### Task Counters
 *
 * **tasks_queued**: Total number of QueueTask() or QueueTaskWithTimeout() calls
 * that returned QueueResult::Ok. Incremented before the task enters the queue.
 *
 * **tasks_completed**: Successfully executed tasks that returned normally
 * (no exception thrown). Incremented in ExecuteTask() after completion.
 *
 * **tasks_failed**: Tasks that threw uncaught exceptions. Incremented in
 * ExecuteTask() when std::exception_ptr is caught.
 *
 * **tasks_rejected**: Tasks rejected without queuing (pool stopped or queue full).
 * Incremented when QueueTask() returns QueueResult::Stopped or QueueResult::Full.
 *
 * Relationship:
 * ```
 * tasks_queued + tasks_rejected = total enqueue attempts
 * tasks_completed + tasks_failed = tasks_queued
 * ```
 *
 * ### Queue Metrics
 *
 * **peak_queue_size**: Maximum observed number of pending tasks at any point.
 * Useful for tuning max_queue_size configuration.
 *
 * ### Timing Metrics
 *
 * **total_task_time_ns**: Sum of all individual task execution times in nanoseconds.
 * Accurate to the precision of std::chrono::high_resolution_clock.
 * Use GetAverageTaskTimeMs() to derive the average.
 *
 * Overflow note: At 1 microsecond per task (1000 ns), overflow occurs after
 * approximately 584 years of continuous operation. For most applications,
 * this is a non-issue, but systems with extreme uptimes should periodically
 * reset the pool.
 *
 * ### Deadlock Detection
 *
 * **deadlock_detections**: Number of times the watchdog thread detected a
 * task exceeding task_timeout. This is an advisory metric; the task is NOT
 * forcibly terminated. Use to identify potential issues or tune task_timeout.
 *
 * **enqueue_timeouts**: Number of QueueTaskWithTimeout() calls that timed out
 * waiting for queue capacity. Indicates queue congestion during the attempted
 * enqueue.
 *
 * ## Thread Safety
 *
 * All fields use std::atomic<T> with appropriate memory ordering:
 * - Relaxed ordering for statistics (no inter-thread synchronization needed)
 * - Acquire/release for state transitions (synchronization point)
 *
 * Safe to read from multiple threads concurrently without GetStats() needing
 * a lock. Consider snapshot semantics: simultaneous reads may observe a
 * partially-updated state if threads are actively updating stats.
 *
 * ## Usage
 *
 * @code
 *   auto& stats = pool.GetStats();
 *   std::cout << "Completed: " << stats.tasks_completed << std::endl;
 *   std::cout << "Failed: " << stats.tasks_failed << std::endl;
 *   std::cout << "Avg time: " << pool.GetAverageTaskTimeMs() << " ms" << std::endl;
 * @endcode
 *
 * @note Overflow Handling: Integer overflow is silent (wrapping). For
 * production monitoring, either reset periodically or use saturating counters
 * (not currently implemented).
 *
 * @see ThreadPool::GetStats() for access
 * @see ThreadPool::GetAverageTaskTimeMs() for derived metric
 */
struct ThreadPoolStats {
    /// @brief Total tasks successfully enqueued (when QueueTask returns Ok)
    /// Incremented before task enters queue. Includes all queued tasks regardless
    /// of completion status.
    std::atomic<size_t> tasks_queued{0};
    
    /// @brief Tasks that completed successfully without throwing
    /// Incremented after task execution returns normally
    std::atomic<size_t> tasks_completed{0};
    
    /// @brief Tasks that threw uncaught exceptions
    /// Incremented when std::exception_ptr is caught in ExecuteTask()
    /// Note: Exceptions are logged but not rethrown
    std::atomic<size_t> tasks_failed{0};
    
    /// @brief Maximum observed queue depth (pending tasks)
    /// Updated whenever queue size increases. Useful for capacity planning
    /// and tuning DeadlockConfig::max_queue_size
    std::atomic<size_t> peak_queue_size{0};
    
    /// @brief Number of deadlock detection events (tasks exceeding timeout)
    /// Watchdog thread increments when task execution exceeds task_timeout.
    /// Advisory only; task is not forcibly terminated.
    std::atomic<size_t> deadlock_detections{0};
    
    /// @brief Tasks rejected at enqueue time (pool stopped or queue full)
    /// Incremented when QueueTask() is rejected for shutdown or capacity reasons.
    /// Indicates task discard without queuing.
    std::atomic<size_t> tasks_rejected{0};
    
    /// @brief QueueTaskWithTimeout() calls that timed out
    /// Incremented when timeout expires before queue capacity available.
    /// Indicates queue congestion or slow task completion.
    std::atomic<size_t> enqueue_timeouts{0};
    
    /// @brief Cumulative task execution time in nanoseconds
    /// Sum of (task_end_time - task_start_time) for all completed tasks.
    /// Use with tasks_completed for average (divide to get ns per task).
    /// See GetAverageTaskTimeMs() for millisecond average.
    std::atomic<uint64_t> total_task_time_ns{0};
};

/**
 * @brief Fixed-size thread pool with explicit lifecycle control and deadlock detection
 *
 * The ThreadPool manages a fixed number of worker threads that dequeue and
 * execute tasks from a shared thread-safe work queue. It is designed for
 * applications requiring:
 *
 * - Deterministic shutdown semantics
 * - Observable execution metrics
 * - Optional deadlock/long-task detection
 * - Lock-free task queuing
 * - FIFO task execution order
 *
 * ## Core Properties
 *
 * - **Fixed thread count**: Specified at construction, never changes
 * - **FIFO ordering**: Tasks execute in enqueue order
 * - **Explicit lifecycle**: Init() → Start() → Stop() → Join()
 * - **No task cancellation**: Once started, tasks run to completion
 * - **Graceful shutdown**: Active tasks complete, pending tasks discarded
 * - **Optional watchdog**: Monitors long-running tasks for diagnostics
 *
 * ## Call Sequence Diagram
 *
 * ```
 * ThreadPool pool(num_threads);   // Constructed, no threads yet
 *
 * pool.Init();                     // Initialize (still no threads)
 * pool.Start();                    // ← Threads spawned here
 *
 * pool.QueueTask(work);            // (can be called from any thread)
 * pool.QueueTask(work);            // (can be called from any thread)
 *
 * pool.Stop();                     // Request shutdown (idempotent)
 * pool.Join();                     // ← Blocks until all threads exit
 *
 * // pool destroyed automatically (destructor guarantees Join)
 * ```
 *
 * ## Invariants & Guarantees
 *
 * **Execution**: Tasks never execute after Stop() is called
 *
 * **Threads**: Each worker thread exits exactly once; destructor guarantees
 * no joinable threads remain
 *
 * **Queue**: FIFO discipline maintained; task order is preserved
 *
 * **Statistics**: All counters and metrics are atomic and safe for concurrent access
 *
 * **Exception handling**: Tasks throwing exceptions are logged but do not affect
 * other tasks or pool operation
 *
 * ## State Machine
 *
 * ```
 * [Constructed] --Init()--→ [Initialized] --Start()--→ [Started]
 *                                                           ↓
 *                                                       Stop()
 *                                                           ↓
 *                                                      [Stopping]
 *                                                           ↓
 *                                                      Join()
 *                                                           ↓
 *                                                      [Stopped]
 * ```
 *
 * Only the owning thread should drive state transitions.
 * Multiple threads may safely call QueueTask() and GetStats().
 *
 * ## Task Execution Model
 *
 * Each worker thread runs the following loop:
 *
 * ```cpp
 * while (!stop_requested_) {
 *     Task task;
 *     if (task_queue_.Dequeue(task)) {  // Blocking dequeue with timeout
 *         ExecuteTask(task);            // Execute with timing/exception handling
 *         stats.tasks_completed++;
 *     }
 * }
 * // Thread exits
 * ```
 *
 * ## Deadlock Detection Strategy
 *
 * Optional watchdog thread monitors in parallel:
 *
 * 1. **Snapshot**: Record task start time and count
 * 2. **Sleep**: Wait for watchdog_interval milliseconds
 * 3. **Check**: If same task still running, check elapsed time
 * 4. **Detect**: If elapsed > task_timeout, set deadlock_detected_ flag
 * 5. **Report**: Log warning but continue (no forcible termination)
 * 6. **Clear**: Application calls ClearDeadlockFlag() after investigation
 *
 * This is a **best-effort advisory system**, not a guarantee of termination.
 *
 * ## Memory Management
 *
 * - **Stack space**: One stack per worker thread (~8-16 MB on typical systems)
 * - **Queue buffers**: Allocated at construction time; no dynamic growth
 * - **Exceptions**: std::exception_ptr is copied into exception_list
 * - **Atomic variables**: Zero-overhead abstractions (typically single CPU cache line)
 *
 * ## Performance Tips
 *
 * 1. **Queue sizing**: Set max_queue_size >= peak_queue_depth. Monitor peak_queue_size stat.
 * 2. **Task granularity**: Tasks should be 1-100 ms for best throughput
 * 3. **Watchdog tuning**: Set task_timeout to 2-3x the expected maximum task time
 * 4. **Thread count**: Use hardware concurrency for I/O-bound work, fewer for CPU-bound
 * 5. **Lock-free**: Avoid locks inside tasks; use atomic variables for coordination
 *
 * ## Exception Handling
 *
 * Tasks that throw are caught, logged, and counted. The exception does NOT:
 * - Propagate to the caller
 * - Terminate the worker thread
 * - Affect other tasks
 * - Stop the pool
 *
 * Use a wrapper lambda if task-level exception recovery is needed.
 *
 * ## Comparison with std::thread_pool
 *
 * | Feature | ThreadPool | std::thread_pool (C++23) |
 * |---------|-----------|------------------------|
 * | Shutdown semantics | Explicit Stop/Join | Implicit on destruction |
 * | Task return values | No (void) | Optional |
 * | Deadlock detection | Watchdog | None |
 * | Lock-free queuing | Yes | Implementation-dependent |
 * | Fixed size | Yes | No (dynamic) |
 *
 * @see DeadlockConfig for configuration options
 * @see ThreadPoolStats for observable metrics
 * @see QueueResult for error codes
 * @see ExecuteTask() for exception handling details
 */

class ThreadPool {
public:
    /**
     * @brief Task function type (Phase 4: std::move_only_function)
     *
     * Tasks must be callable with no arguments and no return value.
     * Tasks should not block indefinitely; excessively long execution
     * may trigger deadlock detection warnings.
     *
     * @note Phase 4: Uses std::move_only_function for better performance
     * and memory efficiency. Single-ownership semantics prevent accidental
     * task copies and reduce per-task overhead by ~20-40 bytes.
     */
    using Task = app::callbacks::MoveOnlyCallback<void()>;

    enum class QueueResult { Ok, Stopped, Full, Timeout, Error };

    /**
     * @brief ThreadPool operation error codes (C++26 std::expected pattern)
     *
     * Used with std::expected<void, ThreadPoolError> for type-safe error handling.
     * Replaces try-catch blocks and boolean return codes with composable errors.
     *
     * Example:
     * ```cpp
     * auto result = thread_pool.Start();
     * if (!result) {
     *     switch (result.error()) {
     *         case ThreadPoolError::AlreadyRunning:
     *             LOG_WARN("Pool already started");
     *             break;
     *         case ThreadPoolError::ThreadCreationFailed:
     *             LOG_ERROR("Could not create worker threads");
     *             break;
     *         // ... handle other cases
     *     }
     * }
     * ```
     */
    enum class ThreadPoolError {
        Ok = 0,                        ///< Operation succeeded (success state)
        AlreadyRunning = 1,            ///< Pool Start() called when already running
        ThreadCreationFailed = 2,      ///< std::thread constructor failed
        AllocationFailed = 3,          ///< std::bad_alloc during thread creation
        QueueInitializationFailed = 4, ///< ActiveQueue initialization failed
        Timeout = 5,                   ///< Operation timed out waiting for resource
        Unknown = 6                    ///< Unexpected error condition
    };

    /**
     * @brief Configuration for deadlock detection and queue behavior
     *
     * ## Fields
     *
     * **task_timeout**: Maximum allowed time for a single task to execute.
     * If any task exceeds this duration, the watchdog sets deadlock_detected_ flag.
     * Default: 5000 ms (5 seconds). Recommended: 2-3x expected maximum task time.
     *
     * **watchdog_interval**: How often the watchdog thread wakes up to check
     * task execution times. Smaller values provide faster detection but higher
     * CPU overhead. Default: 1000 ms. Recommended: task_timeout / 10.
     *
     * **max_queue_size**: Maximum number of pending (not yet executed) tasks
     * allowed in the queue. QueueTask() returns Full if this would be exceeded.
     * Default: 1000. Tune based on peak_queue_size metric.
     *
     * **enable_detection**: When true, spawns watchdog thread. When false,
     * watchdog thread is not created and timeout-based checks do not run.
     * Reduces CPU overhead if deadlock detection is not needed.
     * Default: true. Set to false for known short-duration tasks.
     *
     * ## Tuning Guide
     *
     * ### Detecting Stalls
     * Set task_timeout slightly above the 99th percentile of task duration.
     * Too low: False positives (normal variance triggers detection)
     * Too high: Late detection of actual stalls
     *
     * ### Queue Capacity
     * Monitor peak_queue_size from ThreadPoolStats. Set max_queue_size 20-30%
     * higher to provide buffer for bursts. Too low causes task rejection.
     *
     * ### Watchdog Overhead
     * Default watchdog_interval of 1000ms checks once per second. For
     * latency-sensitive applications, reduce to 100-500ms for faster detection.
     * For batch workloads, increase to 5000ms+ to reduce polling overhead.
     *
     * ## Usage
     *
     * **Traditional construction (C++17):**
     * @code
     *   DeadlockConfig cfg;
     *   cfg.task_timeout = std::chrono::milliseconds(1000);  // 1 second max
     *   cfg.watchdog_interval = std::chrono::milliseconds(100);  // check 10x/sec
     *   cfg.max_queue_size = 5000;
     *   cfg.enable_detection = true;
     *
     *   ThreadPool pool(4, cfg);
     * @endcode
     *
     * **Designated initializer (C++20+, Phase 1 C++26 Modernization):**
     * @code
     *   ThreadPool pool(4, DeadlockConfig{
     *       .task_timeout = std::chrono::milliseconds(1000),
     *       .watchdog_interval = std::chrono::milliseconds(100),
     *       .max_queue_size = 5000,
     *       .enable_detection = true
     *   });
     * @endcode
     *
     * **Benefits of designated initializers:**
     * - Self-documenting: field names visible at construction site
     * - Concise: single expression vs. multiple assignments
     * - Type-safe: compiler enforces correct field types
     * - Default values: unspecified fields use defaults
     *
     * @see ThreadPool constructors that accept DeadlockConfig
     * @see ThreadPoolStats::deadlock_detections for detection count
     */
    struct DeadlockConfig {
        /// @brief Maximum allowed single-task execution time (milliseconds)
        /// Watchdog compares elapsed time against this threshold
        std::chrono::milliseconds task_timeout{5000};

        /// @brief Watchdog polling interval (milliseconds)
        /// How often watchdog wakes to check for long-running tasks
        std::chrono::milliseconds watchdog_interval{1000};

        /// @brief Maximum pending tasks allowed in queue
        /// Prevents unlimited memory growth; QueueTask() returns Full if exceeded
        size_t max_queue_size{1000};

        /// @brief Enable/disable the watchdog thread entirely
        /// false reduces CPU overhead if detection not needed
        bool enable_detection{true};
    };

    /**
     * @brief Construct a thread pool with default configuration
     *
     * Creates a thread pool in the Constructed state. No threads are spawned
     * until Start() is called. The pool must transition through Init() before
     * calling Start().
     *
     * @param num_threads Number of worker threads to manage
     *        - If > 0: Exact number of threads created
     *        - If 0 (default): std::thread::hardware_concurrency() is used
     *
     * Uses default DeadlockConfig with:
     * - task_timeout: 5000 ms
     * - watchdog_interval: 1000 ms
     * - max_queue_size: 1000
     * - enable_detection: true
     *
     * ## Examples
     *
     * @code
     *   ThreadPool pool;               // Uses hardware_concurrency threads
     *   ThreadPool pool(8);             // Explicitly 8 threads
     *   ThreadPool pool(std::thread::hardware_concurrency() * 2);  // 2x cores
     * @endcode
     *
     * @post State is Constructed; no resources allocated beyond object itself
     * @note No throws; construction always succeeds
     * @see Init() for next step in lifecycle
     * @see ThreadPool(size_t, const DeadlockConfig&) for custom configuration
     */
    explicit ThreadPool(size_t num_threads = 0) noexcept;

    /**
     * @brief Construct a thread pool with custom deadlock detection configuration
     *
     * Creates a thread pool in the Constructed state with the specified
     * deadlock detection and queue settings. No threads are spawned until
     * Start() is called.
     *
     * @param num_threads Number of worker threads
     *        - If > 0: Exact number of threads created
     *        - If 0: std::thread::hardware_concurrency() is used
     *
     * @param config Custom deadlock detection and queue configuration
     *        Allows fine-tuning of task_timeout, watchdog_interval,
     *        max_queue_size, and deadlock detection enable flag
     *
     * ## Example: Short-Lived Tasks
     *
     * @code
     *   ThreadPool::DeadlockConfig cfg;
     *   cfg.task_timeout = std::chrono::milliseconds(500);     // 500 ms max
     *   cfg.watchdog_interval = std::chrono::milliseconds(50); // Fast detection
     *   cfg.max_queue_size = 10000;                            // Large burst buffer
     *   cfg.enable_detection = true;
     *
     *   ThreadPool pool(4, cfg);
     * @endcode
     *
     * ## Example: Long-Running Tasks
     *
     * @code
     *   ThreadPool::DeadlockConfig cfg;
     *   cfg.task_timeout = std::chrono::milliseconds(60000);  // 60 second max
     *   cfg.watchdog_interval = std::chrono::milliseconds(5000);  // Check every 5 sec
     *   cfg.enable_detection = true;
     *
     *   ThreadPool pool(2, cfg);  // Fewer threads for CPU-bound work
     * @endcode
     *
     * @post State is Constructed; no resources allocated beyond object itself
     * @note No throws; construction always succeeds
     * @see DeadlockConfig for configuration fields
     * @see Init() for next step in lifecycle
     */
    ThreadPool(size_t num_threads, const DeadlockConfig& config) noexcept;

    // C++26: Explicitly deleted copy/move semantics
    // ThreadPool owns worker threads and cannot be safely copied or moved
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Destructor with guaranteed cleanup
     *
     * The destructor ensures safe cleanup by:
     * 1. Requesting shutdown (if not already done)
     * 2. Blocking until all worker threads exit
     * 3. Blocking until watchdog thread exits (if created)
     * 4. Releasing all allocated resources
     *
     * After destruction, no threads remain joinable.
     *
     * @note **Exception Safety**: No-throw guarantee
     * Does not throw even if Join() or thread cleanup encounters errors
     *
     * ## Implicit Shutdown
     *
     * If the pool is destroyed while in Started state:
     * - Stop() is called automatically
     * - Pending tasks are discarded
     * - Active tasks are allowed to complete
     * - All threads are joined
     *
     * ## Usage
     *
     * Safe destruction patterns:
     * @code
     *   {
     *       ThreadPool pool(4);
     *       pool.Init();
     *       pool.Start();
     *       pool.QueueTask([]{ \/ \/ work });
     *       // Destructor automatically cleans up
     *   }
     *
     *   // Or explicitly:
     *   auto pool = std::make_unique<ThreadPool>(4);
     *   pool->Init();
     *   pool->Start();
     *   pool->QueueTask([]{ \/\/ work });
     *   pool->Stop();
     *   pool->Join();
     *   pool.reset();  // Destructor called here
     * @endcode
     *
     * @post All threads are terminated and joined
     * @post All queued tasks are discarded
     * @post All resources are released
     * @see Stop() for explicit shutdown request
     * @see Join() for waiting on shutdown
     */
    ~ThreadPool() noexcept;

    /**
     * @brief Initialize internal state (preparation phase)
     *
     * Prepares the pool for starting without spawning threads.
     * Called once after construction, before Start().
     *
     * ## What It Does
     * - Validates configuration
     * - Allocates internal structures (queue, condition variables)
     * - Reserves space for thread objects
     * - Does NOT spawn any threads
     *
     * ## When to Call
     * Must be called in the Constructed state, before Start().
     *
     * ## Valid State Transitions
     * Constructed → Init() → Initialized
     *
     * @return true if initialization succeeded
     *         false if already initialized or configuration invalid
     *
     * @pre State must be Constructed
     * @post State transitions to Initialized
     * @note Thread-safe: Only owning thread should call
     *
     * ## Example
     *
     * @code
     *   ThreadPool pool(4);
     *   if (!pool.Init()) {
     *       std::cerr << "Failed to init pool" << std::endl;
     *       return;
     *   }
     *   pool.Start();  // Now safe to start
     * @endcode
     *
     * @see Start() for spawning worker threads
     * @see ThreadPool constructor for options
     */
    [[nodiscard]] bool Init() noexcept;

    /**
     * @brief Start all worker threads and optional watchdog
     *
     * Spawns all configured worker threads and optionally the watchdog thread.
     * The pool transitions from Initialized to Started and begins accepting tasks.
     *
     * ## What It Does
     * 1. Marks pool as running (running_ = true)
     * 2. Spawns worker threads equal to num_threads_
     * 3. Each worker thread starts WorkerMain() loop
     * 4. Spawns watchdog thread if enable_detection=true
     * 5. Watchdog starts WatchdogMain() monitoring loop
     *
     * ## Thread Creation
     * - One thread per configured num_threads value
     * - Each thread independently dequeues and executes tasks
     * - Threads have independent stacks (~8-16 MB each)
     * - No shared stack between worker threads
     *
     * ## Valid State Transitions
     * Initialized → Start() → Started
     *
     * @return true if all threads were created successfully
     *         false if thread creation fails (rare OS error)
     *
     * @pre State must be Initialized (Init() completed successfully)
     * @post State transitions to Started
     * @post running_ = true, threads are executing
     * @note Thread-safe: Only owning thread should call
     *
     * ## Example
     *
     * @code
     *   ThreadPool pool(4);
     *   pool.Init();
     *   if (!pool.Start()) {
     *       std::cerr << "Failed to start threads" << std::endl;
     *       return;
     *   }
     *
     *   // Threads now running and ready for tasks
     *   pool.QueueTask([]{ std::cout << "Hello from thread" << std::endl; });
     *
     *   pool.Stop();
     *   pool.Join();
     * @endcode
     *
     * @see Init() for preparation phase
     * @see WorkerMain() for individual thread behavior
     * @see WatchdogMain() for deadlock detection
     */
    [[nodiscard]] bool Start() noexcept;

    /**
     * @brief Start thread pool with C++26 std::expected error handling (Phase 1 C++26 Modernization)
     *
     * Type-safe alternative to Start() that returns std::expected<void, ThreadPoolError>.
     * Replaces try-catch-return patterns with composable error types.
     *
     * ## Usage
     *
     * @code
     *   ThreadPool pool(4);
     *   pool.Init();
     *
     *   auto result = pool.StartExpected();
     *   if (!result) {
     *       switch (result.error()) {
     *           case ThreadPoolError::AlreadyRunning:
     *               LOG_WARN("Pool already started");
     *               break;
     *           case ThreadPoolError::ThreadCreationFailed:
     *               LOG_ERROR("Could not create worker threads");
     *               return false;
     *           case ThreadPoolError::AllocationFailed:
     *               LOG_ERROR("Memory allocation failed");
     *               return false;
     *           default:
     *               LOG_ERROR("Unexpected error starting pool");
     *               return false;
     *       }
     *   }
     *
     *   // Threads now running and ready for tasks
     *   pool.QueueTask([]{ std::cout << "Hello from thread" << std::endl; });
     *
     *   pool.Stop();
     *   pool.Join();
     * @endcode
     *
     * ## Comparison with Start()
     *
     * | Feature | Start() | StartExpected() |
     * |---------|---------|-----------------|
     * | Return type | bool | std::expected<void, ThreadPoolError> |
     * | Error info | None (bool only) | Detailed error codes |
     * | Exception handling | try-catch | Explicit error enum |
     * | C++ standard | C++17 | C++23+ (std::expected) |
     * | Backward compat | Yes | New pattern (Phase 1) |
     *
     * @return Success (void) or specific ThreadPoolError describing failure
     * @see Start() for backward-compatible bool-returning version
     * @see ThreadPoolError for error code meanings
     * @see app::error::Result for expected<> type alias
     */
    [[nodiscard]] std::expected<void, ThreadPoolError> StartExpected() noexcept;

    /**
     * @brief Request graceful shutdown (idempotent)
     *
     * Signals all threads to stop and prevents new tasks from starting.
     * Does NOT block waiting for threads to exit (use Join() for that).
     *
     * ## What It Does
     * 1. Sets stop_requested_ = true (atomic)
     * 2. Disables task queue (prevents new enqueues)
     * 3. Wakes all worker threads from queue.Dequeue() blocking
     * 4. Threads begin processing their shutdown loops
     * 5. Pending tasks are discarded without execution
     *
     * ## Synchronization Semantics
     * - Non-blocking: Returns immediately
     * - Idempotent: Safe to call multiple times
     * - Atomic: stop_requested_ flag update is atomic
     * - One-way: Cannot be "undone" (no resume/restart)
     *
     * ## After Stop()
     * - No new tasks will start (existing tasks continue)
     * - QueueTask() returns QueueResult::Stopped
     * - Worker threads wake up and check stop_requested_
     * - Threads exit gracefully after current task finishes
     *
     * ## Valid State Transitions
     * Started → Stop() → Stopping
     *
     * @pre Pool should be in Started state (can call at any time)
     * @post stop_requested_ = true
     * @post task_queue is disabled (Dequeue returns empty after queue drained)
     * @note Thread-safe: Can be called from any thread
     * @note Non-blocking: Returns immediately, does not wait
     *
     * ## Example: Graceful Shutdown
     *
     * @code
     *   pool.QueueTask([]{ std::cout << "Task 1" << std::endl; });
     *   pool.QueueTask([]{ std::cout << "Task 2" << std::endl; });
     *
     *   pool.Stop();  // No more tasks accepted, existing finish
     *
     *   // Attempt to queue task after Stop
     *   auto result = pool.QueueTask([]{ });
     *   assert(result == QueueResult::Stopped);  // Rejected
     *
     *   pool.Join();  // Block until threads exit
     * @endcode
     *
     * ## Shutdown Sequence
     *
     * ```
     * main thread          worker threads
     *     |
     *     ├─ Stop()
     *     |  (sets flag, wakes workers)
     *     |
     *     ├─ Join()
     *     |  (blocks here)
     *     |
     *     |              (wake from Dequeue)
     *     |              (check stop_requested)
     *     |              (exit loop)
     *     |              (decrement live_workers)
     *     |              (notify join_cv)
     *     |
     *     └─ (returns from Join)
     * ```
     *
     * @see Join() for blocking until completion
     * @see JoinWithTimeout() for bounded waiting
     * @see GetStopRequested() for query current state
     */
    void Stop() noexcept;

    /**
     * @brief Block until all worker and watchdog threads have exited
     *
     * Blocks the calling thread indefinitely until the pool reaches the Stopped state.
     * All active tasks are allowed to complete before threads exit.
     * Pending (queued) tasks are discarded without execution.
     *
     * ## Synchronization Model
     * - Uses condition variable (join_cv_) for efficient waiting
     * - Threads notify join_cv_ when they decrement live_workers_ to zero
     * - Main thread awakens and returns when count reaches zero
     *
     * ## Valid State Transitions
     * Stopping → Join() → Stopped
     *
     * @pre Stop() should have been called previously
     * @post All worker threads have exited
     * @post All pending tasks have been discarded
     * @post Pool reaches Stopped state
     * @note Thread-safe: Only owning thread should call
     * @note Blocking: Does not return until shutdown completes
     *
     * ## What Happens
     * 1. Call Stop() first (if not done) to request shutdown
     * 2. Call Join() - blocks on condition variable
     * 3. Workers notice stop_requested_ and finish cleanup
     * 4. Last worker decrements live_workers_ to zero
     * 5. Last worker notifies join_cv_
     * 6. Join() unblocks and returns
     *
     * ## Example
     *
     * @code
     *   ThreadPool pool(4);
     *   pool.Init();
     *   pool.Start();
     *
     *   // Queue lots of work
     *   for (int i = 0; i < 1000; ++i) {
     *       pool.QueueTask([i]{ do_work(i); });
     *   }
     *
     *   // Wait for completion
     *   pool.Stop();
     *   pool.Join();  // Blocks here until all tasks done
     *
     *   // Safe to destroy pool now
     * @endcode
     *
     * ## Comparison with JoinWithTimeout
     *
     * | Method | Blocks | Timeout | Use Case |
     * |--------|--------|---------|----------|
     * | Join() | Yes | Never | Guaranteed shutdown |
     * | JoinWithTimeout() | Yes | Yes | Timeout-sensitive apps |
     *
     * @see Stop() for requesting shutdown (must call first)
     * @see JoinWithTimeout() for bounded waiting
     * @see Destructor which calls Join() implicitly
     */
    void Join() noexcept;

    /**
     * @brief Block until shutdown completes or timeout expires
     *
     * Blocks the calling thread for at most the specified duration,
     * waiting for the pool to reach the Stopped state. Returns early
     * if all threads complete before timeout.
     *
     * ## Return Value Semantics
     *
     * @return true if all threads exited before timeout
     *         false if timeout expired while threads still running
     *
     * @param timeout Maximum duration to wait for shutdown
     *                Must be positive (behavior undefined if zero/negative)
     *
     * ## Usage Examples
     *
     * ### Graceful Timeout
     * @code
     *   pool.Stop();
     *
     *   if (pool.JoinWithTimeout(std::chrono::seconds(5))) {
     *       std::cout << "Shutdown complete" << std::endl;
     *   } else {
     *       std::cerr << "Timeout waiting for threads" << std::endl;
     *       // Threads still running after 5 seconds
     *       // Destructor will still Join() (blocks indefinitely)
     *   }
     * @endcode
     *
     * ### Retry Pattern
     * @code
     *   pool.Stop();
     *
     *   for (int attempt = 0; attempt < 3; ++attempt) {
     *       if (pool.JoinWithTimeout(std::chrono::seconds(10))) {
     *           std::cout << "Shutdown succeeded" << std::endl;
     *           break;
     *       }
     *       std::cout << "Attempt " << attempt << " timed out, retrying..." << std::endl;
     *   }
     * @endcode
     *
     * ## Implementation Detail
     *
     * Uses condition_variable::wait_until() with std::chrono::steady_clock
     * for accuracy. The timeout is measured from call time, not affected by
     * system clock adjustments.
     *
     * @pre Stop() should have been called previously
     * @post If returned true: all threads exited (same as Join())
     * @post If returned false: threads may still be running
     * @note Thread-safe: Only owning thread should call
     * @note Blocking: Blocks for up to timeout duration
     *
     * @see Join() for indefinite waiting
     * @see Stop() for requesting shutdown
     */
    [[nodiscard]] bool JoinWithTimeout(std::chrono::milliseconds timeout) noexcept;

    /**
     * @brief Queue a task for immediate (non-blocking) execution
     *
     * Attempts to add a task to the work queue without blocking.
     * Returns immediately with a result code indicating success or failure.
     *
     * ## Return Values
     *
     * **QueueResult::Ok**: Task successfully enqueued and will be executed.
     * The task is guaranteed to execute before pool destruction, subject to
     * active task completion (not pending task execution after Stop()).
     *
     * **QueueResult::Stopped**: Pool has been shut down (stop_requested_ = true).
     * Task rejected; it will never execute. Try again after pool restart (if supported).
     *
     * **QueueResult::Full**: Queue is at capacity (size >= max_queue_size).
     * Task rejected due to congestion. Consider:
     * - Use QueueTaskWithTimeout() to wait for capacity
     * - Increase max_queue_size in DeadlockConfig
     * - Increase number of worker threads
     *
     * **QueueResult::Error**: Unexpected internal error (rare).
     * Indicates serious issue with queue operation.
     *
     * **QueueResult::Timeout**: Unused by QueueTask(); always returns Ok/Stopped/Full/Error.
     *
     * ## Task Semantics
     *
     * Queued tasks:
     * - Execute in FIFO order (enqueue order preserved)
     * - Execute exactly once or never (if Stop() discards pending tasks)
     * - Execute from worker thread context (not main thread)
     * - Share no stack with other tasks (independent stacks)
     * - Cannot be cancelled once queued
     * - May throw exceptions (caught and logged)
     *
     * ## Thread Safety
     *
     * Fully thread-safe: Multiple threads may call QueueTask() concurrently.
     * No locks needed due to ActiveQueue's lock-free implementation.
     *
     * ## Statistics Update
     *
     * On success (Ok return):
     * - tasks_queued is incremented
     * - peak_queue_size may be updated
     * - Other stats unchanged until task executes
     *
     * On failure (any non-Ok return):
     * - tasks_rejected is incremented
     * - tasks_queued is NOT incremented
     * - Task never enters the work queue
     *
     * ## Example Usage
     *
     * @code
     *   ThreadPool pool(4);
     *   pool.Init();
     *   pool.Start();
     *
     *   // Simple task
     *   auto result = pool.QueueTask([]{ std::cout << "Hello\n"; });
     *   if (result == ThreadPool::QueueResult::Ok) {
     *       std::cout << "Task accepted\n";
     *   }
     *
     *   // Task with captures
     *   int counter = 0;
     *   auto result = pool.QueueTask([&counter]{
     *       counter++;  // WARNING: race condition without synchronization
     *   });
     *
     *   // Task with error handling
     *   auto result = pool.QueueTask([]{ 
     *       try {
     *           risky_operation();
     *       } catch (const std::exception& e) {
     *           std::cerr << "Error: " << e.what() << std::endl;
     *       }
     *   });
     * @endcode
     *
     * @param task Callable object with signature void()
     *             Can be lambda, std::function, function pointer, etc.
     *             Should not block indefinitely (triggers watchdog)
     *
     * @return QueueResult::Ok on success
     *         QueueResult::Stopped if shutdown requested
     *         QueueResult::Full if queue at capacity
     *         QueueResult::Error on internal error
     *
     * @post tasks_queued is incremented (if Ok returned)
     * @post Task will execute asynchronously from worker thread
     * @note Thread-safe: Safe to call from multiple threads
     * @note Non-blocking: Returns immediately
     * @note Copy-semantic: Task is copied into queue storage
     *
     * @see QueueTaskWithTimeout() for blocking with timeout
     * @see GetQueueDepth() for current queue size
     * @see Stop() for shutdown semantics
     * @see QueueResult enum for all possible return values
     */
    [[nodiscard]] QueueResult QueueTask(Task task) noexcept;

    /**
     * @brief Queue a task with bounded waiting for queue capacity
     *
     * Attempts to add a task to the work queue, waiting if necessary for
     * capacity to become available. Blocks for at most the specified timeout
     * duration. If capacity becomes available within the timeout, task is queued.
     * If timeout expires, task is rejected.
     *
     * ## Return Values
     *
     * **QueueResult::Ok**: Task successfully enqueued. Returned immediately
     * when queue has capacity (didn't wait) or capacity became available during wait.
     *
     * **QueueResult::Timeout**: Timeout expired before queue capacity available.
     * Task was not queued. Application may:
     * - Retry the call
     * - Drop the task
     * - Use shorter timeout
     * - Increase max_queue_size
     *
     * **QueueResult::Stopped**: Pool shutting down (stop_requested_ = true).
     * Task rejected; will never execute. Returned immediately regardless of timeout.
     *
     * **QueueResult::Full**: Would exceed max_queue_size even after timeout.
     * (Rare; usually Timeout is returned first.)
     *
     * **QueueResult::Error**: Unexpected internal error.
     *
     * ## Timing Behavior
     *
     * The timeout is measured from the call time using steady_clock,
     * ensuring accuracy regardless of system clock adjustments.
     *
     * The timeout is for the **waiting period only**. The timeout does NOT:
     * - Include time spent in task execution (that happens later)
     * - Include time spent queueing the task (negligible, lock-free)
     * - Limit task execution duration (use deadlock detection for that)
     *
     * ## Blocking Semantics
     *
     * This call blocks the **caller's thread** (not a worker thread) while
     * waiting for queue capacity. If called from the main thread:
     * - Main thread blocks in this method
     * - Worker threads continue executing tasks
     * - When capacity available or timeout expires, main thread unblocks
     *
     * ## Comparison with QueueTask
     *
     * | Method | Blocks | When to Use |
     * |--------|--------|------------|
     * | QueueTask() | No | Best-effort, task not critical |
     * | QueueTaskWithTimeout() | Yes, up to timeout | Guaranteed delivery or error |
     *
     * ## Example: Guaranteed Task Delivery
     *
     * @code
     *   // Queue a critical task, waiting if necessary
     *   auto result = pool.QueueTaskWithTimeout(critical_task, std::chrono::seconds(5));
     *
     *   if (result == ThreadPool::QueueResult::Ok) {
     *       std::cout << "Critical task accepted" << std::endl;
     *   } else if (result == ThreadPool::QueueResult::Timeout) {
     *       std::cerr << "Queue congestion: task not accepted within 5 sec" << std::endl;
     *       // Maybe increase max_queue_size or num_threads
     *   } else if (result == ThreadPool::QueueResult::Stopped) {
     *       std::cerr << "Pool shutting down" << std::endl;
     *   }
     * @endcode
     *
     * ## Example: Adaptive Backoff
     *
     * @code
     *   auto backoff = std::chrono::milliseconds(10);
     *   const auto max_backoff = std::chrono::seconds(1);
     *
     *   while (true) {
     *       auto result = pool.QueueTaskWithTimeout(task, backoff);
     *
     *       if (result == ThreadPool::QueueResult::Ok) {
     *           break;  // Success
     *       } else if (result == ThreadPool::QueueResult::Timeout) {
     *           // Exponential backoff on timeout
     *           backoff = std::min(backoff * 2, max_backoff);
     *           std::this_thread::sleep_for(backoff);
     *       } else {
     *           // Stopped or error - give up
     *           break;
     *       }
     *   }
     * @endcode
     *
     * @param task Callable object with signature void()
     * @param timeout Maximum time to wait for queue capacity
     *                Recommended: 100ms - 1 second for most applications
     *                Minimum practical: ~10ms (system scheduling granularity)
     *
     * @return QueueResult::Ok on successful enqueue
     *         QueueResult::Timeout if timeout expired before capacity
     *         QueueResult::Stopped if pool shutting down (returned immediately)
     *         QueueResult::Full/Error for other errors
     *
     * @pre Pool should be in Started state
     * @post If Ok returned: tasks_queued incremented, task will execute
     * @post If not Ok: tasks_rejected incremented, task will NOT execute
     * @note Thread-safe: Safe to call from multiple threads
     * @note Blocking: Blocks caller for up to timeout duration
     * @note Copy-semantic: Task is copied into queue storage
     *
     * @see QueueTask() for non-blocking variant
     * @see DeadlockConfig::max_queue_size for queue capacity
     * @see Stop() for shutdown detection (returns immediately)
     */
    [[nodiscard]] QueueResult QueueTaskWithTimeout(Task task,
                                    std::chrono::milliseconds timeout) noexcept;

    /**
     * @brief Get current number of pending (queued but not yet executed) tasks
     *
     * Returns the number of tasks in the work queue at the time of the call.
     * This is a snapshot value; the depth may change immediately after return
     * as worker threads dequeue tasks.
     *
     * ## Semantics
     *
     * Queue depth = tasks queued - tasks completed
     * - Does NOT include actively executing tasks
     * - Does NOT include tasks rejected before queuing
     * - Updated atomically by worker threads
     * - Not protected by locks (snapshot at call time)
     *
     * ## Use Cases
     *
     * 1. **Congestion detection**: Monitor if queue is growing faster than consumed
     * 2. **Capacity planning**: Track peak_queue_size for max_queue_size tuning
     * 3. **Diagnostics**: Debug slow task execution or insufficient workers
     * 4. **Adaptive backoff**: Adjust submission rate based on queue depth
     *
     * ## Example: Adaptive Submission
     *
     * @code
     *   // Submit more work if queue is not too deep
     *   if (pool.GetQueueDepth() < 500) {
     *       pool.QueueTask(new_work);
     *   } else {
     *       // Queue congestion; wait before submitting
     *       std::this_thread::sleep_for(std::chrono::milliseconds(100));
     *   }
     * @endcode
     *
     * @return Current number of pending tasks (0 if queue empty)
     *
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately with snapshot value
     * @note Snapshot semantics: Value may change immediately after return
     *
     * @see GetStats() for peak_queue_size historical maximum
     * @see DeadlockConfig::max_queue_size for queue capacity limit
     */
    [[nodiscard]] size_t GetQueueDepth() const noexcept;

    /**
     * @brief Get number of worker threads managed by this pool
     *
     * Returns the configured thread count; this never changes after construction.
     * The actual number of running threads may be less during initialization,
     * and zero after all threads have exited during shutdown.
     *
     * ## Semantics
     *
     * - Fixed at construction time (no dynamic resizing)
     * - Does NOT change between Start() and Join()
     * - Returns the configured value, not the actual running count
     * - Does NOT include watchdog thread in the count
     *
     * ## Use Cases
     *
     * 1. **Thread pool sizing**: Verify configuration matches CPU cores
     * 2. **Monitoring**: Log pool characteristics for diagnostics
     * 3. **Capacity planning**: Estimate throughput based on thread count
     * 4. **Migration**: Transfer work between pools based on thread count
     *
     * ## Example: Hardware-Aware Configuration
     *
     * @code
     *   auto physical_cores = std::thread::hardware_concurrency();
     *   ThreadPool io_pool(physical_cores);      // I/O-bound
     *   ThreadPool cpu_pool(physical_cores);     // CPU-bound
     *
     *   std::cout << "I/O pool: " << io_pool.GetThreadCount() << " threads\n";
     *   std::cout << "CPU pool: " << cpu_pool.GetThreadCount() << " threads\n";
     * @endcode
     *
     * @return Configured number of worker threads
     *         Note: Not the same as active/running thread count
     *
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately
     * @note Constant: Always returns the same value for a given pool
     *
     * @see ThreadPool constructors for thread count specification
     * @see GetStats() for task execution statistics
     */
    [[nodiscard]] size_t GetThreadCount() const noexcept;

    /**
     * @brief Access complete execution statistics snapshot
     *
     * Returns a const reference to the ThreadPoolStats structure containing
     * all observable metrics. Each field is individually atomic and safe to
     * read concurrently, but the reference as a whole may represent a
     * partially-updated snapshot if called while stats are being updated.
     *
     * ## Available Metrics
     *
     * - **tasks_queued**: Total successfully enqueued
     * - **tasks_completed**: Successfully executed
     * - **tasks_failed**: Threw exceptions
     * - **tasks_rejected**: Rejected at enqueue
     * - **peak_queue_size**: Maximum observed queue depth
     * - **deadlock_detections**: Watchdog timeout events
     * - **enqueue_timeouts**: QueueTaskWithTimeout() timeouts
     * - **total_task_time_ns**: Cumulative execution time
     *
     * ## Snapshot Semantics
     *
     * Each individual atomic field is read consistently, but the structure
     * as a whole may represent a partially-updated state if workers are
     * actively updating stats during the call.
     *
     * For precise snapshots, consider:
     * 1. Calling Stop() to halt new task execution
     * 2. Waiting a brief period for ongoing tasks to complete
     * 3. Then reading GetStats()
     *
     * ## Usage Examples
     *
     * ### Simple Stats Dump
     * @code
     *   auto& stats = pool.GetStats();
     *   std::cout << "Completed: " << stats.tasks_completed << "\n";
     *   std::cout << "Failed: " << stats.tasks_failed << "\n";
     *   std::cout << "Rejected: " << stats.tasks_rejected << "\n";
     *   std::cout << "Peak queue: " << stats.peak_queue_size << "\n";
     * @endcode
     *
     * ### Health Check
     * @code
     *   auto& stats = pool.GetStats();
     *   if (stats.tasks_failed > 0) {
     *       std::cerr << "Some tasks failed!" << std::endl;
     *   }
     *   if (stats.deadlock_detections > 0) {
     *       std::cerr << "Potential deadlock detected" << std::endl;
     *   }
     *   if (stats.peak_queue_size > config.max_queue_size * 0.9) {
     *       std::cerr << "Queue capacity at risk" << std::endl;
     *   }
     * @endcode
     *
     * ### Performance Monitoring
     * @code
     *   auto& stats = pool.GetStats();
     *   double avg_time_ms = pool.GetAverageTaskTimeMs();
     *   double throughput = stats.tasks_completed / elapsed_seconds;
     *   std::cout << "Avg task time: " << avg_time_ms << " ms\n";
     *   std::cout << "Throughput: " << throughput << " tasks/sec\n";
     * @endcode
     *
     * @return Const reference to ThreadPoolStats struct
     *         Valid for lifetime of pool; pointer remains same
     *
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately
     * @note Snapshot: Values may change immediately after call
     * @note Atomic fields: Each field individually consistent
     *
     * @see ThreadPoolStats for individual field descriptions
     * @see GetAverageTaskTimeMs() for derived metric
     * @see IsDeadlockDetected() for advisory status
     */
    [[nodiscard]] const ThreadPoolStats& GetStats() const noexcept;

    /**
     * @brief Get average task execution time in milliseconds
     *
     * Calculates average execution time per task by dividing cumulative
     * task execution time by the number of completed tasks.
     *
     * **Formula**: total_task_time_ns / tasks_completed / 1,000,000
     *
     * ## Return Value
     *
     * - **>0.0**: Average time in milliseconds (typical case)
     * - **0.0**: No tasks have completed yet (divide-by-zero protection)
     * - **Very small**: Tasks complete in < 1 microsecond (rare)
     *
     * ## Interpretation
     *
     * | Result | Meaning |
     * |--------|---------|
     * | 0.1 ms | Very fast tasks (network acks, cache hits) |
     * | 1-10 ms | Typical I/O operations |
     * | 10-100 ms | Longer I/O, light computation |
     * | 100+ ms | Heavy computation or blocking I/O |
     *
     * Excessively long average times may indicate:
     * - Tasks blocking on locks/I/O
     * - Insufficient worker threads (queue stalling)
     * - Deadlock/stalled tasks (check deadlock_detections)
     * - Task granularity too coarse
     *
     * ## Example: Detecting Performance Regression
     *
     * @code
     *   double baseline_avg = 5.0;  // Expected avg task time
     *   double current_avg = pool.GetAverageTaskTimeMs();
     *   double degradation = (current_avg - baseline_avg) / baseline_avg * 100;
     *
     *   if (degradation > 50.0) {  // 50% slower
     *       std::cerr << "Performance degradation detected: " 
     *                  << degradation << "%" << std::endl;
     *       std::cerr << "Check for system load or task complexity increase" 
     *                  << std::endl;
     *   }
     * @endcode
     *
     * ## Precision Note
     *
     * Measured in nanoseconds internally, converted to milliseconds.
     * Precision is approximately 1 microsecond due to steady_clock resolution
     * and typical OS timer granularity.
     *
     * @return Average task time in milliseconds
     *         Returns 0.0 if no tasks completed (prevents division by zero)
     *
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately with calculation
     * @note Derived metric: Calculated from total_task_time_ns and tasks_completed
     *
     * @see ThreadPoolStats::total_task_time_ns for raw nanosecond count
     * @see ThreadPoolStats::tasks_completed for total count
     * @see GetStats() for complete statistics
     */
    [[nodiscard]] double GetAverageTaskTimeMs() const noexcept;

    /**
     * @brief Check if the watchdog detected a potential deadlock condition
     *
     * Returns true if the watchdog thread observed any task exceeding
     * the configured task_timeout duration. This is an **advisory flag**
     * indicating potential issues, not a guarantee that a true deadlock occurred.
     *
     * ## Semantics
     *
     * - Set to true when watchdog detects task_timeout exceeded
     * - Remains true until explicitly cleared via ClearDeadlockFlag()
     * - Never automatically cleared (advisory, non-intrusive)
     * - Only meaningful if enable_detection=true in DeadlockConfig
     *
     * ## Important Caveats
     *
     * **NOT a deadlock**: The name is slightly misleading. A deadlock in the
     * strict OS sense requires circular wait conditions. This flag indicates
     * **long task execution**, which may be intentional (background work,
     * heavy I/O, legitimate computation).
     *
     * **False positives**: High system load, GC pauses, or task bursts can
     * cause transient timeouts that don't indicate real problems.
     *
     * **Not forcibly stopped**: Detecting a timeout does NOT interrupt the task.
     * The task continues running. This is intentional to avoid partial resource
     * cleanup and data corruption.
     *
     * ## When to Investigate
     *
     * - High deadlock_detections count in GetStats()
     * - Performance degradation coinciding with detections
     * - Consistent timeouts (not transient)
     * - Correlation with system overload
     *
     * ## Recovery Strategies
     *
     * 1. **Increase task_timeout**: If tasks legitimately take longer
     * 2. **Add worker threads**: If queue is growing (task starvation)
     * 3. **Reduce task granularity**: Break long tasks into smaller chunks
     * 4. **Optimize tasks**: Profile to identify blocking operations
     * 5. **Scale hardware**: If CPU-bound, add cores; if I/O-bound, add bandwidth
     *
     * ## Example: Monitoring with Alerts
     *
     * @code
     *   // In monitoring loop
     *   if (pool.IsDeadlockDetected()) {
     *       auto& stats = pool.GetStats();
     *       std::cerr << "Deadlock flag set! Detections: " 
     *                  << stats.deadlock_detections << std::endl;
     *
     *       // Optional: Increase timeout and retry
     *       auto cfg = pool.GetConfig();
     *       cfg.task_timeout *= 2;
     *       pool.SetConfig(cfg);
     *       pool.ClearDeadlockFlag();
     *
     *       std::cout << "Configuration updated, retrying..." << std::endl;
     *   }
     * @endcode
     *
     * @return true if at least one task timeout was detected
     *         false if no timeouts detected since last clear
     *
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately
     * @note Advisory: Not an error condition, just an alert
     * @note Non-intrusive: Does not interrupt or affect task execution
     *
     * @see ClearDeadlockFlag() to reset the flag
     * @see DeadlockConfig::task_timeout for the threshold
     * @see DeadlockConfig::watchdog_interval for check frequency
     * @see ThreadPoolStats::deadlock_detections for detection count
     */
    [[nodiscard]] bool IsDeadlockDetected() const noexcept;

    /**
     * @brief Clear the deadlock detection advisory flag
     *
     * Resets deadlock_detected_ to false after investigation or recovery.
     * This is a manual reset operation; the flag does NOT automatically clear.
     *
     * ## Usage Model
     *
     * Intended workflow:
     * 1. Monitoring code checks IsDeadlockDetected() and finds it true
     * 2. Operator/script investigates the condition
     * 3. Implements recovery (increase timeout, add threads, optimize tasks)
     * 4. Calls ClearDeadlockFlag() to reset for next monitoring period
     * 5. Continues operation with improved configuration
     *
     * ## When to Call
     *
     * - After adjusting DeadlockConfig (timeouts, intervals)
     * - After identifying that detections were false positives
     * - After optimization that addresses the root cause
     * - Before restarting a pool instance
     * - Periodically in long-running applications (e.g., every hour)
     *
     * ## Does NOT Affect
     *
     * - Ongoing task execution
     * - Watchdog thread operation
     * - Worker thread behavior
     * - Statistics (deadlock_detections count remains unchanged)
     *
     * ## Example: Periodic Health Check
     *
     * @code
     *   std::thread monitor_thread([&pool] {
     *       while (!shutdown_requested) {
     *           std::this_thread::sleep_for(std::chrono::minutes(1));
     *
     *           if (pool.IsDeadlockDetected()) {
     *               auto& stats = pool.GetStats();
     *               log_alert("Deadlock detected", stats.deadlock_detections);
     *
     *               // Implement recovery
     *               auto cfg = pool.GetConfig();
     *               cfg.task_timeout = std::chrono::milliseconds(30000);  // 30s
     *               pool.SetConfig(cfg);
     *
     *               // Reset flag for next monitoring period
     *               pool.ClearDeadlockFlag();
     *           }
     *       }
     *   });
     * @endcode
     *
     * @post deadlock_detected_ = false
     * @note Thread-safe: May be called from any thread
     * @note Non-blocking: Returns immediately
     * @note Manual operation: Does NOT automatically reset on its own
     *
     * @see IsDeadlockDetected() for status check
     * @see SetConfig() for updating timeout values
     * @see GetStats() for detection statistics
     */
    void ClearDeadlockFlag() noexcept;

    /**
     * @brief Access current configuration
     *
     * @note The deadlock detection and queue management operates in a lock-free
     * manner where possible. During shutdown, the watchdog thread performs
     * non-blocking queue flushing without mutex acquisition to avoid deadlock
     * if worker threads are stuck. This may cause benign races on queue.Size(),
     * but the FIFO discipline ensures correctness.
     */
    [[nodiscard]] const DeadlockConfig& GetConfig() const noexcept {
        return config_;
    }

    /**
     * @brief Update configuration at runtime
     *
     * Changes apply immediately to watchdog behavior.
     */
    void SetConfig(const DeadlockConfig& config) noexcept {
        config_ = config;   
    }

    [[nodiscard]] bool GetStopRequested() const noexcept {
        return stop_requested_.load();
    }   

void SetStopRequested(bool value) noexcept {
        stop_requested_.store(value);
    }   

private:
    /// Worker thread main loop
    void WorkerMain();

    /// Watchdog thread main loop
    void WatchdogMain();

    /// Execute a single task and update statistics
    bool ExecuteTask(const Task& task);

    // (private data members follow)

     // Configuration
    const size_t num_threads_;
    DeadlockConfig config_;
    static log4cxx::LoggerPtr logger_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> deadlock_detected_{false};

    // Work queue - using ActiveQueue for thread-safe operations
    core::ActiveQueue<Task> task_queue_;

    // Threads
    std::vector<std::thread> worker_threads_;
    std::thread watchdog_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> started_{false};

    // Statistics
    ThreadPoolStats stats_;

    // Task tracking for deadlock detection
    std::atomic<size_t> active_tasks_{0};
    std::atomic<uint64_t> last_task_start_ns_;
    std::mutex join_mtx_;
    std::condition_variable join_cv_;
    std::atomic<size_t> live_workers_{0};
};

} // namespace graph
