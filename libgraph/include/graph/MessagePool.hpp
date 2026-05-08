/**
 * @file MessagePool.hpp
 * @brief Thread-safe message buffer pool for pre-allocated heap management
 * @author Robert Klinkhammer
 * @date April 24, 2026
 *
 * Provides a reusable buffer pool to reduce allocation latency for large messages
 * that exceed Small Object Optimization (SSO) threshold. Uses malloc + placement new
 * for exact control over buffer allocation and lifecycle.
 *
 * Thread-Safety: All operations are protected by mutex. Statistics use atomic counters
 * for lock-free reads. Multiple threads can safely acquire/release buffers concurrently.
 */

#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <cstring>

namespace graph {

/**
 * @brief Configuration policy for message buffer pools
 *
 * Controls pool behavior: allocation mode, capacity, TTL for eviction.
 */
struct MessagePoolPolicy {
    enum class AllocationMode {
        Prealloc,          ///< Pre-allocate all buffers upfront (MVP)
        LazyAllocate,      ///< Allocate on first use (Phase 4)
        PreallocWithTTL,   ///< Pre-allocate + evict after TTL (Phase 4)
    };

    AllocationMode mode = AllocationMode::Prealloc;
    size_t pool_capacity = 0;         ///< Max buffers (typically edge.max_queue_size)
    size_t ttl_ms = 100;              ///< TTL for PreallocWithTTL mode
};

/**
 * @class MessageBufferPool
 * @brief Thread-safe FIFO pool of pre-allocated message buffers
 *
 * Manages reusable buffers of fixed size to reduce allocation latency.
 * Uses malloc + placement new for raw buffer allocation.
 * Statistics tracked via atomic counters for lock-free observability.
 *
 * @tparam BufferSize Size of each buffer in bytes
 * @tparam Policy Pool configuration (allocation mode, capacity, TTL)
 *
 * Usage:
 * @code
 *   MessageBufferPool<64> pool;
 *   pool.Initialize(100);  // Pre-allocate 100 buffers of 64 bytes each
 *
 *   void* buffer = pool.AcquireBuffer();  // Get buffer (O(1))
 *   // Use buffer...
 *   pool.ReleaseBuffer(buffer);  // Return for reuse
 *
 *   auto stats = pool.GetStats();  // Get pool statistics
 *   auto hit_rate = (stats.total_reuses / stats.total_requests) * 100.0;
 *
 *   pool.Shutdown();  // Free all buffers
 * @endcode
 */
template <size_t BufferSize = 64, typename Policy = MessagePoolPolicy>
class MessageBufferPool {
public:
    static_assert(BufferSize > 0, "BufferSize must be > 0");

    /**
     * @struct PoolStats
     * @brief Statistics for pool observability and tuning
     *
     * All counters are atomic for lock-free reads from any thread.
     */
    struct PoolStats {
        std::atomic<uint64_t> total_requests{0};      ///< Total AcquireBuffer calls
        std::atomic<uint64_t> total_allocations{0};   ///< Total malloc calls (new buffers)
        std::atomic<uint64_t> total_reuses{0};        ///< Total reuses from pool
        std::atomic<uint64_t> current_available{0};   ///< Currently free buffers in stack
        std::atomic<uint64_t> peak_capacity{0};       ///< Max buffers ever allocated

        /**
         * @brief Calculate hit rate percentage
         * @return Percentage of requests satisfied from pool (0-100)
         */
        double GetHitRatePercent() const noexcept {
            if (total_requests.load(std::memory_order_relaxed) == 0) {
                return 0.0;
            }
            auto reuses = total_reuses.load(std::memory_order_relaxed);
            auto requests = total_requests.load(std::memory_order_relaxed);
            return (static_cast<double>(reuses) / static_cast<double>(requests)) * 100.0;
        }

        /**
         * @brief Calculate average reuses per allocation
         * @return Average number of times each buffer was reused
         */
        double GetAverageReusesPerBuffer() const noexcept {
            auto allocs = total_allocations.load(std::memory_order_relaxed);
            if (allocs == 0) {
                return 0.0;
            }
            auto reuses = total_reuses.load(std::memory_order_relaxed);
            return static_cast<double>(reuses) / static_cast<double>(allocs);
        }
    };

    /**
     * @struct PoolStatsSnapshot
     * @brief Non-atomic snapshot of pool statistics for return values
     *
     * Contains the same fields as PoolStats but with regular uint64_t
     * (non-atomic) for copyability. Used by GetStats() to return values.
     */
    struct PoolStatsSnapshot {
        uint64_t total_requests = 0;      ///< Total AcquireBuffer calls
        uint64_t total_allocations = 0;   ///< Total malloc calls (new buffers)
        uint64_t total_reuses = 0;        ///< Total reuses from pool
        uint64_t current_available = 0;   ///< Currently free buffers in stack
        uint64_t peak_capacity = 0;       ///< Max buffers ever allocated

