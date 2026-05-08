// MIT License - Copyright (c) 2025 graphlib contributors

/**
 * @file BarometricSensorAdapter.cpp
 * @brief Implementation of barometric altitude adapter
 */

#include "avionics/adapters/BarometricSensorAdapter.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <log4cxx/logger.h>

namespace graph {
namespace avionics {
namespace adapters {

static auto logger = log4cxx::Logger::getLogger("avionics.adapters.BarometricSensorAdapter");

void BarometricSensorAdapter::Initialize(float sea_level_pressure_pa) {
    sea_level_pressure_pa_ = sea_level_pressure_pa;
    last_temperature_c_ = 15.0f;  // Standard day temperature
    last_pressure_pa_ = sea_level_pressure_pa;
    last_altitude_m_ = 0.0f;
    
    is_calibrated_ = false;
    calibration_altitude_m_ = 0.0f;
    calibration_pressure_offset_pa_ = 0.0f;
    
    read_count_ = 0;
    altitude_variance_m2_ = 1.0f;
    
    LOG4CXX_TRACE(logger, "BarometricSensorAdapter initialized with sea level pressure "
                         << sea_level_pressure_pa_ << " Pa");
}

void BarometricSensorAdapter::SetSeaLevelPressure(float pressure_pa) {
    if (pressure_pa < MIN_PRESSURE_PA || pressure_pa > MAX_PRESSURE_PA) {
        LOG4CXX_WARN(logger, "Invalid sea level pressure: " << pressure_pa);
        return;
    }
    
    sea_level_pressure_pa_ = pressure_pa;
    LOG4CXX_TRACE(logger, "Sea level pressure updated to " << pressure_pa << " Pa");
}

void BarometricSensorAdapter::UpdateTemperature(float temp_c) {
    // Validate temperature range (-50°C to +85°C typical sensor range)
    if (temp_c < -50.0f || temp_c > 85.0f) {
        LOG4CXX_WARN(logger, "Temperature out of range: " << temp_c << "°C");
        return;
    }
    
    last_temperature_c_ = temp_c;
}

BarometricAltitudeReading BarometricSensorAdapter::ReadAltitude() {
    BarometricAltitudeReading reading;
    reading.pressure_pa = last_pressure_pa_;
    reading.temperature_c = last_temperature_c_;
    reading.timestamp = std::chrono::nanoseconds(
        std::chrono::system_clock::now().time_since_epoch().count()
    );
    
    // Validate pressure range
    if (last_pressure_pa_ < MIN_PRESSURE_PA || last_pressure_pa_ > MAX_PRESSURE_PA) {
        LOG4CXX_WARN(logger, "Pressure out of range: " << last_pressure_pa_ << " Pa");
        reading.is_valid = false;
        reading.altitude_m = 0.0f;
        reading.altitude_variance_m2_ = 100.0f;  // Low confidence
        return reading;
    }
    
    // Convert pressure to altitude using barometric formula
    float altitude_m = PressureToAltitude(last_pressure_pa_);
    
    // Apply calibration offset if calibrated
    if (is_calibrated_) {
        altitude_m -= calibration_pressure_offset_pa_;
    }
    
    // Validate altitude range
    reading.is_valid = (altitude_m >= MIN_ALTITUDE_M && altitude_m <= MAX_ALTITUDE_M);
    
    // Update cached values
    last_altitude_m_ = altitude_m;
    reading.altitude_m = altitude_m;
    reading.altitude_variance_m2_ = altitude_variance_m2_;
    
    // Update statistics
    read_count_++;
    
    // Exponential moving average of variance
    // Start with 1m², converge to 0.5m² over time
    if (read_count_ > 1) {
        float alpha = 0.1f;  // EMA filter coefficient
        altitude_variance_m2_ = alpha * 0.5f + (1.0f - alpha) * altitude_variance_m2_;
    }
    
    if (read_count_ % 50 == 0) {  // Log every 50 reads (1 second at 50 Hz)
        LOG4CXX_TRACE(logger, "Altitude: " << altitude_m << "m, "
                              "Pressure: " << last_pressure_pa_ << "Pa, "
                              "Variance: " << altitude_variance_m2_ << "m²");
    }
    
    return reading;
}

void BarometricSensorAdapter::CalibrateAtKnownAltitude(float altitude_m) {
    if (altitude_m < MIN_ALTITUDE_M || altitude_m > MAX_ALTITUDE_M) {
        LOG4CXX_WARN(logger, "Calibration altitude out of range: " << altitude_m);
        return;
    }
    
    // Calculate expected pressure at this altitude
    float exponent = EXPONENT / (STANDARD_TEMP_K + TEMP_LAPSE_RATE * altitude_m);
    float expected_pressure = sea_level_pressure_pa_ * 
                             std::pow(1.0f + (TEMP_LAPSE_RATE * altitude_m) / STANDARD_TEMP_K, 
                                     1.0f / exponent);
    
    // Calculate offset to apply
    calibration_pressure_offset_pa_ = expected_pressure - last_pressure_pa_;
    calibration_altitude_m_ = altitude_m;
    is_calibrated_ = true;
    
    LOG4CXX_TRACE(logger, "Calibrated at altitude " << altitude_m << "m, "
                         "offset: " << calibration_pressure_offset_pa_ << "Pa");
}

bool BarometricSensorAdapter::IsCalibrated() const {
    return is_calibrated_;
}

bool BarometricSensorAdapter::IsConnected() const {
    return read_count_ > 0;
}

uint32_t BarometricSensorAdapter::GetReadCount() const {
    return read_count_;
}

float BarometricSensorAdapter::GetLastRawPressure() const {
    return last_pressure_pa_;
}

float BarometricSensorAdapter::GetLastTemperature() const {
    return last_temperature_c_;
}

float BarometricSensorAdapter::GetCalibrationOffset() const {
    return calibration_pressure_offset_pa_;
}

float BarometricSensorAdapter::PressureToAltitude(float pressure_pa) {
    // Barometric formula implementation
    // h = (T0 / L) * ((P0 / P)^(R*L/g*M) - 1)
    
    // Avoid division by zero
    if (pressure_pa <= 0.0f) {
        return 0.0f;
    }
    
    float pressure_ratio = sea_level_pressure_pa_ / pressure_pa;
    
    // Safety check for extremely low pressures
    if (pressure_ratio <= 0.0f) {
        LOG4CXX_WARN(logger, "Invalid pressure ratio: " << pressure_ratio);
        return 0.0f;
    }
    
    float exponent = 1.0f / EXPONENT;
    float power_result = std::pow(pressure_ratio, exponent);
    
    // Calculate altitude
    float altitude = (STANDARD_TEMP_K / TEMP_LAPSE_RATE) * (power_result - 1.0f);
    
    return altitude;
}

} // namespace adapters
} // namespace avionics
} // namespace graph
