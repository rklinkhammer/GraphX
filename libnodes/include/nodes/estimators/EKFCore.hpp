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
#include <vector>
#include <variant>
#include "AdaptiveComplementaryFilter.hpp"
#include "core/VariantHelper.hpp"

namespace avionics::estimators {

/**
 * @brief Measurement-specific types for variant-based dispatch
 */
struct AccelerometerMeasurement {
    Vec3 value;  // 3D acceleration in m/s²
};

struct BarometerMeasurement {
    float value;  // Altitude in meters
};

struct VelocityMeasurement {
    Vec3 value;  // 3D velocity in m/s
};

struct HeadingMeasurement {
    float value;  // Yaw angle in radians
};

/**
 * @struct Matrix9x9
 * @brief 9x9 covariance matrix for EKF state
 */
struct Matrix9x9 {
    std::array<std::array<float, 9>, 9> data;
    
    Matrix9x9() {
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                data[i][j] = (i == j) ? 1.0f : 0.0f;  // Identity by default
            }
        }
    }
    
    // Element access
    float& operator()(int i, int j) { return data[i][j]; }
    float operator()(int i, int j) const { return data[i][j]; }
    
    // Matrix multiplication
    Matrix9x9 operator*(const Matrix9x9& other) const {
        Matrix9x9 result;
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                result(i, j) = 0;
                for (int k = 0; k < 9; ++k) {
                    result(i, j) += data[i][k] * other(k, j);
                }
            }
        }
        return result;
    }
    
    // Matrix-vector multiplication
    std::array<float, 9> operator*(const std::array<float, 9>& vec) const {
        std::array<float, 9> result;
        for (int i = 0; i < 9; ++i) {
            result[i] = 0;
            for (int j = 0; j < 9; ++j) {
                result[i] += data[i][j] * vec[j];
            }
        }
        return result;
    }
    
    // Element-wise addition
    Matrix9x9 operator+(const Matrix9x9& other) const {
        Matrix9x9 result;
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                result(i, j) = data[i][j] + other(i, j);
            }
        }
        return result;
    }
    
    // Element-wise subtraction
    Matrix9x9 operator-(const Matrix9x9& other) const {
        Matrix9x9 result;
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                result(i, j) = data[i][j] - other(i, j);
            }
        }
        return result;
    }
    
    // Scalar multiplication
    Matrix9x9 operator*(float scalar) const {
        Matrix9x9 result;
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                result(i, j) = data[i][j] * scalar;
            }
        }
        return result;
    }
    
    // Transpose
    Matrix9x9 Transpose() const {
        Matrix9x9 result;
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                result(i, j) = data[j][i];
            }
        }
        return result;
    }
    
    // Check if positive definite (all diagonal elements > 0 is basic check)
    bool IsPositiveDefinite() const {
        for (int i = 0; i < 9; ++i) {
            if (data[i][i] <= 0) return false;
        }
        return true;
    }
};

struct Matrix3x3 {
    std::array<std::array<float, 3>, 3> data;
    
    Matrix3x3() {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                data[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
    }
    
    float& operator()(int i, int j) { return data[i][j]; }
    float operator()(int i, int j) const { return data[i][j]; }
    
    Matrix3x3 operator*(const Matrix3x3& other) const {
        Matrix3x3 result;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = 0;
                for (int k = 0; k < 3; ++k) {
                    result(i, j) += data[i][k] * other(k, j);
                }
            }
        }
        return result;
    }
    
    Vec3 operator*(const Vec3& vec) const {
        return Vec3(
            data[0][0]*vec.x + data[0][1]*vec.y + data[0][2]*vec.z,
            data[1][0]*vec.x + data[1][1]*vec.y + data[1][2]*vec.z,
            data[2][0]*vec.x + data[2][1]*vec.y + data[2][2]*vec.z
        );
    }
    
    Matrix3x3 operator+(const Matrix3x3& other) const {
        Matrix3x3 result;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = data[i][j] + other(i, j);
            }
        }
        return result;
    }
    
    Matrix3x3 operator*(float scalar) const {
        Matrix3x3 result;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = data[i][j] * scalar;
            }
        }
        return result;
    }
    
    Matrix3x3 Transpose() const {
        Matrix3x3 result;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result(i, j) = data[j][i];
            }
        }
        return result;
    }
};

