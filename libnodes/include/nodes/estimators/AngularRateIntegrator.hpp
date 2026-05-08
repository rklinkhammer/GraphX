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
 * @file AngularRateIntegrator.hpp
 * @brief Gyroscope angular rate integration to attitude quaternion
 *
 * Converts angular velocity measurements from gyroscope into attitude estimates
 * using quaternion-based integration:
 * - Quaternion integration using exponential map (small angle approximation)
 * - Magnitude normalization to prevent drift
 * - Time-based interpolation for variable-rate inputs
 * - Zero-rate stability
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "sensor/SensorBasicTypes.hpp"
#include <cstdint>
#include <cmath>

namespace avionics::estimators {

using Vector3D = sensors::Vector3D;
using Quaternion = sensors::Quaternion;

/**
 * @class AngularRateIntegrator
 * @brief Integrates angular velocity to compute attitude quaternion
 *
 * Implements quaternion integration from gyroscope angular velocity measurements.
 * Uses first-order exponential map integration for stability and efficiency.
 *
 * Phase 3 Task 3.3 Component: Gyroscope-based attitude estimation
 */
class AngularRateIntegrator {
public:
    /**
     * @brief Constructor - initializes with identity quaternion
     */
    AngularRateIntegrator();

    /**
     * @brief Virtual destructor
     */
    virtual ~AngularRateIntegrator() = default;

    /**
     * @brief Update attitude from angular velocity measurement
     *
     * Integrates the angular velocity vector over the time interval since
     * the last update to increment the attitude quaternion.
     * 
     * Uses first-order integration:
     * q(t+dt) = q(t) * exp(0.5 * omega * dt)
     *
     * @param angular_velocity_rad_s Angular velocity in rad/s (from gyroscope)
     * @param timestamp_us Current timestamp in microseconds
     */
    void Update(const Vector3D& angular_velocity_rad_s, uint64_t timestamp_us);

    /**
     * @brief Get current attitude quaternion
     * @return Current attitude quaternion [w, x, y, z], magnitude = 1.0
     */
    Quaternion GetAttitude() const { return attitude_; }

    /**
     * @brief Get last measured angular velocity
     * @return Last angular velocity measurement in rad/s
     */
    Vector3D GetAngularVelocity() const { return angular_velocity_; }

    /**
     * @brief Reset to initial attitude
     * @param initial_attitude Initial quaternion (default: identity)
     */
    void Reset(const Quaternion& initial_attitude = Quaternion{});

    /**
     * @brief Get time since last update
     * @return Delta time in seconds since last Update() call
     */
    double GetLastDeltaTime() const { return last_delta_time_s_; }

    /**
     * @brief Get update count (for debugging)
     * @return Number of Update() calls since construction/reset
     */
    uint32_t GetUpdateCount() const { return update_count_; }

private:
    Quaternion attitude_;              ///< Current attitude quaternion
    Vector3D angular_velocity_;        ///< Last measured angular velocity
    uint64_t last_timestamp_us_;       ///< Timestamp of last update
    double last_delta_time_s_;         ///< Delta time of last update
    uint32_t update_count_;            ///< Count of updates for debugging

    /**
     * @brief Integrate quaternion using exponential map
     * @param q Current quaternion
     * @param omega Angular velocity vector (rad/s)
     * @param dt Time interval (seconds)
     * @return Updated quaternion
     */
    static Quaternion IntegrateQuaternion(const Quaternion& q, const Vector3D& omega, double dt);

    /**
     * @brief Normalize quaternion to unit magnitude
     * @param q Input quaternion
     * @return Normalized quaternion
     */
    static Quaternion NormalizeQuaternion(const Quaternion& q);
};

} // namespace avionics::estimators

