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

#include "avionics/estimators/AltitudeFusionEstimator.hpp"
#include <algorithm>
#include <cmath>

namespace avionics::estimators {

AltitudeFusionEstimator::AltitudeFusionEstimator()
    : outlier_threshold_(2.0f),
      gps_confidence_mult_(0.8f),
      max_altitude_jump_(50.0f),
      vv_filter_tau_(2.0f),
      last_state_{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, false},
      filtered_vv_ms_(0.0f),
      cumulative_height_gain_(0.0f),
      cumulative_height_loss_(0.0f),
      was_baro_outlier_(false),
      was_gps_outlier_(false),
      last_baro_weight_(0.0f),
      last_gps_weight_(0.0f),
      last_altitude_diff_(0.0f),
      update_count_(0) {
}

AltitudeFusionEstimator::AltitudeFusionEstimator(float outlier_threshold,
                                                 float gps_confidence_mult,
                                                 float max_altitude_jump,
                                                 float vv_filter_tau)
    : outlier_threshold_(std::max(0.5f, std::min(5.0f, outlier_threshold))),
      gps_confidence_mult_(std::max(0.0f, std::min(1.0f, gps_confidence_mult))),
      max_altitude_jump_(std::max(1.0f, std::min(1000.0f, max_altitude_jump))),
      vv_filter_tau_(std::max(0.1f, std::min(10.0f, vv_filter_tau))),
      last_state_{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, false},
      filtered_vv_ms_(0.0f),
      cumulative_height_gain_(0.0f),
      cumulative_height_loss_(0.0f),
      was_baro_outlier_(false),
      was_gps_outlier_(false),
      last_baro_weight_(0.0f),
      last_gps_weight_(0.0f),
      last_altitude_diff_(0.0f),
      update_count_(0) {
}

FusedAltitudeState AltitudeFusionEstimator::Update(const BarometricAltitudeReading& baro_reading,
                                                    const GPSAltitudeReading& gps_reading,
                                                    float dt) {
    FusedAltitudeState state{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, false};
    update_count_++;
    
    // Validate inputs
    if (dt < 1e-6f || dt > 1.0f) {
        state = last_state_;
        return state;
    }
    
    // Compute confidence for each reading
    float baro_confidence = ComputeBarometricConfidence(baro_reading);
    float gps_confidence = ComputeGPSConfidence(gps_reading);
    
    // Scale GPS confidence by multiplier
    gps_confidence *= gps_confidence_mult_;
    
    // Detect outliers
    bool baro_outlier = false;
    bool gps_outlier = false;
    
    // Quick outlier check on individual readings
    if (!baro_reading.valid || baro_confidence < 0.1f) {
        baro_outlier = true;
        baro_confidence = 0.0f;
    }
    if (!gps_reading.valid || gps_confidence < 0.1f) {
        gps_outlier = true;
        gps_confidence = 0.0f;
    }
    
    // Check for gross errors between sources
    float altitude_diff = std::abs(baro_reading.altitude_m - gps_reading.altitude_m);
    last_altitude_diff_ = altitude_diff;
    
    // If readings differ significantly, penalize the less confident one
    if (altitude_diff > 20.0f) {  // More than 20m difference
        if (baro_confidence < gps_confidence) {
            baro_outlier = true;
            baro_confidence *= 0.25f;
        } else {
            gps_outlier = true;
            gps_confidence *= 0.25f;
        }
    }
    
    // Check for excessive altitude jumps
    if (last_state_.valid) {
        float baro_jump = std::abs(baro_reading.altitude_m - last_state_.altitude_m);
        float gps_jump = std::abs(gps_reading.altitude_m - last_state_.altitude_m);
        
        if (baro_jump > max_altitude_jump_) {
            baro_outlier = true;
            baro_confidence *= 0.25f;
        }
        if (gps_jump > max_altitude_jump_) {
            gps_outlier = true;
            gps_confidence *= 0.25f;
        }
    }
    
    // Store outlier flags
    was_baro_outlier_ = baro_outlier;
    was_gps_outlier_ = gps_outlier;
    
    // Compute fused altitude using weighted average
    float weight_sum = baro_confidence + gps_confidence;
    
    if (weight_sum < 1e-6f) {
        // Both readings invalid
        state = last_state_;
        return state;
    }
    
    float fused_altitude = (baro_reading.altitude_m * baro_confidence + 
                           gps_reading.altitude_m * gps_confidence) / weight_sum;
    
    // Store effective weights
    last_baro_weight_ = baro_confidence / weight_sum;
    last_gps_weight_ = gps_confidence / weight_sum;
    
    // Determine primary source based on weight
    uint8_t primary_source = 2;  // Default to fused
    if (last_baro_weight_ > 0.7f) {
        primary_source = 0;  // Barometer dominates
    } else if (last_gps_weight_ > 0.7f) {
        primary_source = 1;  // GPS dominates
    }
    
    // Compute vertical velocity
    float vv_new = 0.0f;
    if (last_state_.valid && dt > 0.01f) {
        vv_new = (fused_altitude - last_state_.altitude_m) / dt;
    } else {
        // Use barometric vertical velocity if available
        vv_new = baro_reading.vertical_velocity_ms;
    }
    
    // Apply low-pass filter to vertical velocity
    FilterVerticalVelocity(vv_new, dt);
    
    // Update cumulative height tracking
    UpdateHeightTracking(filtered_vv_ms_, dt);
    
    // Compute overall confidence as weighted average
    float overall_confidence = (baro_reading.confidence * last_baro_weight_ +
                               gps_reading.confidence * last_gps_weight_);
    
    // Prepare output state
    state.altitude_m = fused_altitude;
    state.vertical_velocity_ms = filtered_vv_ms_;
    state.height_gain_m = cumulative_height_gain_;
    state.height_loss_m = cumulative_height_loss_;
    state.pressure_altitude_m = baro_reading.altitude_m;
    state.gps_altitude_m = gps_reading.altitude_m;
    state.overall_confidence = overall_confidence;
    state.primary_source = primary_source;
    state.valid = true;
    
    last_state_ = state;
    return state;
}

FusedAltitudeState AltitudeFusionEstimator::UpdateBarometricOnly(
    const BarometricAltitudeReading& baro_reading, float dt) {
    
    FusedAltitudeState state{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, false};
    update_count_++;
    
    if (dt < 1e-6f) {
        return last_state_;
    }
    
    if (!baro_reading.valid) {
        return last_state_;
    }
    
    // Use barometric reading directly
    float fused_altitude = baro_reading.altitude_m;
    
    // Compute vertical velocity
    float vv_new = 0.0f;
    if (last_state_.valid && dt > 0.01f) {
        vv_new = (fused_altitude - last_state_.altitude_m) / dt;
    } else {
        vv_new = baro_reading.vertical_velocity_ms;
    }
    
    // Apply low-pass filter
    FilterVerticalVelocity(vv_new, dt);
    
    // Update height tracking
    UpdateHeightTracking(filtered_vv_ms_, dt);
    
    // Barometric-only is less confident
    float confidence = baro_reading.confidence * 0.9f;
    
    state.altitude_m = fused_altitude;
    state.vertical_velocity_ms = filtered_vv_ms_;
    state.height_gain_m = cumulative_height_gain_;
    state.height_loss_m = cumulative_height_loss_;
    state.pressure_altitude_m = baro_reading.altitude_m;
    state.gps_altitude_m = fused_altitude;  // Use baro as fallback
    state.overall_confidence = confidence;
    state.primary_source = 0;  // Barometer only
    state.valid = true;
    
    last_baro_weight_ = 1.0f;
    last_gps_weight_ = 0.0f;
    was_baro_outlier_ = false;
    was_gps_outlier_ = true;
    
    last_state_ = state;
    return state;
}

FusedAltitudeState AltitudeFusionEstimator::UpdateGPSOnly(
    const GPSAltitudeReading& gps_reading, float dt) {
    
    FusedAltitudeState state{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1, false};
    update_count_++;
    
    if (dt < 1e-6f) {
        return last_state_;
    }
    
    if (!gps_reading.valid) {
        return last_state_;
    }
    
    // Use GPS reading directly
    float fused_altitude = gps_reading.altitude_m;
    
    // Compute vertical velocity from GPS altitude change
    float vv_new = 0.0f;
    if (last_state_.valid && dt > 0.01f) {
        vv_new = (fused_altitude - last_state_.altitude_m) / dt;
    }
    
    // Apply low-pass filter
    FilterVerticalVelocity(vv_new, dt);
    
    // Update height tracking
    UpdateHeightTracking(filtered_vv_ms_, dt);
    
    // GPS-only is less confident
    float confidence = gps_reading.confidence * gps_confidence_mult_ * 0.9f;
    
    state.altitude_m = fused_altitude;
    state.vertical_velocity_ms = filtered_vv_ms_;
    state.height_gain_m = cumulative_height_gain_;
    state.height_loss_m = cumulative_height_loss_;
    state.pressure_altitude_m = fused_altitude;  // Use GPS as fallback
    state.gps_altitude_m = gps_reading.altitude_m;
    state.overall_confidence = confidence;
    state.primary_source = 1;  // GPS only
    state.valid = true;
    
    last_baro_weight_ = 0.0f;
    last_gps_weight_ = 1.0f;
    was_baro_outlier_ = true;
    was_gps_outlier_ = false;
    
    last_state_ = state;
    return state;
}

void AltitudeFusionEstimator::Reset() {
    last_state_ = FusedAltitudeState{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2, false};
    filtered_vv_ms_ = 0.0f;
    cumulative_height_gain_ = 0.0f;
    cumulative_height_loss_ = 0.0f;
    was_baro_outlier_ = false;
    was_gps_outlier_ = false;
    update_count_ = 0;
}

void AltitudeFusionEstimator::SetOutlierThreshold(float threshold) {
    outlier_threshold_ = std::max(0.5f, std::min(5.0f, threshold));
}

void AltitudeFusionEstimator::SetGPSConfidenceMultiplier(float multiplier) {
    gps_confidence_mult_ = std::max(0.0f, std::min(1.0f, multiplier));
}

void AltitudeFusionEstimator::SetMaxAltitudeJump(float max_jump) {
    max_altitude_jump_ = std::max(1.0f, std::min(1000.0f, max_jump));
}

void AltitudeFusionEstimator::SetVVFilterTau(float tau) {
    vv_filter_tau_ = std::max(0.1f, std::min(10.0f, tau));
}

float AltitudeFusionEstimator::ComputeBarometricConfidence(
    const BarometricAltitudeReading& baro_reading) const {
    
    // Start with provided confidence
    float confidence = baro_reading.confidence;
    
    // Penalize extreme vertical velocities
    float abs_vv = std::abs(baro_reading.vertical_velocity_ms);
    if (abs_vv > 150.0f) {
        confidence *= 0.7f;  // Penalize extreme ascent
    } else if (abs_vv > 50.0f) {
        confidence *= 0.9f;  // Slight penalty for very fast changes
    }
    
    // Penalize temperature instability
    // (In full implementation, would compare to expected rate)
    
    return std::max(0.0f, std::min(1.0f, confidence));
}

float AltitudeFusionEstimator::ComputeGPSConfidence(
    const GPSAltitudeReading& gps_reading) const {
    
    // Start with provided confidence
    float confidence = gps_reading.confidence;
    
    // Penalize poor fix quality
    if (gps_reading.fix_quality == 0) {
        return 0.0f;  // No fix
    } else if (gps_reading.fix_quality == 1) {
        confidence *= 0.8f;  // Standard GPS fix
    } else if (gps_reading.fix_quality == 2) {
        confidence *= 0.95f;  // DGPS fix
    }
    // fix_quality == 3 (RTK) keeps full confidence
    
    // Penalize low satellite count
    if (gps_reading.satellite_count < 4) {
        confidence *= 0.5f;
    } else if (gps_reading.satellite_count < 6) {
        confidence *= 0.8f;
    }
    
    // Penalize high DOP values
    float vdop = gps_reading.vertical_dop;
    if (vdop > 10.0f) {
        confidence *= 0.5f;
    } else if (vdop > 5.0f) {
        confidence *= 0.8f;
    }
    
    return std::max(0.0f, std::min(1.0f, confidence));
}

void AltitudeFusionEstimator::DetectOutliers(float baro_altitude,
                                             float gps_altitude,
                                             [[maybe_unused]] float fused_altitude,
                                             bool& baro_outlier,
                                             bool& gps_outlier) {
    baro_outlier = false;
    gps_outlier = false;
    
    // Compute mean and stddev
    float mean = (baro_altitude + gps_altitude) / 2.0f;
    float diff = std::abs(baro_altitude - gps_altitude);
    float stddev = diff / 2.0f;  // Rough estimate
    
    float threshold_bound = outlier_threshold_ * stddev;
    
    // Check barometric
    if (std::abs(baro_altitude - mean) > threshold_bound) {
        baro_outlier = true;
    }
    
    // Check GPS
    if (std::abs(gps_altitude - mean) > threshold_bound) {
        gps_outlier = true;
    }
}

void AltitudeFusionEstimator::FilterVerticalVelocity(float vv_new, float dt) {
    // Exponential moving average low-pass filter
    // alpha = dt / (tau + dt)
    float alpha = dt / (vv_filter_tau_ + dt);
    filtered_vv_ms_ = filtered_vv_ms_ * (1.0f - alpha) + vv_new * alpha;
}

void AltitudeFusionEstimator::UpdateHeightTracking(float vv_filtered, float dt) {
    float height_change = vv_filtered * dt;
    
    if (height_change > 0.0f) {
        cumulative_height_gain_ += height_change;
    } else {
        cumulative_height_loss_ += std::abs(height_change);
    }
}

} // namespace avionics::estimators
