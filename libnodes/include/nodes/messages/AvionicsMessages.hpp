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

                   #pragma once

#include "sensor/SensorDataTypes.hpp"
#include "avionics/config/FeedbackParameterComputer.hpp"
#include <cstdint>

namespace avionics {

/**
 * @struct PhaseAdaptiveFeedbackMsg
 * @brief Output message from FlightMonitorNode containing phase info and tuned parameters
 *
 * Combines flight phase classification with adaptive control parameters.
 * Output at 10 Hz (decimated from 50 Hz input).
 */
struct PhaseAdaptiveFeedbackMsg : public sensors::TimestampedData {
    // ===== Flight Phase Information =====
    FlightPhaseClassifier::FlightPhase current_phase = FlightPhaseClassifier::FlightPhase::Rail;
    FlightPhaseClassifier::FlightPhase previous_phase = FlightPhaseClassifier::FlightPhase::Rail;
    uint64_t time_in_phase_us = 0;  ///< How long in current phase (microseconds)
    bool is_phase_transition = false;  ///< true if phase just changed
    
    // ===== Flight State Information =====
    float altitude_m = 0.0f;        ///< Current altitude (meters)
    float velocity_mps = 0.0f;      ///< Vertical velocity (m/s)
    float acceleration_mps2 = 0.0f; ///< Vertical acceleration (m/s²)
    
    // ===== Adaptive Control Parameters =====
    FeedbackParameterComputer::ControllerParameters parameters;
    
    // ===== Transition Blending Information =====
    float transition_progress = 1.0f;  ///< 0.0 = start, 1.0 = complete (for visualization)
    
    // ===== Metadata =====
    uint32_t sample_count = 0;  ///< Number of 50 Hz samples processed (for debugging decimation)

    /**
     * @brief Default constructor - initializes to Rail phase
     */
    PhaseAdaptiveFeedbackMsg() = default;

    /**
     * @brief Construct with timestamp
     * @param ts_us Timestamp in microseconds
     */
    explicit PhaseAdaptiveFeedbackMsg(uint64_t ts_us) {
        timestamp = std::chrono::nanoseconds{ts_us * 1000};
    }
};

}  // namespace avionics
