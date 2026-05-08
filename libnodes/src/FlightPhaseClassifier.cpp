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

#include "avionics/config/FlightPhaseClassifier.hpp"
#include "sensor/SensorDataTypes.hpp"
#include <algorithm>
#include <cmath>

namespace avionics {

FlightPhaseClassifier::FlightPhaseClassifier()
    : current_phase_(FlightPhase::Rail),
      previous_phase_(FlightPhase::Rail),
      last_transition_{FlightPhase::Rail, FlightPhase::Rail, 0, 0.0f, 0.0f, false},
      phase_start_timestamp_us_(0),
      time_in_current_phase_us_(0),
      peak_altitude_m_(0.0f),
      velocity_zero_crossings_(0),
      last_velocity_mps_(0.0f),
      accel_high_last_sample_(false),
      boost_confirmed_(false),
      apogee_detected_(false),
      descent_confirmed_(false) {
}

FlightPhaseClassifier::PhaseTransition FlightPhaseClassifier::Classify(
    const sensors::StateVector& state,
    uint64_t timestamp_us) {
    
    // Save previous velocity before updating metrics (needed for zero crossing detection)
    const float previous_velocity_mps = last_velocity_mps_;
    
    // Update metrics from current sample
    UpdatePhaseMetrics(state, timestamp_us);

    // Check for phase transitions in order (prevents impossible transitions)
    bool transitioned = false;
    
    switch (current_phase_) {
        case FlightPhase::Rail:
            if (ShouldTransitionToBoost(state)) {
                OnPhaseTransition(FlightPhase::Boost, state, timestamp_us);
                transitioned = true;
            }
            break;

        case FlightPhase::Boost:
            if (ShouldTransitionToCoast(state)) {
                OnPhaseTransition(FlightPhase::Coast, state, timestamp_us);
                transitioned = true;
            }
            break;

        case FlightPhase::Coast:
            if (ShouldTransitionToApogee(state, previous_velocity_mps)) {
                OnPhaseTransition(FlightPhase::Apogee, state, timestamp_us);
                transitioned = true;
            }
            break;

        case FlightPhase::Apogee:
            if (ShouldTransitionToDescent(state)) {
                OnPhaseTransition(FlightPhase::Descent, state, timestamp_us);
                transitioned = true;
            }
            break;

        case FlightPhase::Descent:
            if (ShouldTransitionToLanding(state)) {
                OnPhaseTransition(FlightPhase::Landing, state, timestamp_us);
                transitioned = true;
            }
            break;

        case FlightPhase::Landing:
            // Terminal state - no transitions out of landing
            break;
    }

    // Build return value
    PhaseTransition result{
        current_phase_,
        previous_phase_,
        timestamp_us,
        state.position.z,
        state.velocity.z,
        transitioned
    };

    return result;
}

bool FlightPhaseClassifier::ShouldTransitionToBoost(const sensors::StateVector& state) {
    // Rail → Boost when:
    // 1. Acceleration exceeds threshold (motor ignition)
    // 2. Still near ground (altitude < 10m)
    // 3. Sustained for 100ms (hysteresis)
    
    const float accel_z = state.acceleration.z;
    const float altitude = state.position.z;

    // Check if acceleration is high enough for boost
    if (accel_z > thresholds_.accel_boost_threshold_mps2 && 
        altitude < thresholds_.altitude_ground_m) {
        
        // First time we see high acceleration?
        if (!accel_high_last_sample_) {
            boost_confirmed_ = false;
        }
        
        // Track if we've sustained high acceleration for boost_confirm_duration
        if (!boost_confirmed_) {
            uint64_t duration = time_in_current_phase_us_;
            if (duration >= thresholds_.boost_confirm_duration_us) {
                boost_confirmed_ = true;
                return true;
            }
        }
        
        accel_high_last_sample_ = true;
    } else {
        accel_high_last_sample_ = false;
        boost_confirmed_ = false;
    }

    return false;
}

bool FlightPhaseClassifier::ShouldTransitionToCoast(const sensors::StateVector& state) {
    // Boost → Coast when:
    // 1. Acceleration drops below threshold (motor burnout)
    // 2. Still ascending (velocity > 0)
    // 3. Above minimum altitude
    
    const float accel_z = state.acceleration.z;
    const float velocity = state.velocity.z;
    const float altitude = state.position.z;

    // Check if acceleration has dropped (motor off)
    // Use exit threshold to prevent oscillation
    if (accel_z < thresholds_.accel_boost_exit_mps2 &&
        velocity > 0.0f &&
        altitude > thresholds_.altitude_boost_min_m) {
        return true;
    }

    return false;
}

bool FlightPhaseClassifier::ShouldTransitionToApogee(
    const sensors::StateVector& state,
    float previous_velocity_mps) {
    // Coast → Apogee when:
    // 1. Velocity approaches zero (within ±1 m/s)
    // 2. This has been confirmed by zero crossing
    // 3. Duration at apogee threshold met
    
    const float velocity = state.velocity.z;

    // Check for velocity zero crossing (sign change)
    if ((previous_velocity_mps > 0.0f && velocity < 0.0f) ||
        (previous_velocity_mps < 0.0f && velocity > 0.0f)) {
        velocity_zero_crossings_++;
        apogee_detected_ = true;
    }

    // Confirm apogee: zero crossing + velocity near zero for sustained period
    if (apogee_detected_ &&
        std::abs(velocity) < thresholds_.velocity_apogee_mps) {
        
        uint64_t duration = time_in_current_phase_us_;
        if (duration >= thresholds_.apogee_min_duration_us) {
            return true;
        }
    }

    return false;
}

bool FlightPhaseClassifier::ShouldTransitionToDescent(const sensors::StateVector& state) {
    // Apogee → Descent when:
    // 1. Velocity becomes negative (confirmed downward)
    // 2. Sustainable descent detected
    
    const float velocity = state.velocity.z;

    // Check if velocity has clearly become negative (descent)
    // Don't require specific acceleration since it could be zero on impact
    if (velocity < -thresholds_.velocity_apogee_mps) {
        return true;
    }

    return false;
}

bool FlightPhaseClassifier::ShouldTransitionToLanding(const sensors::StateVector& state) {
    // Descent → Landing when:
    // 1. Altitude is very close to zero (< 10m)
    // 2. Velocity is very close to zero (< 1 m/s descent)
    // 3. This has been sustained for 1.0 second
    
    const float altitude = state.position.z;
    const float velocity = state.velocity.z;

    // Check if we're near ground with low velocity
    if (altitude < thresholds_.altitude_ground_m &&
        std::abs(velocity) < thresholds_.velocity_apogee_mps) {
        
        // Check if we've been stable at ground for required duration
        uint64_t duration = time_in_current_phase_us_;
        if (duration >= thresholds_.landing_min_duration_us) {
            return true;
        }
    }

    return false;
}

void FlightPhaseClassifier::UpdatePhaseMetrics(
    const sensors::StateVector& state,
    uint64_t timestamp_us) {
    
    // Update peak altitude
    peak_altitude_m_ = std::max(peak_altitude_m_, state.position.z);

    // Update phase duration
    if (phase_start_timestamp_us_ == 0) {
        phase_start_timestamp_us_ = timestamp_us;
    }
    time_in_current_phase_us_ = timestamp_us - phase_start_timestamp_us_;

    // Update velocity tracking for zero crossing detection
    last_velocity_mps_ = state.velocity.z;
}

void FlightPhaseClassifier::OnPhaseTransition(
    FlightPhase new_phase,
    const sensors::StateVector& state,
    uint64_t timestamp_us) {
    
    // Update phase history
    previous_phase_ = current_phase_;
    current_phase_ = new_phase;

    // Record transition event
    last_transition_ = PhaseTransition{
        current_phase_,
        previous_phase_,
        timestamp_us,
        state.position.z,
        state.velocity.z,
        true
    };

    // Reset phase timer
    phase_start_timestamp_us_ = timestamp_us;
    time_in_current_phase_us_ = 0;

    // Reset hysteresis state
    ResetHysteresisForPhase(new_phase);

    // Call optional callback
    if (on_phase_change) {
        on_phase_change(new_phase);
    }
}

void FlightPhaseClassifier::ResetHysteresisForPhase(FlightPhase phase) {
    // Reset all hysteresis flags
    accel_high_last_sample_ = false;
    boost_confirmed_ = false;
    apogee_detected_ = false;
    descent_confirmed_ = false;
    velocity_zero_crossings_ = 0;
    last_velocity_mps_ = 0.0f;

    // Phase-specific resets
    switch (phase) {
        case FlightPhase::Rail:
            peak_altitude_m_ = 0.0f;
            break;
        case FlightPhase::Boost:
            // Keep peak altitude tracking
            break;
        case FlightPhase::Coast:
            // Keep peak altitude, track zero crossing
            break;
        case FlightPhase::Apogee:
            // Confirm we've seen zero crossing
            apogee_detected_ = true;
            velocity_zero_crossings_ = 1;
            break;
        case FlightPhase::Descent:
            // Reset for descent duration tracking
            break;
        case FlightPhase::Landing:
            // Terminal state - no further resets needed
            break;
    }
}

}  // namespace avionics
