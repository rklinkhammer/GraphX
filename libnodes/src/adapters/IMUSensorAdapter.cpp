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
 * @file IMUSensorAdapter.cpp
 * @brief Implementation of unified IMU adapter for 3-axis sensors
 */

#include "avionics/adapters/IMUSensorAdapter.hpp"

#include <cmath>
#include <algorithm>

namespace graph {
namespace avionics {
namespace adapters {

void IMUSensorAdapter::Initialize(uint8_t accel_range_g,
                                  uint16_t gyro_range_dps) {
    accel_range_g_ = accel_range_g;
    gyro_range_dps_ = gyro_range_dps;

    // Initialize calibration to identity (no correction)
    accel_calibration_ = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                         0.0f, 0.0f, 0.0f};
    gyro_calibration_ = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        0.0f, 0.0f, 0.0f};
    mag_calibration_ = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                       0.0f, 0.0f, 0.0f};

    accel_is_calibrated_ = false;
    gyro_is_calibrated_ = false;
    mag_is_calibrated_ = false;

    // Initialize temperature
    temperature_c_ = 20.0f;  // Assume room temperature
    reference_temperature_c_ = 20.0f;

    // Initialize readings
    last_accel_ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                   std::chrono::nanoseconds(0), false};
    last_gyro_ = {0.0f, 0.0f, 0.0f, 0.0f,
                  std::chrono::nanoseconds(0), false};
    last_mag_ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 std::chrono::nanoseconds(0), false};

    // Initialize statistics
    accel_sample_count_ = 0;
    gyro_sample_count_ = 0;
    mag_sample_count_ = 0;
    last_update_time_ = std::chrono::nanoseconds(0);
}

void IMUSensorAdapter::SetAccelerometerCalibration(
    const AxisCalibration& calibration) {
    accel_calibration_ = calibration;
    accel_is_calibrated_ = true;
}

void IMUSensorAdapter::SetGyroscopeCalibration(
    const AxisCalibration& calibration) {
    gyro_calibration_ = calibration;
    gyro_is_calibrated_ = true;
}

void IMUSensorAdapter::SetMagnetometerCalibration(
    const AxisCalibration& calibration) {
    mag_calibration_ = calibration;
    mag_is_calibrated_ = true;
}

void IMUSensorAdapter::CalibrateAccelerometerAtRest(uint16_t sample_count) {
    // This is a placeholder - real implementation would need actual samples
    AxisCalibration cal = {0.0f, 0.0f, -GRAVITY_MS2, 1.0f, 1.0f, 1.0f,
                           0.0f, 0.0f, 0.0f};
    SetAccelerometerCalibration(cal);
}

void IMUSensorAdapter::CalibrateGyroscopeAtRest(uint16_t sample_count) {
    // This is a placeholder - real implementation would need actual samples
    AxisCalibration cal = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                          0.0f, 0.0f, 0.0f};
    SetGyroscopeCalibration(cal);
}

void IMUSensorAdapter::CalibrateMagnetometerFigure8(bool use_hard_iron) {
    // This is a placeholder - real implementation would track min/max values
    AxisCalibration cal = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                          0.0f, 0.0f, 0.0f};
    SetMagnetometerCalibration(cal);
}

void IMUSensorAdapter::UpdateTemperature(float temperature_c) {
    temperature_c_ = temperature_c;
}

AccelerometerReading IMUSensorAdapter::ProcessAccelerometerData(
    int16_t raw_x, int16_t raw_y, int16_t raw_z) {
    AccelerometerReading reading;

    // Apply calibration
    float cal_x, cal_y, cal_z;
    ApplyAxisCalibration(raw_x, raw_y, raw_z, accel_calibration_,
                        cal_x, cal_y, cal_z);

    // Convert raw to m/s² (assuming 16-bit ADC)
    // For ±16g range: LSB = 32g / 65536 = 0.00488 g/LSB = 0.0479 m/s²/LSB
    float lsb_to_ms2 = (2.0f * accel_range_g_ * GRAVITY_MS2) / 65536.0f;
    cal_x *= lsb_to_ms2;
    cal_y *= lsb_to_ms2;
    cal_z *= lsb_to_ms2;

    // Apply temperature compensation
    cal_x = ApplyAccelTemperatureCompensation(cal_x);
    cal_y = ApplyAccelTemperatureCompensation(cal_y);
    cal_z = ApplyAccelTemperatureCompensation(cal_z);

    reading.x_ms2 = cal_x;
    reading.y_ms2 = cal_y;
    reading.z_ms2 = cal_z;
    reading.total_magnitude = std::sqrt(cal_x * cal_x + cal_y * cal_y +
                                        cal_z * cal_z);
    reading.variance_ms4 = 0.1f;  // Typical noise floor
    reading.timestamp = std::chrono::nanoseconds(
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
            .time_since_epoch().count()));
    reading.is_valid = ValidateAccelerometerReading(reading);

    last_accel_ = reading;
    accel_sample_count_++;
    last_update_time_ = reading.timestamp;

    return reading;
}