/**
 * @struct EKFState
 * @brief Extended Kalman Filter state vector
 * 
 * 9-dimensional state:
 * - Position: [altitude, north, east] (3)
 * - Velocity: [vertical, north, east] (3)
 * - Attitude: [roll, pitch, yaw] Euler angles (3)
 * Plus biases (separately managed):
 * - Accel bias: [bx, by, bz]
 * - Gyro bias: [bx, by, bz]
 */
struct EKFState {
    Vec3 position;        // [altitude, north, east] meters
    Vec3 velocity;        // [vertical, north, east] m/s
    Vec3 attitude;        // [roll, pitch, yaw] radians
    Vec3 accel_bias;      // Accelerometer bias (m/s²)
    Vec3 gyro_bias;       // Gyroscope bias (rad/s)
    
    EKFState() : position(), velocity(), attitude(), accel_bias(), gyro_bias() {}
    
    // Pack state into array for matrix operations
    std::array<float, 9> ToArray() const {
        return {
            position.x, position.y, position.z,
            velocity.x, velocity.y, velocity.z,
            attitude.x, attitude.y, attitude.z
        };
    }
    
    // Unpack state from array
    void FromArray(const std::array<float, 9>& arr) {
        position = Vec3(arr[0], arr[1], arr[2]);
        velocity = Vec3(arr[3], arr[4], arr[5]);
        attitude = Vec3(arr[6], arr[7], arr[8]);
    }
};

/**
 * @struct EKFMeasurement
 * @brief Extended Kalman Filter measurement (variant-based)
 *
 * Uses std::variant for type-safe measurement dispatch with compile-time
 * exhaustiveness checking. Replaces enum-based approach with measurement-
 * specific types for better type safety and reduced switch statements.
 */
struct EKFMeasurement {
    /// Union of measurement types
    std::variant<AccelerometerMeasurement, BarometerMeasurement,
                 VelocityMeasurement, HeadingMeasurement> measurement;
    float variance;
    uint64_t timestamp_us;

    // Factory methods for construction
    static EKFMeasurement CreateAccelerometer(const Vec3& value, float var, uint64_t ts) {
        EKFMeasurement m;
        m.measurement = AccelerometerMeasurement{value};
        m.variance = var;
        m.timestamp_us = ts;
        return m;
    }

    static EKFMeasurement CreateBarometer(float value, float var, uint64_t ts) {
        EKFMeasurement m;
        m.measurement = BarometerMeasurement{value};
        m.variance = var;
        m.timestamp_us = ts;
        return m;
    }

    static EKFMeasurement CreateVelocity(const Vec3& value, float var, uint64_t ts) {
        EKFMeasurement m;
        m.measurement = VelocityMeasurement{value};
        m.variance = var;
        m.timestamp_us = ts;
        return m;
    }

    static EKFMeasurement CreateHeading(float value, float var, uint64_t ts) {
        EKFMeasurement m;
        m.measurement = HeadingMeasurement{value};
        m.variance = var;
        m.timestamp_us = ts;
        return m;
    }
};

/**
 * @class EKFCore
 * @brief Extended Kalman Filter for non-linear state estimation
 * 
 * Implements EKF prediction and update steps for fusing accelerometer,
 * gyroscope, barometer, GPS, and magnetometer measurements.
 */
class EKFCore {
public:
    EKFCore();
    ~EKFCore() = default;
    
    /**
     * @brief Initialize filter with initial state
     * @param initial_state Initial state estimate
     * @param initial_covariance Initial covariance matrix
     */
    void Initialize(const EKFState& initial_state, const Matrix9x9& initial_covariance);
    
    /**
     * @brief Predict step using IMU measurements
     * @param accel Acceleration (m/s²)
     * @param gyro Angular rates (rad/s)
     * @param dt Time step (seconds)
     */
    void Predict(const Vec3& accel, const Vec3& gyro, float dt);
    
    /**
     * @brief Update step using sensor measurement
     * @param measurement Measurement to incorporate
     */
    void Update(const EKFMeasurement& measurement);
    
