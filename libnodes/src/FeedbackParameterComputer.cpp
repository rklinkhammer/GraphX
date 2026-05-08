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

#include "avionics/config/FeedbackParameterComputer.hpp"
#include <cmath>
#include <algorithm>

namespace avionics {

FeedbackParameterComputer::FeedbackParameterComputer() {
    InitializeDefaultParameters();
}

void FeedbackParameterComputer::InitializeDefaultParameters() {
    // ===== Rail Phase =====
    // Pre-ignition: prepare for boost, minimal control needed
    phase_params_[0].phase = FlightPhaseClassifier::FlightPhase::Rail;
    phase_params_[0].parameters = {
        .p_gain = 0.5f,
        .i_gain = 0.01f,
        .d_gain = 0.05f,
        .filter_cutoff_hz = 5.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 5.0f,
        .min_accel_command = -5.0f,
        .phase_weighting = 1.0f
    };

    // ===== Boost Phase =====
    // Motor burn: aggressive control with high dynamics
    phase_params_[1].phase = FlightPhaseClassifier::FlightPhase::Boost;
    phase_params_[1].parameters = {
        .p_gain = 2.0f,
        .i_gain = 0.05f,
        .d_gain = 0.3f,
        .filter_cutoff_hz = 20.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 50.0f,
        .min_accel_command = -50.0f,
        .phase_weighting = 1.0f
    };

    // ===== Coast Phase =====
    // Motor off ascending: moderate control under gravity
    phase_params_[2].phase = FlightPhaseClassifier::FlightPhase::Coast;
    phase_params_[2].parameters = {
        .p_gain = 1.5f,
        .i_gain = 0.02f,
        .d_gain = 0.2f,
        .filter_cutoff_hz = 15.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 30.0f,
        .min_accel_command = -30.0f,
        .phase_weighting = 1.0f
    };

    // ===== Apogee Phase =====
    // Velocity peak: brief transition, minimal control
    phase_params_[3].phase = FlightPhaseClassifier::FlightPhase::Apogee;
    phase_params_[3].parameters = {
        .p_gain = 1.0f,
        .i_gain = 0.01f,
        .d_gain = 0.15f,
        .filter_cutoff_hz = 10.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 20.0f,
        .min_accel_command = -20.0f,
        .phase_weighting = 1.0f
    };

    // ===== Descent Phase =====
    // Descending: balance drag and stability
    phase_params_[4].phase = FlightPhaseClassifier::FlightPhase::Descent;
    phase_params_[4].parameters = {
        .p_gain = 1.2f,
        .i_gain = 0.015f,
        .d_gain = 0.18f,
        .filter_cutoff_hz = 12.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 25.0f,
        .min_accel_command = -25.0f,
        .phase_weighting = 1.0f
    };

    // ===== Landing Phase =====
    // At rest on ground: minimal control, focus on stability
    phase_params_[5].phase = FlightPhaseClassifier::FlightPhase::Landing;
    phase_params_[5].parameters = {
        .p_gain = 0.3f,
        .i_gain = 0.005f,
        .d_gain = 0.02f,
        .filter_cutoff_hz = 3.0f,
        .filter_damping = 0.707f,
        .max_accel_command = 5.0f,
        .min_accel_command = -5.0f,
        .phase_weighting = 1.0f
    };
}

FeedbackParameterComputer::ControllerParameters FeedbackParameterComputer::GetParameters(
    FlightPhaseClassifier::FlightPhase current_phase,
    FlightPhaseClassifier::FlightPhase previous_phase,
    float transition_progress) {
    
    // Clamp transition progress to [0, 1]
    transition_progress = std::max(0.0f, std::min(1.0f, transition_progress));
    
    // If not transitioning or transition complete, return current phase params
    if (transition_progress >= 1.0f || current_phase == previous_phase) {
        return GetParametersByPhase(current_phase);
    }
    
    // Blend from previous phase to current phase
    // transition_progress = 0.0 → 100% previous phase
    // transition_progress = 1.0 → 100% current phase
    const auto& prev_params = GetParametersByPhase(previous_phase);
    const auto& curr_params = GetParametersByPhase(current_phase);
    
    return BlendParameters(prev_params, curr_params, transition_progress);
}

FeedbackParameterComputer::ControllerParameters FeedbackParameterComputer::GetParametersByPhase(
    FlightPhaseClassifier::FlightPhase phase) const {
    
    // Map phase enum (0-5) to array index
    const int phase_index = static_cast<int>(phase);
    
    // Ensure valid index
    if (phase_index >= 0 && phase_index < 6) {
        return phase_params_[phase_index].parameters;
    }
    
    // Should not reach here, but return Rail phase defaults as fallback
    return phase_params_[0].parameters;
}

void FeedbackParameterComputer::SetPhaseParameters(
    FlightPhaseClassifier::FlightPhase phase,
    const ControllerParameters& params) {
    
    const int phase_index = static_cast<int>(phase);
    
    if (phase_index >= 0 && phase_index < 6) {
        phase_params_[phase_index].parameters = params;
    }
}

void FeedbackParameterComputer::ResetToDefaults() {
    InitializeDefaultParameters();
}

FeedbackParameterComputer::ControllerParameters FeedbackParameterComputer::BlendParameters(
    const ControllerParameters& params1,
    const ControllerParameters& params2,
    float blend_factor) {
    
    // Clamp blend factor to [0, 1]
    blend_factor = std::max(0.0f, std::min(1.0f, blend_factor));
    
    // Linear interpolation between param sets
    const float inv_blend = 1.0f - blend_factor;
    
    return ControllerParameters{
        .p_gain = params1.p_gain * inv_blend + params2.p_gain * blend_factor,
        .i_gain = params1.i_gain * inv_blend + params2.i_gain * blend_factor,
        .d_gain = params1.d_gain * inv_blend + params2.d_gain * blend_factor,
        .filter_cutoff_hz = params1.filter_cutoff_hz * inv_blend + params2.filter_cutoff_hz * blend_factor,
        .filter_damping = params1.filter_damping * inv_blend + params2.filter_damping * blend_factor,
        .max_accel_command = params1.max_accel_command * inv_blend + params2.max_accel_command * blend_factor,
        .min_accel_command = params1.min_accel_command * inv_blend + params2.min_accel_command * blend_factor,
        .phase_weighting = params1.phase_weighting * inv_blend + params2.phase_weighting * blend_factor
    };
}

}  // namespace avionics