GyroscopeReading IMUSensorAdapter::ProcessGyroscopeData(int16_t raw_x,
                                                        int16_t raw_y,
                                                        int16_t raw_z) {
    GyroscopeReading reading;

    // Apply calibration
    float cal_x, cal_y, cal_z;
    ApplyAxisCalibration(raw_x, raw_y, raw_z, gyro_calibration_,
                        cal_x, cal_y, cal_z);

    // Convert raw to dps
    // For ±2000 dps range: LSB = 4000 dps / 65536 = 0.0611 dps/LSB
    float lsb_to_dps = (2.0f * gyro_range_dps_) / 65536.0f;
    cal_x *= lsb_to_dps;
    cal_y *= lsb_to_dps;
    cal_z *= lsb_to_dps;

    // Apply temperature compensation
    cal_x = ApplyGyroTemperatureCompensation(cal_x);
    cal_y = ApplyGyroTemperatureCompensation(cal_y);
    cal_z = ApplyGyroTemperatureCompensation(cal_z);

    reading.roll_rate_dps = cal_x;
    reading.pitch_rate_dps = cal_y;
    reading.yaw_rate_dps = cal_z;
    reading.variance_dps2 = 0.5f;  // Typical noise floor
    reading.timestamp = std::chrono::nanoseconds(
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
            .time_since_epoch().count()));
    reading.is_valid = ValidateGyroscopeReading(reading);

    last_gyro_ = reading;
    gyro_sample_count_++;
    last_update_time_ = reading.timestamp;

    return reading;
}

MagnetometerReading IMUSensorAdapter::ProcessMagnetometerData(
    int16_t raw_x, int16_t raw_y, int16_t raw_z) {
    MagnetometerReading reading;

    // Apply calibration
    float cal_x, cal_y, cal_z;
    ApplyAxisCalibration(raw_x, raw_y, raw_z, mag_calibration_,
                        cal_x, cal_y, cal_z);

    // Convert raw to µT (microTesla)
    // Typical range: ±4.7 Gauss = ±470 µT (14-bit resolution)
    // LSB = 940 µT / 16384 = 0.0574 µT/LSB
    float lsb_to_ut = 940.0f / 16384.0f;
    cal_x *= lsb_to_ut;
    cal_y *= lsb_to_ut;
    cal_z *= lsb_to_ut;

    reading.x_uT = cal_x;
    reading.y_uT = cal_y;
    reading.z_uT = cal_z;
    reading.heading_deg = ComputeHeading(cal_x, cal_y);
    reading.inclination_deg = ComputeInclination(cal_x, cal_y, cal_z);
    reading.variance_uT2 = 25.0f;  // Typical noise floor
    reading.timestamp = std::chrono::nanoseconds(
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
            .time_since_epoch().count()));
    reading.is_valid = ValidateMagnetometerReading(reading);

    last_mag_ = reading;
    mag_sample_count_++;
    last_update_time_ = reading.timestamp;

    return reading;
}

AccelerometerReading IMUSensorAdapter::GetAccelerometerReading() const {
    return last_accel_;
}

GyroscopeReading IMUSensorAdapter::GetGyroscopeReading() const {
    return last_gyro_;
}

MagnetometerReading IMUSensorAdapter::GetMagnetometerReading() const {
    return last_mag_;
}

bool IMUSensorAdapter::IsCalibrated() const {
    return accel_is_calibrated_ && gyro_is_calibrated_ &&
           mag_is_calibrated_;
}

bool IMUSensorAdapter::IsConnected() const {
    return accel_sample_count_ > 0 && gyro_sample_count_ > 0 &&
           mag_sample_count_ > 0;
}

float IMUSensorAdapter::GetTemperature() const {
    return temperature_c_;
}

uint32_t IMUSensorAdapter::GetAccelSampleCount() const {
    return accel_sample_count_;
}

uint32_t IMUSensorAdapter::GetGyroSampleCount() const {
    return gyro_sample_count_;
}

uint32_t IMUSensorAdapter::GetMagSampleCount() const {
    return mag_sample_count_;
}

std::chrono::nanoseconds IMUSensorAdapter::GetTimeSinceLastUpdate() const {
    return last_update_time_;
}

