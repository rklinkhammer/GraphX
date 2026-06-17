// SPDX-License-Identifier: MIT

/**
 * @file AdaptiveCapacityMonitor.cpp
 * @brief Implementation of adaptive capacity monitoring
 */

#include "graph/AdaptiveCapacityMonitor.hpp"
#include <algorithm>
#include <iostream>

namespace graph {

/**
 * @brief Start.
 */
void AdaptiveCapacityMonitor::Start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        throw std::runtime_error("AdaptiveCapacityMonitor already running");
    }

    monitor_thread_ = std::make_unique<std::thread>([this] {
        this->MonitoringLoop();
    });
}

/**
 * @brief Stop.
 */
void AdaptiveCapacityMonitor::Stop() noexcept {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (monitor_thread_ && monitor_thread_->joinable()) {
            monitor_thread_->join();
        }
        monitor_thread_.reset();
    }
}

/**
 * @brief Is running.
 */
bool AdaptiveCapacityMonitor::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void AdaptiveCapacityMonitor::RegisterPool(size_t pool_id,
                                           app::callbacks::MoveOnlyCallback<double()> monitor_fn) {
    if (!monitor_fn) {
        throw std::invalid_argument("Monitor function cannot be null");
    }

    std::lock_guard<std::mutex> lock(pools_mutex_);

    // Check if already registered
    for (auto& [id, monitor] : registered_pools_) {
        if (id == pool_id) {
            throw std::logic_error("Pool already registered");
        }
    }

    registered_pools_.emplace_back(pool_id, PoolMonitor{std::move(monitor_fn), nullptr});
}

/**
 * @brief Unregister pool.
 * @param pool_id Parameter for unregister pool.
 */
void AdaptiveCapacityMonitor::UnregisterPool(size_t pool_id) noexcept {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    auto it = std::find_if(registered_pools_.begin(), registered_pools_.end(),
                           [pool_id](const auto& p) { return p.first == pool_id; });
    if (it != registered_pools_.end()) {
        registered_pools_.erase(it);
    }
}

void AdaptiveCapacityMonitor::RegisterAdjustmentCallback(
    size_t pool_id, app::callbacks::MoveOnlyCallback<void(size_t)> adjust_fn) {
    if (!adjust_fn) {
        throw std::invalid_argument("Adjustment function cannot be null");
    }

    std::lock_guard<std::mutex> lock(pools_mutex_);

    for (auto& [id, monitor] : registered_pools_) {
        if (id == pool_id) {
            monitor.adjust_fn = std::move(adjust_fn);
            return;
        }
    }

    throw std::logic_error("Pool not registered");
}

/**
 * @brief Get config.
 */
AdaptiveCapacityConfig AdaptiveCapacityMonitor::GetConfig() const noexcept {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

void AdaptiveCapacityMonitor::SetConfig(
    const AdaptiveCapacityConfig& config) noexcept {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

std::vector<AdaptiveCapacityMonitor::AdjustmentEvent>
AdaptiveCapacityMonitor::GetAdjustmentHistory() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return adjustment_history_;
}

/**
 * @brief Clear adjustment history.
 */
void AdaptiveCapacityMonitor::ClearAdjustmentHistory() noexcept {
    std::lock_guard<std::mutex> lock(history_mutex_);
    adjustment_history_.clear();
}

/**
 * @brief Monitoring loop.
 */
void AdaptiveCapacityMonitor::MonitoringLoop() noexcept {
    while (running_.load(std::memory_order_acquire)) {
        {
            auto config = GetConfig();
            if (!config.enabled) {
                std::this_thread::sleep_for(config.adjustment_interval);
                continue;
            }

            // Collect pool IDs first (while holding lock), then check them (without lock)
            // This prevents deadlock since CheckAndAdjustPool also needs the lock
            std::vector<size_t> pool_ids_to_check;
            {
                std::lock_guard<std::mutex> lock(pools_mutex_);
                for (auto& [pool_id, monitor] : registered_pools_) {
                    pool_ids_to_check.push_back(pool_id);
                }
            }

            // Check each pool WITHOUT holding the lock
            for (size_t pool_id : pool_ids_to_check) {
                CheckAndAdjustPool(pool_id);
            }
        }

        // Sleep until next check
        auto config = GetConfig();
        std::this_thread::sleep_for(config.adjustment_interval);
    }
}

/**
 * @brief Check and adjust pool.
 * @param pool_id Parameter for check and adjust pool.
 */
bool AdaptiveCapacityMonitor::CheckAndAdjustPool(size_t pool_id) noexcept {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    // Find pool
    auto it = std::find_if(registered_pools_.begin(), registered_pools_.end(),
                           [pool_id](const auto& p) { return p.first == pool_id; });
    if (it == registered_pools_.end()) {
        return false;  // Pool not found
    }

    auto& [id, monitor] = *it;

    if (!monitor.hit_rate_fn || !monitor.adjust_fn) {
        return false;  // Callbacks not set
    }

    // Get current hit rate
    double hit_rate = monitor.hit_rate_fn();

    // Calculate new capacity
    auto config = GetConfig();

    // Determine if adjustment needed based on hit rate
    bool should_scale_up = (hit_rate < config.low_hit_rate_threshold);
    bool should_scale_down =
        config.enable_scale_down && (hit_rate > config.high_hit_rate_threshold);

    if (!should_scale_up && !should_scale_down) {
        return false;  // No adjustment needed
    }

    // For this implementation, we'll need to track capacity elsewhere
    // For now, call the adjustment function with a calculated capacity
    // The actual capacity tracking will be in the pool itself

    // Record adjustment event
    {
        std::lock_guard<std::mutex> hist_lock(history_mutex_);
        adjustment_history_.push_back(AdjustmentEvent{
            pool_id,
            0,  // old_capacity (would be tracked by pool)
            0,  // new_capacity (calculated by pool)
            hit_rate,
            std::chrono::system_clock::now()
        });

        // Keep history size bounded
        if (adjustment_history_.size() > 1000) {
            adjustment_history_.erase(adjustment_history_.begin());
        }
    }

    return true;
}

size_t AdaptiveCapacityMonitor::CalculateNewCapacity(
    size_t current_capacity, double hit_rate, [[maybe_unused]] size_t old_capacity) const noexcept {
    auto config = GetConfig();

    if (hit_rate < config.low_hit_rate_threshold) {
        // Scale up
        size_t new_capacity =
            static_cast<size_t>(current_capacity * config.scale_up_factor);
        return std::min(new_capacity, config.max_capacity);
    } else if (config.enable_scale_down &&
               hit_rate > config.high_hit_rate_threshold) {
        // Scale down
        size_t new_capacity =
            static_cast<size_t>(current_capacity * config.scale_down_factor);
        return std::max(new_capacity, config.min_capacity);
    }

    return current_capacity;  // No change
}

}  // namespace graph
