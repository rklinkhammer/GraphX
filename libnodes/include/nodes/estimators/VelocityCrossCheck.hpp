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

#include <cmath>
#include <cstdint>
#include <vector>

namespace avionics::estimators {

/**
 * @brief Velocity reading from GPS with confidence metrics
 * @details Contains 3D velocity vector and fix quality indicators
 */
struct GPSVelocityReading {
    float north_velocity_ms{0.0f};      ///< North velocity component (m/s)
    float east_velocity_ms{0.0f};       ///< East velocity component (m/s)
    float vertical_velocity_ms{0.0f};   ///< Vertical velocity component (m/s)
    
    float velocity_dop{10.0f};          ///< Velocity Dilution of Precision (1.0-100.0)
    uint8_t fix_quality{0};             ///< Fix quality (0=none, 1=float, 2=fixed, 3=float RTK, 4=fixed RTK)
    uint8_t satellite_count{0};         ///< Number of satellites used (0-30+)
    
    float confidence{0.5f};             ///< GPS velocity confidence (0.0-1.0)
    bool is_valid{false};               ///< Velocity data is valid
    
    /**
     * @brief Compute ground speed magnitude
     * @return 3D velocity magnitude in m/s
     */
    float GetVelocityMagnitude() const {
        return std::sqrt(north_velocity_ms * north_velocity_ms + 
                        east_velocity_ms * east_velocity_ms + 
                        vertical_velocity_ms * vertical_velocity_ms);
    }
    
    /**
     * @brief Compute horizontal speed magnitude
     * @return 2D velocity magnitude (north/east only) in m/s
     */
    float GetHorizontalSpeed() const {
        return std::sqrt(north_velocity_ms * north_velocity_ms + 
                        east_velocity_ms * east_velocity_ms);
    }
};

/**
 * @brief Acceleration reading with confidence
 * @details Contains 3D acceleration vector from IMU
 */
struct AccelerationReading {
    float north_accel_ms2{0.0f};        ///< North acceleration (m/s²)
    float east_accel_ms2{0.0f};         ///< East acceleration (m/s²)
    float vertical_accel_ms2{9.81f};    ///< Vertical acceleration, including gravity (m/s²)
    
    float confidence{0.9f};             ///< Acceleration confidence (0.0-1.0)
    bool is_valid{false};               ///< Acceleration data is valid
};

/**
 * @brief Result of velocity cross-validation
 * @details Quantifies how well integrated acceleration matches GPS velocity
 */
struct VelocityCrossCheckResult {
    float integrated_north_velocity_ms{0.0f};   ///< Integrated north velocity (m/s)
    float integrated_east_velocity_ms{0.0f};    ///< Integrated east velocity (m/s)
    float integrated_vertical_velocity_ms{0.0f};///< Integrated vertical velocity (m/s)
    
    float velocity_difference_ms{0.0f};         ///< Magnitude of velocity difference vector
    float horizontal_velocity_difference_ms{0.0f}; ///< Horizontal component difference
    float vertical_velocity_difference_ms{0.0f}; ///< Vertical component difference
    
    float gps_velocity_confidence{0.0f};        ///< GPS velocity confidence (0.0-1.0)
    float integrated_confidence{0.0f};          ///< Integrated acceleration confidence (0.0-1.0)
    float cross_check_confidence{0.0f};         ///< Overall cross-check confidence (0.0-1.0)
    
    bool is_gps_primary{true};                  ///< GPS velocity should be trusted more
    bool cross_check_passed{true};              ///< Consistency check passed
    
    uint32_t update_count{0};                   ///< Total number of updates
};

/**
 * @brief Velocity Cross-Validator
 * @details Validates GPS velocity against integrated acceleration vectors
 *          Detects drift, glitches, and inconsistencies
 *          Provides confidence metrics for velocity fusion
 */
class VelocityCrossCheck {
public:
    /**
     * @brief Default constructor
     */
    VelocityCrossCheck();
    
    /**
     * @brief Destructor
     */
    ~VelocityCrossCheck() = default;
    
    /**
     * @brief Update cross-check with new GPS and acceleration readings
     * @param gps_velocity GPS velocity measurement
     * @param acceleration Integrated acceleration reading
     * @param delta_time Time since last update (seconds)
     * @return VelocityCrossCheckResult with confidence metrics
     */
    VelocityCrossCheckResult Update(const GPSVelocityReading& gps_velocity,
                                     const AccelerationReading& acceleration,
                                     float delta_time);
    
    /**
     * @brief Set the maximum acceptable velocity difference threshold
     * @param threshold Velocity difference threshold (m/s), clamped to [0.1, 50.0]
     */
    void SetMaxVelocityDifference(float threshold);
    
