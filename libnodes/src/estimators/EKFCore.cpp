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

#include "avionics/estimators/EKFCore.hpp"
#include "core/VariantHelper.hpp"
#include <algorithm>
#include <cmath>

namespace avionics::estimators {

// Initialize static phase matrices for EKFTuner
const std::array<Matrix9x9, 6> EKFTuner::PHASE_Q_MATRICES = {};

// Initialize static phase R values for EKFTuner
const std::array<std::array<float, 4>, 6> EKFTuner::PHASE_R_VALUES = {{
    {{0.01f, 1.0f, 0.5f, 0.1f}},     // Rail: Low accel noise, high baro
    {{0.02f, 2.0f, 0.8f, 0.2f}},     // Boost: Higher sensor noise
    {{0.01f, 1.5f, 0.6f, 0.3f}},     // Coast: Balanced
    {{0.05f, 0.5f, 0.4f, 0.5f}},     // Apogee: Trust gyro/mag over accel
    {{0.02f, 1.2f, 0.5f, 0.4f}},     // Descent: Moderate noise
    {{0.01f, 2.0f, 1.0f, 0.1f}}      // Landing: High baro noise
}};

EKFCore::EKFCore()
    : state_(),
      covariance_(),
      update_count_(0),
      prediction_count_(0),
      last_timestamp_us_(0) {
    InitializeQ();
}

void EKFCore::Initialize(const EKFState& initial_state, const Matrix9x9& initial_covariance) {
    state_ = initial_state;
    covariance_ = initial_covariance;
    update_count_ = 0;
    prediction_count_ = 0;
    last_timestamp_us_ = 0;
}

void EKFCore::Predict(const Vec3& accel, const Vec3& gyro, float dt) {
    if (dt <= 0 || dt > 1.0f) return;
    
    // Correct for biases
    Vec3 accel_corrected = accel - state_.accel_bias;
    Vec3 gyro_corrected = gyro - state_.gyro_bias;
    
    // Remove gravity from accel
    accel_corrected.z -= 9.81f;
    
    // State transition: x_new = f(x_old, u)
    // Position update
    state_.position = state_.position + state_.velocity * dt;
    
    // Velocity update (using corrected acceleration)
    state_.velocity = state_.velocity + accel_corrected * dt;
    
    // Attitude update (using corrected gyro)
    Quaternion q_current = Quaternion::from_euler_angles(state_.attitude.x, state_.attitude.y, state_.attitude.z);
    float angle = gyro_corrected.Magnitude() * dt;
    if (angle > 1e-6f) {
        sensors::Vector3D gyro_vec(gyro_corrected.x, gyro_corrected.y, gyro_corrected.z);
        Quaternion q_omega = Quaternion::from_axis_angle(gyro_vec, angle);
        q_current = (q_omega * q_current).normalized();
    }
    q_current.to_euler_angles(state_.attitude.x, state_.attitude.y, state_.attitude.z);
    
    // Bias updates (slow drift estimation)
    // state_.accel_bias += bias_drift_accel * dt;
    // state_.gyro_bias += bias_drift_gyro * dt;
    // (Skip for simplicity, assume biases are slowly varying)
    
    // Compute Jacobian F matrix (linearized state transition)
    Matrix9x9 F = ComputeF_Jacobian(dt);
    
    // Covariance prediction: P_new = F * P * F^T + Q
    Matrix9x9 P_pred = F * covariance_ * F.Transpose();
    covariance_ = P_pred + Q_;
    
    // Ensure positive definiteness (add small diagonal regularization if needed)
    if (!covariance_.IsPositiveDefinite()) {
        for (int i = 0; i < 9; ++i) {
            covariance_(i, i) = std::max(covariance_(i, i), 1e-6f);
        }
    }
    
    ++prediction_count_;
}

void EKFCore::Update(const EKFMeasurement& measurement) {
    // Compute Jacobian H matrix for this measurement type
    Matrix9x9 H = ComputeH_Jacobian(measurement);
    
    // Compute innovation (measurement residual)
    Vec3 innovation = ComputeInnovation(measurement);
    
    // Compute innovation covariance: S = H * P * H^T + R
    Matrix9x9 HP = H * covariance_;
    Matrix9x9 S = HP * H.Transpose();
    
    // Add measurement noise to S
    for (int i = 0; i < 3; ++i) {
        S(i, i) += measurement.variance;
    }
    
    // Ensure S is invertible (add regularization if needed)
    for (int i = 0; i < 9; ++i) {
        S(i, i) = std::max(S(i, i), 1e-6f);
    }
    
    // Compute Kalman gain: K = P * H^T * S^-1
    // (Simplified: for now just use diagonal approximation)
    std::array<float, 9> gain;
    for (int i = 0; i < 9; ++i) {
        if (std::abs(S(i, i)) > 1e-6f) {
            gain[i] = covariance_(i, i) / S(i, i);
        } else {
            gain[i] = 0;
        }
    }
    
    // State update: x_new = x + K * innovation
    auto state_arr = state_.ToArray();
    float innovation_components[] = {innovation.x, innovation.y, innovation.z};
    for (int i = 0; i < 3; ++i) {
        state_arr[i] += gain[i] * innovation_components[i];  // Position update
        if (i + 3 < 9) {
            state_arr[i + 3] += gain[i + 3] * innovation_components[i];  // Velocity update
        }
    }
    state_.FromArray(state_arr);
    
    // Covariance update: P_new = (I - K * H) * P
    Matrix9x9 I_KH;
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            I_KH(i, j) = (i == j ? 1.0f : 0.0f) - gain[i] * H(i, j);
        }
    }
    covariance_ = I_KH * covariance_;
    
    // Ensure positive definiteness
    if (!covariance_.IsPositiveDefinite()) {
        for (int i = 0; i < 9; ++i) {
            covariance_(i, i) = std::max(covariance_(i, i), 1e-6f);
        }
    }
    
    ++update_count_;
}

