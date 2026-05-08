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

#include "avionics/estimators/AdaptiveComplementaryFilter.hpp"
#include <algorithm>

namespace avionics::estimators {

// Initialize phase-specific weight configurations
const std::array<FilterWeights, 6> AdaptiveComplementaryFilter::PHASE_WEIGHTS = {{
    FilterWeights(0.8f, 0.2f, 0.0f, 0.0f),  // Rail: High accel trust
    FilterWeights(0.6f, 0.4f, 0.0f, 0.0f),  // Boost: Balanced accel/gyro
    FilterWeights(0.5f, 0.5f, 0.2f, 0.0f),  // Coast: Add some mag
    FilterWeights(0.2f, 0.8f, 0.5f, 0.0f),  // Apogee: Trust gyro/mag
    FilterWeights(0.5f, 0.5f, 0.3f, 0.0f),  // Descent: Balanced
    FilterWeights(0.9f, 0.1f, 0.0f, 0.0f),  // Landing: High accel
}};

AdaptiveComplementaryFilter::AdaptiveComplementaryFilter()
    : attitude_(sensors::Quaternion()),  // Identity quaternion (w=1, x=y=z=0)
      position_(0, 0, 0),
      velocity_(0, 0, 0),
      magnetic_bias_(0, 0, 0),
      accel_integrated_altitude_(0),
      accel_integrated_velocity_(0, 0, 0),
      current_weights_(0.5f, 0.5f, 0.0f, 0.0f),
      current_phase_(0),
      update_count_(0),
      last_timestamp_us_(0) {
}

void AdaptiveComplementaryFilter::SetFlightPhase(int phase) {
    current_phase_ = std::max(0, std::min(5, phase));  // Clamp to [0, 5]
    current_weights_ = GetPhaseWeights();
}

void AdaptiveComplementaryFilter::Update(
    const Vec3& accel_m_s2,
    const Vec3& gyro_rad_s,
    const Vec3& mag_calibrated,
    float external_altitude,
    const Vec3& external_velocity,
    float dt,
    uint64_t timestamp_us
) {
    if (dt <= 0 || dt > 1.0f) return;  // Invalid time step
    
    // Update phase-dependent weights
    current_weights_ = GetPhaseWeights();
    
    // Normalize weights
    FilterWeights normalized = current_weights_.Normalize();
    
    // Step 1: Update attitude from gyroscope
    UpdateAttitudeFromGyro(gyro_rad_s, dt);
    
    // Step 2: Update attitude from accelerometer (corrective influence)
    if (accel_m_s2.Magnitude() > 1e-6f) {
        UpdateAttitudeFromAccel(accel_m_s2);
    }
    
    // Step 3: Update heading from magnetometer
    if (mag_calibrated.Magnitude() > 1e-6f) {
        UpdateHeadingFromMag(mag_calibrated);
    }
    
    // Step 4: Integrate velocity from acceleration (accel-biased)
    Vec3 accel_corrected = accel_m_s2;
    accel_corrected.z -= 9.81f;  // Remove gravity component
    IntegrateVelocity(accel_corrected, dt);
    
    // Step 5: Correct altitude from external reference
    if (normalized.external_weight > 1e-6f) {
        accel_integrated_altitude_ = 
            accel_integrated_altitude_ * (1.0f - normalized.external_weight) +
            external_altitude * normalized.external_weight;
    }
    
    // Step 6: Correct velocity from external reference
    if (normalized.external_weight > 1e-6f) {
        Vec3 external_corrected = external_velocity;
        accel_integrated_velocity_ = 
            accel_integrated_velocity_ * (1.0f - normalized.external_weight) +
            external_corrected * normalized.external_weight;
    }
    
    // Update state
    position_.z = accel_integrated_altitude_;  // Altitude in Z
    velocity_ = accel_integrated_velocity_;
    
    ++update_count_;
    last_timestamp_us_ = timestamp_us;
}

FilterWeights AdaptiveComplementaryFilter::GetPhaseWeights() const {
    if (current_phase_ < 0 || current_phase_ >= 6) {
        return FilterWeights(0.5f, 0.5f, 0.0f, 0.0f);
    }
    return PHASE_WEIGHTS[current_phase_];
}

void AdaptiveComplementaryFilter::UpdateAttitudeFromGyro(const Vec3& omega, float dt) {
    // Integrate angular velocity to get rotation quaternion
    float angle = omega.Magnitude() * dt;
    if (angle > 1e-6f) {
        sensors::Vector3D omega_vec(omega.x, omega.y, omega.z);
        Quaternion omega_q = Quaternion::from_axis_angle(omega_vec, angle);
        attitude_ = (omega_q * attitude_).normalized();
    }
}

void AdaptiveComplementaryFilter::UpdateAttitudeFromAccel(const Vec3& accel) {
    if (accel.Magnitude() < 1e-6f) return;
    
    // Compute roll and pitch from accelerometer
    Vec3 accel_normalized = accel.Normalize();
    
    // Roll: atan2(ay, az)
    float roll = std::atan2(accel_normalized.y, accel_normalized.z);
    
    // Pitch: asin(-ax / g)
    float pitch = std::asin(-accel_normalized.x);
    
    // Get current yaw from attitude
    float current_roll, current_pitch, current_yaw;
    attitude_.to_euler_angles(current_roll, current_pitch, current_yaw);
    
    // Blend with accelerometer-derived angles (small correction)
    float alpha = 0.1f;  // Complementary filter blend factor
    current_roll = current_roll * (1.0f - alpha) + roll * alpha;
    current_pitch = current_pitch * (1.0f - alpha) + pitch * alpha;
    
    // Recreate quaternion with corrected roll/pitch
    attitude_ = Quaternion::from_euler_angles(current_roll, current_pitch, current_yaw).normalized();
}

void AdaptiveComplementaryFilter::UpdateHeadingFromMag(const Vec3& mag) {
    if (mag.Magnitude() < 1e-6f) return;
    
    // Get current attitude
    float roll, pitch, yaw;
    attitude_.to_euler_angles(roll, pitch, yaw);
    
    // Compute heading from magnetometer (simplified)
    // In real implementation, would account for declination and tilt
    float mag_heading = std::atan2(mag.y, mag.x);
    
    // Small correction to yaw
    float alpha = 0.05f;  // Magnetometer correction strength
    yaw = yaw * (1.0f - alpha) + mag_heading * alpha;
    
    // Recreate quaternion with corrected yaw
    attitude_ = Quaternion::from_euler_angles(roll, pitch, yaw).normalized();
}

void AdaptiveComplementaryFilter::IntegrateVelocity(const Vec3& accel, float dt) {
    // Simple integration (Euler method)
    accel_integrated_velocity_ = accel_integrated_velocity_ + accel * dt;
    
    // Limit velocity to reasonable bounds (prevent unbounded growth from bias)
    float vel_mag = accel_integrated_velocity_.Magnitude();
    if (vel_mag > 100.0f) {
        accel_integrated_velocity_ = accel_integrated_velocity_.Normalize() * 100.0f;
    }
    
    // Integrate altitude from vertical velocity
    accel_integrated_altitude_ += accel_integrated_velocity_.z * dt;
}

void AdaptiveComplementaryFilter::Reset(const Quaternion& initial_attitude) {
    attitude_ = initial_attitude.normalized();
    position_ = Vec3(0, 0, 0);
    velocity_ = Vec3(0, 0, 0);
    magnetic_bias_ = Vec3(0, 0, 0);
    accel_integrated_altitude_ = 0;
    accel_integrated_velocity_ = Vec3(0, 0, 0);
    update_count_ = 0;
    last_timestamp_us_ = 0;
}

} // namespace avionics::estimators
