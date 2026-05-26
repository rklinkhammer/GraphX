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

/*!
 * @file SensorBasicTypes.hpp
 * @brief Basic sensor data types (Vector3D, Quaternion) without circular dependencies
 *
 * Provides lightweight, dependency-free implementations of fundamental 3D geometry types
 * for sensor data representation (acceleration, angular velocity, orientation).
 *
 * @author GraphX Contributors
 * @date 2025
 * @license MIT
 *
 * MIT License
 *
 * Copyright (c) 2025 GraphX Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cmath>

namespace sensors {

// –––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––
// Basic Sensor Data Structures (no dependencies to avoid circular includes)
// –––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––

/**
 * @struct Vector3D
 * @brief Three-axis vector for acceleration, angular velocity, and magnetic field
 * 
 * Represents a 3D vector in Cartesian coordinates. Used for representing
 * acceleration, angular velocity, and magnetic field measurements.
 * 
 * @note Members are initialized to zero by default for safe construction
 * @see Quaternion for orientation representation
 */
struct Vector3D {
    /// X-axis component (m/s², rad/s, or Tesla depending on context)
    float x{0.0f};
    /// Y-axis component (m/s², rad/s, or Tesla depending on context)
    float y{0.0f};
    /// Z-axis component (m/s², rad/s, or Tesla depending on context)
    float z{0.0f};
    
    /// Default constructor initializes components to zero
    Vector3D() = default;
    
    /// Parameterized constructor
    /// @param x_val X-axis component value
    /// @param y_val Y-axis component value
    /// @param z_val Z-axis component value
    Vector3D(float x_val, float y_val, float z_val) : x(x_val), y(y_val), z(z_val) {}
    
    /// Vector addition operator
    /// @param other Vector to add
    /// @return Sum of this vector and other
    Vector3D operator+(const Vector3D& other) const {
        return Vector3D(x + other.x, y + other.y, z + other.z);
    }
    
    /// Vector subtraction operator
    /// @param other Vector to subtract
    /// @return Difference of this vector and other
    Vector3D operator-(const Vector3D& other) const {
        return Vector3D(x - other.x, y - other.y, z - other.z);
    }
    
    /// Scalar multiplication operator
    /// @param scalar Multiplicative factor
    /// @return Vector scaled by scalar
    Vector3D operator*(float scalar) const {
        return Vector3D(x * scalar, y * scalar, z * scalar);
    }