Matrix9x9 EKFCore::ComputeF_Jacobian(float dt) {
    // Linearized state transition matrix
    // ∂x_new / ∂x_old
    Matrix9x9 F;
    
    // Position depends on velocity: ∂p / ∂p = I, ∂p / ∂v = I*dt
    for (int i = 0; i < 3; ++i) {
        F(i, i) = 1.0f;
        F(i, i + 3) = dt;  // Position += velocity * dt
    }
    
    // Velocity depends on accel: ∂v / ∂v = I, ∂v / ∂a ≈ I*dt
    for (int i = 0; i < 3; ++i) {
        F(i + 3, i + 3) = 1.0f;
        // (Accel comes from measurement, not state, so Jacobian is identity)
    }
    
    // Attitude (Euler angles) - simplified linear approximation
    // In reality, this would involve quaternion derivatives
    for (int i = 0; i < 3; ++i) {
        F(i + 6, i + 6) = 1.0f;
        // Attitude += angular_rate * dt (in measurement update, not state)
    }
    
    return F;
}

Matrix9x9 EKFCore::ComputeH_Jacobian(const EKFMeasurement& measurement) {
    Matrix9x9 H;  // Initialize to zero
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            H(i, j) = 0;
        }
    }

    std::visit(reflection::Overload{
        [&H](const AccelerometerMeasurement&) {
            // Measurement is accel, which relates to attitude
            // H maps state to expected accel measurement
            for (int i = 0; i < 3; ++i) {
                H(i, i + 6) = 1.0f;  // Attitude affects observed accel
            }
        },
        [&H](const BarometerMeasurement&) {
            // Measurement is altitude
            H(0, 0) = 1.0f;  // Altitude is state[0]
        },
        [&H](const VelocityMeasurement&) {
            // Measurement is 3D velocity
            for (int i = 0; i < 3; ++i) {
                H(i, i + 3) = 1.0f;  // Velocity is state[3:6]
            }
        },
        [&H](const HeadingMeasurement&) {
            // Measurement is yaw (3rd Euler angle)
            H(0, 8) = 1.0f;  // Yaw is state[8]
        }
    }, measurement.measurement);

    return H;
}

