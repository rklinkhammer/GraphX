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
 * @file avionics/StateVectorCaptureSinkNode.cpp
 * @brief Implementation of StateVectorCaptureSinkNode
 * 
 * @author GraphX Project
 * @date 2026-01-05
 */

#include "avionics/nodes/StateVectorCaptureSinkNode.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace avionics::nodes {

log4cxx::LoggerPtr StateVectorCaptureSinkNode::log_ =
    log4cxx::Logger::getLogger("avionics.StateVectorCaptureSinkNode");

// ============================================================================
// LIFECYCLE
// ============================================================================

StateVectorCaptureSinkNode::StateVectorCaptureSinkNode()
    : graph::NamedSinkNode<StateVectorCaptureSinkNode,
                                  sensors::StateVector>() {
    SetName("StateVectorCaptureSinkNode");
    LOG4CXX_TRACE(log_, "StateVectorCaptureSinkNode constructed");
}

// ============================================================================
// CONSUME - Main Input Handler
// ============================================================================

bool StateVectorCaptureSinkNode::Consume(
    const sensors::StateVector& state,
    std::integral_constant<std::size_t, 0>) {
    {
        std::lock_guard<std::mutex> lock(states_mutex_);

        // Check if we've exceeded max frame limit
        if (max_captured_frames_ > 0 &&
            captured_states_.size() >= max_captured_frames_) {
            LOG4CXX_WARN(log_,
                         "Max captured frames ("
                             << max_captured_frames_
                             << ") reached, discarding oldest frame");
            captured_states_.erase(captured_states_.begin());
        }

        // Capture the state vector
        captured_states_.push_back(state);

        // Update statistics if enabled
        if (enable_statistics_) {
            UpdateStatisticsLocked(state);

            // Log statistics periodically
            if (captured_states_.size() % statistics_interval_frames_ == 0) {
                LOG4CXX_TRACE(log_,
                              "Captured "
                                  << captured_states_.size()
                                  << " frames, altitude range: ["
                                  << std::fixed << std::setprecision(2)
                                  << stats_.min_altitude << ", "
                                  << stats_.max_altitude << "] m");
            }
        }
    }

    // Always continue consuming
    return true;
}

// ============================================================================
// GETTERS
// ============================================================================

std::vector<sensors::StateVector>
StateVectorCaptureSinkNode::GetCapturedStates() const {
    std::lock_guard<std::mutex> lock(states_mutex_);
    return captured_states_;
}

StateVectorCaptureSinkNode::Statistics
StateVectorCaptureSinkNode::ComputeStatistics() const {
    std::lock_guard<std::mutex> lock(states_mutex_);

    Statistics result;
    result.frame_count = captured_states_.size();

    if (captured_states_.empty()) {
        LOG4CXX_WARN(log_, "ComputeStatistics called with no captured frames");
        return result;
    }

    // Recompute statistics from scratch (in case incremental updates were
    // inaccurate due to floating point)
    float altitude_sum = 0.0f;
    float confidence_sum = 0.0f;
    result.min_altitude = std::numeric_limits<float>::max();
    result.max_altitude = -std::numeric_limits<float>::max();
    result.min_confidence = std::numeric_limits<float>::max();
    result.max_confidence = -std::numeric_limits<float>::max();

    for (size_t i = 0; i < captured_states_.size(); ++i) {
        const auto& state = captured_states_[i];

        // Altitude (Z component of position)
        float altitude = state.position.z;
        result.min_altitude = std::min(result.min_altitude, altitude);
        result.max_altitude = std::max(result.max_altitude, altitude);
        altitude_sum += altitude;

        // Confidence (composite of altitude and velocity confidence)
        // For Phase 5, use a simple average of available confidence metrics
        float confidence =
            (state.confidence.altitude + state.confidence.velocity) / 2.0f;
        result.min_confidence = std::min(result.min_confidence, confidence);
        result.max_confidence = std::max(result.max_confidence, confidence);
        confidence_sum += confidence;

        // Timestamp (first and last)
        if (i == 0) {
            result.timestamp_first = state.GetTimestamp().count();
        }
        result.timestamp_last = state.GetTimestamp().count();
    }

    // Compute means
    result.mean_altitude = altitude_sum / captured_states_.size();
    result.mean_confidence = confidence_sum / captured_states_.size();

    // Compute ranges
    result.altitude_range = result.max_altitude - result.min_altitude;
    result.confidence_range = result.max_confidence - result.min_confidence;

    // Compute duration and frame rate
    if (result.timestamp_last > result.timestamp_first) {
        // Convert nanoseconds to microseconds
        result.duration_us =
            (result.timestamp_last - result.timestamp_first) / 1000;

        // Frame rate = frames / (duration in seconds)
        double duration_s = result.duration_us / 1e6;
        if (duration_s > 0.0) {
            result.frame_rate_hz =
                static_cast<float>(captured_states_.size() / duration_s);
        }
    }

    LOG4CXX_TRACE(log_,
                  "Statistics: "
                      << result.frame_count << " frames, alt=["
                      << std::fixed << std::setprecision(2)
                      << result.min_altitude << ", " << result.max_altitude
                      << "] m, conf=[" << result.min_confidence << ", "
                      << result.max_confidence << "], rate="
                      << result.frame_rate_hz << " Hz");

    return result;
}

