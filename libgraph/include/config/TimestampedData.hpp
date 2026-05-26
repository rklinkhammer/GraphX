// MIT License
//
// Copyright (c) 2025 GraphX Contributors
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

/*!
 * @file TimestampedData.hpp
 * @brief Base struct for all timestamped sensor data
 * @author GraphX Contributors
 * @date 2025
 * @license MIT
 */

#pragma once

#include <chrono>

namespace sensors {

/**
 * @struct TimestampedData
 * @brief Base struct for all timestamped sensor data
 * 
 * Provides a common timestamp field for all sensor data types,
 * enabling consistent time-based sorting and synchronization across
 * different sensor modalities.
 * 
 * **Timing Semantics:**
 * - Timestamp is stored as std::chrono::nanoseconds for maximum precision
 * - All conversions from seconds use nanosecond granularity
 * - No precision loss when converting double seconds to nanoseconds
 * - Helper methods provided for seconds ↔ nanoseconds conversion
 * 
 * @note All sensor data types should inherit from TimestampedData
 * @see SensorDataTypes.hpp for concrete implementations
 */
struct TimestampedData {
    /// Timestamp in nanoseconds from launch (uniform across all sensors)
    std::chrono::nanoseconds timestamp{0};
    
    /// Retrieve timestamp in nanosecond precision
    /// @return Current timestamp as nanoseconds
    std::chrono::nanoseconds GetTimestamp() const {
        return timestamp;
    }
    
    /// Set timestamp in nanosecond precision
    /// @param ts Timestamp in nanoseconds
    void SetTimestamp(const std::chrono::nanoseconds& ts) {
        timestamp = ts;
    }

    /// Get timestamp as seconds (double precision)
    /// Useful for logging and analysis
    /// @return Timestamp converted to seconds as double
    double GetTimestampSeconds() const {
        using Seconds = std::chrono::duration<double>;
        return std::chrono::duration_cast<Seconds>(timestamp).count();
    }
    
    /// Set timestamp from seconds (double precision)
    /// Automatically converts to nanoseconds with full precision
    /// @param seconds Timestamp in seconds as double
    void SetTimestampSeconds(double seconds) {
        timestamp = std::chrono::nanoseconds{
            static_cast<uint64_t>(seconds * 1'000'000'000)};
    }
};

} // namespace sensors

