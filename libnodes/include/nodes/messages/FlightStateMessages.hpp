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
 * @file FlightStateMessages.hpp
 * @brief Message structures for flight state FSM integration
 *
 * Defines all message types for communication within the flight FSM,
 * including state vectors, debug messages, and phase transitions.
 *
 * @author Robert Klinkhammer
 * @date 2025-01-05
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <cmath>
#include <array>

namespace graph {
namespace avionics {
namespace messages {

/**
 * @brief Flight phase enumeration
 */
enum class FlightPhase : uint8_t {
    PRE_FLIGHT = 0,      ///< Pre-flight checks (motors off, stationary)
    IDLE = 1,            ///< Engines started, not yet flying
    LAUNCH = 2,          ///< Active ascent phase
    COAST = 3,           ///< Coasting (engine cutoff, ascending)
    APOGEE = 4,          ///< Near apogee, final descent preparation
    DESCENT = 5,         ///< Active descent, parachute deployed
    LANDED = 6,          ///< Landed and powered down
    ABORT = 7,           ///< Emergency abort sequence
    UNKNOWN = 255        ///< Phase indeterminate
};

/**
 * @brief Flight state vector
 *
 * Comprehensive state containing position, velocity, acceleration, attitude,
 * and flight phase information.
 */
struct FlightStateVector {
    // Kinematic state
    float altitude_m;              ///< Altitude above sea level (meters)
    float vertical_velocity_ms;    ///< Vertical velocity (m/s, positive = up)
    float vertical_acceleration_ms2; ///< Vertical acceleration (m/s²)
    
    float latitude_deg;            ///< Latitude (degrees)
    float longitude_deg;           ///< Longitude (degrees)
    float horizontal_velocity_ms;  ///< Horizontal velocity (m/s)
    
    // Attitude
    float heading_deg;             ///< Heading/yaw (degrees, 0=North)
    float pitch_deg;               ///< Pitch angle (degrees)
    float roll_deg;                ///< Roll angle (degrees)
    
    // Rotation rates
    float roll_rate_dps;           ///< Roll rate (degrees/second)
    float pitch_rate_dps;          ///< Pitch rate (degrees/second)
    float yaw_rate_dps;            ///< Yaw rate (degrees/second)
    
    // Environment
    float temperature_c;           ///< Temperature (°C)
    float pressure_pa;             ///< Atmospheric pressure (Pa)
    
    // Flight phase and validation
    FlightPhase flight_phase;      ///< Current flight phase
    uint8_t sensor_health;         ///< Bitmask of sensor health (bit 0=accel, bit 1=gyro, etc.)
    bool is_valid;                 ///< Is state vector valid?
    
    // Timestamp
    std::chrono::nanoseconds timestamp; ///< State vector timestamp
};

/**
 * @brief Debug telemetry message
 *
 * Detailed diagnostic information for flight analysis and debugging.
 */
struct DebugTelemetry {
    // Raw sensor data (before fusion)
    float raw_accel_x_ms2;
    float raw_accel_y_ms2;
    float raw_accel_z_ms2;
    
    float raw_gyro_x_dps;
    float raw_gyro_y_dps;
    float raw_gyro_z_dps;
    
    float raw_mag_x_ut;
    float raw_mag_y_ut;
    float raw_mag_z_ut;
    
    float raw_pressure_pa;
    float raw_altitude_m;
    
    // Estimator states
    float estimated_vertical_velocity_ms;
    float estimated_drag_coefficient;
    float estimated_mass_kg;
    
    // Fusion metrics
    float altitude_fusion_weight_baro;  ///< Weight given to barometer (0-1)
    float altitude_fusion_weight_gps;   ///< Weight given to GPS (0-1)
    uint8_t fusion_degree;              ///< Number of sensors contributing (1-3)
    
    // Error metrics
    uint32_t sensor_error_count;
    uint32_t fusion_skip_count;
    float filter_residual;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Phase transition event
 *
 * Fired when flight phase changes (e.g., LAUNCH → COAST → DESCENT).
 */
struct PhaseTransitionEvent {
    FlightPhase previous_phase;    ///< Previous flight phase
    FlightPhase new_phase;         ///< New flight phase
    
    // Trigger condition
    float trigger_value;           ///< Metric that triggered transition
    const char* trigger_reason;    ///< Human-readable reason (max 64 chars)
    
    // State at transition
    float altitude_at_transition_m;
    float velocity_at_transition_ms;
    float time_in_previous_phase_s;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Sensor health change event
 *
 * Fired when a sensor transitions between health states.
 */
struct SensorHealthEvent {
    enum class SensorType : uint8_t {
        ACCELEROMETER = 0,
        GYROSCOPE = 1,
        MAGNETOMETER = 2,
        BAROMETER = 3,
        GPS = 4
    };
    
