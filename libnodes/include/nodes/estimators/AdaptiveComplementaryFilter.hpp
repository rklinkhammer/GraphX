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

#include <array>
#include <cmath>
#include <cstdint>
#include "AngularRateIntegrator.hpp"
#include "avionics/config/FlightPhaseClassifier.hpp"
#include "sensor/SensorBasicTypes.hpp"
namespace avionics::estimators {

/**
 * @struct Vec3
 * @brief 3D vector for kinematics
 */
struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }
    
    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }
    
    Vec3 operator*(float scalar) const {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }
    
    float Magnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    
    Vec3 Normalize() const {
        float mag = Magnitude();
        if (mag < 1e-6f) return Vec3(0, 0, 0);
        return Vec3(x / mag, y / mag, z / mag);
    }
    
    float Dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    
    Vec3 Cross(const Vec3& other) const {
        return Vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }
};

/**
 * @brief Quaternion type alias - using unified definition from SensorBasicTypes
 * 
 * Provides component order conversion between SensorBasicTypes::Quaternion (w,x,y,z)
 * and the internal storage convention where needed. Most code uses the
 * sensors::Quaternion directly with (w,x,y,z) order.
 */
using Quaternion = sensors::Quaternion;

/**
 * @struct QuaternionAdapter
 * @brief Helper struct for converting between quaternion component orders
 * 
 * The unified Quaternion uses (w,x,y,z) order. This adapter helps convert
 * to/from (x,y,z,w) order if needed for external APIs or legacy code.
 */
struct QuaternionAdapter {
    /// Convert from (w,x,y,z) to (x,y,z,w) order
    /// @param q Quaternion in (w,x,y,z) order
    /// @return Quaternion in (x,y,z,w) order
    static Quaternion from_xyzw(float x, float y, float z, float w) {
        return Quaternion(w, x, y, z);
    }
    
    /// Convert from (x,y,z,w) to (w,x,y,z) order
    /// @param q Quaternion in (x,y,z,w) order
    /// @return Quaternion in (w,x,y,z) order
    static Quaternion to_xyzw(const Quaternion& q, float& x, float& y, float& z, float& w) {
        x = q.x;
        y = q.y;
        z = q.z;
        w = q.w;
        return q;
    }
};

/**
 * @struct FilterWeights
 * @brief Sensor weighting configuration for complementary filter
 */
struct FilterWeights {
    float accel_weight;      // 0-1: Accelerometer influence
    float gyro_weight;       // 0-1: Gyroscope influence
    float mag_weight;        // 0-1: Magnetometer influence
    float external_weight;   // 0-1: GPS/altitude external reference
    
    FilterWeights() : accel_weight(0.5f), gyro_weight(0.5f), 
                      mag_weight(0.0f), external_weight(0.0f) {}
    
    FilterWeights(float a, float g, float m, float e) 
        : accel_weight(a), gyro_weight(g), mag_weight(m), external_weight(e) {}
    
    // Normalize weights to sum to 1.0
    FilterWeights Normalize() const {
        float sum = accel_weight + gyro_weight + mag_weight + external_weight;
        if (sum < 1e-6f) return FilterWeights(0.25f, 0.25f, 0.25f, 0.25f);
        return FilterWeights(
            accel_weight / sum,
            gyro_weight / sum,
            mag_weight / sum,
            external_weight / sum
        );
    }
};

/**
 * @class AdaptiveComplementaryFilter
 * @brief Phase-aware complementary filter for multi-sensor fusion
 * 
 * Fuses accelerometer, gyroscope, magnetometer, and external references
 * with phase-dependent weighting for optimal estimation during different
 * flight phases.
 */
class AdaptiveComplementaryFilter {
public:
    AdaptiveComplementaryFilter();
    ~AdaptiveComplementaryFilter() = default;
    
    /**
     * @brief Set current flight phase for adaptive weighting
     * @param phase Flight phase from classifier
     */
    void SetFlightPhase(int phase);  // 0=Rail, 1=Boost, 2=Coast, 3=Apogee, 4=Descent, 5=Landing
    
    /**
     * @brief Update filter state with sensor measurements
     * @param accel_m_s2 Accelerometer reading (m/s²)
     * @param gyro_rad_s Gyroscope angular rates (rad/s)
     * @param mag_calibrated Calibrated magnetic field (Tesla)
     * @param external_altitude External altitude reference (meters)
     * @param external_velocity External velocity reference (m/s)
     * @param dt Time step (seconds)
     * @param timestamp_us Timestamp in microseconds
     */
    void Update(
        const Vec3& accel_m_s2,
        const Vec3& gyro_rad_s,
        const Vec3& mag_calibrated,
        float external_altitude,
        const Vec3& external_velocity,
        float dt,
        uint64_t timestamp_us
    );
    
    /**
     * @brief Get current attitude quaternion
     * @return Unit quaternion representing attitude
     */
    Quaternion GetAttitude() const { return attitude_; }
    
    /**
     * @brief Get current position estimate
     * @return Position [altitude, north, east] in meters
     */
    Vec3 GetPosition() const { return position_; }
    
    /**
     * @brief Get current velocity estimate
     * @return Velocity [vertical, north, east] in m/s
     */
    Vec3 GetVelocity() const { return velocity_; }
    
    /**
     * @brief Get current sensor weights in use
     * @return Current filter weights
     */
    FilterWeights GetCurrentWeights() const { return current_weights_; }
    
    /**
     * @brief Get accel-based altitude estimate
     * @return Altitude in meters
     */
    float GetAccelIntegratedAltitude() const { return accel_integrated_altitude_; }
    
    /**
     * @brief Get update count
     * @return Number of updates processed
     */
    uint32_t GetUpdateCount() const { return update_count_; }
    
    /**
     * @brief Reset filter to initial state
     * @param initial_attitude Optional initial attitude quaternion
     */
    void Reset(const Quaternion& initial_attitude = Quaternion());

private:
    // Phase-specific weight configurations
    static const std::array<FilterWeights, 6> PHASE_WEIGHTS;
    
    // State variables
    Quaternion attitude_;
    Vec3 position_;
    Vec3 velocity_;
    Vec3 magnetic_bias_;
    
    // Integration states
    float accel_integrated_altitude_;
    Vec3 accel_integrated_velocity_;
    
    // Filter configuration
    FilterWeights current_weights_;
    int current_phase_;
    
    // Metrics
    uint32_t update_count_;
    uint64_t last_timestamp_us_;
    
    /**
     * @brief Get weights for current flight phase
     * @return Weight configuration for current phase
     */
    FilterWeights GetPhaseWeights() const;
    
    /**
     * @brief Update attitude from gyroscope
     * @param omega Angular velocity (rad/s)
     * @param dt Time step (seconds)
     */
    void UpdateAttitudeFromGyro(const Vec3& omega, float dt);
    
    /**
     * @brief Update attitude from accelerometer
     * @param accel Acceleration (m/s²)
     */
    void UpdateAttitudeFromAccel(const Vec3& accel);
    
    /**
     * @brief Update heading from magnetometer
     * @param mag Calibrated magnetic field (Tesla)
     */
    void UpdateHeadingFromMag(const Vec3& mag);
    
    /**
     * @brief Integrate velocity from acceleration
     * @param accel Acceleration (m/s²)
     * @param dt Time step (seconds)
     */
    void IntegrateVelocity(const Vec3& accel, float dt);
};

} // namespace avionics::estimators
