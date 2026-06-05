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
 * @file sensor/SensorDataTypes.hpp
 * @brief Standard sensor data types for flight simulation and telemetry
 *
 * Defines timestamped sensor data structures compatible with OpenRocket,
 * RASAero, and JSBSim flight simulators. All types inherit from TimestampedData
 * for consistent timestamp management across the telemetry pipeline.
 *
 * @author GraphX Contributors
 * @date 2025
 */

#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <variant>
#include "config/BasicTypes.hpp"
#include "config/TimestampedData.hpp"

namespace sensors {

// ──────────────────────────────────────────────────────────────────────────
// Basic Timestamped Types
// ──────────────────────────────────────────────────────────────────────────

/**
 * @struct TimestampedVector3D
 * @brief 3-axis vector with nanosecond-resolution timestamp
 *
 * Used for representing timestamped sensor measurements such as accelerometer,
 * gyroscope, and magnetometer readings.
 */
struct TimestampedVector3D : public TimestampedData {
    Vector3D value;  ///< 3D vector value
    
    TimestampedVector3D() = default;
    
    /// Construct with individual components and timestamp (in seconds)
    TimestampedVector3D(double ts_seconds, float x, float y, float z) 
        : value(x, y, z) {
        SetTimestampSeconds(ts_seconds);
    }
    