bool IMUSensorAdapter::ValidateAccelerometerReading(
    const AccelerometerReading& reading) {
    // Check magnitude is reasonable
    // Expect mostly 9.81 m/s² gravity, but allow variance
    float expected = std::abs(GRAVITY_MS2 - reading.total_magnitude);
    if (expected > GRAVITY_TOLERANCE_MS2 && reading.total_magnitude > 1.0f) {
        // Allow some deviation but not too much
        if (expected > GRAVITY_TOLERANCE_MS2 * 3.0f) {
            return false;  // Too far from gravity
        }
    }

    // Check individual axes are within sensor limits
    if (reading.x_ms2 < ACCEL_MIN_MS2 || reading.x_ms2 > ACCEL_MAX_MS2 ||
        reading.y_ms2 < ACCEL_MIN_MS2 || reading.y_ms2 > ACCEL_MAX_MS2 ||
        reading.z_ms2 < ACCEL_MIN_MS2 || reading.z_ms2 > ACCEL_MAX_MS2) {
        return false;
    }

    return true;
}

bool IMUSensorAdapter::ValidateGyroscopeReading(
    const GyroscopeReading& reading) {
    // Check values are within sensor limits
    if (reading.roll_rate_dps < GYRO_MIN_DPS ||
        reading.roll_rate_dps > GYRO_MAX_DPS ||
        reading.pitch_rate_dps < GYRO_MIN_DPS ||
        reading.pitch_rate_dps > GYRO_MAX_DPS ||
        reading.yaw_rate_dps < GYRO_MIN_DPS ||
        reading.yaw_rate_dps > GYRO_MAX_DPS) {
        return false;
    }

    return true;
}

bool IMUSensorAdapter::ValidateMagnetometerReading(
    const MagnetometerReading& reading) {
    // Check magnitude is reasonable for Earth's magnetic field
    // Earth's field is typically 25-65 µT
    float magnitude = std::sqrt(reading.x_uT * reading.x_uT +
                               reading.y_uT * reading.y_uT +
                               reading.z_uT * reading.z_uT);
    if (magnitude < 10.0f || magnitude > 100.0f) {
        return false;
    }

    // Check individual axes are within sensor limits
    if (reading.x_uT < MAG_MIN_UT || reading.x_uT > MAG_MAX_UT ||
        reading.y_uT < MAG_MIN_UT || reading.y_uT > MAG_MAX_UT ||
        reading.z_uT < MAG_MIN_UT || reading.z_uT > MAG_MAX_UT) {
        return false;
    }

    return true;
}

void IMUSensorAdapter::ApplyAxisCalibration(
    int16_t raw_x, int16_t raw_y, int16_t raw_z,
    const AxisCalibration& calibration,
    float& cal_x, float& cal_y, float& cal_z) {
    // Convert raw to float
    float x = static_cast<float>(raw_x);
    float y = static_cast<float>(raw_y);
    float z = static_cast<float>(raw_z);

    // Apply calibration: corrected = (raw - offset) * scale + cross_coupling
    cal_x = (x - calibration.offset_x) * calibration.scale_x +
            calibration.cross_xy * (y - calibration.offset_y) +
            calibration.cross_xz * (z - calibration.offset_z);

    cal_y = (y - calibration.offset_y) * calibration.scale_y +
            calibration.cross_xy * (x - calibration.offset_x);

    cal_z = (z - calibration.offset_z) * calibration.scale_z +
            calibration.cross_xz * (x - calibration.offset_x);
}

float IMUSensorAdapter::ApplyAccelTemperatureCompensation(float accel) {
    float temp_delta = temperature_c_ - reference_temperature_c_;
    return accel * (1.0f + ACCEL_TEMP_COEFF * temp_delta);
}

float IMUSensorAdapter::ApplyGyroTemperatureCompensation(float rate) {
    float temp_delta = temperature_c_ - reference_temperature_c_;
    return rate * (1.0f + GYRO_TEMP_COEFF * temp_delta);
}

float IMUSensorAdapter::ComputeHeading(float mag_x, float mag_y) {
    float heading = std::atan2(mag_y, mag_x) * 180.0f / M_PI;
    if (heading < 0.0f) {
        heading += 360.0f;
    }
    return heading;
}

float IMUSensorAdapter::ComputeInclination(float mag_x, float mag_y,
                                          float mag_z) {
    float horizontal = std::sqrt(mag_x * mag_x + mag_y * mag_y);
    float inclination = std::atan2(mag_z, horizontal) * 180.0f / M_PI;
    return inclination;
}

} // namespace adapters
} // namespace avionics
} // namespace graph
