// SPDX-License-Identifier: MIT

/**
 * @file MessagePoolRouter.hpp
 * @brief Message Pool Router Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <graph/PooledMessage.hpp>
#include <cstddef>
#include <type_traits>

namespace graph {

/**
 * @class MessagePoolRouter
 * @brief Intelligent allocation router with compile-time SSO dispatch
 *
 * Determines at compile-time whether a type should use SSO or pooling,
 * and routes allocations accordingly at runtime. Provides zero overhead
 * for small types that fit in SSO buffer.
 *
 * @tparam SSO_THRESHOLD Size threshold for pooling decision (default 32 bytes)
 *
 * Usage:
 * @code
 *   // Compile-time decision: sizeof(int) <= 32, uses SSO (no pool overhead)
 *   static_assert(!MessagePoolRouter<>::ShouldPool<int>);
 *
 *   // Compile-time decision: sizeof(StateVector) > 32, uses pool
 *   static_assert(MessagePoolRouter<>::ShouldPool<StateVector>);
 *
 *   // Runtime check for pool initialization
 *   auto& registry = MessagePoolRegistry::GetInstance();
 *   registry.InitializeCommonPools(max_edge_capacity);
 *
 *   // Transparent allocation (handled by Message class)
 *   Message msg(my_large_payload);  // Automatically pooled if size > 32
 * @endcode
 */
/**
 * @class MessagePoolRouter
 * @brief Message Pool Router message type.
 *
 * @details Carries typed data between graph nodes. Fields describe payload shape, metadata, and sequencing information used by downstream processing.
 */
template <size_t SSO_THRESHOLD = 32>
class MessagePoolRouter {
public:
    /**
     * @brief Compile-time check: should type use pooling?
     * @tparam T Payload type
     * @return true if sizeof(T) > SSO_THRESHOLD, false otherwise
     *
     * Determines at compile-time whether a type should use buffer pooling.
     * Types smaller than threshold use SSO (no allocation overhead).
     * Types larger than threshold route through MessagePoolRegistry.
     */
    template <typename T>
    static constexpr bool ShouldPool = (sizeof(T) > SSO_THRESHOLD);

    /**
     * @brief Get pool for payload type (compile-time aware)
     * @tparam T Payload type
     * @return Pointer to MessageBufferPool<64> or nullptr if pooling not applicable
     *
     * Returns the appropriate pool for a type based on its size.
     * Only large types (sizeof(T) > SSO_THRESHOLD) return a pool pointer.
     * Small types return nullptr (they use SSO).
     */
    template <typename T>
    /**
     * @brief Returns the Pool For Type.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static MessageBufferPool<64>* GetPoolForType() noexcept {
        /**
         * @brief Executes the Constexpr operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        if constexpr (ShouldPool<T>) {
            return MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
        }
        return nullptr;
    }

    /**
     * @brief Get pool statistics for a type
     * @tparam T Payload type
     * @return PoolStatsSnapshot with reuse hit rate and allocation metrics
     *
     * Returns statistics for the pool handling type T's allocations.
     * For small types, returns zero statistics (SSO used instead).
     * For large types, returns actual pool statistics (reuse rate, allocation count).
     */
    template <typename T>
    /**
     * @brief Returns the Pool Stats For Type.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static MessageBufferPool<64>::PoolStatsSnapshot GetPoolStatsForType() noexcept {
        /**
         * @brief Executes the Constexpr operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        if constexpr (ShouldPool<T>) {
            auto* pool = MessagePoolRegistry::GetInstance().GetPoolForSize(sizeof(T));
            if (pool) {
                return pool->GetStats();
            }
        }
        // Return empty stats for non-pooled types
        return MessageBufferPool<64>::PoolStatsSnapshot();
    }

    /**
     * @brief Check if type should use pool based on compile-time threshold
     * @tparam T Payload type
     * @return true if sizeof(T) > SSO_THRESHOLD
     *
     * This is a named alias for ShouldPool for code clarity.
     * Used to determine if Message allocation should route through pool registry.
     */
    template <typename T>
    static constexpr bool IsLargePayload = ShouldPool<T>;

    /**
     * @brief Get SSO threshold configured for this router
     * @return Threshold in bytes
     *
     * Returns the configured threshold for SSO vs pooling decision.
     * Default is 32 bytes (Message class SSO buffer size).
     */
    static constexpr size_t GetThreshold() noexcept {
        return SSO_THRESHOLD;
    }
};

/**
 * @brief Aggregate statistics across all active pools
 *
 * Provides observability into pooling behavior across all message types
 * and sizes currently in use.
 *
 * Usage:
 * @code
 *   auto stats = MessagePoolRegistry::GetInstance().GetAggregateStats();
 *   printf("Pool hit rate: %.1f%%\n", stats.aggregate_hit_rate);
 *   printf("Total allocations: %lu\n", stats.total_allocations);
 *   printf("Total reuses: %lu\n", stats.total_reuses);
 * @endcode
 */
/**
 * @struct AggregatePoolStats
 * @brief Aggregate Pool Stats data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
struct AggregatePoolStats {
    uint64_t total_requests = 0;      ///< Total allocation requests across all pools
    uint64_t total_allocations = 0;   ///< Total new allocations (pool misses)
    uint64_t total_reuses = 0;        ///< Total buffer reuses from pools
    double aggregate_hit_rate = 0.0;  ///< Weighted average hit rate (0-100%)

    /**
     * @brief Get average reuses per allocation
     * @return Number of times each buffer is reused on average
     */
    double GetAverageReusesPerAllocation() const noexcept {
        if (total_allocations == 0) {
            return 0.0;
        }
        return static_cast<double>(total_reuses) / static_cast<double>(total_allocations);
    }

    /**
     * @brief Check if pooling is effective
     * @return true if hit rate > 50%, indicating good pool utilization
     */
    bool IsEffective() const noexcept {
        return aggregate_hit_rate > 50.0;
    }
};

}  // namespace graph