    enum class HealthStatus : uint8_t {
        HEALTHY = 0,
        DEGRADED = 1,
        STALE_DATA = 2,
        FAILED = 3
    };
    
    SensorType sensor;
    HealthStatus previous_status;
    HealthStatus new_status;
    
    uint32_t error_count;
    std::chrono::nanoseconds time_since_last_reading;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Apogee detection event
 *
 * Fires when rocket reaches peak altitude (vertical velocity crosses zero).
 */
struct ApogeeDetectionEvent {
    float peak_altitude_m;         ///< Maximum altitude detected
    float altitude_above_launch_m; ///< Height above launch site
    float descent_rate_at_apogee_ms; ///< Descent rate at peak (should be ~0)
    
    // Time and position
    float time_to_apogee_s;        ///< Time from launch to apogee
    float latitude_at_apogee_deg;
    float longitude_at_apogee_deg;
    
    // Parachute deployment decision
    bool deploy_parachute;         ///< Should parachute deploy?
    float deployment_confidence;   ///< Confidence in apogee detection (0-1)
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Estimator state message
 *
 * Internal state of altitude and velocity estimators for diagnostics.
 */
struct EstimatorStateMessage {
    // Altitude estimator
    float altitude_estimate_m;
    float altitude_variance_m2;
    float barometer_innovation;    ///< Residual between estimate and measurement
    
    // Velocity estimator  
    float velocity_estimate_ms;
    float velocity_variance_ms2;
    float accelerometer_innovation;
    
    // Drag estimation
    float drag_coefficient_estimate;
    float mass_estimate_kg;
    
    // Filter health
    float filter_condition_number; ///< Numerical conditioning (higher = worse)
    bool filter_converged;         ///< Has filter converged?
    uint32_t filter_update_count;  ///< Number of filter updates
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief GPS fix quality event
 *
 * Fires when GPS fix quality changes.
 */
struct GPSFixQualityEvent {
    enum class FixQuality : uint8_t {
        NO_FIX = 0,
        GPS_FIX = 1,
        DGPS_FIX = 2,
        RTK_FIX = 3,
        RTK_FLOAT = 4,
        ESTIMATED = 5
    };
    
    FixQuality previous_quality;
    FixQuality new_quality;
    
    uint8_t satellite_count;
    float position_dop;
    float vertical_dop;
    
    float position_accuracy_m;
    float altitude_accuracy_m;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief State vector with metadata
 *
 * FlightStateVector packaged with quality indicators and flags.
 */
struct StateVectorPacket {
    FlightStateVector state;
    
    // Quality indicators
    uint8_t num_sensors_healthy;   ///< How many sensors are healthy
    float state_confidence;        ///< Overall confidence (0-1)
    bool altitude_from_gps;        ///< Altitude source (true=GPS, false=barometer)
    bool position_from_gps;        ///< Position source
    
    // Sequence number for loss detection
    uint32_t sequence_number;
    
    // Metadata
    uint32_t processing_time_us;   ///< Time to compute this state
    uint8_t update_rate_hz;        ///< Reported update rate
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Acceleration data from all three axes
 *
 * Raw accelerometer data for integration into velocity.
 */
struct AccelerationUpdate {
    float x_ms2;
    float y_ms2;
    float z_ms2;
    float magnitude_ms2;
    
    // Quality
    float variance_ms4;
    bool is_valid;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Angular velocity data from all three axes
 *
 * Raw gyroscope data for attitude propagation.
 */
struct AngularVelocityUpdate {
    float roll_dps;
    float pitch_dps;
    float yaw_dps;
    float magnitude_dps;
    
    // Quality
    float variance_dps2;
    bool is_valid;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Position and altitude update
 *
 * Combined position (lat/lon) and altitude data.
 */
struct PositionAltitudeUpdate {
    float latitude_deg;
    float longitude_deg;
    float altitude_m;
    
    // Accuracy
    float horizontal_accuracy_m;
    float vertical_accuracy_m;
    
    // Quality
    uint8_t satellite_count;
    float position_dop;
    bool is_valid;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Velocity update
 *
 * Horizontal and vertical velocity measurements.
 */
struct VelocityUpdate {
    float horizontal_velocity_ms;
    float vertical_velocity_ms;
    float speed_ms;                ///< Total speed magnitude
    
    // Quality
    float variance_ms2;
    bool is_valid;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

/**
 * @brief Attitude update
 *
 * Heading, pitch, roll from magnetometer and accelerometer.
 */
struct AttitudeUpdate {
    float heading_deg;             ///< 0=North, 90=East, 180=South, 270=West
    float pitch_deg;               ///< Positive = nose up
    float roll_deg;                ///< Positive = right wing down
    
    // Quality
    float variance_deg2;
    bool is_valid;
    
    // Timestamp
    std::chrono::nanoseconds timestamp;
};

} // namespace messages
} // namespace avionics
} // namespace graph

