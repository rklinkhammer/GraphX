/**
 * @file AdaptiveCapacityMonitor.hpp
 * @brief Adaptive pool capacity monitoring and adjustment
 * @author Robert Klinkhammer
 * @date April 24, 2026
 *
 * Background monitoring system for message pool capacity auto-scaling.
 * Monitors hit rate and adjusts pool capacity based on configurable thresholds.
 *
 * Thread-Safety: Safe for concurrent pool access while monitoring.
 * Background thread runs independently, adjusts capacity asynchronously.
 */

#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include "core/CallbackUtilities.hpp"

namespace graph {

/**
 * @struct AdaptiveCapacityConfig
 * @brief Configuration for adaptive capacity adjustment
 */
struct AdaptiveCapacityConfig {
    /// Hit rate threshold below which capacity increases
    double low_hit_rate_threshold = 50.0;

    /// Hit rate threshold above which capacity decreases
    double high_hit_rate_threshold = 90.0;

    /// Factor to multiply capacity when scaling up (e.g., 1.5 = +50%)
    double scale_up_factor = 1.5;

    /// Factor to multiply capacity when scaling down (e.g., 0.9 = -10%)
    double scale_down_factor = 0.9;

    /// Interval between adjustment checks
    std::chrono::seconds adjustment_interval{300};  // 5 minutes

    /// Enable capacity reduction (scale down)
    bool enable_scale_down = true;

    /// Maximum capacity (hard limit)
    size_t max_capacity = 10000;

    /// Minimum capacity (hard floor)
    size_t min_capacity = 32;

    /// Enable adaptive monitoring
    bool enabled = true;
};

/**
 * @class AdaptiveCapacityMonitor
 * @brief Background thread for monitoring and adjusting pool capacity
 *
 * Runs in a separate thread, periodically checking pool hit rates and
 * adjusting capacity up/down based on configured thresholds.
 *
 * Thread-safe: All operations protected by mutexes.
 */
class AdaptiveCapacityMonitor {
public:
    /**
     * @brief Constructor
     * @param config Adaptive capacity configuration
     */
    explicit AdaptiveCapacityMonitor(const AdaptiveCapacityConfig& config = {})
        : config_(config), running_(false) {}

    /**
     * @brief Destructor - stops monitoring thread
     */
    ~AdaptiveCapacityMonitor() noexcept { Stop(); }

    /**
     * @brief Start monitoring thread
     * @throws std::runtime_error if already running
     */
    void Start();

    /**
     * @brief Stop monitoring thread
     *
     * Waits for thread to finish gracefully. Safe to call multiple times.
     */
    void Stop() noexcept;

    /**
     * @brief Check if monitoring is active
     * @return true if monitoring thread is running
     */
    [[nodiscard]] bool IsRunning() const noexcept;

    /**
     * @brief Register a pool for monitoring
     * @param pool_id Unique identifier for pool (size or type)
     * @param monitor_fn Callback that returns current hit rate
     *
     * Callback signature: double() -> returns hit rate 0-100%
     *
     * @note Phase 4: Uses std::move_only_function for single-ownership
     * semantics and improved performance.
     */
    void RegisterPool(size_t pool_id, app::callbacks::MoveOnlyCallback<double()> monitor_fn);

    /**
     * @brief Unregister a pool from monitoring
     * @param pool_id Identifier of pool to stop monitoring
     */
    void UnregisterPool(size_t pool_id) noexcept;

    /**
     * @brief Register capacity adjustment callback
     * @param pool_id Pool identifier
     * @param adjust_fn Callback to adjust capacity
     *
     * Callback signature: void(size_t new_capacity)
     *
     * @note Phase 4: Uses std::move_only_function for single-ownership
     * semantics and improved performance.
     */
    void RegisterAdjustmentCallback(size_t pool_id,
                                    app::callbacks::MoveOnlyCallback<void(size_t)> adjust_fn);

    /**
     * @brief Get current configuration
     * @return Configuration struct
     */
    [[nodiscard]] AdaptiveCapacityConfig GetConfig() const noexcept;

    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void SetConfig(const AdaptiveCapacityConfig& config) noexcept;

    /**
     * @struct AdjustmentEvent
     * @brief Record of a capacity adjustment
     */
    struct AdjustmentEvent {
        size_t pool_id = 0;
        size_t old_capacity = 0;
        size_t new_capacity = 0;
        double hit_rate = 0.0;
        std::chrono::system_clock::time_point timestamp;
    };

    /**
     * @brief Get adjustment history
     * @return Vector of recent adjustment events
     */
    [[nodiscard]] std::vector<AdjustmentEvent> GetAdjustmentHistory() const;

    /**
     * @brief Clear adjustment history
     */
    void ClearAdjustmentHistory() noexcept;

private:
    /**
     * @brief Main monitoring loop (runs in background thread)
     */
    void MonitoringLoop() noexcept;

    /**
     * @brief Check and adjust single pool capacity
     * @param pool_id Pool identifier
     * @return true if adjustment was made
     */
    bool CheckAndAdjustPool(size_t pool_id) noexcept;

    /**
     * @brief Calculate new capacity based on hit rate
     * @param current_capacity Current pool capacity
     * @param hit_rate Current hit rate (0-100)
     * @param old_capacity Previous capacity for comparison
     * @return New capacity, or 0 if no change
     */
    [[nodiscard]] size_t CalculateNewCapacity(size_t current_capacity,
                                              double hit_rate,
                                              size_t old_capacity) const noexcept;

    // Configuration
    mutable std::mutex config_mutex_;
    AdaptiveCapacityConfig config_;

    // Thread management
    std::unique_ptr<std::thread> monitor_thread_;
    std::atomic<bool> running_{false};

    // Pool monitoring
    struct PoolMonitor {
        app::callbacks::MoveOnlyCallback<double()> hit_rate_fn;  // Returns current hit rate
        app::callbacks::MoveOnlyCallback<void(size_t)> adjust_fn;  // Adjusts capacity
    };

    mutable std::mutex pools_mutex_;
    std::vector<std::pair<size_t, PoolMonitor>> registered_pools_;

    // Adjustment history
    mutable std::mutex history_mutex_;
    std::vector<AdjustmentEvent> adjustment_history_;
};

}  // namespace graph