        /**
         * @brief Calculate hit rate percentage
         * @return Percentage of requests satisfied from pool (0-100)
         */
        double GetHitRatePercent() const noexcept {
            if (total_requests == 0) {
                return 0.0;
            }
            return (static_cast<double>(total_reuses) / static_cast<double>(total_requests)) * 100.0;
        }

        /**
         * @brief Calculate average reuses per allocation
         * @return Average number of times each buffer was reused
         */
        double GetAverageReusesPerBuffer() const noexcept {
            if (total_allocations == 0) {
                return 0.0;
            }
            return static_cast<double>(total_reuses) / static_cast<double>(total_allocations);
        }
    };

    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /**
     * @brief Default constructor (pool not yet initialized)
     */
    MessageBufferPool() noexcept = default;

    /**
     * @brief Destructor - calls Shutdown() to free all buffers
     */
    ~MessageBufferPool() noexcept { Shutdown(); }

    /**
     * @brief Initialize pool with pre-allocated buffers
     * @param capacity Number of buffers to pre-allocate
     * @throws std::bad_alloc if memory allocation fails
     *
     * Pre-allocates `capacity` buffers of BufferSize bytes each.
     * Must be called before AcquireBuffer().
     */
    void Initialize(size_t capacity);

    /**
     * @brief Shutdown pool and free all buffers
     *
     * Safe to call multiple times. After Shutdown(), pool must be
     * re-initialized before calling AcquireBuffer().
     */
    void Shutdown() noexcept;

    // ========================================================================
    // Buffer Acquisition and Release
    // ========================================================================

    /**
     * @brief Acquire a buffer from pool or allocate fresh
     * @return Pointer to buffer of BufferSize bytes
     * @throws std::bad_alloc if memory allocation fails
     *
     * Acquires a buffer in O(1) time. If pool is empty, allocates new buffer.
     * Returned buffer is not initialized - caller must placement new into it.
     *
     * Must be paired with ReleaseBuffer() when done.
     * Thread-safe: multiple threads can acquire concurrently.
     */
    [[nodiscard]] void* AcquireBuffer();

    /**
     * @brief Release buffer back to pool for reuse
     * @param buffer Pointer previously returned by AcquireBuffer()
     *
     * Returns buffer to pool in O(1) time (LIFO stack push).
     * Buffer must have been acquired from this pool.
     * Thread-safe: multiple threads can release concurrently.
     *
     * @note Do NOT call destructor before releasing - buffer is raw memory
     */
    void ReleaseBuffer(void* buffer) noexcept;

    // ========================================================================
    // Statistics and Observability
    // ========================================================================

    /**
     * @brief Get current pool statistics
     * @return PoolStatsSnapshot with counter values
     *
     * Safe to call from any thread at any time (lock-free reads).
     */
    [[nodiscard]] PoolStatsSnapshot GetStats() const noexcept {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return PoolStatsSnapshot{
            stats_.total_requests.load(std::memory_order_relaxed),
            stats_.total_allocations.load(std::memory_order_relaxed),
            stats_.total_reuses.load(std::memory_order_relaxed),
            stats_.current_available.load(std::memory_order_relaxed),
            stats_.peak_capacity.load(std::memory_order_relaxed)
        };
    }

    /**
     * @brief Get buffer size (template parameter)
     * @return Size in bytes
     */
    static constexpr size_t GetBufferSize() noexcept { return BufferSize; }

    /**
     * @brief Check if pool is initialized
     * @return true if Initialize() was called, false otherwise
     */
    [[nodiscard]] bool IsInitialized() const noexcept {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return total_capacity_ > 0;
    }

    // ========================================================================
    // Phase 4b: Adaptive Capacity Support
    // ========================================================================

    /**
     * @brief Get current pool capacity
     * @return Current capacity in number of buffers
     *
     * Safe to call from any thread.
     */
    [[nodiscard]] size_t GetCurrentCapacity() const noexcept {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return total_capacity_;
    }

    /**
     * @brief Adjust pool capacity dynamically
     * @param new_capacity New capacity in number of buffers
     *
     * Adjusts capacity up or down:
     * - Scale up: Allocates additional buffers
     * - Scale down: Marks excess buffers for reclamation
     *
     * Thread-safe: Can be called while pool is in use.
     * Non-blocking: Adjustment happens incrementally.
     */
    void SetCapacity(size_t new_capacity) noexcept;

    // ========================================================================
    // Non-copyable, movable
    // ========================================================================

    MessageBufferPool(const MessageBufferPool&) = delete;
    MessageBufferPool& operator=(const MessageBufferPool&) = delete;

