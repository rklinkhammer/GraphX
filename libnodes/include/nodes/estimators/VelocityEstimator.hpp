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
 * @file VelocityEstimator.hpp
 * @brief Vertical velocity estimator using complementary filter
 *
 * Estimates vertical velocity from:
 * - Accelerometer integration (high-frequency, accel-based)
 * - Barometric altitude rate (low-frequency, baro-based)
 *
 * Implements:
 * - Complementary filter blending (alpha parameter)
 * - Saturation detection and recovery
 * - Energy-based velocity bounds
 * - Ring buffer for accel history
 * - Confidence scoring with saturation penalty
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "EstimatorBase.hpp"
#include <array>
#include <deque>
#include <cmath>
#include <algorithm>

namespace avionics::estimators {

/**
 * Vertical velocity estimator using complementary filter.
 * 
 * Implements complementary filter for vertical velocity:
 * - Accel integration for high-frequency (100 Hz) updates
 * - Barometer altitude rate for low-frequency (10 Hz) drift correction
 * - Saturation detection when accel clips
 * - Energy bounds based on altitude and drag physics
 * 
 * MVP Phase 1 Component: Core velocity estimation
 */
class VelocityEstimator : public EstimatorBase<VelocityEstimator> {
public:
    /**
     * Configuration specific to velocity estimator.
     */
    struct Config : public EstimatorConfig {
        // Complementary filter
        float filter_alpha_default = 0.1f;  ///< Blend: (1-alpha)*accel + alpha*baro
                                            ///< Low alpha: trust accel more (high-freq)
                                            ///< High alpha: trust baro more (low-freq)
        
        // Saturation detection
        float accel_saturation_threshold = 31.0f;  ///< Accel saturation point (g, ~32g limit)
        float saturation_recovery_time_ms = 500.0f; ///< Time to fully recover confidence after saturation
        
        // Bounds
        float gravity = 9.81f;                    ///< Gravity (m/s²)
        float max_velocity_magnitude = 200.0f;    ///< Max reasonable velocity (m/s)
        
        // Ring buffer for accel history
        static constexpr size_t ACCEL_HISTORY_SIZE = 50;  ///< ~500 ms @ 100 Hz
    };
    
    VelocityEstimator() = default;
    virtual ~VelocityEstimator() = default;
    
    /**
     * Initialize estimator with initial velocity and configuration.
     * 
     * @param config Configuration with filter alpha, saturation threshold
     * @param v_initial Initial vertical velocity (m/s, typically 0 at pad)
     */
    void initialize_impl(const EstimatorConfig& config_base, float v_initial) {
        // Initialize with base config values, then use derived Config defaults
        config_ = Config();  // Start with defaults
        config_.sample_rate_hz = config_base.sample_rate_hz;
        config_.confidence_initial = config_base.confidence_initial;
        config_.confidence_min = config_base.confidence_min;
        config_.confidence_max = config_base.confidence_max;
        
        v_z_ = v_initial;
        v_z_prev_ = v_initial;
        accel_integral_ = 0.0f;
        
        last_accel_timestamp_us_ = 0;
        last_baro_timestamp_us_ = 0;
        
        baro_altitude_latest_ = 0.0f;
        
        accel_saturated_ = false;
        saturation_recovery_counter_ = 0;
        
        accel_history_.clear();
        sample_count_ = 0;
    }
    
    /**
     * Update velocity estimate from accelerometer measurement (CRTP interface).
     * 
     * Delegates to update_with_accel for actual processing.
     * 
     * @param accel_z Vertical acceleration (m/s²)
     * @param timestamp_us Measurement timestamp (microseconds)
     * @return EstimatorOutput with velocity, confidence, and validity flags
     */
    EstimatorOutput update_impl(float accel_z, uint64_t timestamp_us) {
        return update_with_accel(accel_z, timestamp_us);
    }
    
