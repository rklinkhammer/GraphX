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
 * @file SensorManager.cpp
 * @brief Implementation of unified sensor manager with fusion
 */

#include "avionics/adapters/SensorManager.hpp"

#include <cmath>
#include <algorithm>

namespace graph {
namespace avionics {
namespace adapters {

void SensorManager::Initialize() {
    barometer_.Initialize();
    gps_.Initialize();
    imu_.Initialize();

    fusion_enabled_ = true;
    fusion_cycle_count_ = 0;
    last_fusion_timestamp_ = std::chrono::nanoseconds(0);

    // Initialize with equal weights
    barometer_weight_ = 0.5f;
    gps_weight_ = 0.5f;

    // Reset error counters
    barometer_error_count_ = 0;
    gps_error_count_ = 0;
    imu_error_count_ = 0;
}

void SensorManager::Shutdown() {
    fusion_enabled_ = false;
}

BarometricSensorAdapter& SensorManager::GetBarometer() {
    return barometer_;
}

GPSSensorAdapter& SensorManager::GetGPS() {
    return gps_;
}

IMUSensorAdapter& SensorManager::GetIMU() {
    return imu_;
}

FusedSensorOutput SensorManager::FuseAllSensors() {
    FusedSensorOutput output;

    // Get current readings from all sensors
    last_accel_ = imu_.GetAccelerometerReading();
    last_gyro_ = imu_.GetGyroscopeReading();
    last_mag_ = imu_.GetMagnetometerReading();
    last_gps_alt_ = gps_.GetAltitudeReading();
    last_baro_alt_ = barometer_.GetAltitudeReading();

    // Determine sensor health
    auto current_time = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    output.barometer_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        last_baro_alt_.timestamp,
        barometer_error_count_,
        last_baro_alt_.is_valid);

    output.gps_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        last_gps_alt_.timestamp,
        gps_error_count_,
        last_gps_alt_.is_valid);

    output.imu_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        last_accel_.timestamp,
        imu_error_count_,
        last_accel_.is_valid);

    // Fuse altitude
    output.fusion_degree = 0;
    if (last_baro_alt_.is_valid && last_gps_alt_.is_valid && fusion_enabled_) {
        output.fusion_degree = FuseAltitude(
            last_baro_alt_.altitude_m,
            last_baro_alt_.altitude_variance_m2,
            last_gps_alt_.altitude_m,
            last_gps_alt_.altitude_variance_m2,
            output.altitude_m,
            output.altitude_variance_m2);
    } else if (last_baro_alt_.is_valid) {
        output.altitude_m = last_baro_alt_.altitude_m;
        output.altitude_variance_m2 = last_baro_alt_.altitude_variance_m2;
        output.fusion_degree = 1;
    } else if (last_gps_alt_.is_valid) {
        output.altitude_m = last_gps_alt_.altitude_m;
        output.altitude_variance_m2 = last_gps_alt_.altitude_variance_m2;
        output.fusion_degree = 1;
    }

    output.altitude_health = output.barometer_health;
    output.has_valid_altitude = (output.fusion_degree > 0);

    // Use GPS position if available
    if (last_gps_alt_.is_valid && gps_.GetPosition(output.latitude_deg,
                                                      output.longitude_deg)) {
        output.position_accuracy_m = std::sqrt(
            last_gps_alt_.position_dop * last_gps_alt_.position_dop);
        output.position_health = output.gps_health;
        output.has_valid_position = true;
    }

    // Use GPS velocity if available
    auto gps_vel = gps_.GetVelocityReading();
    if (gps_vel.is_valid) {
        output.vertical_velocity_ms = gps_vel.vertical_velocity_ms;
        output.horizontal_velocity_ms = gps_vel.horizontal_velocity_ms;
        output.velocity_variance_ms2 = 0.5f;
        output.velocity_health = output.gps_health;
    }

