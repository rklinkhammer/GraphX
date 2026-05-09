#pragma once

#include <memory>
#include "dashboard/metrics/MetricsEvent.hpp"

namespace app::interfaces {

/**
 * @class IMetricsPublisher
 * @brief Abstract interface for publishing metrics to UI
 *
 * Implemented by UI adapters to receive and display metrics updates.
 * Decouples metrics collection (business logic) from UI display logic.
 *
 * Lifecycle:
 * 1. MetricsCapability calls OnMetricsEvent() on each subscriber (from metrics thread)
 * 2. Subscriber stores or queues the event (must be very fast, <1ms)
 * 3. UI periodically calls FlushMetrics() to push updates (from UI thread)
 * 4. GetLatestMetrics() called by web API or status commands
 *
 * Thread Safety:
 * - OnMetricsEvent() called from MetricsPolicy background thread
 * - FlushMetrics() called from UI render thread (or main thread)
 * - GetLatestMetrics() may be called from any thread
 * - Implementations MUST use atomics or locks appropriately
 *
 * Performance Requirements:
 * - OnMetricsEvent() must complete in <1ms (lock-free preferred)
 * - FlushMetrics() should complete in <50ms
 * - GetLatestMetrics() must be thread-safe and fast
 *
 * Implementations:
 * - TerminalUIAdapter - Updates MetricsPanel with latest data
 * - WebUIAdapter - Broadcasts metrics via WebSocket
 * - CLIAdapter - Prints metrics to stdout
 *
 * Example Usage:
 * ```cpp
 * auto publisher = bus->Get<IMetricsPublisher>();
 *
 * // Called by MetricsCapability (from metrics thread)
 * void OnMetricsEventCallback(const MetricsEvent& event) {
 *     publisher->OnMetricsEvent(event);
 * }
 *
 * // Called by UI render loop (from UI thread)
 * void RenderUI() {
 *     publisher->FlushMetrics();  // Push queued updates
 *     auto metrics = publisher->GetLatestMetrics();
 *     DisplayMetrics(metrics);
 * }
 * ```
 */
class IMetricsPublisher {
public:
    virtual ~IMetricsPublisher() = default;

    /**
     * @brief Called when metrics event occurs
     *
     * Invoked by MetricsCapability whenever new metrics are available.
     * Called from MetricsPolicy background thread, so MUST be very fast
     * and thread-safe.
     *
     * Typical implementation:
     * - Store event in atomic or thread-safe queue
     * - Update atomic counters
     * - Do NOT call UI update functions (too slow, wrong thread)
     *
     * Performance Requirements:
     * - MUST complete in <1ms
     * - Lock-free preferred (use atomic, CAS)
     * - Avoid heap allocation if possible
     * - Should not acquire locks held by main thread
     *
     * @param event Metrics event with node metrics, edge metrics, timing
     *
     * @see FlushMetrics() for deferred UI updates
     *
     * Thread: MetricsPolicy background thread
     * Frequency: Configurable via MetricsCapability (default: every 100ms)
     */
    virtual void OnMetricsEvent(const app::metrics::MetricsEvent& event) = 0;

    /**
     * @brief Flush buffered metrics to UI
     *
     * Called periodically by UI render loop to push buffered metrics to display.
     * Called from UI thread (Terminal event loop, Web request handler, CLI loop).
     *
     * Typical implementation:
     * - Copy latest metrics from atomic storage
     * - Update UI components (safe on this thread)
     * - Send WebSocket messages (safe on this thread)
     * - Clear buffer or prepare for next batch
     *
     * Performance Requirements:
     * - Should complete in <50ms (for smooth 20 FPS UI)
     * - May perform heavier operations than OnMetricsEvent
     * - After this call, metrics should be visible in UI
     *
     * @see OnMetricsEvent() for fast event reception
     * @see GetLatestMetrics() for synchronous access
     *
     * Thread: UI thread (Terminal, Web, CLI)
     * Frequency: Every UI update cycle (e.g., 50ms for 20 FPS)
     */
    virtual void FlushMetrics() = 0;

    /**
     * @brief Get latest metrics snapshot
     *
     * Returns most recent MetricsEvent without flushing.
     * Used by:
     * - Web API handlers serving /api/metrics endpoint
     * - Status command handlers
     * - Any code needing synchronous access to latest metrics
     *
     * Performance Requirements:
     * - Must be thread-safe
     * - Should be fast (<10ms), called from request handlers
     * - OK to block briefly if necessary
     *
     * Returns:
     * - Most recent MetricsEvent received via OnMetricsEvent()
     * - Returns copy (safe to use in any thread)
     * - May return default/empty event if no metrics yet
     *
     * @return Snapshot of latest metrics
     *
     * Thread: Any thread (must be thread-safe)
     * Frequency: On-demand (API requests, commands)
     */
    virtual app::metrics::MetricsEvent GetLatestMetrics() const = 0;
};

}  // namespace app::interfaces
