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
 * @file EstimatorBase.hpp
 * @brief Base class for modular state estimators using CRTP pattern
 *
 * Provides template-based estimator pattern with common configuration,
 * output, and feedback interface for altitude, velocity, and attitude
 * estimation modules.
 *
 * CRTP Pattern:
 *   class DerivedEstimator : public EstimatorBase<DerivedEstimator>
 *
 * Derived classes implement:
 *   - initialize_impl(const Config&, float initial_state)
 *   - update_impl(float sensor_input, uint64_t timestamp_us) -> Output
 *   - set_feedback_parameters_impl(const FeedbackParams&)
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include <cstdint>

namespace avionics::estimators {

/**
 * Configuration for an estimator.
 * 
 * Common settings shared by all estimator types.
 */
struct EstimatorConfig {
    float sample_rate_hz = 100.0f;      ///< Input sample rate (Hz)
    float confidence_initial = 0.5f;    ///< Initial confidence score [0,1]
    float confidence_min = 0.1f;        ///< Minimum confidence floor
    float confidence_max = 0.95f;       ///< Maximum confidence ceiling
};

/**
 * Output of an estimator.
 * 
 * Common result format for all estimator types.
 */
struct EstimatorOutput {
    float value = 0.0f;                 ///< Estimated value (altitude m, velocity m/s, etc.)
    float confidence = 0.5f;            ///< Confidence score [0,1] (conservative bias)
    uint32_t validity_flags = 0;        ///< Bit flags for anomalies/saturation
    uint64_t timestamp_us = 0;          ///< Timestamp when estimate was generated
    
    /**
     * Check if a validity flag is set.
     */
    bool HasFlag(uint32_t flag) const {
        return (validity_flags & flag) != 0;
    }
};

/**
 * Validity flag definitions (common across all estimators)
 */
namespace ValidityFlags {
    static constexpr uint32_t ACCEL_SATURATED      = 0x0001;  ///< Accelerometer clipping detected
    static constexpr uint32_t SENSOR_DROPOUT       = 0x0002;  ///< Sensor stopped producing data
    static constexpr uint32_t OUTLIER_DETECTED     = 0x0004;  ///< Measurement outside expected range
    static constexpr uint32_t BARO_OUTLIER         = 0x0008;  ///< Barometric pressure anomaly
    static constexpr uint32_t TEMPERATURE_UNSTABLE = 0x0010; ///< Temperature drift detected
    static constexpr uint32_t FILTER_DIVERGENCE    = 0x0020;  ///< Filter state becoming unreliable
    static constexpr uint32_t SATURATION_RECOVERY  = 0x0040;  ///< Recovering from saturation
    static constexpr uint32_t BUFFER_UNDERFLOW     = 0x0080;  ///< Insufficient history for derivative
}

/**
 * Feedback parameters for phase-adaptive tuning.
 * 
 * Flight phase classification and phase-specific parameters
 * sent from FlightMonitorNode to adjust estimator behavior.
 */
struct FeedbackParams {
    enum class FlightPhase : uint8_t {
        RAIL_CONSTRAINED = 0,   ///< On launch rail
        BOOST = 1,              ///< Motor burning
        COAST = 2,              ///< Motor off, ascending
        APOGEE = 3,             ///< Peak altitude (v≈0)
        DESCENT = 4,            ///< Descending
        LANDING = 5             ///< Near ground
    } flight_phase = FlightPhase::RAIL_CONSTRAINED;
    
    // Phase-specific tuning parameters
    float velocity_filter_alpha = 0.1f;  ///< Complementary filter blend (altitude/accel)
    float altitude_confidence = 0.5f;    ///< Confidence multiplier for altitude
    float magnetometer_weight = 0.0f;    ///< Magnetometer influence [0,1] (0=ignore)
    float baro_outlier_threshold = 10.0f; ///< Outlier rejection threshold (pressure Pa)
    
    // Safety margins
    float apogee_hysteresis = 2.0f;      ///< Velocity hysteresis at apogee (m/s)
    float drogue_altitude_margin = 500.0f; ///< Safety margin above drogue deploy
    float main_altitude_margin = 150.0f;  ///< Safety margin above main deploy
};

/**
 * Base class for all state estimators using CRTP pattern.
 * 
 * Provides:
 * - Common configuration interface (Config struct)
 * - Common output interface (Output struct)
 * - Common feedback interface (FeedbackParams)
 * - Virtual dispatch through static_cast to derived type
 * 
 * @tparam Derived The derived estimator class (CRTP)
 */
template<typename Derived>
class EstimatorBase {
public:
    /**
     * Initialize estimator with configuration and initial state.
     * 
     * @param config Configuration settings (sample rate, confidence bounds)
     * @param initial_state Initial value (altitude, velocity, etc.)
     */
    void Initialize(const EstimatorConfig& config, float initial_state) {
        static_cast<Derived*>(this)->initialize_impl(config, initial_state);
    }
    
    /**
     * Update estimator with new sensor measurement.
     * 
     * Computes new state estimate, updates buffers, evaluates validity flags.
     * 
     * @param sensor_input Raw sensor measurement (pressure Pa, accel m/s², etc.)
     * @param timestamp_us Measurement timestamp (microseconds)
     * @return EstimatorOutput with estimated value, confidence, flags, timestamp
     */
    EstimatorOutput Update(float sensor_input, uint64_t timestamp_us) {
        return static_cast<Derived*>(this)->update_impl(sensor_input, timestamp_us);
    }
    
    /**
     * Apply phase-adaptive feedback parameters.
     * 
     * Adjusts filter gains, thresholds, and weights based on detected flight phase.
     * Implementation uses smooth parameter blending (no state resets).
     * 
     * @param params FeedbackParams with phase classification and tuning values
     */
    void SetFeedbackParameters(const FeedbackParams& params) {
        static_cast<Derived*>(this)->set_feedback_parameters_impl(params);
    }
    
    virtual ~EstimatorBase() = default;

protected:
    EstimatorBase() = default;
};

} // namespace avionics::estimators