    // Estimate attitude from IMU
    if (last_accel_.is_valid) {
        output.pitch_deg = EstimatePitchFromAccel(last_accel_);
        output.roll_deg = EstimateRollFromAccel(last_accel_);
        output.attitude_health = output.imu_health;
    }

    if (last_mag_.is_valid) {
        output.heading_deg = last_mag_.heading_deg;
    }

    // Temperature
    output.temperature_c = imu_.GetTemperature();

    // Timestamp
    output.timestamp = std::chrono::nanoseconds(
        static_cast<uint64_t>(current_time.count()));
    last_fusion_timestamp_ = output.timestamp;
    fusion_cycle_count_++;

    return output;
}

SensorStatus SensorManager::GetSensorStatus() const {
    SensorStatus status;

    auto current_time = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto baro_reading = barometer_.GetAltitudeReading();
    auto gps_reading = gps_.GetAltitudeReading();
    auto imu_reading = imu_.GetAccelerometerReading();

    status.barometer_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        baro_reading.timestamp,
        barometer_error_count_,
        baro_reading.is_valid);

    status.gps_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        gps_reading.timestamp,
        gps_error_count_,
        gps_reading.is_valid);

    status.imu_health = DetermineSensorHealth(
        std::chrono::nanoseconds(static_cast<uint64_t>(current_time.count())) -
        imu_reading.timestamp,
        imu_error_count_,
        imu_reading.is_valid);

    status.barometer_error_count = barometer_error_count_;
    status.gps_error_count = gps_error_count_;
    status.imu_error_count = imu_error_count_;

    status.last_barometer_update = baro_reading.timestamp;
    status.last_gps_update = gps_reading.timestamp;
    status.last_imu_update = imu_reading.timestamp;

    status.has_valid_altitude = baro_reading.is_valid || gps_reading.is_valid;
    status.has_valid_position = gps_.HasValidFix();
    status.has_valid_attitude = imu_reading.is_valid;

    return status;
}

bool SensorManager::HasMinimumSensors() const {
    // Minimum: IMU must be present and connected
    return imu_.IsConnected();
}

bool SensorManager::HasAllSensors() const {
    return barometer_.IsConnected() && gps_.IsConnected() &&
           imu_.IsConnected();
}

std::chrono::nanoseconds SensorManager::GetTimeSinceLastUpdate() const {
    auto baro_time = barometer_.GetAltitudeReading().timestamp;
    auto gps_time = gps_.GetAltitudeReading().timestamp;
    auto imu_time = imu_.GetAccelerometerReading().timestamp;

    auto max_time = std::max({baro_time, gps_time, imu_time});
    return max_time;
}

bool SensorManager::PerformSelfTest(uint32_t duration_ms) {
    // Auto-calibrate stationary sensors
    if (!imu_.IsCalibrated()) {
        imu_.CalibrateAccelerometerAtRest(50);
        imu_.CalibrateGyroscopeAtRest(50);
    }

    // Verify all sensors are responding
    bool baro_ok = barometer_.IsConnected();
    bool gps_ok = gps_.IsConnected();
    bool imu_ok = imu_.IsConnected();

    return HasMinimumSensors();
}

void SensorManager::UpdateTemperature(float temperature_c) {
    imu_.UpdateTemperature(temperature_c);
}

void SensorManager::ResetErrorCounters() {
    barometer_error_count_ = 0;
    gps_error_count_ = 0;
    imu_error_count_ = 0;
}

uint32_t SensorManager::GetFusionCycleCount() const {
    return fusion_cycle_count_;
}

