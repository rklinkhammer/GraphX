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

#include <cstdint>
#include <array>
#include <cmath>

namespace avionics::estimators {

/**
 * @struct BarometricAltitudeReading
 * @brief Barometric pressure altitude measurement
 * 
 * Pressure-based altitude from barometric sensor. Requires calibration and
 * accounts for temperature variations.
 */
struct BarometricAltitudeReading {
    float altitude_m;              ///< Altitude in meters (MSL)
    float pressure_pa;             ///< Atmospheric pressure in Pascals
    float temperature_c;           ///< Temperature in Celsius
    float vertical_velocity_ms;    ///< Vertical velocity in m/s (derived from pressure rate)
    float confidence;              ///< Measurement confidence [0.0, 1.0]
    bool valid = true;             ///< Reading validity flag
};

/**
 * @struct GPSAltitudeReading
 * @brief GPS-based altitude measurement
 * 
 * GNSS altitude with DOP and fix quality indicators. Typically has lower
 * update rate but better long-term stability than barometer.
 */
struct GPSAltitudeReading {
    float altitude_m;              ///< GPS altitude in meters (WGS84 ellipsoid)
    float vertical_dop;            ///< Vertical dilution of precision (VDOP)
    float horizontal_dop;          ///< Horizontal dilution of precision (HDOP)
    uint8_t fix_quality;           ///< Fix quality (0=none, 1=GPS, 2=DGPS, 3=RTK)
    uint8_t satellite_count;       ///< Number of tracked satellites
    float confidence;              ///< Measurement confidence [0.0, 1.0]
    bool valid = true;             ///< Reading validity flag
};

/**
 * @struct FusedAltitudeState
 * @brief Fused altitude state from barometric and GPS data
 * 
 * Combined altitude estimate with vertical velocity, height gain/loss
 * tracking, and overall system confidence.
 */
struct FusedAltitudeState {
    float altitude_m;              ///< Fused altitude estimate (meters MSL)
    float vertical_velocity_ms;    ///< Vertical velocity (m/s, positive = climbing)
    float height_gain_m;           ///< Cumulative height gain (meters)
    float height_loss_m;           ///< Cumulative height loss (meters)
    float pressure_altitude_m;     ///< Barometric altitude contribution
    float gps_altitude_m;          ///< GPS altitude contribution
    float overall_confidence;      ///< System confidence [0.0, 1.0]
    uint8_t primary_source;        ///< 0=Barometer, 1=GPS, 2=Fused
    bool valid;                    ///< Estimate validity flag
};

/**
 * @class AltitudeFusionEstimator
 * @brief Multi-sensor altitude fusion with height tracking
 * 
 * Combines barometric and GPS altitude measurements using:
 * - Confidence-weighted averaging
 * - Outlier detection for gross error rejection
 * - Vertical velocity filtering
 * - Cumulative height gain/loss tracking
 * - Cross-validation between sources
 * 
 * Algorithm:
 * 1. Validate input readings (confidence > threshold, fix quality for GPS)
 * 2. Compute vertical velocity from pressure rate (barometer)
 * 3. Detect and penalize outliers using confidence weighting
 * 4. Fuse altitudes using weighted average with outlier penalty
 * 5. Apply low-pass filter to vertical velocity
 * 6. Update cumulative height gain/loss from integrated velocity
 * 7. Return fused altitude with confidence and diagnostics
 */
class AltitudeFusionEstimator {
public:
    /**
     * @brief Construct altitude fusion estimator with default configuration
     * 
     * Default settings:
     * - Outlier threshold: 2.0 sigma
     * - GPS confidence multiplier: 0.8 (less preferred than stable barometer)
     * - Max altitude jump: 50 meters
     * - VV low-pass cutoff: 0.5 Hz (tau = 1/(2*pi*f))
     */
    AltitudeFusionEstimator();
    
    /**
     * @brief Construct altitude fusion estimator with custom parameters
     * 
     * @param outlier_threshold Detection threshold in standard deviations
     * @param gps_confidence_mult Confidence multiplier for GPS (0.0 to 1.0)
     * @param max_altitude_jump Maximum allowed altitude jump (meters)
     * @param vv_filter_tau Vertical velocity low-pass filter time constant (seconds)
     */
    AltitudeFusionEstimator(float outlier_threshold, float gps_confidence_mult,
                            float max_altitude_jump, float vv_filter_tau);
    
