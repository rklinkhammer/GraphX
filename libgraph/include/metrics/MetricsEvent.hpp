/**
 * @file MetricsEvent.hpp
 * @brief Metrics Event Graph runtime support.
 *
 * @details Provides metrics event and subscriber contracts for runtime observability. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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
#include <cstdint>
#include <optional>
#include <string>
#include <map>
#include <variant>
#include <vector>


namespace app::metrics {

/** Exact graph identity attached by the runtime policy, never by display-name
 * correlation.  Edge identities are usable only when all endpoint fields are
 * present. */
struct MetricTarget {
    enum class Kind { Node, Edge } kind{Kind::Node};
    std::string node_id;
    std::string source_node_id;
    std::string source_port_kind;
    std::variant<std::uint64_t, std::string> source_port{std::uint64_t{0}};
    std::string target_node_id;
    std::string target_port_kind;
    std::variant<std::uint64_t, std::string> target_port{std::uint64_t{0}};

    bool operator==(const MetricTarget&) const = default;
};

using MetricScalar =
    std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

/** One typed current-value sample.  Descriptor fields are repeated so a
 * subscriber can reject schema/value mismatches without string heuristics. */
struct MetricSample {
    std::string metric_id;
    std::string scalar_type;
    std::string unit;
    std::string semantics;
    std::string aggregation;
    std::string availability_rule;
    MetricScalar value{std::uint64_t{0}};
    bool available{true};
    std::string unavailable_reason;
    std::uint64_t counter_epoch{0};
};

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
/**
 * @struct MetricsEvent
 * @brief Metrics Event data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
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

    /** Runtime-stamped authoritative target and generation. */
    MetricTarget target;
    std::uint64_t graph_generation{0};

    /** Typed samples validated against the node's declared schema. */
    std::vector<MetricSample> samples;

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

inline bool MetricsEventWithinEncodedBound(
    const MetricsEvent& event, const std::size_t limit = 16384U) noexcept {
    std::size_t bytes = 256U;
    const auto add = [&bytes, limit](const std::size_t amount) {
        if (amount > limit || bytes > limit - amount) return false;
        bytes += amount;
        return true;
    };
    const auto add_string = [&add](const std::string& value) {
        std::size_t escaped = 2U;
        for (const unsigned char character : value) {
            const std::size_t encoded = character < 0x20U ? 6U
                : (character == '"' || character == '\\' ? 2U : 1U);
            if (!add(encoded)) return false;
        }
        return add(escaped);
    };
    if (!add_string(event.source) || !add_string(event.event_type) ||
        !add_string(event.target.node_id) ||
        !add_string(event.target.source_node_id) ||
        !add_string(event.target.source_port_kind) ||
        !add_string(event.target.target_node_id) ||
        !add_string(event.target.target_port_kind)) return false;
    const auto add_port = [&add, &add_string](const auto& port) {
        if (std::holds_alternative<std::string>(port) &&
            !add_string(std::get<std::string>(port))) return false;
        if (!add(32U)) return false;
        return true;
    };
    // Inspect publisher-owned variants by reference. An initializer list
    // would copy both strings before their size is validated and could throw
    // from this noexcept boundary for a deliberately oversized port name.
    if (!add_port(event.target.source_port) ||
        !add_port(event.target.target_port)) return false;
    for (const auto& [key, value] : event.data) {
        if (!add(8U) || !add_string(key) || !add_string(value)) return false;
    }
    for (const auto& sample : event.samples) {
        if (!add(192U) || !add_string(sample.metric_id) ||
            !add_string(sample.scalar_type) || !add_string(sample.unit) ||
            !add_string(sample.semantics) || !add_string(sample.aggregation) ||
            !add_string(sample.availability_rule) ||
            !add_string(sample.unavailable_reason)) return false;
        if (std::holds_alternative<std::string>(sample.value)) {
            if (!add_string(std::get<std::string>(sample.value))) return false;
        } else if (!add(64U)) return false;
    }
    return bytes <= limit;
}

}  // namespace app::metrics
