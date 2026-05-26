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

#include "sensor/SensorClassificationType.hpp"

#include <algorithm>
#include <cctype>

namespace sensors {

std::string SensorClassificationTypeToString(sensors::SensorClassificationType type) {
    switch (type) {
        case sensors::SensorClassificationType::ACCELEROMETER:
            return "accel";
        case sensors::SensorClassificationType::GYROSCOPE:
            return "gyro";
        case sensors::SensorClassificationType::MAGNETOMETER:
            return "mag";
        case sensors::SensorClassificationType::BAROMETRIC:
            return "baro";
        case sensors::SensorClassificationType::GPS_POSITION:
            return "gps";
        case sensors::SensorClassificationType::DSP_SENSOR_SINE:
            return "dsp_sine";
        case sensors::SensorClassificationType::UNKNOWN:
        default:
            return "unknown";
    }
}

sensors::SensorClassificationType StringToSensorClassificationType(const std::string& str) {
    // Create lowercase version for case-insensitive comparison
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_str == "accel" || lower_str == "accelerometer") {
        return SensorClassificationType::ACCELEROMETER;
    } else if (lower_str == "gyro" || lower_str == "gyroscope") {
        return SensorClassificationType::GYROSCOPE;
    } else if (lower_str == "mag" || lower_str == "magnetometer") {
        return SensorClassificationType::MAGNETOMETER;
    } else if (lower_str == "baro" || lower_str == "barometric") {
        return SensorClassificationType::BAROMETRIC;
    } else if (lower_str == "gps" || lower_str == "gps_position") {
        return SensorClassificationType::GPS_POSITION;
    } else if (lower_str == "dsp_sine" || lower_str == "sine") {
        return SensorClassificationType::DSP_SENSOR_SINE;
    }

    return SensorClassificationType::UNKNOWN;
}

}  // namespace graph