    /**
     * @brief Get current state estimate
     * @return Current EKF state
     */
    EKFState GetState() const { return state_; }
    
    /**
     * @brief Get state covariance matrix
     * @return 9x9 covariance matrix
     */
    Matrix9x9 GetCovariance() const { return covariance_; }
    
    /**
     * @brief Get process noise matrix Q
     * @return Q matrix for current configuration
     */
    Matrix9x9 GetQ() const { return Q_; }
    
    /**
     * @brief Check filter health
     * @return True if filter is well-conditioned
     */
    bool IsHealthy() const { return covariance_.IsPositiveDefinite(); }
    
    /**
     * @brief Get update count
     * @return Number of updates processed
     */
    uint32_t GetUpdateCount() const { return update_count_; }
    
    /**
     * @brief Get prediction count
     * @return Number of predictions processed
     */
    uint32_t GetPredictionCount() const { return prediction_count_; }
    
    /**
     * @brief Reset filter
     */
    void Reset();

private:
    // State and covariance
    EKFState state_;
    Matrix9x9 covariance_;
    
    // Noise matrices
    Matrix9x9 Q_;  // Process noise
    
    // Metrics
    uint32_t update_count_;
    uint32_t prediction_count_;
    uint64_t last_timestamp_us_;
    
    /**
     * @brief Compute state transition Jacobian (F matrix)
     * @param dt Time step
     * @return F matrix (linearized state dynamics)
     */
    Matrix9x9 ComputeF_Jacobian(float dt);
    
    /**
     * @brief Compute measurement Jacobian (H matrix)
     * @param measurement The measurement containing type information
     * @return H matrix (linearized measurement model)
     */
    Matrix9x9 ComputeH_Jacobian(const EKFMeasurement& measurement);
    
    /**
     * @brief Compute measurement residual (innovation)
     * @param measurement Measurement
     * @return Innovation vector (measurement - prediction)
     */
    Vec3 ComputeInnovation(const EKFMeasurement& measurement);
    
    /**
     * @brief Initialize process noise matrix Q
     */
    void InitializeQ();
    
    /**
     * @brief Normalize quaternion to prevent drift
     * @param q Quaternion to normalize
     * @return Normalized quaternion
     */
    Quaternion NormalizeQuaternion(const Quaternion& q);
};

/**
 * @class EKFTuner
 * @brief Phase-dependent EKF tuning for flight phases
 * 
 * Provides phase-specific noise matrices (Q and R) to adapt filter
 * behavior for different flight regimes.
 */
class EKFTuner {
public:
    EKFTuner();
    ~EKFTuner() = default;
    
    /**
     * @brief Set current flight phase
     * @param phase Flight phase (0=Rail, 1=Boost, 2=Coast, 3=Apogee, 4=Descent, 5=Landing)
     */
    void SetFlightPhase(int phase);
    
    /**
     * @brief Get current flight phase
     * @return Current phase
     */
    int GetFlightPhase() const { return current_phase_; }
    
    /**
     * @brief Get process and measurement noise matrices
     * @return Pair of (Q matrix, measurement variance scaling)
     */
    std::pair<Matrix9x9, std::array<float, 4>> GetQAndR() const;
    
    /**
     * @brief Get process noise matrix Q for current phase
     * @return Q matrix
     */
    Matrix9x9 GetQ() const;
    
    /**
     * @brief Get measurement variance scaling for current phase
     * @return Array of variance scalings [accel, baro, velocity, heading]
     */
    std::array<float, 4> GetR() const;

private:
    int current_phase_;
    
    // Phase-specific Q matrices (process noise)
    static const std::array<Matrix9x9, 6> PHASE_Q_MATRICES;
    
    // Phase-specific R values (measurement noise scaling)
    // Format: [accel_var, baro_var, velocity_var, heading_var]
    static const std::array<std::array<float, 4>, 6> PHASE_R_VALUES;
    
    /**
     * @brief Initialize phase Q matrices
     */
    void InitializePhaseMatrices();
    
    /**
     * @brief Get default Q matrix for a phase
     * @param phase Flight phase
     * @return Q matrix for phase
     */
    Matrix9x9 GetPhaseQ(int phase) const;
};

} // namespace avionics::estimators
