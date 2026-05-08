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

/**
 * @file CompletionSignal.hpp
 * @brief Explicit completion signaling for bounded data sources
 * @author Robert Klinkhammer
 * @date December 19, 2025
 * 
 * Provides a signal-based mechanism for bounded data producers to communicate
 * completion when data is exhausted or constraints are met.
 * 
 * DESIGN: Event-Based Signaling
 * - Data producers emit CompletionSignal when data exhausted
 * - Propagates through graph to aggregators/sinks
 * - Decoupled from timeout-based termination
 * - Generic to all bounded data sources (CSV, sensors, simulations, etc)
 * 
 * USAGE EXAMPLE:
 * @code
 * // CompletionSignal emitted by producer when data exhausted:
 * CompletionSignal signal{
 *     .reason = CompletionSignal::Reason::DATA_EXHAUSTED,
 *     .source_name = "source node",
 *     .final_timestamp = std::chrono::nanoseconds{1000000000},
 *     .total_items_produced = 10000,
 *     .diagnostics = ""
 * };
 * 
 * // Received by graph aggregator/sink
 * @endcode
 */

#pragma once

#include <chrono>
#include <string>
#include <cstdint>

namespace graph::message {

/**
 * @struct CompletionSignal
 * @brief Signal emitted by sensors when data production is complete
 * 
 * Used to explicitly communicate completion of CSV data playback or
 * hardware sensor constraints. Replaces implicit timeout-based termination.
 * 
 * This is a sensor-domain concern, not avionics-specific. It defines how
 * all sensors communicate completion status regardless of source (CSV, hardware, etc).
 */
struct CompletionSignal {
    /**
     * @enum Reason
     * @brief Reason codes for completion signals (generic sensor semantics)
     */
    enum class Reason : uint8_t {
        CSV_DATA_EXHAUSTED = 0,      ///< Normal completion - all CSV samples produced
        SENSOR_ERROR = 1,             ///< Sensor encountered error condition
        HARDWARE_MALFUNCTION = 2,     ///< Hardware sensor failure
        USER_REQUESTED = 3,           ///< User explicitly stopped sensor
        TIMEOUT = 4                   ///< Timeout waiting for hardware response
    };

    /**
     * @brief Reason for signal emission
     */
    Reason reason = Reason::CSV_DATA_EXHAUSTED;

    /**
     * @brief Name of node emitting signal
     */
    std::string node_name_;

    /**
     * @brief Timestamp of final valid sample produced
     * Represents the time coordinate of the last sample.
     * For hardware: time when last measurement was taken.
     */
    std::chrono::nanoseconds final_timestamp{0};

    /**
     * @brief Total number of samples successfully produced
     * Includes all samples emitted before this signal.
     * For hardware: number of measurements taken.
     */
    uint64_t total_samples_produced = 0;

    /**
     * @brief Diagnostic information
     * Optional string describing completion details.
     * Examples: "All 10000 samples processed", "Error: I2C timeout"
     */
    std::string diagnostics;

    /**
     * @brief Construct default CompletionSignal
     */
    CompletionSignal() = default;

    /**
     * @brief Construct CompletionSignal with all fields
     */
    CompletionSignal(
        Reason reason_,
        std::string node_name_,
        std::chrono::nanoseconds final_timestamp_,
        uint64_t total_samples_,
        std::string diagnostics_ = ""
    )
        : reason(reason_)
        , node_name_(std::move(node_name_))
        , final_timestamp(final_timestamp_)
        , total_samples_produced(total_samples_)
        , diagnostics(std::move(diagnostics_))
    {
    }

    /**
     * @brief Check if signal indicates successful completion
     * @return true if reason is CSV_DATA_EXHAUSTED, false otherwise
     */
    [[nodiscard]] bool is_successful() const {
        return reason == Reason::CSV_DATA_EXHAUSTED;
    }

    /**
     * @brief Check if signal indicates an error condition
     * @return true if reason indicates error, false otherwise
     */
    [[nodiscard]] bool is_error() const {
        return reason == Reason::SENSOR_ERROR ||
               reason == Reason::HARDWARE_MALFUNCTION ||
               reason == Reason::TIMEOUT;
    }

    /**
     * @brief Get human-readable reason string
     * @return String description of reason code
     */
    [[nodiscard]] std::string reason_str() const {
        switch (reason) {
            case Reason::CSV_DATA_EXHAUSTED:
                return "CSV_DATA_EXHAUSTED";
            case Reason::SENSOR_ERROR:
                return "SENSOR_ERROR";
            case Reason::HARDWARE_MALFUNCTION:
                return "HARDWARE_MALFUNCTION";
            case Reason::USER_REQUESTED:
                return "USER_REQUESTED";
            case Reason::TIMEOUT:
                return "TIMEOUT";
            default:
                return "UNKNOWN";
        }
    }
};

}  // namespace graphsim::engine