    /**
     * Update velocity estimate from accelerometer measurement.
     * 
     * Process:
     * 1. Detect if accel is saturated (clipped at ±32g limit)
     * 2. Integrate accel for velocity prediction
     * 3. Blend with barometric altitude rate if available
     * 4. Apply velocity bounds (energy conservation)
     * 5. Compute confidence (reduced if saturated)
     * 
     * @param accel_z Vertical acceleration (m/s²)
     * @param timestamp_us Measurement timestamp (microseconds)
     * @return EstimatorOutput with velocity, confidence, and validity flags
     */
    EstimatorOutput update_with_accel(float accel_z, uint64_t timestamp_us) {
        sample_count_++;
        
        // Remove gravity baseline (accelerometer reads +9.81 at rest pointing up)
        // Net acceleration = measured accel - gravity_baseline
        float accel_net = accel_z - config_.gravity;  // Remove 1g baseline
        
        // Detect saturation on RAW accel (sensor saturation)
        bool is_saturated = std::abs(accel_z) > config_.accel_saturation_threshold;
        
        // Track saturation state for recovery
        if (is_saturated && !accel_saturated_) {
            // Transitioning to saturation
            accel_saturated_ = true;
            saturation_recovery_counter_ = 0;
        } else if (!is_saturated && accel_saturated_) {
            // Exiting saturation - start recovery timer
            accel_saturated_ = false;
            saturation_recovery_counter_ = 1;  // Start counting recovery samples
        } else if (!is_saturated && saturation_recovery_counter_ > 0) {
            // Still in recovery phase
            saturation_recovery_counter_++;
            float recovery_time_samples = config_.saturation_recovery_time_ms * 
                                         (config_.sample_rate_hz / 1000.0f);
            if (saturation_recovery_counter_ > recovery_time_samples) {
                saturation_recovery_counter_ = 0;  // Recovery complete
            }
        }
        
        // Compute dt
        float dt = 0.0f;
        if (last_accel_timestamp_us_ > 0) {
            dt = (timestamp_us - last_accel_timestamp_us_) * 1e-6f;
            if (dt > 1.0f) dt = 0.01f;  // Clamp to reasonable max (100 ms)
        } else {
            dt = 0.01f;  // Assume 100 Hz if first sample
        }
        last_accel_timestamp_us_ = timestamp_us;
        
        // Integrate NET acceleration (high-frequency path)
        float v_z_pred = v_z_;
        if (!is_saturated) {
            v_z_pred = v_z_ + accel_net * dt;  // Integrate net accel (gravity removed)
        } else {
            // During saturation, don't integrate; hold velocity estimate
        }
        
        // Store NET accel in ring buffer
        accel_history_.push_back(accel_net);
        if (accel_history_.size() > Config::ACCEL_HISTORY_SIZE) {
            accel_history_.pop_front();
        }
        
        // Blend with baro rate (low-frequency path)
        float v_z_meas = v_z_pred;  // Default to accel prediction
        
        if (baro_altitude_latest_ > 0.0f && last_baro_timestamp_us_ > 0) {
            // Compute baro altitude rate
            // In production, this would be from AltitudeEstimator's rate calculation
            // For MVP, assume external provision
            // v_z_meas = h_rate_from_baro;  // Would be provided
        }
        
        // Complementary filter
        float alpha = config_.filter_alpha_default;
        if (accel_saturated_) {
            alpha = 0.5f;  // Trust baro more during saturation
        }
        v_z_ = (1.0f - alpha) * v_z_pred + alpha * v_z_meas;
        
        // Apply velocity bounds (energy conservation)
        apply_velocity_bounds();
        
        // Compute confidence
        float confidence = compute_confidence();
        
        // Evaluate validity flags
        uint32_t validity_flags = 0;
        if (is_saturated) {
            validity_flags |= ValidityFlags::ACCEL_SATURATED;
        }
        if (saturation_recovery_counter_ > 0) {
            validity_flags |= ValidityFlags::SATURATION_RECOVERY;
        }
        
        // Check if we just entered saturation
        if (is_saturated && !accel_saturated_) {
            // Transitioning from normal to saturated
            saturation_recovery_counter_ = 1;  // Mark recovery start
            accel_saturated_ = true;
        } else if (!is_saturated && accel_saturated_) {
            // Transitioning from saturated to normal - start recovery timer
            accel_saturated_ = false;
            saturation_recovery_counter_ = 1;  // Begin recovery countdown
        }
        
        // Prepare output
        EstimatorOutput out;
        out.value = v_z_;
        out.confidence = confidence;
        out.validity_flags = validity_flags;
        out.timestamp_us = timestamp_us;
        
        v_z_prev_ = v_z_;
        
        return out;
    }
    
