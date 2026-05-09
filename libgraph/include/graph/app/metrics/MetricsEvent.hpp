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
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <chrono>
#include <string>
#include <map>

/**
 * @file MetricsEvent.hpp
 * @brief Event structure for async metrics publishing from nodes
 *
 * Represents a metrics event published by a node when an important state
 * change occurs. Used by both polling and async metrics streams.
 *
 * @section usage Usage Example
 *
 * \code
 * // In FlightMonitorNode when phase changes
 * if (new_phase != current_phase_) {
 *     current_phase_ = new_phase;
 *
 *     MetricsEvent event{
 *         .timestamp = std::chrono::system_clock::now(),
 *         .source = "FlightMonitorNode",
 *         .event_type = "phase_transition",
 *         .data = {
 *             {"current_phase", EnumToString(new_phase)},
 *             {"timestamp_ms", std::to_string(GetElapsedMs())}
 *         }
 *     };
 *
 *     metrics_callback_->PublishAsync(event);
 * }
 * \endcode
 */

namespace app::metrics {

/**
 * @brief Event structure for async metrics publishing
 *
 * Encapsulates a metrics event with timestamp, source, event type, and
 * arbitrary key-value data. Used for:
 * - Phase transitions (FlightMonitorNode: phase_transition)
 * - Completion signals (CompletionAggregationNode: completion_status)
 * - Custom threshold events (future nodes)
 * - Key state changes (any metrics-capable node)
 *
 * @note Timestamp is captured at event creation time in node
 * @note Source identifies which node published the event
 * @note Event type describes what happened (e.g., "phase_transition")
 * @note Data is flexible key-value map for event-specific information
 */
struct MetricsEvent {
    /**
     * @brief Timestamp when event was created
     *
     * Captured in the node when the event occurs. Should be close to
     * the actual state change timestamp.
     */
    std::chrono::system_clock::time_point timestamp = 
        std::chrono::system_clock::now();


    /**
     * @brief Source node that published this event
     *
     * Examples:
     * - "FlightMonitorNode"
     * - "CompletionAggregationNode"
     * - "CustomSensorNode"
     *
     * @note Should match node's name or class name for traceability
     */
    std::string source;

    /**
     * @brief Type of event that occurred
     *
     * Examples:
     * - "phase_transition" (FlightMonitorNode)
     * - "completion_status" (CompletionAggregationNode)
     * - "threshold_exceeded" (sensor nodes)
     * - "state_change" (generic)
     *
     * @note Used by subscribers to filter events
     */
    std::string event_type;

    /**
     * @brief Event-specific data as key-value pairs
     *
     * Flexible map for event-specific information:
     * - Phase transitions: {"previous_phase": "...", "current_phase": "..."}
     * - Completion: {"completion_rate": "1.0", "nodes_complete": "5"}
     * - Thresholds: {"threshold": "...", "value": "...", "exceeded": "true"}
     *
     * @note Keys and values are strings for flexible interop
     * @note Empty if event has no additional data
     */
    std::map<std::string, std::string> data;

    /**
     * @brief Constructor
     *
     * Initializes with timestamp, source, event_type.
     * Data map can be populated afterward or via initializer list.
     */
    //MetricsEvent() = default;

    // /**
    //  * @brief Explicit constructor with all fields
    //  */
    // MetricsEvent(
    //     const std::chrono::system_clock::time_point& ts,
    //     const std::string& src,
    //     const std::string& type,
    //     const std::map<std::string, std::string>& evt_data = {})
    //     : timestamp(ts), source(src), event_type(type), data(evt_data) {}
};

}  // namespace app::metrics

