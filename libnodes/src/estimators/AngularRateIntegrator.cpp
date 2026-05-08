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

#include "avionics/estimators/AngularRateIntegrator.hpp"
#include "avionics/estimators/AttitudeQuaternion.hpp"
#include <cmath>

namespace avionics::estimators {

AngularRateIntegrator::AngularRateIntegrator()
    : attitude_{1.0f, 0.0f, 0.0f, 0.0f},  // Identity quaternion
      angular_velocity_{0, 0, 0},
      last_timestamp_us_{0},
      last_delta_time_s_{0.0},
      update_count_{0} {
}

void AngularRateIntegrator::Update(const Vector3D& angular_velocity_rad_s, uint64_t timestamp_us) {
    // Store the angular velocity
    angular_velocity_ = angular_velocity_rad_s;
    
    // First update: initialize timestamp
    if (update_count_ == 0) {
        last_timestamp_us_ = timestamp_us;
        update_count_ = 1;
        return;
    }
    
    // Compute delta time in seconds
    double dt_us = static_cast<double>(timestamp_us - last_timestamp_us_);
    double dt_s = dt_us / 1'000'000.0;  // Convert microseconds to seconds
    
    // Skip update if dt is negative or zero (invalid timestamp)
    if (dt_s <= 0.0) {
        return;
    }
    
    // Cap maximum dt to 1 second (prevents huge jumps if timestamp wraps)
    dt_s = std::min(dt_s, 1.0);
    
    // Integrate quaternion using angular velocity
    attitude_ = IntegrateQuaternion(attitude_, angular_velocity_rad_s, dt_s);
    
    // Normalize to prevent drift
    attitude_ = NormalizeQuaternion(attitude_);
    
    // Update state for next iteration
    last_timestamp_us_ = timestamp_us;
    last_delta_time_s_ = dt_s;
    update_count_++;
}

void AngularRateIntegrator::Reset(const Quaternion& initial_attitude) {
    attitude_ = initial_attitude;
    angular_velocity_ = Vector3D{0, 0, 0};
    last_timestamp_us_ = 0;
    last_delta_time_s_ = 0.0;
    update_count_ = 0;
}

Quaternion AngularRateIntegrator::IntegrateQuaternion(
    const Quaternion& q, const Vector3D& omega, double dt) {
    
    // Exponential map integration: q_new = q_old * exp(0.5 * omega * dt)
    // 
    // For a quaternion representing rotation of angle theta around axis n:
    // exp(0.5 * omega * dt) = [cos(0.5*theta), n*sin(0.5*theta)]
    // where theta = |omega| * dt
    
    float omega_mag = std::sqrt(omega.x * omega.x + omega.y * omega.y + omega.z * omega.z);
    
    // Avoid division by zero for zero angular velocity
    if (omega_mag < 1e-7f) {
        return q;
    }
    
    // Half-angle
    float half_angle = 0.5f * omega_mag * static_cast<float>(dt);
    
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
    
    // Quaternion multiplication: q_new = q * dq
    // Multiply q by delta quaternion
    return {
        q.w * dq.w - q.x * dq.x - q.y * dq.y - q.z * dq.z,
        q.w * dq.x + q.x * dq.w + q.y * dq.z - q.z * dq.y,
        q.w * dq.y - q.x * dq.z + q.y * dq.w + q.z * dq.x,
        q.w * dq.z + q.x * dq.y - q.y * dq.x + q.z * dq.w
    };
}

Quaternion AngularRateIntegrator::NormalizeQuaternion(const Quaternion& q) {
    float mag = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    
    // Prevent division by zero
    if (mag < 1e-7f) {
        return Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    }
    
    return Quaternion{q.w / mag, q.x / mag, q.y / mag, q.z / mag};
}

} // namespace avionics::estimators