    /**
     * @brief Fuse barometric and GPS altitude readings
     * 
     * @param baro_reading Barometric pressure altitude measurement
     * @param gps_reading GPS ellipsoid altitude measurement
     * @param dt Time delta since last update (seconds)
     * @return Fused altitude state with height gain/loss
     * 
     * Process:
     * 1. Validate inputs and compute confidence metrics
     * 2. Detect outliers (gross errors, unusual jumps)
     * 3. Apply outlier penalties to confidence values
     * 4. Compute weighted average: (baro*conf_b + gps*conf_g) / (conf_b + conf_g)
     * 5. Filter vertical velocity with low-pass filter
     * 6. Integrate vertical velocity for height gain/loss
     * 7. Return complete altitude state
     */
    FusedAltitudeState Update(const BarometricAltitudeReading& baro_reading,
                              const GPSAltitudeReading& gps_reading,
                              float dt);
    
    /**
     * @brief Process barometric altitude only (GPS unavailable)
     * 
     * @param baro_reading Barometric measurement
     * @param dt Time delta since last update (seconds)
     * @return Altitude state with barometric source only
     */
    FusedAltitudeState UpdateBarometricOnly(const BarometricAltitudeReading& baro_reading,
                                            float dt);
    
    /**
     * @brief Process GPS altitude only (barometer unavailable)
     * 
     * @param gps_reading GPS measurement
     * @param dt Time delta since last update (seconds)
     * @return Altitude state with GPS source only
     */
    FusedAltitudeState UpdateGPSOnly(const GPSAltitudeReading& gps_reading,
                                     float dt);
    
    /**
     * @brief Get the last computed altitude state
     * 
     * @return Previous state (useful if current update fails)
     */
    const FusedAltitudeState& GetLastState() const { return last_state_; }
    
    /**
     * @brief Check if barometric reading was detected as outlier
     * 
     * @return true if barometric was marked as outlier in last update
     */
    bool WasBarometricOutlier() const { return was_baro_outlier_; }
    
    /**
     * @brief Check if GPS reading was detected as outlier
     * 
     * @return true if GPS was marked as outlier in last update
     */
    bool WasGPSOutlier() const { return was_gps_outlier_; }
    
    /**
     * @brief Get the effective weight of barometric reading
     * 
     * After outlier detection and penalty application.
     * Range: [0.0, 1.0]
     * 
     * @return Barometric reading's contribution weight
     */
    float GetBarometricWeight() const { return last_baro_weight_; }
    
    /**
     * @brief Get the effective weight of GPS reading
     * 
     * After outlier detection and penalty application.
     * Range: [0.0, 1.0]
     * 
     * @return GPS reading's contribution weight
     */
    float GetGPSWeight() const { return last_gps_weight_; }
    
    /**
     * @brief Get absolute altitude difference between sources
     * 
     * |barometric_altitude - gps_altitude| from last update
     * 
     * @return Absolute difference in meters
     */
    float GetAltitudeDifference() const { return last_altitude_diff_; }
    
    /**
     * @brief Get cumulative update count
     * 
     * @return Number of times Update() has been called
     */
    uint32_t GetUpdateCount() const { return update_count_; }
    
    /**
     * @brief Reset altitude fusion estimator to initial state
     * 
     * Clears height gain/loss, outlier flags, and update count.
     * Keeps configuration parameters.
     */
    void Reset();
    
    /**
     * @brief Configure outlier detection threshold
     * 
     * @param threshold Standard deviations for outlier boundary
     *                  Value is clamped to [0.5, 5.0]
     */
    void SetOutlierThreshold(float threshold);
    
    /**
     * @brief Configure GPS confidence multiplier
     * 
     * @param multiplier Confidence scaling for GPS readings [0.0, 1.0]
     *                   Accounts for GPS drift and slower update rate
     */
    void SetGPSConfidenceMultiplier(float multiplier);
    
