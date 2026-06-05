/**
 * @file TestMetricsSubscriber.hpp
 * @brief Shared test utility for capturing and validating metrics events
 *
 * Provides a reusable metrics subscriber implementation for test suites
 * that need to validate metrics publishing behavior.
 *
 * @author Test Suite
 * @date May 29, 2026
 */

#pragma once

#include <mutex>
#include <vector>
#include <algorithm>
#include <string>
#include "metrics/IMetricsSubscriber.hpp"
#include "metrics/MetricsEvent.hpp"

namespace test {

/**
 * @class TestMetricsSubscriber
 * @brief Captures metrics events for test validation
 *
 * Implements IMetricsSubscriber to receive and store metrics events
 * published by graph nodes during topology execution.
 *
 * Thread-safe: Uses mutex to protect concurrent event delivery from
 * background metrics distribution thread.
 *
 * **Usage**:
 * ```cpp
 * auto subscriber = std::make_shared<test::TestMetricsSubscriber>();
 * executor->SubscribeToMetrics(subscriber);
 * 
 * // Run graph
 * executor->Execute();
 * 
 * // Validate metrics
 * EXPECT_EQ(subscriber->GetEventCount(), expected_count);
 * EXPECT_GT(subscriber->GetEventCountBySource("MyNode"), 0);
 * EXPECT_GT(subscriber->GetEventCountByType("message_produced"), 0);
 * ```
 */
class TestMetricsSubscriber : public app::metrics::IMetricsSubscriber {
public:
    /**
     * @brief Receive a metrics event (called by background metrics thread)
     * @param event The metrics event published by a node
     */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
    }

    /**
     * @brief Get all captured events (thread-safe)
     * @return Vector of captured MetricsEvent objects
     */
    std::vector<app::metrics::MetricsEvent> GetCapturedEvents() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    /**
     * @brief Get total number of captured events
     * @return Count of events received
     */
    size_t GetEventCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    /**
     * @brief Count events of a specific type
     * @param event_type The event type to count (e.g., "message_produced")
     * @return Number of events matching the type
     */
    size_t GetEventCountByType(const std::string& event_type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::count_if(events_.begin(), events_.end(),
            [&](const auto& e) { return e.event_type == event_type; });
    }

    /**
     * @brief Count events from a specific node source
     * @param source The source node name (e.g., "SourceTestNode")
     * @return Number of events from that source
     */
    size_t GetEventCountBySource(const std::string& source) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::count_if(events_.begin(), events_.end(),
            [&](const auto& e) { return e.source == source; });
    }

    /**
     * @brief Clear all captured events
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<app::metrics::MetricsEvent> events_;
};

} // namespace test