size_t StateVectorCaptureSinkNode::GetFrameCount() const {
    std::lock_guard<std::mutex> lock(states_mutex_);
    return captured_states_.size();
}

void StateVectorCaptureSinkNode::Clear() {
    std::lock_guard<std::mutex> lock(states_mutex_);
    captured_states_.clear();

    // Reset statistics
    stats_ = Statistics();
    LOG4CXX_TRACE(log_, "Cleared captured states");
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void StateVectorCaptureSinkNode::Configure(const graph::JsonView& /*cfg*/) {
    // TODO: Parse JsonView configuration
    // For now, accept configuration silently
    LOG4CXX_TRACE(log_, "Configure called");
    // In future:
    // - enable_statistics_: cfg.Get<bool>("enable_statistics", true);
    // - max_captured_frames_: cfg.Get<size_t>("max_captured_frames", 100000);
    // - statistics_interval_frames_: cfg.Get<size_t>("statistics_interval_frames", 1000);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void StateVectorCaptureSinkNode::UpdateStatisticsLocked(
    const sensors::StateVector& state) {
    // PRECONDITION: states_mutex_ must be held by caller

    float altitude = state.position.z;
    float confidence =
        (state.confidence.altitude + state.confidence.velocity) / 2.0f;

    // Update min/max
    stats_.min_altitude = std::min(stats_.min_altitude, altitude);
    stats_.max_altitude = std::max(stats_.max_altitude, altitude);
    stats_.min_confidence = std::min(stats_.min_confidence, confidence);
    stats_.max_confidence = std::max(stats_.max_confidence, confidence);

    // Update frame count and timestamps
    stats_.frame_count++;
    if (stats_.frame_count == 1) {
        stats_.timestamp_first = state.GetTimestamp().count();
    }
    stats_.timestamp_last = state.GetTimestamp().count();

    // Compute duration
    if (stats_.timestamp_last > stats_.timestamp_first) {
        stats_.duration_us =
            (stats_.timestamp_last - stats_.timestamp_first) / 1000;
    }
}

// ============================================================================
// Port Metadata
// ============================================================================

std::vector<graph::PortMetadata> StateVectorCaptureSinkNode::GetInputPortMetadata() const {
    std::vector<graph::PortMetadata> metadata;
    metadata.push_back({
        0,                              // port_index
        "StateVector",                  // payload_type
        "input",                        // direction
        "Input"                         // port_name
    });
    return metadata;
}

}  // namespace avionics::nodes
