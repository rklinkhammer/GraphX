/**
 * @file SensorClassificationType.hpp
 * @brief GraphX source file.
 */

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

#include <string>

namespace sensors {

/**
 * @enum SensorClassificationType
 * @brief Formal sensor classification nomenclature
 * 
 * Defines the official taxonomy of sensor data types. Used by all sensor
 * producer nodes (CSV-based simulation and real hardware devices) to
 * classify the data they generate for unified routing and discovery.
 * 
 * This enum provides:
 * - Type safety: compiler prevents invalid classifications
 * - Centralized taxonomy: single source of truth for all sensor types
 * - Refactoring resilience: changes propagate automatically
 * - Pattern matching: efficient switch-based dispatch
 * 
 * @note All sensor nodes (hardware or simulated) use this classification
 *       to enable interchangeable data sources in processing pipelines.
 */
enum class SensorClassificationType {
    ACCELEROMETER,    ///< Acceleration data (X, Y, Z axes in m/s²)
    GYROSCOPE,        ///< Rotational velocity data (roll, pitch, yaw rates in rad/s)
    MAGNETOMETER,     ///< Magnetic field strength (X, Y, Z axes in Tesla)
    BAROMETRIC,       ///< Atmospheric pressure and altitude data
    GPS_POSITION,     ///< GPS coordinates (latitude, longitude, altitude)
    DSP_SENSOR_SINE,  ///< DSP sine wave signal generator (IQ samples)
    UNKNOWN           ///< Unknown or unclassified sensor type
};

/**
 * @brief Convert sensors::SensorClassificationType enum to string representation
 * 
 * Converts the enum value to its lowercase string identifier for use in
 * configuration, logging, and data routing.
 * 
 * @param type Sensor classification type enum value
 * @return Lowercase string identifier:
 *   - ACCELEROMETER → "accel"
 *   - GYROSCOPE → "gyro"
 *   - MAGNETOMETER → "mag"
 *   - BAROMETRIC → "baro"
 *   - GPS_POSITION → "gps"
 *   - UNKNOWN → "unknown"
 * 
 * @note This is the canonical representation used in:
 *   - CSVDataStreamManager discovery and routing
 *   - Configuration files and JSON schemas
 *   - Log messages and debugging output
 */
std::string SensorClassificationTypeToString(SensorClassificationType type);

/**
 * @brief Convert string identifier to sensors::SensorClassificationType enum
 * 
 * Converts a string identifier (typically from configuration files or
 * CSV headers) to the corresponding enum value.
 * 
 * @param str String identifier (case-insensitive):
 *   - "accel" → ACCELEROMETER
 *   - "gyro" → GYROSCOPE
 *   - "mag" → MAGNETOMETER
 *   - "baro" → BAROMETRIC
 *   - "gps" → GPS_POSITION
 *   - anything else → UNKNOWN
 * 
 * @return Corresponding enum value, or UNKNOWN if not recognized
 * 
 * @note Input strings are case-insensitive for robustness
 */
SensorClassificationType StringToSensorClassificationType(const std::string& str);

}  // namespace sensors