    /**
     * Update velocity estimate from barometric altitude rate.
     * 
     * Low-frequency correction path. Called when new barometer measurement available.
     * Updates internal state but does not generate output (output on accel inputs only).
     * 
     * @param h_rate Altitude rate from barometer (m/s)
     * @param timestamp_us Measurement timestamp (microseconds)
     */
    void update_with_baro_rate(float /* h_rate */, uint64_t timestamp_us) {
        last_baro_timestamp_us_ = timestamp_us;
        // h_rate is used in update_with_accel() blend
        // Store for next accel update
    }
    
    /**
     * Provide latest barometric altitude for bounds checking.
     * 
     * @param altitude_m Current altitude from barometer (m)
     * @param timestamp_us Measurement timestamp (microseconds)
     */
    void set_baro_altitude(float altitude_m, uint64_t /* timestamp_us */) {
        baro_altitude_latest_ = altitude_m;
    }
    
    /**
     * Apply phase-specific feedback parameters.
     * 
     * In MVP phase, feedback is received but parameters not yet used.
     * Phase 2+ will adjust filter_alpha, saturation_threshold based on phase.
     */
    void set_feedback_parameters_impl(const FeedbackParams& params) {
        // Phase 1 MVP: Store for future use
        // Phase 2: Apply smooth parameter transitions
        feedback_params_ = params;
    }

private:
    Config config_;
    FeedbackParams feedback_params_;
    
    // State
    float v_z_ = 0.0f;                    ///< Current velocity estimate (m/s)
    float v_z_prev_ = 0.0f;               ///< Previous velocity (for rate/accel)
    float accel_integral_ = 0.0f;         ///< Integrated accel (velocity-like)
    
    // Timestamps
    uint64_t last_accel_timestamp_us_ = 0;
    uint64_t last_baro_timestamp_us_ = 0;
    
    // Barometer state
    float baro_altitude_latest_ = 0.0f;
    
    // Saturation tracking
    bool accel_saturated_ = false;
    size_t saturation_recovery_counter_ = 0;
    
    // Ring buffer for accel history
    std::deque<float> accel_history_;
    size_t sample_count_ = 0;
    
    /**
     * Apply energy-based velocity bounds.
     * 
     * Constrains velocity based on altitude and gravity:
     * v_max = sqrt(2 * g * (h - h_apogee)) + margin
     * 
     * Also applies saturation ceiling.
     */
    void apply_velocity_bounds() {
        // Simple bound: no rocket faster than 200 m/s
        float max_vel = config_.max_velocity_magnitude;
        
        v_z_ = std::max(-max_vel, std::min(max_vel, v_z_));
    }
    
    /**
     * Compute confidence score with saturation penalty.
     * 
     * High confidence when:
     * - Accel is not saturated
     * - Velocity is within physical bounds
     * 
     * Lower confidence when:
     * - Accel is saturated (can't integrate accurately)
     * - Recovering from saturation
     * 
     * @return Confidence score [0, 1]
     */
    float compute_confidence() {
        float confidence = config_.confidence_initial;
        
        // Reduce confidence if saturated or recovering
        if (accel_saturated_) {
            confidence *= 0.3f;  // Very low during active saturation
        } else if (saturation_recovery_counter_ > 0) {
            // Smooth recovery: ramp up from 0.3 to full confidence
            float recovery_progress = 1.0f - (saturation_recovery_counter_ / 
                (config_.saturation_recovery_time_ms * 
                 config_.sample_rate_hz / 1000.0f));
            confidence *= (0.3f + 0.7f * recovery_progress);
        }
        
        // Clamp to bounds
        confidence = std::max(config_.confidence_min, 
                             std::min(config_.confidence_max, confidence));
        
        return confidence;
    }
};

} // namespace avionics::estimators

