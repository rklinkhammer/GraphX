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

#include "avionics/estimators/AttitudeQuaternion.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace avionics::estimators {

// ============================================================================
// Euler Angle Conversions
// ============================================================================

Quaternion AttitudeQuaternion::FromEulerRadians(float roll, float pitch, float yaw) {
    // ZYX intrinsic (yaw-pitch-roll) Euler angle to quaternion conversion
    // Following aerospace convention (Z-Y'-X'' intrinsic rotations)
    
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);
    
    // ZYX composition: q = q_z * q_y * q_x
    Quaternion q;
    q.w = cy * cp * cr + sy * sp * sr;
    q.x = cy * cp * sr - sy * sp * cr;
    q.y = cy * sp * cr + sy * cp * sr;
    q.z = sy * cp * cr - cy * sp * sr;
    
    return Normalize(q);
}

bool AttitudeQuaternion::ToEulerRadians(const Quaternion& q, float& roll, float& pitch, float& yaw) {
    // Convert quaternion to ZYX Euler angles
    // Check for gimbal lock condition first
    
    // Normalize input
    Quaternion normalized = Normalize(q);
    
    float sinp = 2.0f * (normalized.w * normalized.y - normalized.z * normalized.x);
    
    // Gimbal lock check: |sinp| > 0.99999 means we're near ±90 degrees pitch
    if (std::abs(sinp) > 0.99999f) {
        // In gimbal lock, set roll = 0 and compute yaw + pitch
        roll = 0.0f;
        pitch = (sinp > 0) ? (M_PI / 2.0f) : (-M_PI / 2.0f);
        yaw = std::atan2(
            2.0f * (normalized.w * normalized.z + normalized.x * normalized.y),
            1.0f - 2.0f * (normalized.y * normalized.y + normalized.z * normalized.z)
        );
        return false;  // Signal gimbal lock
    }
    
    // Normal case: no gimbal lock
    roll = std::atan2(
        2.0f * (normalized.w * normalized.x + normalized.y * normalized.z),
        1.0f - 2.0f * (normalized.x * normalized.x + normalized.y * normalized.y)
    );
    
    pitch = std::asin(sinp);
    
    yaw = std::atan2(
        2.0f * (normalized.w * normalized.z - normalized.x * normalized.y),
        1.0f - 2.0f * (normalized.y * normalized.y + normalized.z * normalized.z)
    );
    
    return true;
}

bool AttitudeQuaternion::IsNearGimbalLock(const Quaternion& q, float tolerance) {
    Quaternion normalized = Normalize(q);
    float sinp = 2.0f * (normalized.w * normalized.y - normalized.z * normalized.x);
    return std::abs(sinp) > (1.0f - tolerance);  // Threshold at sin(pitch) > 1 - tolerance
}

// ============================================================================
// Quaternion Arithmetic
// ============================================================================

