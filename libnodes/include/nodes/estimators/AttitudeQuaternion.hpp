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
 * @file AttitudeQuaternion.hpp
 * @brief Quaternion utility functions for attitude representation and conversion
 *
 * Provides static utility methods for:
 * - Euler angle ↔ Quaternion conversions (with gimbal lock handling)
 * - Quaternion arithmetic (magnitude, normalization, multiplication)
 * - Angular integration (small angle approximation)
 * - Spherical linear interpolation (Slerp)
 *
 * Uses intrinsic ZYX (yaw-pitch-roll) convention for Euler angles.
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "sensor/SensorBasicTypes.hpp"
#include <cmath>
#include <algorithm>

namespace avionics::estimators {

using Vector3D = sensors::Vector3D;
using Quaternion = sensors::Quaternion;

/**
 * @class AttitudeQuaternion
 * @brief Static utility methods for quaternion-based attitude mathematics
 *
 * Provides comprehensive quaternion operations for attitude representation,
 * conversion, and manipulation in aerospace applications.
 *
 * Phase 3 Task 3.3 Component: Quaternion math utilities
 */
class AttitudeQuaternion {
public:
    // Prevent instantiation (static class)
    AttitudeQuaternion() = delete;

    // ===== Euler Angle Conversions =====

    /**
     * @brief Convert Euler angles to quaternion (intrinsic ZYX order)
     *
     * Uses the ZYX (yaw-pitch-roll) intrinsic rotation convention:
     * - First rotate around Z axis by yaw
     * - Then rotate around Y axis by pitch
     * - Then rotate around X axis by roll
     *
     * This matches typical aerospace convention where:
     * - Roll: rotation about longitudinal axis (X) - ±180°
     * - Pitch: rotation about lateral axis (Y) - ±90°
     * - Yaw: rotation about vertical axis (Z) - ±180°
     *
     * @param roll Roll angle in radians (X rotation)
     * @param pitch Pitch angle in radians (Y rotation)
     * @param yaw Yaw angle in radians (Z rotation)
     * @return Unit quaternion representing the combined rotation
     */
    static Quaternion FromEulerRadians(float roll, float pitch, float yaw);

    /**
     * @brief Convert quaternion to Euler angles (intrinsic ZYX order)
     *
     * Extracts roll, pitch, yaw from quaternion.
     * Note: Gimbal lock region near pitch = ±90° has representation singularity.
     *
     * @param q Input quaternion (should be normalized)
     * @param[out] roll Roll angle in radians
     * @param[out] pitch Pitch angle in radians
     * @param[out] yaw Yaw angle in radians
     * @return true if conversion successful, false if gimbal lock region
     */
    static bool ToEulerRadians(const Quaternion& q, float& roll, float& pitch, float& yaw);

    /**
     * @brief Check if quaternion is in gimbal lock region
     * @param q Input quaternion
     * @param tolerance Tolerance for gimbal lock detection (default: 0.01)
     * @return true if near gimbal lock
     */
    static bool IsNearGimbalLock(const Quaternion& q, float tolerance = 0.01f);

    // ===== Quaternion Arithmetic =====

    /**
     * @brief Compute quaternion magnitude (norm)
     * @param q Input quaternion
     * @return Magnitude value (should be ~1.0 for unit quaternions)
     */
    static float Magnitude(const Quaternion& q);

    /**
     * @brief Normalize quaternion to unit magnitude
     * @param q Input quaternion
     * @return Normalized quaternion
     */
    static Quaternion Normalize(const Quaternion& q);

    /**
     * @brief Quaternion conjugate (inverse for unit quaternions)
     * @param q Input quaternion
     * @return Conjugate quaternion
     */
    static Quaternion Conjugate(const Quaternion& q);

    /**
     * @brief Quaternion multiplication (left * right)
     * 
     * Computes q_result = q_left * q_right
     * Represents successive rotations: first apply q_right, then q_left
     *
     * @param q_left Left quaternion
     * @param q_right Right quaternion
     * @return Product quaternion
     */
    static Quaternion Multiply(const Quaternion& q_left, const Quaternion& q_right);

    // ===== Angular Integration =====

    /**
     * @brief Integrate angular velocity to update quaternion (exponential map)
     *
     * Uses first-order integration via exponential map:
     * q_new = q_old * exp(0.5 * omega * dt)
     *
     * This is equivalent to small angle approximation for small dt,
     * but valid for larger angles due to exponential map properties.
     *
     * @param q Current quaternion
     * @param omega Angular velocity vector in rad/s
     * @param dt Time interval in seconds
     * @return Updated quaternion
     */
    static Quaternion Integrate(const Quaternion& q, const Vector3D& omega, float dt);

    // ===== Interpolation =====

    /**
     * @brief Spherical linear interpolation (Slerp) between two quaternions
     *
     * Smoothly interpolates between two orientations along the shortest path
     * on the unit quaternion sphere.
     *
     * @param q1 First quaternion (at t=0)
     * @param q2 Second quaternion (at t=1)
     * @param t Interpolation parameter (0.0 to 1.0)
     * @return Interpolated quaternion at parameter t
     */
    static Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t);

    // ===== Rotation Operations =====

    /**
     * @brief Rotate a vector by quaternion
     * @param q Rotation quaternion
     * @param v Vector to rotate
     * @return Rotated vector
     */
    static Vector3D Rotate(const Quaternion& q, const Vector3D& v);

    /**
     * @brief Get rotation matrix equivalent of quaternion (3x3)
     * 
     * Returns matrix such that v_rotated = R * v
     * 
     * @param q Input quaternion
     * @return 3x3 rotation matrix (stored row-major)
     */
    static std::array<std::array<float, 3>, 3> ToRotationMatrix(const Quaternion& q);

    // ===== Validation =====

    /**
     * @brief Check if quaternion is valid (properly normalized)
     * @param q Input quaternion
     * @param tolerance Tolerance for magnitude check (default: 0.001)
     * @return true if quaternion is valid unit quaternion
     */
    static bool IsValid(const Quaternion& q, float tolerance = 0.001f);

    /**
     * @brief Ensure quaternion is in canonical form (w > 0)
     * @param q Input quaternion
     * @return Canonical form quaternion
     */
    static Quaternion Canonical(const Quaternion& q);
};

} // namespace avionics::estimators

