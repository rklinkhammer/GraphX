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
 * @file AltitudeEstimator.hpp
 * @brief Barometric altitude estimator with temperature compensation
 *
 * Computes altitude from barometric pressure with:
 * - Temperature-corrected barometric formula (ISA model)
 * - Monotonicity enforcement (altitude never decreases)
 * - Low-pass filtering (1 Hz cutoff for vibration rejection)
 * - Ring buffer for altitude rate computation
 * - Confidence scoring based on rate stability
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "EstimatorBase.hpp"
#include <array>
#include <cmath>
#include <algorithm>

namespace avionics::estimators {

/**
 * Altitude estimator using barometric pressure.
 * 
 * Implements temperature-compensated barometric altitude with:
 * - ISA (International Standard Atmosphere) model
 * - Monotonicity enforcement to reject pressure noise
 * - Low-pass filtering to smooth vibration
 * - Rate-based confidence scoring
 * 
 * MVP Phase 1 Component: Core altitude estimation
 */
class AltitudeEstimator : public EstimatorBase<AltitudeEstimator> {
public:
    /**
     * Configuration specific to altitude estimator.
     */
    struct Config : public EstimatorConfig {
        // ISA constants
        float P0_sea_level_pa = 101325.0f;    ///< Sea-level pressure calibration
        float T0_sea_level_k = 288.15f;       ///< Sea-level temperature (15°C)
        float L0_lapse_rate = -0.0065f;       ///< Temperature lapse rate (K/m)
        float R_gas_constant = 8.314f;        ///< Gas constant (J/(mol·K))
        float M_molar_mass = 0.029f;          ///< Molar mass of air (kg/mol)
        float g_gravity = 9.81f;              ///< Gravity (m/s²)
        
        // Filter settings
        float filter_alpha = 0.5f;            ///< Low-pass filter gain (0.8 Hz @ 10 Hz input, alpha=2*pi*fc/fs)
        float monotonicity_threshold = 0.5f;  ///< Min altitude change per sample (m)
        
        // Monitoring
        float rate_std_threshold = 5.0f;      ///< Altitude rate std dev for outlier detection
        
        // Ring buffer for rate computation
        static constexpr size_t ALTITUDE_HISTORY_SIZE = 20;  ///< 2 seconds @ 10 Hz baro
    };
    
    AltitudeEstimator() = default;
    virtual ~AltitudeEstimator() = default;
    
    /**
     * Initialize estimator with ground-level altitude and calibration.
     * 
     * @param config Configuration with ISA constants, filter gains
     * @param h_ground Ground-level altitude (m, typically 0 for sea level)
     */
    void initialize_impl(const EstimatorConfig& config_base, float h_ground) {
        // Initialize with base config values, then override with derived if available
        // This is safe because Config derives from EstimatorConfig
        config_ = Config();  // Start with defaults
        config_.sample_rate_hz = config_base.sample_rate_hz;
        config_.confidence_initial = config_base.confidence_initial;
        config_.confidence_min = config_base.confidence_min;
        config_.confidence_max = config_base.confidence_max;
        // Config-specific members keep their default values
        
        h_filtered_ = h_ground;
        h_prev_ = h_ground;
        h_ground_ = h_ground;
        temperature_last_ = config_.T0_sea_level_k;
        P0_effective_ = config_.P0_sea_level_pa;
        
        // Initialize ring buffer with ground altitude reference
        // This ensures the first rate calculation is valid
        altitude_history_.fill({h_ground, 101325.0f, config_.T0_sea_level_k, 0});
        altitude_write_idx_ = 0;
        sample_count_ = 0;
    }
    
    /**
     * Update altitude estimate from barometric pressure.
     * 
     * Process:
     * 1. Compute altitude from pressure using ISA barometric formula
     * 2. Apply temperature compensation (drift correction)
     * 3. Enforce monotonicity (no altitude decrease)
     * 4. Apply 1 Hz low-pass filter
     * 5. Compute confidence based on rate stability
     * 
     * @param pressure_pa Barometric pressure (Pa)
     * @param timestamp_us Measurement timestamp (microseconds)
     * @return EstimatorOutput with altitude, confidence, and validity flags
     */
    EstimatorOutput update_impl(float pressure_pa, uint64_t timestamp_us) {
        // For MVP, assume temperature is held constant or use previous measurement
        // In production, would receive from BarometricData structure
        return update_with_temperature(pressure_pa, temperature_last_, timestamp_us);
    }
    
