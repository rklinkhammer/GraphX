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

#include "avionics/estimators/MagnetometerIntegrator.hpp"
#include "avionics/estimators/AttitudeQuaternion.hpp"
#include <cmath>
#include <algorithm>

namespace avionics::estimators {

MagnetometerIntegrator::MagnetometerIntegrator(float declination_rad,
                                               const MagneticCalibration& calibration)
    : declination_rad_(declination_rad),
      calibration_(calibration),
      last_measurement_{0, 0, 0},
      last_calibrated_field_{0, 0, 0},
      last_heading_rad_(0.0f),
      last_confidence_(0.0f),
      measurement_count_(0) {
}

bool MagnetometerIntegrator::EstimateHeading(const Vector3D& magnetic_field,
                                              const sensors::Quaternion& attitude_quaternion,
                                              float& heading_rad,
                                              float& confidence) {
    // Store raw measurement
    last_measurement_ = magnetic_field;
    measurement_count_++;
    
    // Check if measurement is valid (non-zero)
    float magnitude = std::sqrt(magnetic_field.x * magnetic_field.x +
                                magnetic_field.y * magnetic_field.y +
                                magnetic_field.z * magnetic_field.z);
    
    if (magnitude < 1e-6f) {
        confidence = 0.0f;
        return false;
    }
    
    // Apply calibration: hard iron bias correction
    Vector3D corrected = ApplyHardIronCorrection(magnetic_field);
    
    // Apply soft iron scaling
    corrected = ApplySoftIronCorrection(corrected);
    last_calibrated_field_ = corrected;
    
    // Project magnetic field to horizontal plane (compensate for tilt)
    Vector3D horizontal = ProjectToHorizontal(corrected, attitude_quaternion);
    
    // Extract heading from horizontal component
    // Heading is angle from magnetic north in horizontal plane
    // atan2(East, North) where E=Y, N=X in body frame
    float heading_magnetic = std::atan2(horizontal.y, horizontal.x);
    
    // Apply magnetic declination correction
    // heading_true = heading_magnetic + declination
    heading_rad = heading_magnetic + declination_rad_;
    
    // Normalize to [-π, π]
    while (heading_rad > M_PI) heading_rad -= 2.0f * M_PI;
    while (heading_rad < -M_PI) heading_rad += 2.0f * M_PI;
    
    // Estimate confidence
    confidence = EstimateConfidence(corrected);
    
    // Store results
    last_heading_rad_ = heading_rad;
    last_confidence_ = confidence;
    
    return true;
}

void MagnetometerIntegrator::Reset() {
    last_measurement_ = Vector3D{0, 0, 0};
    last_calibrated_field_ = Vector3D{0, 0, 0};
    last_heading_rad_ = 0.0f;
    last_confidence_ = 0.0f;
    measurement_count_ = 0;
}

Vector3D MagnetometerIntegrator::ApplyHardIronCorrection(const Vector3D& raw_field) const {
    // Subtract hard iron bias (offset)
    return Vector3D{
        raw_field.x - calibration_.hard_iron_bias.x,
        raw_field.y - calibration_.hard_iron_bias.y,
        raw_field.z - calibration_.hard_iron_bias.z
    };
}

Vector3D MagnetometerIntegrator::ApplySoftIronCorrection(const Vector3D& biased_field) const {
    // Scale by soft iron correction factors
    // These account for frequency-dependent effects and installation variations
    return Vector3D{
        biased_field.x * calibration_.soft_iron_x_scale,
        biased_field.y * calibration_.soft_iron_y_scale,
        biased_field.z * calibration_.soft_iron_z_scale
    };
}

Vector3D MagnetometerIntegrator::ProjectToHorizontal(
    const Vector3D& calibrated_field,
    const sensors::Quaternion& attitude) const {
    
    // Rotate magnetic field from body frame to navigation frame
    // This compensates for aircraft tilt (pitch and roll)
    // We ignore yaw since it's what we're trying to estimate
    
    Vector3D nav_field = AttitudeQuaternion::Rotate(attitude, calibrated_field);
    
    // Project to horizontal plane by zeroing vertical component
    // Keep only X and Y (North and East)
    return Vector3D{nav_field.x, nav_field.y, 0.0f};
}

float MagnetometerIntegrator::EstimateConfidence(const Vector3D& calibrated_field) const {
    // Calculate magnitude of calibrated field
    float magnitude = std::sqrt(calibrated_field.x * calibrated_field.x +
                                calibrated_field.y * calibrated_field.y +
                                calibrated_field.z * calibrated_field.z);
    
    if (magnitude < 1e-6f) {
        return 0.0f;
    }
    
    // Confidence based on field strength deviation from expected
    // Earth's magnetic field typical range: 25-65 μT depending on latitude
    // Stronger confidence when field is closer to expected magnitude
    
    float expected = calibration_.expected_magnitude;
    float deviation = std::abs(magnitude - expected) / expected;
    
    // Confidence: high when close to expected, decreases for large deviations
    // At 25% deviation: confidence ~0.8
    // At 50% deviation: confidence ~0.5
    // At 100% deviation: confidence ~0
    float confidence = std::max(0.0f, 1.0f - deviation);
    
    // Apply additional penalty for extreme vertical component
    // If |Z| > 0.9 * magnitude, we're near magnetic poles (gimbal lock)
    // This gives low confidence for heading estimation
    if (magnitude > 1e-6f) {
        float vertical_ratio = std::abs(calibrated_field.z) / magnitude;
        if (vertical_ratio > 0.8f) {
            // Near magnetic poles, heading becomes unreliable
            confidence *= (1.0f - vertical_ratio);
        }
    }
    
    return std::clamp(confidence, 0.0f, 1.0f);
}

} // namespace avionics::estimators
