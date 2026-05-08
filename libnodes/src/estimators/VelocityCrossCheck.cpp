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

#include "avionics/estimators/VelocityCrossCheck.hpp"
#include <algorithm>
#include <cmath>

namespace avionics::estimators {

VelocityCrossCheck::VelocityCrossCheck()
    : last_result_{},
      velocity_differences_() {
    last_result_.update_count = 0;
    last_result_.is_gps_primary = true;
    last_result_.cross_check_passed = true;
}

VelocityCrossCheckResult VelocityCrossCheck::Update(const GPSVelocityReading& gps_velocity,
                                                     const AccelerationReading& acceleration,
                                                     float delta_time) {
    // Input validation
    if (delta_time <= 0.0f || delta_time > 1.0f) {
        last_result_.cross_check_passed = false;
        return last_result_;
    }
    
    if (!gps_velocity.is_valid && !acceleration.is_valid) {
        last_result_.cross_check_passed = false;
        return last_result_;
    }
    
    // Update integration state if acceleration is valid
    if (acceleration.is_valid) {
        UpdateIntegration(acceleration, delta_time);
    }
    
    // Compute confidence metrics
    float gps_conf = 0.0f;
    if (gps_velocity.is_valid) {
        gps_conf = ComputeGPSConfidence(gps_velocity);
    }
    
    float integrated_conf = ComputeIntegratedConfidence(acceleration);
    
    // Compute velocity difference vector
    float north_diff = gps_velocity.north_velocity_ms - integrated_north_velocity_ms_;
    float east_diff = gps_velocity.east_velocity_ms - integrated_east_velocity_ms_;
    float vert_diff = gps_velocity.vertical_velocity_ms - integrated_vertical_velocity_ms_;
    
    float velocity_diff = std::sqrt(north_diff * north_diff + 
                                    east_diff * east_diff + 
                                    vert_diff * vert_diff);
    
    float horiz_diff = std::sqrt(north_diff * north_diff + east_diff * east_diff);
    float vert_diff_abs = std::abs(vert_diff);
    
    // Detect inconsistencies
    bool inconsistency_detected = DetectInconsistency(
        gps_velocity.GetVelocityMagnitude(),
        std::sqrt(integrated_north_velocity_ms_ * integrated_north_velocity_ms_ +
                 integrated_east_velocity_ms_ * integrated_east_velocity_ms_ +
                 integrated_vertical_velocity_ms_ * integrated_vertical_velocity_ms_),
        horiz_diff,
        vert_diff_abs
    );
    
    // Update history
    velocity_differences_.push_back(velocity_diff);
    if (velocity_differences_.size() > MAX_HISTORY) {
        velocity_differences_.erase(velocity_differences_.begin());
    }
    
    // Determine primary source based on confidence and inconsistency
    bool is_gps_primary = (gps_conf >= integrated_conf);
    if (inconsistency_detected) {
        is_gps_primary = false;  // Favor integrated on inconsistency
    }
    
    // Compute cross-check confidence
    // High confidence when readings match, low when they diverge
    float confidence_penalty = std::min(velocity_diff / max_velocity_difference_ms_, 1.0f);
    float cross_check_conf = std::max(0.0f, 1.0f - (confidence_penalty * 0.5f));
    
    // Weight by source confidences
    if (gps_velocity.is_valid && acceleration.is_valid) {
        cross_check_conf *= std::sqrt(gps_conf * integrated_conf);
    } else if (!gps_velocity.is_valid) {
        cross_check_conf *= integrated_conf;
    } else if (!acceleration.is_valid) {
        cross_check_conf *= gps_conf;
    }
    
    // Build result
    last_result_.integrated_north_velocity_ms = integrated_north_velocity_ms_;
    last_result_.integrated_east_velocity_ms = integrated_east_velocity_ms_;
    last_result_.integrated_vertical_velocity_ms = integrated_vertical_velocity_ms_;
    
    last_result_.velocity_difference_ms = velocity_diff;
    last_result_.horizontal_velocity_difference_ms = horiz_diff;
    last_result_.vertical_velocity_difference_ms = vert_diff_abs;
    
    last_result_.gps_velocity_confidence = gps_conf;
    last_result_.integrated_confidence = integrated_conf;
    last_result_.cross_check_confidence = cross_check_conf;
    
    last_result_.is_gps_primary = is_gps_primary;
    last_result_.cross_check_passed = !inconsistency_detected;
    
    last_result_.update_count++;
    
    return last_result_;
}

void VelocityCrossCheck::SetMaxVelocityDifference(float threshold) {
    max_velocity_difference_ms_ = std::clamp(threshold, 0.1f, 50.0f);
}

void VelocityCrossCheck::SetGPSVelocityWeight(float factor) {
    gps_velocity_weight_ = std::clamp(factor, 0.0f, 1.0f);
}

void VelocityCrossCheck::SetIntegrationTau(float tau) {
    integration_tau_ = std::clamp(tau, 0.1f, 10.0f);
}

void VelocityCrossCheck::SetHVRatioThreshold(float ratio) {
    hv_ratio_threshold_ = std::clamp(ratio, 0.1f, 2.0f);
}

void VelocityCrossCheck::Reset() {
    integrated_north_velocity_ms_ = 0.0f;
    integrated_east_velocity_ms_ = 0.0f;
    integrated_vertical_velocity_ms_ = 0.0f;
    velocity_differences_.clear();
    last_result_.update_count = 0;
    last_result_.cross_check_passed = true;
    last_result_.integrated_north_velocity_ms = 0.0f;
    last_result_.integrated_east_velocity_ms = 0.0f;
    last_result_.integrated_vertical_velocity_ms = 0.0f;
}

float VelocityCrossCheck::ComputeGPSConfidence(const GPSVelocityReading& reading) const {
    if (!reading.is_valid) {
        return 0.0f;
    }
    
    float base_conf = reading.confidence;
    
    // Penalize high DOP
    float dop_factor = 1.0f;
    if (reading.velocity_dop > 5.0f) {
        dop_factor = 5.0f / reading.velocity_dop;  // Scale down high DOP
    }
    
    // Penalize poor fix quality
    float quality_factor = 1.0f;
    if (reading.fix_quality < 2) {
        quality_factor = 0.5f;  // Float fix has lower confidence
    }
    
    // Penalize low satellite count
    float sat_factor = 1.0f;
    if (reading.satellite_count < 6) {
        sat_factor = 0.5f;
    } else if (reading.satellite_count < 10) {
        sat_factor = 0.8f;
    }
    
    return base_conf * dop_factor * quality_factor * sat_factor;
}

float VelocityCrossCheck::ComputeIntegratedConfidence(const AccelerationReading& reading) const {
    if (!reading.is_valid) {
        return 0.0f;
    }
    
    float base_conf = reading.confidence;
    
    // Check for unrealistic accelerations (aircraft normally < 3g)
    float accel_mag = std::sqrt(reading.north_accel_ms2 * reading.north_accel_ms2 +
                                reading.east_accel_ms2 * reading.east_accel_ms2 +
                                (reading.vertical_accel_ms2 - 9.81f) * (reading.vertical_accel_ms2 - 9.81f));
    
    float accel_factor = 1.0f;
    if (accel_mag > 30.0f) {  // ~3g limit
        accel_factor = 30.0f / accel_mag;
    }
    
    return base_conf * accel_factor;
}

bool VelocityCrossCheck::DetectInconsistency(float gps_vel, float integrated_vel,
                                             float horiz_diff, float vert_diff) const {
    // Large absolute difference
    if (horiz_diff > max_velocity_difference_ms_) {
        return true;
    }
    
    // Extreme vertical difference
    if (vert_diff > (max_velocity_difference_ms_ * 0.5f)) {
        return true;
    }
    
    // Horizontal/vertical ratio imbalance
    float ratio = (horiz_diff > 0.1f) ? vert_diff / horiz_diff : 0.0f;
    if (ratio > hv_ratio_threshold_) {
        return true;
    }
    
    // Significant magnitude difference (one is zero, other is high)
    if (gps_vel < 1.0f && integrated_vel > 2.0f) {
        return true;
    }
    if (integrated_vel < 1.0f && gps_vel > 2.0f) {
        return true;
    }
    
    return false;
}

void VelocityCrossCheck::UpdateIntegration(const AccelerationReading& accel, float dt) {
    // Remove gravity from vertical acceleration
    float vert_accel_corrected = RemoveGravity(accel.vertical_accel_ms2);
    
    // Exponential moving average integration
    // v(t+1) = v(t) + a * dt * confidence
    // With exponential smoothing tau
    float alpha = dt / (integration_tau_ + dt);
    
    // Update north velocity
    integrated_north_velocity_ms_ = 
        integrated_north_velocity_ms_ * (1.0f - alpha) +
        (integrated_north_velocity_ms_ + accel.north_accel_ms2 * dt * accel.confidence) * alpha;
    
    // Update east velocity
    integrated_east_velocity_ms_ = 
        integrated_east_velocity_ms_ * (1.0f - alpha) +
        (integrated_east_velocity_ms_ + accel.east_accel_ms2 * dt * accel.confidence) * alpha;
    
    // Update vertical velocity
    integrated_vertical_velocity_ms_ = 
        integrated_vertical_velocity_ms_ * (1.0f - alpha) +
        (integrated_vertical_velocity_ms_ + vert_accel_corrected * dt * accel.confidence) * alpha;
}

}  // namespace avionics::estimators
