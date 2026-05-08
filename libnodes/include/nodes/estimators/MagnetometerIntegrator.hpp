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
 * @file MagnetometerIntegrator.hpp
 * @brief Magnetometer-based heading estimation with declination correction
 *
 * Provides heading estimation from magnetometer measurements with:
 * - Magnetic declination correction (location-based)
 * - Hard iron bias calibration
 * - Soft iron scaling correction
 * - Confidence estimation based on measurement quality
 *
 * Phase 3 Task 3.4 Component: Magnetometer heading estimator
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "sensor/SensorBasicTypes.hpp"
#include <cmath>
#include <cstdint>

namespace avionics::estimators {

using Vector3D = sensors::Vector3D;

/**
 * @struct MagneticCalibration
 * @brief Calibration parameters for magnetometer
 *
 * Stores hard iron bias (DC offset) and soft iron scaling
 * to account for local magnetic anomalies and sensor characteristics.
 */
struct MagneticCalibration {
    /// Hard iron bias (offset) in microteslas
    Vector3D hard_iron_bias{0.0f, 0.0f, 0.0f};
    
    /// Soft iron scaling matrix (diagonal for simplified model)
    /// Represents frequency-dependent gain variations
    float soft_iron_x_scale{1.0f};
    float soft_iron_y_scale{1.0f};
    float soft_iron_z_scale{1.0f};
    
    /// Expected magnetic field strength at location (μT)
    /// Typical range 25-65 μT depending on latitude
    float expected_magnitude{48.0f};  // Default for 40°N latitude
};

/**
 * @class MagnetometerIntegrator
 * @brief Estimates heading from magnetometer measurements
 *
 * Converts raw magnetometer data (X, Y, Z components) to heading estimate
 * with magnetic declination correction and calibration.
 *
 * Phase 3 Task 3.4 Component: Heading estimator
 * 
 * Integration points:
 * - Input: Raw magnetometer Vector3D (μT)
 * - Output: Heading estimate (radians), confidence (0.0-1.0)
 * - Updates StateVector.magnetic_field and confidence.heading
 */
class MagnetometerIntegrator {
public:
    /**
     * @brief Construct magnetometer integrator
     * 
     * @param declination_rad Magnetic declination in radians (typically ±25°)
     *        Positive east of true north, negative west
     * @param calibration Optional calibration parameters (default: identity)
     */
    explicit MagnetometerIntegrator(float declination_rad = 0.0f,
                                     const MagneticCalibration& calibration = MagneticCalibration{});

    /**
     * @brief Update heading estimate from magnetometer measurement
     *
     * Takes raw magnetometer reading and produces heading estimate.
     * Applies calibration, declination correction, and confidence assessment.
     *
     * @param magnetic_field Raw magnetometer measurement (X, Y, Z in μT)
     * @param attitude_quaternion Current attitude quaternion for tilt compensation
     * @param[out] heading_rad Estimated heading in radians (-π to π)
     * @param[out] confidence Confidence in heading estimate (0.0 to 1.0)
     * @return true if successful, false if measurement invalid
     */
    bool EstimateHeading(const Vector3D& magnetic_field,
                         const sensors::Quaternion& attitude_quaternion,
                         float& heading_rad,
                         float& confidence);

    /**
     * @brief Get the last estimated heading
     * @return Last heading estimate in radians
     */
    float GetLastHeading() const { return last_heading_rad_; }

    /**
     * @brief Get the last confidence estimate
     * @return Last confidence value (0.0 to 1.0)
     */
    float GetLastConfidence() const { return last_confidence_; }

    /**
     * @brief Get the raw magnetic field from last measurement
     * @return Raw measured magnetic field
     */
    Vector3D GetLastMeasurement() const { return last_measurement_; }

    /**
     * @brief Get the calibrated magnetic field from last measurement
     * @return Calibrated magnetic field
     */
    Vector3D GetLastCalibratedField() const { return last_calibrated_field_; }

    /**
     * @brief Set magnetic calibration parameters
     * @param calibration New calibration parameters
     */
    void SetCalibration(const MagneticCalibration& calibration) {
        calibration_ = calibration;
    }

    /**
     * @brief Set magnetic declination
     * @param declination_rad Magnetic declination in radians
     */
    void SetDeclination(float declination_rad) {
        declination_rad_ = declination_rad;
    }

    /**
     * @brief Get magnetic declination
     * @return Current declination in radians
     */
    float GetDeclination() const { return declination_rad_; }

    /**
     * @brief Reset estimator to initial state
     */
    void Reset();

    /**
     * @brief Get number of measurements processed
     * @return Count of Update calls
     */
    uint32_t GetMeasurementCount() const { return measurement_count_; }

private:
    /// Apply hard iron bias correction
    Vector3D ApplyHardIronCorrection(const Vector3D& raw_field) const;
    
    /// Apply soft iron scaling correction
    Vector3D ApplySoftIronCorrection(const Vector3D& biased_field) const;
    
    /// Project magnetic field to horizontal plane (tilt compensation)
    Vector3D ProjectToHorizontal(const Vector3D& calibrated_field,
                                 const sensors::Quaternion& attitude) const;
    
    /// Estimate confidence based on measurement quality
    float EstimateConfidence(const Vector3D& calibrated_field) const;

    // Configuration
    float declination_rad_;
    MagneticCalibration calibration_;

    // State tracking
    Vector3D last_measurement_;
    Vector3D last_calibrated_field_;
    float last_heading_rad_;
    float last_confidence_;
    uint32_t measurement_count_;
};

} // namespace avionics::estimators