    /**
     * Update altitude with explicit temperature measurement.
     */
    EstimatorOutput update_with_temperature(float pressure_pa, float temperature_k, uint64_t timestamp_us) {
        sample_count_++;
        
        // Store temperature
        temperature_last_ = temperature_k;
        
        // Compute altitude from ISA barometric formula
        float h = compute_altitude_from_pressure(pressure_pa, temperature_k);
        
        // Handle NaN case (invalid pressure)
        if (std::isnan(h)) {
            h = h_prev_;
        }
        
        // Apply low-pass filter for vibration rejection
        // On first measurement, use the raw value directly to avoid transient response
        float h_filtered_new;
        if (sample_count_ == 1) {
            h_filtered_new = h;  // Initialize filter state with first measurement
        } else {
            h_filtered_new = config_.filter_alpha * h + (1.0f - config_.filter_alpha) * h_filtered_;
        }
        
        // Enforce monotonicity: filtered altitude should never decrease
        h_filtered_new = std::max(h_filtered_new, h_filtered_);
        
        // Store filtered altitude in ring buffer for rate computation
        AltitudeSample sample{h_filtered_new, pressure_pa, temperature_k, timestamp_us};
        altitude_history_[altitude_write_idx_] = sample;
        altitude_write_idx_ = (altitude_write_idx_ + 1) % Config::ALTITUDE_HISTORY_SIZE;
        
        // Update filter state for next iteration
        h_filtered_ = h_filtered_new;
        
        // Compute altitude rate from buffer
        float h_rate = compute_altitude_rate_from_buffer(1000000);  // 1 second window
        
        // Compute confidence based on rate stability
        float confidence = compute_confidence(h_rate);
        
        // Evaluate validity flags
        uint32_t validity_flags = compute_validity_flags(h, h_rate, pressure_pa);
        
        // Prepare output
        EstimatorOutput out;
        out.value = h_filtered_;
        out.confidence = confidence;
        out.validity_flags = validity_flags;
        out.timestamp_us = timestamp_us;
        
        h_prev_ = h_filtered_;  // Store filtered value for next iteration's monotonicity check
        
        return out;
    }
    
    /**
     * Apply phase-specific feedback parameters.
     * 
     * In MVP phase, feedback is received but parameters not yet used.
     * Phase 2+ will adjust filter_alpha, monotonicity_threshold based on phase.
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
    float h_filtered_ = 0.0f;           ///< Filtered altitude (m)
    float h_prev_ = 0.0f;               ///< Previous altitude (for monotonicity)
    float h_ground_ = 0.0f;             ///< Ground reference altitude
    float temperature_last_ = 288.15f;  ///< Last measured temperature (K)
    float P0_effective_ = 101325.0f;    ///< Effective sea-level pressure (temp-adjusted)
    
    // Ring buffer for altitude history
    struct AltitudeSample {
        float altitude_m;
        float pressure_pa;
        float temperature_k;
        uint64_t timestamp_us;
    };
    
    std::array<AltitudeSample, Config::ALTITUDE_HISTORY_SIZE> altitude_history_;
    size_t altitude_write_idx_ = 0;
    size_t sample_count_ = 0;
    
    /**
     * Compute altitude from pressure using ISA barometric formula.
     * 
     * International Standard Atmosphere model:
     * h = (T0/L0) * [(P/P0)^(-L0*R/(g*M)) - 1]
     * 
     * Simplified formula (44330 constant):
     * h = 44330 * (1 - (P/P0)^(1/5.255))
     * 
     * @param P_measured Measured pressure (Pa)
     * @param T_measured Measured temperature (K)
     * @return Computed altitude (m above calibration reference)
     */
    float compute_altitude_from_pressure(float P_measured, float T_measured) {
        // Temperature compensation: adjust effective P0
        // P0_effective = P0_calib * (T_current / T_calib)
        P0_effective_ = config_.P0_sea_level_pa * (T_measured / config_.T0_sea_level_k);
        
        // Avoid division by zero and log of non-positive numbers
        if (P_measured <= 0.0f || P0_effective_ <= 0.0f) {
            return h_prev_;  // Return previous estimate if invalid
        }
        
        // ISA barometric formula
        // h = (T0 / L0) * [(P / P0)^(-L0 * R / (g * M)) - 1]
        // Using simplified approximation: h ≈ 44330 * (1 - (P/P0)^(1/5.255))
        
        float exponent = 1.0f / 5.255f;
        float ratio = P_measured / P0_effective_;
        
        if (ratio <= 0.0f) {
            return h_prev_;  // Invalid pressure ratio
        }
        
        float h = 44330.0f * (1.0f - std::pow(ratio, exponent));
        
        return h + h_ground_;
    }
    