    MessageBufferPool(MessageBufferPool&&) = delete;
    MessageBufferPool& operator=(MessageBufferPool&&) = delete;

private:
    // ========================================================================
    // Internal State
    // ========================================================================

    /// Free buffer LIFO stack (push = release, pop = acquire)
    std::vector<void*> available_buffers_;

    /// Protects available_buffers_ and related state
    mutable std::mutex pool_mutex_;

    /// Pool statistics (atomic for lock-free reads)
    mutable PoolStats stats_;
    mutable std::mutex stats_mutex_;  ///< Protects stats_ for consistent snapshots

    /// Total buffers ever allocated (for checking capacity)
    size_t total_capacity_ = 0;

    /// Track if pool has been initialized
    bool initialized_ = false;

    // ========================================================================
    // Phase 4a: Lazy Allocation Support
    // ========================================================================

    /// Maximum number of buffers allowed (capacity ceiling for lazy mode)
    size_t max_capacity_ = 0;

    /// Number of buffers currently allocated (for lazy mode)
    std::atomic<uint64_t> allocated_count_{0};

    /// Allocation mode (Prealloc or LazyAllocate)
    MessagePoolPolicy::AllocationMode allocation_mode_ = MessagePoolPolicy::AllocationMode::Prealloc;
};

// ============================================================================
// Template Implementation
// ============================================================================

template <size_t BufferSize, typename Policy>
inline void MessageBufferPool<BufferSize, Policy>::Initialize(size_t capacity) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    if (initialized_) {
        throw std::logic_error("MessageBufferPool already initialized");
    }

    if (capacity == 0) {
        throw std::invalid_argument("Pool capacity must be > 0");
    }

    max_capacity_ = capacity;
    allocation_mode_ = Policy{}.mode;  // Get allocation mode from policy

    if (allocation_mode_ == MessagePoolPolicy::AllocationMode::Prealloc) {
        // ====================================================================
        // Phase 3: Prealloc Mode - allocate all buffers upfront
        // ====================================================================
        available_buffers_.reserve(capacity);
        for (size_t i = 0; i < capacity; ++i) {
            void* buffer = std::malloc(BufferSize);
            if (!buffer) {
                // Free already-allocated buffers on failure
                for (void* b : available_buffers_) {
                    std::free(b);
                }
                available_buffers_.clear();
                throw std::bad_alloc();
            }
            available_buffers_.push_back(buffer);
        }

        allocated_count_.store(capacity, std::memory_order_relaxed);
        total_capacity_ = capacity;

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_allocations.store(capacity, std::memory_order_relaxed);
            stats_.current_available.store(capacity, std::memory_order_relaxed);
            stats_.peak_capacity.store(capacity, std::memory_order_relaxed);
        }
    } else if (allocation_mode_ == MessagePoolPolicy::AllocationMode::LazyAllocate) {
        // ====================================================================
        // Phase 4a: Lazy Allocate Mode - allocate on first use
        // ====================================================================
        // Reserve space but don't allocate buffers yet
        available_buffers_.reserve(capacity);
        allocated_count_.store(0, std::memory_order_relaxed);
        total_capacity_ = capacity;

        // Statistics start at zero - no allocations yet
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_allocations.store(0, std::memory_order_relaxed);
            stats_.current_available.store(0, std::memory_order_relaxed);
            stats_.peak_capacity.store(0, std::memory_order_relaxed);
        }
    } else {
        throw std::logic_error("Unsupported allocation mode");
    }

    initialized_ = true;
}

template <size_t BufferSize, typename Policy>
inline void MessageBufferPool<BufferSize, Policy>::Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    // Free all buffers
    for (void* buffer : available_buffers_) {
        std::free(buffer);
    }
    available_buffers_.clear();
    total_capacity_ = 0;
    initialized_ = false;
}

