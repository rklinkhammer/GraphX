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

#include "avionics/config/FlightPhaseClassifier.hpp"
#include <cstdint>
#include <array>

namespace avionics {

/**
 * @class FeedbackParameterComputer
 * @brief Computes phase-adaptive feedback control parameters.
 *
 * Maps flight phases to controller parameters (PD gains, filter constants, etc.)
 * with smooth blending at phase transitions.
 *
 * Provides phase-specific tuning for:
 * - Rail: Ground-based preparation
 * - Boost: Motor burn with high dynamics
 * - Coast: Coasting ascent with low acceleration
 * - Apogee: Velocity peak transition
 * - Descent: Parachute deployment / freefall
 * - Landing: Impact absorption / ground stability
 */
class FeedbackParameterComputer {
public:
    /**
     * @struct ControllerParameters
     * @brief Phase-adaptive control parameters
     */
    struct ControllerParameters {
        // PID controller gains
        float p_gain = 1.0f;        ///< Proportional gain
        float i_gain = 0.0f;        ///< Integral gain
        float d_gain = 0.1f;        ///< Derivative gain
        
        // Filter parameters
        float filter_cutoff_hz = 10.0f;  ///< Low-pass filter cutoff
        float filter_damping = 0.707f;   ///< Filter damping ratio (Butterworth)
        
        // Rate limiters (m/s²)
        float max_accel_command = 20.0f;  ///< Maximum acceleration command
        float min_accel_command = -20.0f; ///< Minimum acceleration command
        
        // Phase-specific tuning
        float phase_weighting = 1.0f;     ///< Blend factor (0-1) for phase mix
        
        // Comparison operator for testing
        bool operator==(const ControllerParameters& other) const {
            const float epsilon = 0.001f;
            return std::abs(p_gain - other.p_gain) < epsilon &&
                   std::abs(i_gain - other.i_gain) < epsilon &&
                   std::abs(d_gain - other.d_gain) < epsilon &&
                   std::abs(filter_cutoff_hz - other.filter_cutoff_hz) < epsilon &&
                   std::abs(filter_damping - other.filter_damping) < epsilon &&
                   std::abs(max_accel_command - other.max_accel_command) < epsilon &&
                   std::abs(min_accel_command - other.min_accel_command) < epsilon;
        }
    };

    /**
     * @struct PhaseParameters
     * @brief Parameter set for a specific flight phase
     */
    struct PhaseParameters {
        FlightPhaseClassifier::FlightPhase phase;
        ControllerParameters parameters;
    };

    /**
     * @brief Constructor - initializes phase parameter table
     */
    FeedbackParameterComputer();

    /**
     * @brief Destructor
     */
    virtual ~FeedbackParameterComputer() = default;

    /**
     * @brief Get parameters for current flight phase
     * 
     * Returns parameters optimized for the given flight phase.
     * If transitioning between phases, blends parameters smoothly.
     *
     * @param current_phase Current flight phase
     * @param previous_phase Previous flight phase (for transition blending)
     * @param transition_progress 0.0-1.0 indicating progress through transition (0=start, 1=end)
     * @return ControllerParameters tuned for current phase
     */
    ControllerParameters GetParameters(
        FlightPhaseClassifier::FlightPhase current_phase,
        FlightPhaseClassifier::FlightPhase previous_phase,
        float transition_progress = 1.0f
    );

    /**
     * @brief Get parameters directly by phase (no blending)
     * 
     * @param phase Flight phase
     * @return ControllerParameters for the phase
     */
    ControllerParameters GetParametersByPhase(FlightPhaseClassifier::FlightPhase phase) const;

    /**
     * @brief Set parameters for a specific phase
     * 
     * Allows runtime tuning of phase parameters.
     *
     * @param phase Target flight phase
     * @param params New parameters
     */
    void SetPhaseParameters(
        FlightPhaseClassifier::FlightPhase phase,
        const ControllerParameters& params
    );

    /**
     * @brief Reset all parameters to default values
     */
    void ResetToDefaults();

    /**
     * @brief Get the parameter table size (number of phases)
     * @return Number of flight phases (should be 6)
     */
    static constexpr size_t GetPhaseCount() { return 6; }

private:
    // Parameter table indexed by FlightPhase enum value
    std::array<PhaseParameters, 6> phase_params_;

    /**
     * @brief Initialize default parameters for each phase
     */
    void InitializeDefaultParameters();

    /**
     * @brief Blend two parameter sets
     * 
     * @param params1 First parameter set
     * @param params2 Second parameter set
     * @param blend_factor Mix factor (0 = all params1, 1 = all params2)
     * @return Blended parameters
     */
    static ControllerParameters BlendParameters(
        const ControllerParameters& params1,
        const ControllerParameters& params2,
        float blend_factor
    );
};

}  // namespace avionics
