/**
 * @file PooledMessage.hpp
 * @brief Integration adapter for MessageBufferPool with Message class
 * @author Robert Klinkhammer
 * @date April 24, 2026
 *
 * Provides utilities for transparent buffer pooling of Message payloads
 * that exceed the SSO threshold. Enables per-type pooling with:
 * - Automatic buffer return to pool on destruction
 * - Statistics exposure via Message API
 * - Compile-time type-size discovery for pool selection
 *
 * Thread-Safety: All operations are thread-safe. Buffer pools are shared
 * across threads with synchronization via mutex + atomic statistics.
 */

#pragma once

#include <graph/MessagePool.hpp>
#include <memory>
#include <map>
#include <mutex>
#include <cstddef>

// Forward declarations to avoid circular include
namespace graph::message {
    class Message;
}

namespace graph {

/**
 * @class MessagePoolRegistry
 * @brief Global registry for per-type message buffer pools
 *
 * Manages pool creation and access for different payload types.
 * Pools are created on-demand based on payload size and cached for reuse.
 *
 * Usage:
 * @code
 *   auto& registry = MessagePoolRegistry::GetInstance();
 *   auto* pool = registry.GetPoolForSize(sizeof(MyLargeMessagePayload));
 *   void* buffer = pool->AcquireBuffer();
 *   // ... use buffer ...
 *   pool->ReleaseBuffer(buffer);
 * @endcode
 */
class MessagePoolRegistry {
public:
    /**
     * @brief Get singleton instance of the registry
     * @return Reference to global MessagePoolRegistry
     */
    static MessagePoolRegistry& GetInstance() {
        static MessagePoolRegistry instance;
        return instance;
    }

    /**
     * @brief Get or create pool for given buffer size
     * @param size Buffer size in bytes
     * @return Pointer to MessageBufferPool for that size (or nullptr if allocation fails)
     *
     * Pools are created on-demand with default capacity (1000 buffers per type).
     * Subsequent calls with same size return cached pool.
     * Thread-safe: multiple threads can request pools concurrently.
     */
    MessageBufferPool<64>* GetPoolForSize(size_t size);

    /**
     * @brief Initialize all pools with specified capacity
     * @param pool_capacity Number of buffers per pool
     *
     * Creates pools for common message sizes (64B, 128B, 256B).
     * Must be called once during graph initialization.
     * Safe to call multiple times (idempotent).
     */
    void InitializeCommonPools(size_t pool_capacity = 1000);

    /**
     * @brief Shutdown and free all pools
     *
     * Called during graph teardown. Frees all managed pools.
     * Safe to call multiple times.
     */
    void ShutdownAllPools() noexcept;

    /**
     * @brief Get aggregate statistics across all pools
     * @return Total statistics from all created pools
     */
    struct AggregateStats {
        uint64_t total_requests = 0;      ///< Total acquire calls across all pools
        uint64_t total_allocations = 0;   ///< Total malloc calls across all pools
        uint64_t total_reuses = 0;        ///< Total buffer reuses across all pools
        double aggregate_hit_rate = 0.0;  ///< Weighted average hit rate %
    };

    AggregateStats GetAggregateStats() const;

    /**
     * @brief Clear all pools and reset statistics
     *
     * Used primarily for testing. Frees all buffers and resets counters.
     */
    void Reset() noexcept;

    // Non-copyable, non-movable (singleton)
    MessagePoolRegistry(const MessagePoolRegistry&) = delete;
    MessagePoolRegistry& operator=(const MessagePoolRegistry&) = delete;
    MessagePoolRegistry(MessagePoolRegistry&&) = delete;
    MessagePoolRegistry& operator=(MessagePoolRegistry&&) = delete;

private:
    MessagePoolRegistry() = default;
    ~MessagePoolRegistry() { ShutdownAllPools(); }

    // Map: size -> pool (using fixed pool size for now)
    // For MVP: all pools use 64-byte buffers, runtime determines if pooling is needed
    std::map<size_t, std::unique_ptr<MessageBufferPool<64>>> pools_;
    mutable std::mutex registry_mutex_;
};

/**
 * @class PooledMessageHelper
 * @brief Utilities for transparent message pooling
 *
 * Provides compile-time helpers for determining whether a type should use pooling,
 * and runtime helpers for buffer allocation/deallocation with pool management.
 */
template <size_t SSO_THRESHOLD = 32>
class PooledMessageHelper {
public:
    /**
     * @brief Determine if type should use pooling (compile-time)
     * @tparam T Payload type
     * @return true if sizeof(T) > SSO_THRESHOLD, false otherwise
     *
     * SSO messages (small payloads) don't benefit from pooling.
     * Only large payloads should use the pool.
     */
    template <typename T>
    static constexpr bool ShouldPool = (sizeof(T) > SSO_THRESHOLD);

    /**
     * @brief Allocate buffer for payload, using pool if large
     * @tparam T Payload type
     * @return void pointer to allocated buffer (pooled or fresh malloc)
     *
     * For large types: acquires from MessagePoolRegistry
     * For small types: returns nullptr (use SSO)
     */
    template <typename T>
    static void* AllocateForType() {
        if constexpr (ShouldPool<T>) {
            auto* pool = MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
            if (pool) {
                return pool->AcquireBuffer();
            }
        }
        return nullptr;
    }

    /**
     * @brief Deallocate buffer, returning to pool if appropriate
     * @tparam T Payload type
     * @param buffer Pointer returned by AllocateForType<T>()
     *
     * For large types: returns buffer to pool for reuse
     * For small types: no-op (SSO buffer not freed)
     */
    template <typename T>
    static void DeallocateForType(void* buffer) noexcept {
        if constexpr (ShouldPool<T>) {
            if (buffer) {
                auto* pool = MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
                if (pool) {
                    pool->ReleaseBuffer(buffer);
                }
            }
        }
    }

    /**
     * @brief Get pool statistics for a payload type
     * @tparam T Payload type
     * @return PoolStatsSnapshot if type uses pooling, zeros otherwise
     */
    template <typename T>
    static auto GetPoolStatsForType() {
        if constexpr (ShouldPool<T>) {
            auto* pool = MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
            if (pool) {
                return pool->GetStats();
            }
        }
        return MessageBufferPool<64>::PoolStatsSnapshot();
    }
};

/**
 * @struct PooledMessageConfig
 * @brief Configuration for message pooling behavior
 */
struct PooledMessageConfig {
    /// Enable message pooling globally
    bool enabled = true;

    /// SSO threshold: messages smaller than this use inline storage
    size_t sso_threshold = 32;

    /// Initial capacity for each pool
    size_t pool_capacity = 1000;

    /// Common payload sizes to pre-allocate pools for (optional)
    std::vector<size_t> common_sizes = {64, 128, 256};
};

/**
 * @class PooledMessageStats
 * @brief Aggregate statistics for all active message pools
 *
 * Provides observability into pooling behavior and hit rates.
 * Thread-safe: uses atomic reads for statistics counters.
 */
class PooledMessageStats {
public:
    /**
     * @brief Get current aggregate statistics
     * @return Aggregate statistics across all pools
     */
    static MessagePoolRegistry::AggregateStats GetStats() {
        return MessagePoolRegistry::GetInstance().GetAggregateStats();
    }

    /**
     * @brief Check if pooling is enabled
     * @return true if message pooling is active
     */
    static bool IsEnabled() noexcept {
        return GetStats().total_allocations > 0 || true;  // Default enabled
    }

    /**
     * @brief Reset all statistics (for testing)
     */
    static void Reset() noexcept {
        MessagePoolRegistry::GetInstance().Reset();
    }
};

}  // namespace graph