    /**
     * @brief Configure maximum allowed altitude jump
     * 
     * Readings with |Δaltitude| > this value are marked as gross errors.
     * 
     * @param max_jump Maximum altitude change in meters [1.0, 1000.0]
     */
    void SetMaxAltitudeJump(float max_jump);
    
    /**
     * @brief Configure vertical velocity filter time constant
     * 
     * @param tau Low-pass filter time constant in seconds [0.1, 10.0]
     *            Smaller tau = faster response, more noise
     */
    void SetVVFilterTau(float tau);
    
    /**
     * @brief Get current outlier detection threshold
     * @return Threshold in standard deviations
     */
    float GetOutlierThreshold() const { return outlier_threshold_; }
    
    /**
     * @brief Get current GPS confidence multiplier
     * @return Multiplier value [0.0, 1.0]
     */
    float GetGPSConfidenceMultiplier() const { return gps_confidence_mult_; }

private:
    // Configuration parameters
    float outlier_threshold_;           ///< Detection threshold (sigma)
    float gps_confidence_mult_;         ///< GPS confidence scaling
    float max_altitude_jump_;           ///< Max allowed altitude change
    float vv_filter_tau_;               ///< VV filter time constant
    
    // State
    FusedAltitudeState last_state_;     ///< Previous state
    float filtered_vv_ms_;              ///< Filtered vertical velocity
    float cumulative_height_gain_;      ///< Total height gained
    float cumulative_height_loss_;      ///< Total height lost
    
    // Diagnostics
    bool was_baro_outlier_;             ///< Barometric outlier flag
    bool was_gps_outlier_;              ///< GPS outlier flag
    float last_baro_weight_;            ///< Barometric effective weight
    float last_gps_weight_;             ///< GPS effective weight
    float last_altitude_diff_;          ///< |Baro - GPS| altitude
    uint32_t update_count_;             ///< Update call count
    
    // Private helper methods
    
    /**
     * @brief Compute confidence from barometric reading
     * 
     * Factors: sensor confidence, vertical velocity reasonableness,
     * temperature compensation validity
     * 
     * @param baro_reading Input barometric reading
     * @return Confidence value [0.0, 1.0]
     */
    float ComputeBarometricConfidence(const BarometricAltitudeReading& baro_reading) const;
    
    /**
     * @brief Compute confidence from GPS reading
     * 
     * Factors: sensor confidence, DOP values, fix quality, satellite count
     * Lower confidence for poor fix quality or high DOP.
     * 
     * @param gps_reading Input GPS reading
     * @return Confidence value [0.0, 1.0]
     */
    float ComputeGPSConfidence(const GPSAltitudeReading& gps_reading) const;
    
    /**
     * @brief Detect outliers in altitude pair
     * 
     * Checks for:
     * 1. Gross errors: |altitude - mean| > threshold * stddev
     * 2. Excessive jumps: |altitude - last_altitude| > max_jump
     * 
     * @param baro_altitude Barometric altitude
     * @param gps_altitude GPS altitude
     * @param fused_altitude Current fused estimate
     * @param baro_outlier Output: barometric outlier flag
     * @param gps_outlier Output: GPS outlier flag
     */
    void DetectOutliers(float baro_altitude, float gps_altitude,
                       float fused_altitude,
                       bool& baro_outlier, bool& gps_outlier);
    
    /**
     * @brief Apply low-pass filter to vertical velocity
     * 
     * Exponential moving average:
     * vv_filtered = vv_filtered * (1 - alpha) + vv_new * alpha
     * where alpha = dt / (tau + dt)
     * 
     * @param vv_new New vertical velocity
     * @param dt Time delta since last update
     */
    void FilterVerticalVelocity(float vv_new, float dt);
    
    /**
     * @brief Update cumulative height gain and loss
     * 
     * Integrates vertical velocity with sign tracking:
     * - Positive VV contributes to height_gain
     * - Negative VV contributes to height_loss
     * 
     * @param vv_filtered Filtered vertical velocity
     * @param dt Time delta since last update
     */
    void UpdateHeightTracking(float vv_filtered, float dt);
};

} // namespace avionics::estimators