    /// Compute dot product with another vector
    /// @param other Vector to compute dot product with
    /// @return Scalar dot product
    float dot(const Vector3D& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    /// Compute cross product with another vector
    /// @param other Vector to compute cross product with
    /// @return Vector perpendicular to both input vectors
    Vector3D cross(const Vector3D& other) const {
        return Vector3D(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }
    
    /// Compute L2 norm (Euclidean magnitude) of vector
    /// @return Magnitude of vector
    float magnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    /// Convenience alias for magnitude (returns L2 norm)
    /// @return Magnitude of vector
    float norm() const { return magnitude(); }

    /// Compute squared magnitude (more efficient when full norm not needed)
    /// @return Squared magnitude of vector
    float magnitude_squared() const {
        return x * x + y * y + z * z;
    }

    /// Compute normalized (unit) vector in same direction
    /// @return Unit vector; returns original if magnitude is zero
    Vector3D normalized() const {
        float mag = magnitude();
        if (mag == 0.0f) return *this;
        return Vector3D(x / mag, y / mag, z / mag);
    }
    
    /// Equality comparison operator
    /// @param other Vector to compare with
    /// @return True if all components are exactly equal
    bool operator==(const Vector3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    /// Inequality comparison operator
    /// @param other Vector to compare with
    /// @return True if any component differs
    bool operator!=(const Vector3D& other) const {
        return !(*this == other);
    }
};

/**
 * @struct Quaternion
 * @brief Quaternion representation for 3D rotations and orientation (w, x, y, z)
 * 
 * Quaternions provide an efficient, singularity-free representation of 3D rotations.
 * Stored in the format (w, x, y, z) where w is the scalar part and (x, y, z) is
 * the vector part. Default construction creates an identity quaternion (no rotation).
 * 
 * @note Members are initialized to represent identity rotation by default
 * @see Vector3D for positional vectors
 */
struct Quaternion {
    /// Scalar (real) part of quaternion; w=1,x=y=z=0 represents no rotation
    float w{1.0f};
    /// X component of vector (imaginary) part (radians)
    float x{0.0f};
    /// Y component of vector (imaginary) part (radians)
    float y{0.0f};
    /// Z component of vector (imaginary) part (radians)
    float z{0.0f};
    
    /// Default constructor creates identity quaternion (no rotation)
    Quaternion() = default;
    
    /// Parameterized constructor
    /// @param w_val Scalar (real) component
    /// @param x_val X component of vector part
    /// @param y_val Y component of vector part  
    /// @param z_val Z component of vector part
    Quaternion(float w_val, float x_val, float y_val, float z_val) 
        : w(w_val), x(x_val), y(y_val), z(z_val) {}

    /// Compute magnitude (norm) of quaternion
    /// @return Quaternion magnitude; normalized quaternions have magnitude = 1
    float magnitude() const {
        return std::sqrt(w * w + x * x + y * y + z * z);
    }

    /// Convenience alias for magnitude
    /// @return Quaternion magnitude
    float norm() const { return magnitude(); }

    /// Normalize quaternion to unit magnitude
    /// @return Normalized quaternion; returns original if magnitude is zero
    Quaternion normalized() const {
        float mag = magnitude();
        if (mag == 0.0f) return *this;
        return Quaternion(w / mag, x / mag, y / mag, z / mag);
    }

    /// Compute conjugate (inverse) of quaternion
    /// For unit quaternions, conjugate is the inverse rotation
    /// @return Conjugate quaternion with negated vector part
    Quaternion conjugate() const {
        return Quaternion(w, -x, -y, -z);
    }

    /// Multiply two quaternions (rotation composition)
    /// @param other Quaternion to multiply with
    /// @return Product quaternion representing combined rotation
    Quaternion operator*(const Quaternion& other) const {
        return Quaternion(
            w*other.w - x*other.x - y*other.y - z*other.z,
            w*other.x + x*other.w + y*other.z - z*other.y,
            w*other.y - x*other.z + y*other.w + z*other.x,
            w*other.z + x*other.y - y*other.x + z*other.w
        );
    }

    /// Rotate a 3D vector by this quaternion
    /// @param v Vector to rotate
    /// @return Rotated vector
    Vector3D rotate_vector(const Vector3D& v) const {
        // Create pure quaternion from vector (w=0, x=v.x, y=v.y, z=v.z)
        Quaternion q_pure(0.0f, v.x, v.y, v.z);
        // Rotate: q_rotated = q * q_pure * q_conjugate
        Quaternion result = (*this) * q_pure * conjugate();
        return Vector3D(result.x, result.y, result.z);
    }

    /// Create quaternion from axis-angle representation
    /// @param axis Unit vector representing rotation axis
    /// @param angle_radians Rotation angle in radians
    /// @return Unit quaternion representing the rotation
    static Quaternion from_axis_angle(const Vector3D& axis, float angle_radians) {
        Vector3D normalized_axis = axis.normalized();
        float half_angle = angle_radians * 0.5f;
        float sin_half = std::sin(half_angle);
        return Quaternion(
            std::cos(half_angle),
            normalized_axis.x * sin_half,
            normalized_axis.y * sin_half,
            normalized_axis.z * sin_half
        );
    }

    /// Convert quaternion to Euler angles (roll, pitch, yaw)
    /// Uses XYZ intrinsic rotation order
    /// @param roll Output: Rotation around X-axis (radians), range [-π, π]
    /// @param pitch Output: Rotation around Y-axis (radians), range [-π/2, π/2]
    /// @param yaw Output: Rotation around Z-axis (radians), range [-π, π]
    void to_euler_angles(float& roll, float& pitch, float& yaw) const {
        // Roll (rotation around X-axis)
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        roll = std::atan2(sinr_cosp, cosr_cosp);
        
        // Pitch (rotation around Y-axis)
        float sinp = 2.0f * (w * y - z * x);
        if (std::abs(sinp) >= 1.0f) {
            pitch = std::copysign(M_PI / 2.0f, sinp);
        } else {
            pitch = std::asin(sinp);
        }
        
        // Yaw (rotation around Z-axis)
        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        yaw = std::atan2(siny_cosp, cosy_cosp);
    }

    /// Create quaternion from Euler angles (XYZ order)
    /// @param roll Rotation around X-axis (radians)
    /// @param pitch Rotation around Y-axis (radians)
    /// @param yaw Rotation around Z-axis (radians)
    /// @return Unit quaternion representing the combined rotation
    static Quaternion from_euler_angles(float roll, float pitch, float yaw) {
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        
        return Quaternion(
            cr * cp * cy + sr * sp * sy,
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy
        );
    }

    /// Create quaternion from Euler angles (ZYX intrinsic order - legacy)
    /// Uses ZYX (yaw-pitch-roll) intrinsic rotation order.
    /// @param pitch Rotation around Y-axis (radians), range [-π/2, π/2]
    /// @param roll Rotation around X-axis (radians), range [-π, π]
    /// @param yaw Rotation around Z-axis (radians), range [-π, π]
    /// @return Unit quaternion representing the combined rotation
    static Quaternion from_euler_radians(float pitch, float roll, float yaw) {
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);
        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        
        return Quaternion(
            cp * cr * cy + sp * sr * sy,
            sp * cr * cy - cp * sr * sy,
            cp * sr * cy + sp * cr * sy,
            cp * cr * sy - sp * sr * cy
        );
    }

    /// Equality comparison operator
    /// @param other Quaternion to compare with
    /// @return True if all components are exactly equal
    bool operator==(const Quaternion& other) const {
        return w == other.w && x == other.x && y == other.y && z == other.z;
    }

    /// Inequality comparison operator
    /// @param other Quaternion to compare with
    /// @return True if any component differs
    bool operator!=(const Quaternion& other) const {
        return !(*this == other);
    }
};

} // namespace sensors

