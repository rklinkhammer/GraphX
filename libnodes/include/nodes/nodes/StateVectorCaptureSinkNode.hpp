// MIT License
//
// Copyright (c) 2025 graphlib contributors
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
 * @file avionics/StateVectorCaptureSinkNode.hpp
 * @brief Sink node for capturing and analyzing StateVector output from AltitudeFusionNode
 * 
 * Provides thread-safe capture of all StateVector messages for:
 * - Unit test validation
 * - Integration test verification
 * - Post-flight analysis
 * - Flight recorder data collection
 * 
 * @author GraphX Project
 * @date 2026-01-05
 */

#pragma once

#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <log4cxx/logger.h>

#include "graph/NamedNodes.hpp"
#include "graph/IConfigurable.hpp"
#include "sensor/SensorDataTypes.hpp"

namespace avionics::nodes {

/**
 * @class StateVectorCaptureSinkNode
 * @brief Sink node that captures and analyzes flight state vectors
 * 
 * Single input port accepting StateVector messages from AltitudeFusionNode.
 * Provides:
 * - Thread-safe capture of all output frames
 * - Statistical analysis (min/max altitude, confidence, timing)
 * - Metrics collection (frame count, timestamps)
 * - Flight recorder integration support
 * 
 * @invariant captured_states_ is protected by states_mutex_
 * @invariant statistics_ is updated atomically with state capture
 * @invariant All public methods are thread-safe
 */
class StateVectorCaptureSinkNode
    : public graph::NamedSinkNode<
        StateVectorCaptureSinkNode,
        sensors::StateVector>,
      public graph::IConfigurable {
public:
    /**
     * @brief Statistics computed from captured state vectors
     * 
     * Aggregates metrics useful for post-flight analysis and validation.
     */
    struct Statistics {
        /// Number of StateVector frames captured
        size_t frame_count = 0;
        
        /// Minimum altitude across all frames (meters)
        float min_altitude = std::numeric_limits<float>::max();
        
        /// Maximum altitude across all frames (meters)
        float max_altitude = -std::numeric_limits<float>::max();
        
        /// Minimum confidence value across all frames [0, 1]
        float min_confidence = std::numeric_limits<float>::max();
        
        /// Maximum confidence value across all frames [0, 1]
        float max_confidence = -std::numeric_limits<float>::max();
        
        /// Average altitude across all frames (meters)
        float mean_altitude = 0.0f;
        
        /// Average confidence across all frames [0, 1]
        float mean_confidence = 0.0f;
        
        /// Timestamp of first captured frame (nanoseconds)
        uint64_t timestamp_first = 0;
        
        /// Timestamp of last captured frame (nanoseconds)
        uint64_t timestamp_last = 0;
        
        /// Duration from first to last frame (microseconds)
        uint64_t duration_us = 0;
        
        /// Average frame rate (frames per second)
        float frame_rate_hz = 0.0f;
        
        /// Altitude range (max - min, meters)
        float altitude_range = 0.0f;
        
        /// Maximum confidence minus minimum (measure of stability)
        float confidence_range = 0.0f;
    };

    /**
     * @brief Construct a StateVectorCaptureSinkNode
     * 
     * @note Configuration can be customized via Configure() before Start()
     */
    StateVectorCaptureSinkNode();

    /**
     * @brief Virtual destructor
     */
    virtual ~StateVectorCaptureSinkNode() = default;

    /**
     * @brief Consume a StateVector from AltitudeFusionNode
     * 
     * This method is called by the GraphX framework when a StateVector
     * arrives on the input port. It stores the state in a thread-safe
     * manner and updates statistics.
     * 
     * @param state The StateVector to capture
     * @param integral_constant Port identifier (always 0 for this sink)
     * @return true to continue consuming, false to stop (always true)
     * 
     * @thread_safety This method is thread-safe. Multiple threads may call
     * it concurrently; all accesses are protected by states_mutex_.
     * 
     * @note This method never blocks the calling thread (no waiting on
     * external resources, only quick mutex operations).
     */
    bool Consume(const sensors::StateVector& state,
                 std::integral_constant<std::size_t, 0>) override;

    /**
     * @brief Retrieve all captured state vectors
     * 
     * @return Vector of StateVector objects in capture order
     * 
     * @thread_safety This method is thread-safe. A copy of the internal
     * state vector is made and returned under lock.
     * 
     * @note The returned vector may be large if many frames have been
     * captured. For analysis, prefer ComputeStatistics() if only aggregate
     * information is needed.
     */
    std::vector<sensors::StateVector> GetCapturedStates() const;

    /**
     * @brief Compute statistics from captured states
     * 
     * Analyzes all captured StateVector frames and computes aggregate
     * statistics useful for validation:
     * - Altitude min/max/mean
     * - Confidence min/max/mean
     * - Frame timing and rate
     * - Duration and ranges
     * 
     * @return Statistics object with computed values
     * 
     * @thread_safety This method is thread-safe. A snapshot of states
     * is taken under lock for analysis.
     * 
     * @complexity O(n) where n = number of captured frames
     */
    Statistics ComputeStatistics() const;

    /**
     * @brief Get count of captured frames
     * 
     * @return Number of StateVector frames captured so far
     * 
     * @thread_safety This method is thread-safe for read.
     * 
     * @note Useful for quick checks without retrieving all states.
     */
    size_t GetFrameCount() const;

    /**
     * @brief Clear all captured state vectors
     * 
     * Resets internal state and statistics for a new capture session.
     * 
     * @thread_safety This method is thread-safe. All captures are cleared
     * under lock.
     * 
     * @note Called automatically by Init() to ensure clean state.
     */
    void Clear();

    /**
     * @brief IConfigurable interface: Apply configuration
     *
     * Supported configuration keys:
     * - "enable_statistics": bool (default: true)
     * - "max_captured_frames": size_t (default: 100000, 0 = unlimited)
     * - "statistics_interval_frames": size_t (default: 1000)
     *
     * @param cfg Configuration object (JsonView)
     *
     * @see IConfigurable
     */
    virtual void Configure(const graph::JsonView& cfg) override;

    /**
     * @brief Provide input port metadata for graph visualization
     */
    virtual std::vector<graph::PortMetadata> GetInputPortMetadata() const override;

private:
    /// Internal logger instance
    static log4cxx::LoggerPtr log_;

    /// Mutex protecting captured_states_ and statistics_
    mutable std::mutex states_mutex_;

    /// Captured StateVector frames in order
    std::vector<sensors::StateVector> captured_states_;

    /// Current statistics (updated incrementally)
    Statistics stats_;

    /// Configuration: whether to update statistics
    bool enable_statistics_ = true;

    /// Configuration: maximum frames to capture (0 = unlimited)
    size_t max_captured_frames_ = 100000;

    /// Configuration: interval for statistics computation logging
    size_t statistics_interval_frames_ = 1000;

    /**
     * @brief Update statistics with a new state vector
     * 
     * Called by Consume() to incrementally update min/max/sum values.
     * Must be called with states_mutex_ already held.
     * 
     * @param state The newly captured state
     * 
     * @internal
     */
    void UpdateStatisticsLocked(const sensors::StateVector& state);
};

}  // namespace avionics::nodes

