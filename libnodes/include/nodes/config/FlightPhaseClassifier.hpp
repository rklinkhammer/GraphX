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
#include <chrono>
#include <functional>
#include <cstdint>

namespace avionics {

/**
 * @class FlightPhaseClassifier
 * @brief Detects flight phase transitions from StateVector data.
 *
 * Implements a state machine to classify rocket flight into 6 distinct phases:
 * Rail → Boost → Coast → Apogee → Descent → Landing
 *
 * Uses hysteresis thresholds to prevent oscillation during phase transitions.
 * Each transition is confirmed by multiple samples before committing to new phase.
 *
 * Thread-safe: Single-threaded use only. Not designed for concurrent calls.
 */
class FlightPhaseClassifier {
public:
    /**
     * @enum FlightPhase
     * @brief Six distinct flight phases
     */
    enum class FlightPhase {
        Rail,      // 0: Pre-ignition (on launcher rail, on ground)
        Boost,     // 1: Motor burn (high acceleration, ascending)
        Coast,     // 2: Motor off, ascending under gravity alone
        Apogee,    // 3: Velocity peak (transition point, brief)
        Descent,   // 4: Descending (free-fall or parachute descent)
        Landing    // 5: At rest on ground (terminal state)
    };

    /**
     * @struct PhaseTransition
     * @brief Snapshot of phase transition event
     */
    struct PhaseTransition {
        FlightPhase current_phase;   ///< Current phase after this classify call
        FlightPhase previous_phase;  ///< Previous phase before transition
        uint64_t timestamp_us;       ///< Timestamp of transition (microseconds)
        float altitude_m;            ///< Altitude at transition
        float velocity_mps;          ///< Velocity at transition (m/s)
        bool is_transition;          ///< true if phase changed, false if same phase
    };

    /**
     * @brief Hysteresis thresholds for phase transitions.
     * Prevents oscillation near phase boundaries.
     */
    struct Thresholds {
        // Altitude thresholds (meters)
        float altitude_ground_m = 10.0f;           ///< Below this = ground/rail
        float altitude_boost_min_m = 1.0f;         ///< Minimum altitude to sustain boost

        // Velocity thresholds (m/s)
        float velocity_apogee_mps = 1.0f;          ///< Velocity within ±this = apogee
        float velocity_descent_mps = -5.0f;        ///< Descent starts below this

        // Acceleration thresholds (m/s²)
        float accel_boost_threshold_mps2 = 5.0f;   ///< Above this = boost phase
        float accel_boost_exit_mps2 = 3.0f;        ///< Exit boost below this (hysteresis)
        float accel_coast_nominal_mps2 = -9.81f;   ///< Expected gravity during coast

        // Duration thresholds (microseconds)
        uint64_t apogee_min_duration_us = 500000;    ///< Min time at apogee (0.5s)
        uint64_t landing_min_duration_us = 1000000;  ///< Min time at landing (1.0s)
        uint64_t boost_confirm_duration_us = 100000; ///< Time to confirm boost (0.1s)
    };

    /**
     * @brief Constructor - initializes phase to Rail
     */
    FlightPhaseClassifier();

    /**
     * @brief Destructor
     */
    virtual ~FlightPhaseClassifier() = default;

    /**
     * @brief Classify flight phase from StateVector.
     * 
     * Called once per input sample (typically at 50 Hz).
     * Updates internal state machine and returns current phase + transition info.
     *
     * @param state StateVector with altitude, velocity, acceleration
     * @param timestamp_us Timestamp in microseconds (for phase duration tracking)
     * @return PhaseTransition containing current phase and transition flag
     */
    PhaseTransition Classify(const sensors::StateVector& state, uint64_t timestamp_us);

    /**
     * @brief Get current flight phase
     * @return Current FlightPhase enum
     */
    FlightPhase GetCurrentPhase() const { return current_phase_; }

    /**
     * @brief Get previous flight phase (before last transition)
     * @return Previous FlightPhase enum
     */
    FlightPhase GetPreviousPhase() const { return previous_phase_; }

    /**
     * @brief Get last recorded phase transition
     * @return PhaseTransition struct from last Classify() call that changed phase
     */
    const PhaseTransition& GetLastTransition() const { return last_transition_; }

    /**
     * @brief Optional callback on phase change
     * Can be set by user to be notified of phase transitions.
     * Called from within Classify() if phase changes.
     */
    std::function<void(FlightPhase)> on_phase_change;

    /**
     * @brief Get reference to hysteresis thresholds (for testing/tuning)
     * @return Reference to current threshold set
     */
    Thresholds& GetThresholds() { return thresholds_; }
    const Thresholds& GetThresholds() const { return thresholds_; }

private:
    // ===== Phase state machine =====
    FlightPhase current_phase_ = FlightPhase::Rail;
    FlightPhase previous_phase_ = FlightPhase::Rail;
    PhaseTransition last_transition_;

    // ===== Hysteresis thresholds =====
    Thresholds thresholds_;

    // ===== Phase duration tracking =====
    uint64_t phase_start_timestamp_us_ = 0;  ///< When current phase started
    uint64_t time_in_current_phase_us_ = 0;  ///< How long in current phase

    // ===== Metrics for phase detection =====
    float peak_altitude_m_ = 0.0f;           ///< Maximum altitude seen so far
    int velocity_zero_crossings_ = 0;        ///< Count of velocity sign changes
    float last_velocity_mps_ = 0.0f;         ///< Previous sample velocity
    bool accel_high_last_sample_ = false;    ///< Was acceleration high last sample?

    // ===== Hysteresis state flags =====
    bool boost_confirmed_ = false;           ///< Has boost been confirmed for 100ms?
    bool apogee_detected_ = false;           ///< Has apogee velocity crossing been seen?
    bool descent_confirmed_ = false;         ///< Has descent been sustained?

    // ===== Private transition methods =====
    /// @brief Check if should transition from Rail to Boost
    bool ShouldTransitionToBoost(const sensors::StateVector& state);

    /// @brief Check if should transition from Boost to Coast
    bool ShouldTransitionToCoast(const sensors::StateVector& state);

    /// @brief Check if should transition from Coast to Apogee
    bool ShouldTransitionToApogee(const sensors::StateVector& state, float previous_velocity_mps);

    /// @brief Check if should transition from Apogee to Descent
    bool ShouldTransitionToDescent(const sensors::StateVector& state);

    /// @brief Check if should transition from Descent to Landing
    bool ShouldTransitionToLanding(const sensors::StateVector& state);

    // ===== Helper methods =====
    /// @brief Update phase metrics (altitude, velocity zero crossings, etc.)
    void UpdatePhaseMetrics(const sensors::StateVector& state, uint64_t timestamp_us);

    /// @brief Execute phase transition, call callbacks, update history
    void OnPhaseTransition(FlightPhase new_phase, const sensors::StateVector& state, uint64_t timestamp_us);

    /// @brief Reset hysteresis flags for new phase
    void ResetHysteresisForPhase(FlightPhase phase);
};

}  // namespace avionics