    /// Construct with Vector3D value and timestamp (in seconds)
    TimestampedVector3D(double ts_seconds, const Vector3D& v) : value(v) {
        SetTimestampSeconds(ts_seconds);
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Scalar and Aggregated Sensor Types
// ──────────────────────────────────────────────────────────────────────────

/**
 * @struct TimestampedScalar
 * @brief Scalar value with nanosecond-resolution timestamp
 *
 * Used for single numeric measurements such as altitude, velocity magnitude,
 * or temperature.
 */
struct TimestampedScalar : public TimestampedData {
    double value{0.0};  ///< Scalar value
    
    TimestampedScalar() = default;
    
    /// Construct with value and timestamp (in seconds)
    TimestampedScalar(double ts_seconds, double v) : value(v) {
        SetTimestampSeconds(ts_seconds);
    }
};

/**
 * @struct StateVector
 * @brief Complete 6-DOF state vector (position, velocity, orientation) with confidence metrics
 *
 * Comprehensive vehicle state combining position, velocity, acceleration,
 * attitude, angular velocity, magnetic field, mass, thrust, and confidence estimates.
 * Phase 3 enhancement adds magnetic field and per-field confidence metrics.
 */
struct StateVector : public TimestampedData {
    // ===== Core Position & Velocity (Phase 1-2) =====
    Vector3D position{0, 0, 0};              ///< Position (m) - typically ECI or ECEF coordinates
    Vector3D velocity{0, 0, 0};              ///< Velocity (m/s)
    Vector3D acceleration{0, 0, 0};          ///< Acceleration (m/s²)
    
    // ===== Orientation & Angular Motion (Phase 3) =====
    Quaternion attitude;                     ///< Orientation quaternion [qx, qy, qz, qw]
    Vector3D angular_velocity{0, 0, 0};      ///< Angular velocity (rad/s)
    float heading_rad{0.0f};                 ///< Magnetic heading in radians [0, 2π)
    
    // ===== GPS Position Data (Phase 5D) =====
    double latitude{0.0};                    ///< Latitude (degrees WGS84)
    double longitude{0.0};                   ///< Longitude (degrees WGS84)
    double horizontal_accuracy{0.0};         ///< Horizontal accuracy (m)
    double vertical_accuracy{0.0};           ///< Vertical accuracy (m)
    uint8_t num_satellites{0};               ///< Number of satellites in fix
    bool gps_fix_valid{false};               ///< Whether GPS fix is valid
    
    // ===== Vehicle Properties =====
    double mass{0.0};                        ///< Mass (kg)
    double thrust{0.0};                      ///< Thrust (N) - if powered stage
    
    // ===== Environmental Measurements (Phase 3) =====
    Vector3D magnetic_field{0, 0, 0};        ///< Magnetic field (Tesla) - calibrated Earth field
    
    // ===== Per-Sensor Altitude Sources (Phase 7 - Voting Architecture) =====
    /// Independent altitude estimates from each sensor source
    struct {
        float from_accel{0.0f};        ///< Altitude estimate from integrated acceleration
        float from_baro{0.0f};         ///< Altitude estimate from barometric pressure (ISA model)
        float from_gps{0.0f};          ///< Altitude estimate from GPS (WGS84 ellipsoid height)
        float confidence_accel{0.0f};  ///< Confidence in accel-derived altitude
        float confidence_baro{0.0f};   ///< Confidence in barometric altitude (based on pressure stability)
        float confidence_gps{0.0f};    ///< Confidence in GPS altitude (based on fix quality, satellite count)
    } altitude_sources;
    
    // ===== Per-Sensor Velocity Sources (Phase 7 - Voting Architecture) =====
    /// Independent vertical velocity estimates from each sensor source
    struct {
        float z_from_accel{0.0f};      ///< Vertical velocity from integrated acceleration
        float z_from_baro{0.0f};       ///< Vertical velocity from barometric altitude rate
        float z_from_gps{0.0f};        ///< Vertical velocity from GPS (vertical speed component)
        float confidence_accel{0.0f};  ///< Confidence in accel-derived velocity
        float confidence_baro{0.0f};   ///< Confidence in baro-derived velocity (based on altitude rate stability)
        float confidence_gps{0.0f};    ///< Confidence in GPS velocity (based on fix quality)
    } velocity_sources;
    
    // ===== Confidence Metrics (Phase 3) =====
    /// Confidence levels (0.0 = no confidence, 1.0 = full confidence)
    struct {
        float altitude{1.0f};       ///< Altitude measurement confidence (fused from consensus voting)
        float velocity{1.0f};       ///< Velocity estimate confidence (fused from consensus voting)
        float acceleration{1.0f};   ///< Acceleration measurement confidence
        float attitude{1.0f};       ///< Attitude (heading) estimate confidence
        float heading{1.0f};        ///< Magnetic heading confidence
        float gps_position{1.0f};   ///< GPS position confidence (Phase 5D)
    } confidence;
    
    /**
     * @brief Validate that confidence metrics are in valid range [0, 1]
     * @return true if all confidence values are in range, false otherwise
     */
    bool IsConfidenceValid() const {
        return confidence.altitude >= 0.0f && confidence.altitude <= 1.0f &&
               confidence.velocity >= 0.0f && confidence.velocity <= 1.0f &&
               confidence.acceleration >= 0.0f && confidence.acceleration <= 1.0f &&
               confidence.attitude >= 0.0f && confidence.attitude <= 1.0f &&
               confidence.heading >= 0.0f && confidence.heading <= 1.0f &&
               confidence.gps_position >= 0.0f && confidence.gps_position <= 1.0f;
    }
    
    /**
     * @brief Get average confidence across all fields
     * @return Average confidence (0.0-1.0)
     */
    float GetAverageConfidence() const {
        return (confidence.altitude + confidence.velocity + confidence.acceleration + 
                confidence.attitude + confidence.heading + confidence.gps_position) / 6.0f;
    }
    
    /**
     * @brief Set all confidence metrics to same value
     * @param value Confidence value (should be in [0, 1])
     */
    void SetConfidenceUniform(float value) {
        confidence.altitude = value;
        confidence.velocity = value;
        confidence.acceleration = value;
        confidence.attitude = value;
        confidence.heading = value;
        confidence.gps_position = value;
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Modern Sensor Data Wrappers (Individual Sensor Types)
// ──────────────────────────────────────────────────────────────────────────

/**
 * @struct GPSPosition
 * @brief GPS position data with accuracy metrics
 *
 * Represents a single GPS fix including position, accuracy estimates,
 * and quality indicators. Simplified alternative to GNSSData for
 * applications that only need position information.
 */
struct GPSPositionData : public TimestampedData {
    double latitude{0.0};              ///< Latitude (degrees)
    double longitude{0.0};             ///< Longitude (degrees)
    double altitude{0.0};              ///< Altitude (m) - WGS84 ellipsoid height
    double horizontal_accuracy{0.0};   ///< Horizontal accuracy (m)
    double vertical_accuracy{0.0};     ///< Vertical accuracy (m)
    double ground_speed{0.0};          ///< Ground speed (m/s)
    double heading{0.0};               ///< Heading (degrees from north, 0-360)
    uint8_t num_satellites{0};         ///< Number of satellites in fix
    bool fix_valid{false};             ///< Whether the GPS fix is valid
    
    /// Default constructor
    GPSPositionData() = default;
    
    /// Construct with position values
    GPSPositionData(double lat, double lon, double alt, 
                    double h_acc = 0.0, double v_acc = 0.0,
                    double speed = 0.0, double head = 0.0,
                    uint8_t sats = 0, bool valid = false)
        : latitude(lat), longitude(lon), altitude(alt),
          horizontal_accuracy(h_acc), vertical_accuracy(v_acc),
          ground_speed(speed), heading(head),
          num_satellites(sats), fix_valid(valid) {}
};

/**
 * @struct AccelerometerData
 * @brief Accelerometer sensor data with timestamp
 *
 * Represents 3-axis acceleration measurements from an accelerometer sensor.
 */
struct AccelerometerData : public TimestampedData {
    Vector3D acceleration{0, 0, 0};  ///< Acceleration (m/s²)
    
    AccelerometerData() = default;
    
    /// Construct from Vector3D with timestamp
    AccelerometerData(const Vector3D& accel, double ts_seconds = 0.0) 
        : acceleration(accel) {
        SetTimestampSeconds(ts_seconds);
    }
    
    /// Construct from individual components
    explicit AccelerometerData(float x, float y, float z) 
        : acceleration(x, y, z) {}
    
};

/**
 * @struct GyroscopeData
 * @brief Gyroscope sensor data with timestamp
 *
 * Represents 3-axis angular velocity measurements from a gyroscope sensor.
 */
struct GyroscopeData : public TimestampedData {
    Vector3D angular_velocity{0, 0, 0};  ///< Angular velocity (rad/s)
    
    GyroscopeData() = default;
    
    /// Construct from Vector3D with timestamp
    GyroscopeData(const Vector3D& gyro, double ts_seconds = 0.0) 
        : angular_velocity(gyro) {
        SetTimestampSeconds(ts_seconds);
    }
    
    /// Construct from individual components
    explicit GyroscopeData(float x, float y, float z) 
        : angular_velocity(x, y, z) {}
    
};

/**
 * @struct MagnetometerData
 * @brief Magnetometer sensor data with timestamp
 *
 * Represents 3-axis magnetic field measurements from a magnetometer sensor.
 */
struct MagnetometerData : public TimestampedData {
    Vector3D magnetic_field{0, 0, 0};  ///< Magnetic field (µT - microtesla)
    
    MagnetometerData() = default;
    
    /// Construct from Vector3D with timestamp
    MagnetometerData(const Vector3D& mag, double ts_seconds = 0.0) 
        : magnetic_field(mag) {
        SetTimestampSeconds(ts_seconds);
    }
    
    /// Construct from individual components
    explicit MagnetometerData(float x, float y, float z) 
        : magnetic_field(x, y, z) {}
    
};

/**
 * @struct BarometricData
 * @brief Barometric pressure and temperature measurement
 *
 * Simplified pressure sensor data containing pressure and temperature readings.
 * Includes equality operators for testing and comparison.
 */
struct BarometricData : public TimestampedData {
    float pressure_pa{0.0f};      ///< Pressure (Pa)
    float temperature_k{288.15f}; ///< Temperature (K) - defaults to standard sea level
    
    BarometricData() = default;
    
    /// Construct with pressure and temperature
    BarometricData(float p, float t) 
        : pressure_pa(p), temperature_k(t) {}
    
    /// Equality comparison operator
    bool operator==(const BarometricData& other) const {
        return pressure_pa == other.pressure_pa && 
               temperature_k == other.temperature_k;
    }
    
    /// Inequality comparison operator
    bool operator!=(const BarometricData& other) const {
        return !(*this == other);
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Polymorphic Sensor Type Variant
// ──────────────────────────────────────────────────────────────────────────

/// Variant type for polymorphic access to sensor payloads
using SensorPayload = std::variant<AccelerometerData,
                                   GyroscopeData,
                                   MagnetometerData,
                                   BarometricData,
                                   GPSPositionData>;

}  // namespace sensors