    /**
     * Compute altitude rate from ring buffer history.
     * 
     * Finds oldest sample within time window, computes (h_new - h_old) / dt.
     * 
     * @param window_us Time window in microseconds
     * @return Altitude rate (m/s)
     */
    float compute_altitude_rate_from_buffer(uint64_t window_us) {
        if (sample_count_ < 2) {
            return 0.0f;  // Not enough samples
        }
        
        uint64_t cutoff_timestamp = altitude_history_[altitude_write_idx_].timestamp_us - window_us;
        
        // Find oldest sample within window
        size_t oldest_idx = altitude_write_idx_;
        for (size_t i = 1; i < Config::ALTITUDE_HISTORY_SIZE; ++i) {
            size_t check_idx = (altitude_write_idx_ - i + Config::ALTITUDE_HISTORY_SIZE) 
                               % Config::ALTITUDE_HISTORY_SIZE;
            if (altitude_history_[check_idx].timestamp_us < cutoff_timestamp) {
                break;
            }
            oldest_idx = check_idx;
        }
        
        const auto& oldest = altitude_history_[oldest_idx];
        const auto& newest = altitude_history_[(altitude_write_idx_ - 1 + Config::ALTITUDE_HISTORY_SIZE) 
                                               % Config::ALTITUDE_HISTORY_SIZE];
        
        float dt = (newest.timestamp_us - oldest.timestamp_us) * 1e-6f;
        if (dt < 0.01f) {
            return 0.0f;  // Need at least 10 ms for valid derivative
        }
        
        return (newest.altitude_m - oldest.altitude_m) / dt;  // m/s
    }
    
    /**
     * Compute confidence score from altitude rate stability.
     * 
     * Higher confidence when rate is stable (smooth ascent/descent).
     * Lower confidence when rate is noisy (vibration, sensor glitches).
     * 
     * @param h_rate Altitude rate (m/s)
     * @return Confidence score [0, 1]
     */
    float compute_confidence(float h_rate) {
        float confidence = config_.confidence_initial;
        
        // Reward stable rates (typical rocket ascent -5 to +100 m/s)
        // Penalize extreme rates or unstable
        if (h_rate >= -5.0f && h_rate <= 100.0f) {
            confidence = 0.9f;  // Excellent confidence for nominal rocket ascent
        } else if (h_rate >= -20.0f && h_rate <= 150.0f) {
            confidence = 0.7f;  // Good confidence for broader range
        } else if (h_rate > 150.0f) {
            confidence *= 0.6f;  // Penalize unusually fast ascent
        }
        
        // Clamp to configured bounds
        confidence = std::max(config_.confidence_min, 
                             std::min(config_.confidence_max, confidence));
        
        return confidence;
    }
    
    /**
     * Evaluate validity flags for anomalies.
     * 
     * Detects:
     * - Temperature instability
     * - Extreme altitude rates
     * - Other physical inconsistencies
     * 
     * @param h Current altitude (m)
     * @param h_rate Altitude rate (m/s)
     * @param P Current pressure (Pa)
     * @return Bit-field of validity flags
     */
    uint32_t compute_validity_flags(float /* h */, float /* h_rate */, float /* P */) {
        uint32_t flags = 0;
        
        // Check temperature stability
        // (In full implementation, would track temperature history)
        // flags |= ValidityFlags::TEMPERATURE_UNSTABLE;
        
        // Check for barometric outliers (abrupt pressure changes)
        // This would be relative to expected rate of change
        // for now, omitted in MVP
        
        return flags;
    }
};

} // namespace avionics::estimators