float SensorManager::GetBlendedAltitude() const {
    auto baro_alt = barometer_.GetAltitudeReading();
    auto gps_alt = gps_.GetAltitudeReading();

    if (baro_alt.is_valid && gps_alt.is_valid) {
        // Weight by inverse variance
        float baro_weight = 1.0f / (baro_alt.altitude_variance_m2 + 0.01f);
        float gps_weight = 1.0f / (gps_alt.altitude_variance_m2 + 0.01f);
        float total_weight = baro_weight + gps_weight;

        return (baro_alt.altitude_m * baro_weight +
                gps_alt.altitude_m * gps_weight) / total_weight;
    } else if (baro_alt.is_valid) {
        return baro_alt.altitude_m;
    } else if (gps_alt.is_valid) {
        return gps_alt.altitude_m;
    }

    return 0.0f;
}

float SensorManager::GetAltitudeConfidence() const {
    auto baro_alt = barometer_.GetAltitudeReading();
    auto gps_alt = gps_.GetAltitudeReading();

    if (!baro_alt.is_valid && !gps_alt.is_valid) {
        return 0.0f;
    }

    if (baro_alt.is_valid && gps_alt.is_valid) {
        // Both available - check agreement
        float diff = std::abs(baro_alt.altitude_m - gps_alt.altitude_m);
        if (diff < ALTITUDE_AGREEMENT_THRESHOLD_M) {
            return 1.0f;  // High confidence when sensors agree
        } else {
            return 0.5f;  // Medium confidence when they disagree
        }
    }

    // Single sensor - moderate confidence
    return 0.7f;
}

void SensorManager::SetFusionEnabled(bool enabled) {
    fusion_enabled_ = enabled;
}

bool SensorManager::IsFusionEnabled() const {
    return fusion_enabled_;
}

uint8_t SensorManager::FuseAltitude(float baro_alt, float baro_var,
                                     float gps_alt, float gps_var,
                                     float& fused_alt, float& fused_var) {
    // Simple weighted average based on variance
    // Lower variance = higher weight
    float baro_weight = 1.0f / (baro_var + 0.1f);
    float gps_weight = 1.0f / (gps_var + 0.1f);
    float total_weight = baro_weight + gps_weight;

    // Normalize weights
    baro_weight /= total_weight;
    gps_weight /= total_weight;

    // Ensure minimum weights
    baro_weight = std::max(baro_weight, MIN_FUSION_WEIGHT);
    gps_weight = std::max(gps_weight, MIN_FUSION_WEIGHT);

    // Renormalize
    total_weight = baro_weight + gps_weight;
    baro_weight /= total_weight;
    gps_weight /= total_weight;

    fused_alt = baro_alt * baro_weight + gps_alt * gps_weight;
    fused_var = baro_var * baro_weight * baro_weight +
                gps_var * gps_weight * gps_weight;

    return 2;  // Both sensors contributing
}

float SensorManager::EstimatePitchFromAccel(
    const AccelerometerReading& accel_reading) {
    // Pitch = atan2(ax, sqrt(ay² + az²))
    float denominator = std::sqrt(accel_reading.y_ms2 * accel_reading.y_ms2 +
                                  accel_reading.z_ms2 * accel_reading.z_ms2);
    float pitch_rad = std::atan2(accel_reading.x_ms2, denominator);
    return pitch_rad * 180.0f / M_PI;
}

float SensorManager::EstimateRollFromAccel(
    const AccelerometerReading& accel_reading) {
    // Roll = atan2(ay, az)
    float roll_rad =
        std::atan2(accel_reading.y_ms2, accel_reading.z_ms2);
    return roll_rad * 180.0f / M_PI;
}

SensorHealth SensorManager::DetermineSensorHealth(
    std::chrono::nanoseconds last_update, uint32_t error_count,
    bool data_valid) {
    // Check if data is stale
    if (last_update > SENSOR_TIMEOUT_NS) {
        return SensorHealth::STALE_DATA;
    }

    // Check error rate
    if (error_count > 10) {
        return SensorHealth::FAILED;
    }

    // Check if latest data is valid
    if (!data_valid && error_count > 0) {
        return SensorHealth::DEGRADED;
    }

    return SensorHealth::HEALTHY;
}

} // namespace adapters
} // namespace avionics
} // namespace graph