Vec3 EKFCore::ComputeInnovation(const EKFMeasurement& measurement) {
    return std::visit(reflection::Overload{
        [this](const AccelerometerMeasurement& m) -> Vec3 {
            // Predicted accel from attitude (simplified)
            return m.value - state_.attitude;
        },
        [this](const BarometerMeasurement& m) -> Vec3 {
            // Predicted altitude (scalar stored in x, y/z = 0)
            return Vec3(m.value, 0, 0) - Vec3(state_.position.x, 0, 0);
        },
        [this](const VelocityMeasurement& m) -> Vec3 {
            // Predicted velocity
            return m.value - state_.velocity;
        },
        [this](const HeadingMeasurement& m) -> Vec3 {
            // Predicted heading (yaw, scalar stored in x, y/z = 0)
            return Vec3(m.value, 0, 0) - Vec3(state_.attitude.z, 0, 0);
        }
    }, measurement.measurement);
}

void EKFCore::InitializeQ() {
    // Process noise matrix (9x9)
    // Assume uncertainty in state evolution
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            Q_(i, j) = 0;
        }
    }
    
    // Position noise (integration uncertainty from velocity)
    for (int i = 0; i < 3; ++i) {
        Q_(i, i) = 0.01f;
    }
    
    // Velocity noise (integration uncertainty from accel)
    for (int i = 3; i < 6; ++i) {
        Q_(i, i) = 0.1f;
    }
    
    // Attitude noise (gyro integration uncertainty)
    for (int i = 6; i < 9; ++i) {
        Q_(i, i) = 0.01f;
    }
}

void EKFCore::Reset() {
    state_ = EKFState();
    covariance_ = Matrix9x9();  // Reset to identity
    InitializeQ();
    update_count_ = 0;
    prediction_count_ = 0;
    last_timestamp_us_ = 0;
}

//=============================================================================
// EKFTuner Implementation
//=============================================================================

EKFTuner::EKFTuner() : current_phase_(0) {
    InitializePhaseMatrices();
}

void EKFTuner::SetFlightPhase(int phase) {
    current_phase_ = std::max(0, std::min(5, phase));
}

std::pair<Matrix9x9, std::array<float, 4>> EKFTuner::GetQAndR() const {
    return {GetQ(), GetR()};
}

Matrix9x9 EKFTuner::GetQ() const {
    return GetPhaseQ(current_phase_);
}

std::array<float, 4> EKFTuner::GetR() const {
    if (current_phase_ < 0 || current_phase_ >= 6) {
        return {{0.01f, 1.0f, 0.5f, 0.1f}};
    }
    return PHASE_R_VALUES[current_phase_];
}

void EKFTuner::InitializePhaseMatrices() {
    // Phase-specific Q matrices are defined statically in header
    // This function is called during initialization for any future setup
}

Matrix9x9 EKFTuner::GetPhaseQ(int phase) const {
    // Create phase-specific Q matrix based on phase
    Matrix9x9 Q;
    
    // Base values
    float pos_noise = 0.01f;
    float vel_noise = 0.1f;
    float att_noise = 0.01f;
    
    switch (phase) {
        case 0:  // Rail: Low acceleration noise
            pos_noise = 0.005f;
            vel_noise = 0.05f;
            att_noise = 0.005f;
            break;
        case 1:  // Boost: High acceleration noise
            pos_noise = 0.02f;
            vel_noise = 0.2f;
            att_noise = 0.01f;
            break;
        case 2:  // Coast: Moderate noise
            pos_noise = 0.01f;
            vel_noise = 0.1f;
            att_noise = 0.01f;
            break;
        case 3:  // Apogee: Low velocity noise, high attitude
            pos_noise = 0.005f;
            vel_noise = 0.05f;
            att_noise = 0.05f;
            break;
        case 4:  // Descent: Moderate noise
            pos_noise = 0.01f;
            vel_noise = 0.1f;
            att_noise = 0.02f;
            break;
        case 5:  // Landing: High position noise
            pos_noise = 0.02f;
            vel_noise = 0.15f;
            att_noise = 0.005f;
            break;
    }
    
    // Set diagonal elements
    for (int i = 0; i < 3; ++i) {
        Q(i, i) = pos_noise;
        Q(i + 3, i + 3) = vel_noise;
        Q(i + 6, i + 6) = att_noise;
    }
    
    return Q;
}

} // namespace avionics::estimators