    /**
     * @brief Set the GPS velocity trust factor (0.0-1.0)
     * @param factor Weight for GPS velocity in fusion, clamped to [0.0, 1.0]
     */
    void SetGPSVelocityWeight(float factor);
    
    /**
     * @brief Set the acceleration integration time constant
     * @param tau Time constant in seconds for velocity integration, clamped to [0.1, 10.0]
     */
    void SetIntegrationTau(float tau);
    
    /**
     * @brief Set the horizontal/vertical velocity difference ratio threshold
     * @param ratio Ratio (0.0-2.0) for cross-component error detection
     */
    void SetHVRatioThreshold(float ratio);
    
    /**
     * @brief Reset integrated velocity state
     */
    void Reset();
    
    // === Diagnostic Methods ===
    
    /**
     * @brief Check if last GPS reading was flagged as inconsistent
     * @return True if cross-check failed
     */
    bool WasLastCheckFailed() const { return !last_result_.cross_check_passed; }
    
    /**
     * @brief Get the velocity difference magnitude from last update
     * @return Velocity difference in m/s
     */
    float GetLastVelocityDifference() const { return last_result_.velocity_difference_ms; }
    
    /**
     * @brief Get the cross-check confidence
     * @return Confidence value (0.0-1.0)
     */
    float GetCrossCheckConfidence() const { return last_result_.cross_check_confidence; }
    
    /**
     * @brief Get current integrated velocity north component
     * @return Integrated north velocity (m/s)
     */
    float GetIntegratedNorthVelocity() const { return last_result_.integrated_north_velocity_ms; }
    
    /**
     * @brief Get current integrated velocity east component
     * @return Integrated east velocity (m/s)
     */
    float GetIntegratedEastVelocity() const { return last_result_.integrated_east_velocity_ms; }
    
    /**
     * @brief Get current integrated velocity vertical component
     * @return Integrated vertical velocity (m/s)
     */
    float GetIntegratedVerticalVelocity() const { return last_result_.integrated_vertical_velocity_ms; }
    
    /**
     * @brief Check if GPS velocity is primary source
     * @return True if GPS velocity should be trusted
     */
    bool IsGPSPrimary() const { return last_result_.is_gps_primary; }
    
    /**
     * @brief Get total number of updates processed
     * @return Update count
     */
    uint32_t GetUpdateCount() const { return last_result_.update_count; }
    
private:
    // Configuration parameters
    float max_velocity_difference_ms_{10.0f};   ///< Maximum acceptable velocity difference
    float gps_velocity_weight_{0.85f};          ///< GPS velocity trust factor
    float integration_tau_{2.0f};               ///< Velocity integration smoothing
    float hv_ratio_threshold_{1.5f};            ///< Horizontal/vertical error ratio
    
    // State tracking
    VelocityCrossCheckResult last_result_;
    
    // Integration state
    float integrated_north_velocity_ms_{0.0f};
    float integrated_east_velocity_ms_{0.0f};
    float integrated_vertical_velocity_ms_{0.0f};
    
    // History for drift detection
    std::vector<float> velocity_differences_;   ///< Recent velocity differences
    static constexpr size_t MAX_HISTORY{100};   ///< Max history samples
    
    // === Private Methods ===
    
    /**
     * @brief Compute GPS velocity confidence
     * @param reading GPS reading
     * @return Confidence (0.0-1.0)
     */
    float ComputeGPSConfidence(const GPSVelocityReading& reading) const;
    
    /**
     * @brief Compute integrated acceleration confidence
     * @param reading Acceleration reading
     * @return Confidence (0.0-1.0)
     */
    float ComputeIntegratedConfidence(const AccelerationReading& reading) const;
    
    /**
     * @brief Detect velocity inconsistencies
     * @param gps_vel GPS velocity magnitude
     * @param integrated_vel Integrated velocity magnitude
     * @param horiz_diff Horizontal difference
     * @param vert_diff Vertical difference
     * @return True if inconsistency detected
     */
    bool DetectInconsistency(float gps_vel, float integrated_vel,
                            float horiz_diff, float vert_diff) const;
    
    /**
     * @brief Update velocity integration with new acceleration
     * @param accel Acceleration reading
     * @param dt Delta time
     */
    void UpdateIntegration(const AccelerationReading& accel, float dt);
    
    /**
     * @brief Remove gravity from vertical acceleration
     * @param vert_accel Raw vertical acceleration
     * @return Gravity-corrected vertical acceleration
     */
    static float RemoveGravity(float vert_accel) {
        constexpr float GRAVITY = 9.81f;
        return vert_accel - GRAVITY;
    }
};

}  // namespace avionics::estimators