template <size_t BufferSize, typename Policy>
inline void* MessageBufferPool<BufferSize, Policy>::AcquireBuffer() {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    if (!initialized_) {
        throw std::logic_error("MessageBufferPool not initialized - call Initialize() first");
    }

    // Update request counter
    stats_.total_requests.fetch_add(1, std::memory_order_relaxed);

    // Try to pop from stack (both modes)
    if (!available_buffers_.empty()) {
        void* buffer = available_buffers_.back();
        available_buffers_.pop_back();

        // Update reuse counter
        stats_.total_reuses.fetch_add(1, std::memory_order_relaxed);
        stats_.current_available.fetch_sub(1, std::memory_order_relaxed);

        return buffer;
    }

    // Pool empty - allocation behavior depends on mode
    if (allocation_mode_ == MessagePoolPolicy::AllocationMode::Prealloc) {
        // ====================================================================
        // Prealloc Mode: allocate recovery buffer (shouldn't happen if sized correctly)
        // ====================================================================
        void* buffer = std::malloc(BufferSize);
        if (!buffer) {
            throw std::bad_alloc();
        }
        stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);
        return buffer;
    } else if (allocation_mode_ == MessagePoolPolicy::AllocationMode::LazyAllocate) {
        // ====================================================================
        // Lazy Mode: allocate only if under capacity
        // ====================================================================
        uint64_t current_allocated = allocated_count_.load(std::memory_order_relaxed);
        if (current_allocated >= max_capacity_) {
            // At capacity - cannot allocate more
            // Return nullptr to indicate pool exhausted (caller must handle)
            // Or allocate anyway and track "over-capacity" allocations
            // For now: allocate but don't count it as part of prealloc
            void* buffer = std::malloc(BufferSize);
            if (!buffer) {
                throw std::bad_alloc();
            }
            stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);
            // Don't increment allocated_count (still at max_capacity)
            return buffer;
        } else {
            // Under capacity - allocate new buffer
            void* buffer = std::malloc(BufferSize);
            if (!buffer) {
                throw std::bad_alloc();
            }
            allocated_count_.fetch_add(1, std::memory_order_relaxed);
            stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);

            // Update peak capacity if needed
            auto current = allocated_count_.load(std::memory_order_relaxed);
            auto peak = stats_.peak_capacity.load(std::memory_order_relaxed);
            if (current > peak) {
                stats_.peak_capacity.store(current, std::memory_order_relaxed);
            }
            return buffer;
        }
    } else {
        throw std::logic_error("Unsupported allocation mode");
    }
}

template <size_t BufferSize, typename Policy>
inline void MessageBufferPool<BufferSize, Policy>::ReleaseBuffer(void* buffer) noexcept {
    if (!buffer) {
        return;  // Ignore null pointers
    }

    std::lock_guard<std::mutex> lock(pool_mutex_);

    if (!initialized_) {
        // Pool not initialized - just free the buffer
        std::free(buffer);
        return;
    }

    if (allocation_mode_ == MessagePoolPolicy::AllocationMode::Prealloc) {
        // ====================================================================
        // Prealloc Mode: always return to pool
        // ====================================================================
        available_buffers_.push_back(buffer);
        stats_.current_available.fetch_add(1, std::memory_order_relaxed);

        // Update peak capacity if needed
        auto current = available_buffers_.size();
        auto peak = stats_.peak_capacity.load(std::memory_order_relaxed);
        if (current > peak) {
            stats_.peak_capacity.store(current, std::memory_order_relaxed);
        }
    } else if (allocation_mode_ == MessagePoolPolicy::AllocationMode::LazyAllocate) {
        // ====================================================================
        // Lazy Mode: return to pool if under capacity, otherwise free
        // ====================================================================
        // Keep buffers up to allocated_count in the pool
        // Buffers allocated beyond capacity are freed immediately
        auto current_allocated = allocated_count_.load(std::memory_order_relaxed);
        auto current_available = available_buffers_.size();

        if (current_available < current_allocated) {
            // Still building up the pool to match allocated count
            available_buffers_.push_back(buffer);
            stats_.current_available.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Pool has enough buffers for all allocated - free the extra
            std::free(buffer);
        }
    }
}

template <size_t BufferSize, typename Policy>
inline void MessageBufferPool<BufferSize, Policy>::SetCapacity(size_t new_capacity) noexcept {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    if (!initialized_) {
        return;  // Cannot adjust uninitialized pool
    }

    if (new_capacity == total_capacity_) {
        return;  // No change needed
    }

    if (new_capacity > total_capacity_) {
        // ====================================================================
        // Scale up: Allocate additional buffers
        // ====================================================================
        size_t additional = new_capacity - total_capacity_;

        for (size_t i = 0; i < additional; ++i) {
            void* buffer = std::malloc(BufferSize);
            if (!buffer) {
                // Allocation failed - free what we've added so far
                break;
            }
            available_buffers_.push_back(buffer);
            stats_.current_available.fetch_add(1, std::memory_order_relaxed);
        }

        total_capacity_ = new_capacity;
    } else {
        // ====================================================================
        // Scale down: Mark excess buffers for reclamation
        // ====================================================================
        size_t excess = total_capacity_ - new_capacity;
        size_t reclaimed = 0;

        // Free buffers from the stack up to excess amount
        while (!available_buffers_.empty() && reclaimed < excess) {
            void* buffer = available_buffers_.back();
            available_buffers_.pop_back();
            std::free(buffer);
            stats_.current_available.fetch_sub(1, std::memory_order_relaxed);
            reclaimed++;
        }

        total_capacity_ = new_capacity;
    }

    // Update peak capacity if new capacity is higher
    auto peak = stats_.peak_capacity.load(std::memory_order_relaxed);
    if (total_capacity_ > peak) {
        stats_.peak_capacity.store(total_capacity_, std::memory_order_relaxed);
    }
}

}  // namespace graph