float AttitudeQuaternion::Magnitude(const Quaternion& q) {
    return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

Quaternion AttitudeQuaternion::Normalize(const Quaternion& q) {
    float mag = Magnitude(q);
    
    if (mag < 1e-7f) {
        return Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    }
    
    return Quaternion{q.w / mag, q.x / mag, q.y / mag, q.z / mag};
}

Quaternion AttitudeQuaternion::Conjugate(const Quaternion& q) {
    return Quaternion{q.w, -q.x, -q.y, -q.z};
}

Quaternion AttitudeQuaternion::Multiply(const Quaternion& q1, const Quaternion& q2) {
    // Quaternion multiplication: q1 * q2
    return Quaternion{
        q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w
    };
}

// ============================================================================
// Integration
// ============================================================================

Quaternion AttitudeQuaternion::Integrate(const Quaternion& q, const Vector3D& omega, float dt) {
    // Exponential map integration: q_new = q * exp(0.5 * omega * dt)
    // This is equivalent to rotating the quaternion by a small angle
    
    float omega_mag = std::sqrt(omega.x * omega.x + omega.y * omega.y + omega.z * omega.z);
    
    // For very small angular velocity, use first-order approximation
    if (omega_mag < 1e-7f) {
        return q;
    }
    
    // Half-angle
    float half_angle = 0.5f * omega_mag * dt;
    
    // Normalized axis
    float n_x = omega.x / omega_mag;
    float n_y = omega.y / omega_mag;
    float n_z = omega.z / omega_mag;
    
    // sin(half_angle)
    float sin_half = std::sin(half_angle);
    
    // Delta quaternion: [cos(half_angle), sin(half_angle)*n]
    Quaternion dq{
        std::cos(half_angle),
        sin_half * n_x,
        sin_half * n_y,
        sin_half * n_z
    };
    
    // Result: q * dq
    return Multiply(q, dq);
}

// ============================================================================
// Interpolation
// ============================================================================

Quaternion AttitudeQuaternion::Slerp(const Quaternion& q1, const Quaternion& q2, float t) {
    // Spherical Linear Interpolation between two quaternions
    // t = 0 returns q1, t = 1 returns q2
    
    Quaternion qa = Normalize(q1);
    Quaternion qb = Normalize(q2);
    
    // Compute dot product
    float dot = qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z;
    
    // If dot product is negative, negate one quaternion to take shorter path
    if (dot < 0.0f) {
        qb = Quaternion{-qb.w, -qb.x, -qb.y, -qb.z};
        dot = -dot;
    }
    
    // Clamp dot product to avoid numerical issues with acos
    dot = std::max(-1.0f, std::min(1.0f, dot));
    
    // Compute the angle between the quaternions
    float theta = std::acos(dot);
    
    // If quaternions are very close, use linear interpolation
    if (std::abs(theta) < 1e-6f) {
        return Normalize(Quaternion{
            qa.w + t * (qb.w - qa.w),
            qa.x + t * (qb.x - qa.x),
            qa.y + t * (qb.y - qa.y),
            qa.z + t * (qb.z - qa.z)
        });
    }
    
    // Compute slerp
    float sin_theta = std::sin(theta);
    float w1 = std::sin((1.0f - t) * theta) / sin_theta;
    float w2 = std::sin(t * theta) / sin_theta;
    
    return Quaternion{
        w1 * qa.w + w2 * qb.w,
        w1 * qa.x + w2 * qb.x,
        w1 * qa.y + w2 * qb.y,
        w1 * qa.z + w2 * qb.z
    };
}

// ============================================================================
// Rotation Operations
// ============================================================================

Vector3D AttitudeQuaternion::Rotate(const Quaternion& q, const Vector3D& v) {
    // Rotate vector v by quaternion q using: v' = q * v * q^*
    Quaternion normalized = Normalize(q);
    Quaternion v_quat{0.0f, v.x, v.y, v.z};
    Quaternion q_conj = Conjugate(normalized);
    
    Quaternion result = Multiply(Multiply(normalized, v_quat), q_conj);
    
    return Vector3D{result.x, result.y, result.z};
}

std::array<std::array<float, 3>, 3> AttitudeQuaternion::ToRotationMatrix(const Quaternion& q) {
    // Convert quaternion to 3x3 rotation matrix
    Quaternion normalized = Normalize(q);
    
    float w = normalized.w;
    float x = normalized.x;
    float y = normalized.y;
    float z = normalized.z;
    
    float x2 = x + x;
    float y2 = y + y;
    float z2 = z + z;
    
    float xx = x * x2;
    float xy = x * y2;
    float xz = x * z2;
    float yy = y * y2;
    float yz = y * z2;
    float zz = z * z2;
    float wx = w * x2;
    float wy = w * y2;
    float wz = w * z2;
    
    std::array<std::array<float, 3>, 3> matrix;
    matrix[0][0] = 1.0f - (yy + zz);
    matrix[0][1] = xy - wz;
    matrix[0][2] = xz + wy;
    
    matrix[1][0] = xy + wz;
    matrix[1][1] = 1.0f - (xx + zz);
    matrix[1][2] = yz - wx;
    
    matrix[2][0] = xz - wy;
    matrix[2][1] = yz + wx;
    matrix[2][2] = 1.0f - (xx + yy);
    
    return matrix;
}

// ============================================================================
// Validation
// ============================================================================

bool AttitudeQuaternion::IsValid(const Quaternion& q, float tolerance) {
    // Check that quaternion is normalized (magnitude ≈ 1)
    float mag = Magnitude(q);
    float target = 1.0f;
    return std::abs(mag - target) < tolerance;
}

Quaternion AttitudeQuaternion::Canonical(const Quaternion& q) {
    // Ensure quaternion is in canonical form (w >= 0)
    if (q.w >= 0.0f) {
        return q;
    }
    return Quaternion{-q.w, -q.x, -q.y, -q.z};
}

} // namespace avionics::estimators
