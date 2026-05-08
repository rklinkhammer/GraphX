/**
 * @file PooledMessage.cpp
 * @brief Implementation of MessagePoolRegistry and message pooling utilities
 */

#include <graph/PooledMessage.hpp>
#include <algorithm>
#include <stdexcept>

namespace graph {

MessageBufferPool<64>* MessagePoolRegistry::GetPoolForSize(size_t size) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    // Check if pool already exists for this size
    auto it = pools_.find(size);
    if (it != pools_.end()) {
        return it->second.get();
    }

    // Create new pool for this size
    // For MVP: all pools use 64-byte buffers by design
    // Size parameter is used as key to track different message types
    auto new_pool = std::make_unique<MessageBufferPool<64>>();

    try {
        // Initialize with default capacity
        new_pool->Initialize(1000);
    } catch (const std::bad_alloc&) {
        // Return nullptr if allocation fails
        return nullptr;
    } catch (const std::exception&) {
        // Return nullptr on any initialization error
        return nullptr;
    }

    auto* pool_ptr = new_pool.get();
    pools_[size] = std::move(new_pool);

    return pool_ptr;
}

void MessagePoolRegistry::InitializeCommonPools(size_t pool_capacity) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    // Pre-create pools for common message sizes
    std::vector<size_t> common_sizes = {64, 128, 256};

    for (size_t size : common_sizes) {
        // Check if already exists
        if (pools_.find(size) != pools_.end()) {
            continue;
        }

        auto new_pool = std::make_unique<MessageBufferPool<64>>();
        try {
            new_pool->Initialize(pool_capacity);
            pools_[size] = std::move(new_pool);
        } catch (const std::exception&) {
            // Log and continue if pool creation fails
            // In production, consider logging via log4cxx
        }
    }
}

void MessagePoolRegistry::ShutdownAllPools() noexcept {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    for (auto& [size, pool] : pools_) {
        if (pool) {
            pool->Shutdown();
        }
    }
    pools_.clear();
}

MessagePoolRegistry::AggregateStats MessagePoolRegistry::GetAggregateStats() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    AggregateStats stats;

    for (const auto& [size, pool] : pools_) {
        if (pool) {
            auto pool_stats = pool->GetStats();
            stats.total_requests += pool_stats.total_requests;
            stats.total_allocations += pool_stats.total_allocations;
            stats.total_reuses += pool_stats.total_reuses;
        }
    }

    // Calculate aggregate hit rate
    if (stats.total_requests > 0) {
        stats.aggregate_hit_rate = (static_cast<double>(stats.total_reuses) /
                                     static_cast<double>(stats.total_requests)) *
                                    100.0;
    }

    return stats;
}

void MessagePoolRegistry::Reset() noexcept {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    for (auto& [size, pool] : pools_) {
        if (pool) {
            pool->Shutdown();
        }
    }
    pools_.clear();
}

}  // namespace graph
