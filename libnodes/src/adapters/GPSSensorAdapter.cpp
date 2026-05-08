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
 * @file GPSSensorAdapter.cpp
 * @brief Implementation of GPS adapter for NMEA 0183 protocol
 */

#include "avionics/adapters/GPSSensorAdapter.hpp"

#include <sstream>
#include <algorithm>
#include <cmath>

namespace graph {
namespace avionics {
namespace adapters {

void GPSSensorAdapter::Initialize(float position_dop_threshold) {
    position_dop_threshold_ = position_dop_threshold;
    
    // Initialize position
    latitude_deg_ = 0.0f;
    longitude_deg_ = 0.0f;
    
    // Initialize altitude
    altitude_m_ = 0.0f;
    altitude_variance_m2_ = 1.0f;
    
    // Initialize velocity
    vertical_velocity_ms_ = 0.0f;
    horizontal_velocity_ms_ = 0.0f;
    ground_speed_kmh_ = 0.0f;
    true_course_deg_ = 0.0f;
    
    // Initialize quality metrics
    position_dop_ = 100.0f;  // Invalid initially
    vertical_dop_ = 100.0f;
    satellite_count_ = 0;
    satellites_in_view_ = 0;
    fix_quality_ = GPSFixQuality::INVALID;
    
    // Initialize timestamps
    last_fix_timestamp_ = std::chrono::nanoseconds(0);
    
    // Initialize statistics
    sentence_count_ = 0;
    checksum_error_count_ = 0;
    parse_error_count_ = 0;
}

bool GPSSensorAdapter::ProcessGPSData(const std::string& nmea_sentence) {
    // Validate checksum first
    if (!ValidateChecksum(nmea_sentence)) {
        checksum_error_count_++;
        return false;
    }
    
    // Extract sentence identifier
    size_t dollar_pos = nmea_sentence.find('$');
    size_t comma_pos = nmea_sentence.find(',');
    
    if (dollar_pos == std::string::npos || comma_pos == std::string::npos) {
        parse_error_count_++;
        return false;
    }
    
    std::string sentence_id = nmea_sentence.substr(dollar_pos + 1, 
                                                   comma_pos - dollar_pos - 1);
    
    bool success = false;
    
    if (sentence_id == "GPGGA") {
        success = ParseGPGGA(nmea_sentence);
    } else if (sentence_id == "GPVTG") {
        success = ParseGPVTG(nmea_sentence);
    } else if (sentence_id == "GPGSV") {
        success = ParseGPGSV(nmea_sentence);
    }
    
    if (success) {
        sentence_count_++;
    } else {
        parse_error_count_++;
    }
    
    return success;
}

int GPSSensorAdapter::ProcessRawData(const uint8_t* buffer, size_t length) {
    int sentences_processed = 0;
    static std::string current_sentence;
    
    for (size_t i = 0; i < length; ++i) {
        char c = static_cast<char>(buffer[i]);
        
        if (c == '$') {
            // Start of new sentence
            current_sentence.clear();
            current_sentence += c;
        } else if (c == '\r' || c == '\n') {
            // End of sentence
            if (!current_sentence.empty() && current_sentence.front() == '$') {
                if (ProcessGPSData(current_sentence)) {
                    sentences_processed++;
                }
            }
            current_sentence.clear();
        } else {
            // Accumulate sentence data
            current_sentence += c;
            
            // Prevent buffer overflow (max NMEA length ~82 chars)
            if (current_sentence.length() > 100) {
                current_sentence.clear();
            }
        }
    }
    
    return sentences_processed;
}

GPSAltitudeReading GPSSensorAdapter::GetAltitudeReading() const {
    GPSAltitudeReading reading;
    reading.altitude_m = altitude_m_;
    reading.altitude_variance_m2 = altitude_variance_m2_;
    reading.position_dop = position_dop_;
    reading.vertical_dop = vertical_dop_;
    reading.satellite_count = satellite_count_;
    reading.fix_quality = fix_quality_;
    reading.timestamp = last_fix_timestamp_;
    reading.is_valid = (fix_quality_ != GPSFixQuality::INVALID) && 
                       (position_dop_ <= position_dop_threshold_);
    return reading;
}

GPSVelocityReading GPSSensorAdapter::GetVelocityReading() const {
    GPSVelocityReading reading;
    reading.vertical_velocity_ms = vertical_velocity_ms_;
    reading.horizontal_velocity_ms = horizontal_velocity_ms_;
    reading.ground_speed_kmh = ground_speed_kmh_;
    reading.true_course_deg = true_course_deg_;
    reading.timestamp = last_fix_timestamp_;
    reading.is_valid = (fix_quality_ != GPSFixQuality::INVALID);
    return reading;
}

bool GPSSensorAdapter::GetPosition(float& latitude_deg, 
                                   float& longitude_deg) const {
    if (fix_quality_ == GPSFixQuality::INVALID) {
        return false;
    }
    latitude_deg = latitude_deg_;
    longitude_deg = longitude_deg_;
    return true;
}

float GPSSensorAdapter::GetPositionDOP() const {
    return position_dop_;
}

float GPSSensorAdapter::GetVerticalDOP() const {
    return vertical_dop_;
}

uint8_t GPSSensorAdapter::GetSatelliteCount() const {
    return satellite_count_;
}

uint8_t GPSSensorAdapter::GetSatellitesInView() const {
    return satellites_in_view_;
}

GPSFixQuality GPSSensorAdapter::GetFixQuality() const {
    return fix_quality_;
}

bool GPSSensorAdapter::HasValidFix() const {
    return fix_quality_ != GPSFixQuality::INVALID && 
           position_dop_ <= position_dop_threshold_;
}

bool GPSSensorAdapter::IsConnected() const {
    // Connected if we've received data recently
    return sentence_count_ > 0;
}

uint32_t GPSSensorAdapter::GetSentenceCount() const {
    return sentence_count_;
}

uint32_t GPSSensorAdapter::GetChecksumErrorCount() const {
    return checksum_error_count_;
}

std::chrono::nanoseconds GPSSensorAdapter::GetTimeSinceLastFix() const {
    // Return 0 if no fix yet
    return last_fix_timestamp_;
}

bool GPSSensorAdapter::ParseGPGGA(const std::string& sentence) {
    // $GPGGA,hhmmss.ss,ddmm.mmmm,a,dddmm.mmmm,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
    
    std::string utc_time, lat, lat_dir, lon, lon_dir, quality_str;
    std::string sats_str, hdop_str, alt_str, geoid_height_str;
    
    if (!ExtractField(sentence, 1, utc_time) ||
        !ExtractField(sentence, 2, lat) ||
        !ExtractField(sentence, 3, lat_dir) ||
        !ExtractField(sentence, 4, lon) ||
        !ExtractField(sentence, 5, lon_dir) ||
        !ExtractField(sentence, 6, quality_str) ||
        !ExtractField(sentence, 7, sats_str) ||
        !ExtractField(sentence, 8, hdop_str) ||
        !ExtractField(sentence, 9, alt_str)) {
        return false;
    }
    
    try {
        // Parse quality
        uint8_t quality = static_cast<uint8_t>(std::stoi(quality_str));
        if (quality > 5) {
            return false;
        }
        fix_quality_ = static_cast<GPSFixQuality>(quality);
        
        // Parse satellites
        satellite_count_ = static_cast<uint8_t>(std::stoi(sats_str));
        if (satellite_count_ == 0 && fix_quality_ != GPSFixQuality::INVALID) {
            return false;  // No satellites but has fix - invalid
        }
        
        // Parse DOP
        position_dop_ = std::stof(hdop_str);
        if (position_dop_ < 0.0f || position_dop_ > 1000.0f) {
            return false;
        }
        
        // Parse altitude
        if (!alt_str.empty()) {
            altitude_m_ = std::stof(alt_str);
            // Validate altitude range (-500m to 10,000m typical)
            if (altitude_m_ < -500.0f || altitude_m_ > 50000.0f) {
                return false;
            }
            // Variance based on DOP
            altitude_variance_m2_ = position_dop_ * position_dop_;
        }
        
        // Parse latitude
        if (!lat.empty() && !lat_dir.empty()) {
            latitude_deg_ = DMSToDecimalDegrees(lat);
            if (lat_dir == "S") {
                latitude_deg_ = -latitude_deg_;
            }
        }
        
        // Parse longitude
        if (!lon.empty() && !lon_dir.empty()) {
            longitude_deg_ = DMSToDecimalDegrees(lon);
            if (lon_dir == "W") {
                longitude_deg_ = -longitude_deg_;
            }
        }
        
        // Update timestamp
        last_fix_timestamp_ = std::chrono::nanoseconds(
            static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count()));
        
        return true;
    } catch (...) {
        return false;
    }
}

bool GPSSensorAdapter::ParseGPVTG(const std::string& sentence) {
    // $GPVTG,x.x,T,x.x,M,x.x,N,x.x,K*hh
    
    std::string track_true, track_mag, speed_knots, speed_kmh;
    
    if (!ExtractField(sentence, 1, track_true) ||
        !ExtractField(sentence, 3, track_mag) ||
        !ExtractField(sentence, 5, speed_knots) ||
        !ExtractField(sentence, 7, speed_kmh)) {
        return false;
    }
    
    try {
        if (!track_true.empty()) {
            true_course_deg_ = std::stof(track_true);
        }
        
        if (!speed_knots.empty()) {
            float knots = std::stof(speed_knots);
            // 1 knot = 0.51444 m/s
            horizontal_velocity_ms_ = knots * 0.51444f;
        }
        
        if (!speed_kmh.empty()) {
            ground_speed_kmh_ = std::stof(speed_kmh);
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool GPSSensorAdapter::ParseGPGSV(const std::string& sentence) {
    // $GPGSV,x,x,xx,...*hh
    // Total messages, message number, satellites in view
    
    std::string sats_in_view_str;
    
    if (!ExtractField(sentence, 3, sats_in_view_str)) {
        return false;
    }
    
    try {
        satellites_in_view_ = static_cast<uint8_t>(std::stoi(sats_in_view_str));
        return true;
    } catch (...) {
        return false;
    }
}

bool GPSSensorAdapter::ValidateChecksum(const std::string& sentence) {
    size_t dollar_pos = sentence.find('$');
    size_t asterisk_pos = sentence.find('*');
    
    if (dollar_pos == std::string::npos || asterisk_pos == std::string::npos) {
        return false;
    }
    
    if (asterisk_pos + 3 > sentence.length()) {
        return false;
    }
    
    // Calculate XOR of all bytes between $ and *
    uint8_t calculated_checksum = 0;
    for (size_t i = dollar_pos + 1; i < asterisk_pos; ++i) {
        calculated_checksum ^= static_cast<uint8_t>(sentence[i]);
    }
    
    // Extract checksum from sentence
    std::string checksum_str = sentence.substr(asterisk_pos + 1, 2);
    
    try {
        uint8_t sentence_checksum = static_cast<uint8_t>(
            std::stoi(checksum_str, nullptr, 16));
        return calculated_checksum == sentence_checksum;
    } catch (...) {
        return false;
    }
}

bool GPSSensorAdapter::ExtractField(const std::string& sentence, 
                                    int field_index, std::string& field) {
    int current_field = 0;
    size_t start_pos = 0;
    
    // Find start of fields (after $GPXXX,)
    size_t first_comma = sentence.find(',');
    if (first_comma == std::string::npos) {
        return false;
    }
    
    start_pos = first_comma + 1;
    
    for (size_t i = start_pos; i < sentence.length(); ++i) {
        if (sentence[i] == ',' || sentence[i] == '*') {
            if (current_field == field_index) {
                field = sentence.substr(start_pos, i - start_pos);
                return true;
            }
            
            current_field++;
            start_pos = i + 1;
            
            if (sentence[i] == '*') {
                return false;  // Reached checksum before finding field
            }
        }
    }
    
    return false;
}

float GPSSensorAdapter::DMSToDecimalDegrees(const std::string& dms_string) {
    if (dms_string.empty()) {
        return 0.0f;
    }
    
    try {
        // Find decimal point
        size_t decimal_pos = dms_string.find('.');
        if (decimal_pos == std::string::npos) {
            return 0.0f;
        }
        
        // Degrees are all digits before the last 5 digits (MM.MMMM)
        int degrees_len = static_cast<int>(decimal_pos) - 2;
        if (degrees_len < 1) {
            return 0.0f;
        }
        
        float degrees = std::stof(dms_string.substr(0, degrees_len));
        float minutes = std::stof(dms_string.substr(degrees_len));
        
        float decimal = degrees + (minutes / 60.0f);
        
        return decimal;
    } catch (...) {
        return 0.0f;
    }
}

} // namespace adapters
} // namespace avionics
} // namespace graph
