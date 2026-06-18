/**
 * @file IqPacket.hpp
 * @brief IQ Packet DSP support.
 *
 * @details Provides public DSP API for deterministic signal-processing graph nodes and packets. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

#pragma once

#include <array>
#include <complex>
#include <chrono>
#include <cstdint>

namespace dsp {

/**
 * @brief Complex number type for IQ samples
 * @tparam T Underlying scalar type (float, double, etc.)
 */
template<typename T>
using Complex = std::complex<T>;

/**
 * @class IqPacket
 * @brief Packet containing multiple IQ (In-phase/Quadrature) samples with metadata
 *
 * IQ samples represent complex signals as pairs of real (in-phase) and imaginary
 * (quadrature) components. A packet aggregates multiple samples with shared metadata.
 *
 * **Template Parameters:**
 * - SampleT: Scalar type for I and Q components (float, double)
 * - N: Number of samples per packet
 *
 * **Key Features:**
 * - Fixed-size array of N complex samples
 * - Metadata: timestamp, sample rate, frequency offset
 * - Copy/move semantics for efficient data passing
 * - Validation methods for data integrity
 *
 * **Usage Example:**
 * @code
 * // Create packet with 256 float-based IQ samples
 * auto packet = dsp::IqPacket<float, 256>();
 * packet.samples[0] = {1.0f, 0.5f};  // I=1.0, Q=0.5
 * packet.timestamp = std::chrono::system_clock::now();
 * packet.sample_rate_hz = 48000.0;
 * @endcode
 *
 * @tparam SampleT Scalar type for I/Q components
 * @tparam N Number of samples per packet
 *
 * @see SineWaveGenerator for example producer
 */
/**
 * @class IqPacket
 * @brief IQ Packet message type.
 *
 * @details Carries typed data between graph nodes. Fields describe payload shape, metadata, and sequencing information used by downstream processing.
 */
template<typename SampleT, size_t N>
class IqPacket {
public:
    /// Array of complex IQ samples
    std::array<Complex<SampleT>, N> samples;

    /// Timestamp when first sample was acquired/generated
    std::chrono::system_clock::time_point timestamp;

    /// Sample rate in Hz
    double sample_rate_hz = 1.0;

    /// Carrier frequency offset in Hz (optional, for superheterodyne)
    double carrier_frequency_hz = 0.0;

    /// Sequence number for tracking packet order
    uint64_t packet_number = 0;

    /// Constructor: initializes all samples to zero, timestamp to now
    IqPacket()
        : samples{},
          timestamp(std::chrono::system_clock::now()),
          sample_rate_hz(1.0),
          carrier_frequency_hz(0.0),
          packet_number(0) {}

    /// @brief Get number of samples in this packet
    constexpr size_t GetNumSamples() const { return N; }

    /// @brief Check if all samples are valid (not NaN or Inf)
    bool IsValid() const {
        for (const auto& sample : samples) {
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
                return false;
            }
        }
        return true;
    }

    /// @brief Zero out all samples
    void Clear() {
        for (auto& sample : samples) {
            sample = {0, 0};
        }
    }

    /// @brief Get magnitude of sample at index
    SampleT GetMagnitude(size_t index) const {
        if (index >= N) return 0;
        return std::abs(samples[index]);
    }

    /// @brief Get phase of sample at index (radians)
    SampleT GetPhase(size_t index) const {
        if (index >= N) return 0;
        return std::arg(samples[index]);
    }

    /// @brief Get timestamp as nanoseconds since epoch
    std::chrono::nanoseconds GetTimestampNanos() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            timestamp.time_since_epoch());
    }

    /// @brief Convert to string for debugging
    std::string ToString() const {
        std::string result = "IqPacket<" + std::string(typeid(SampleT).name()) +
                           ", " + std::to_string(N) + ">(";
        result += "samples=" + std::to_string(N);
        result += ", rate=" + std::to_string(sample_rate_hz) + "Hz";
        result += ", carrier=" + std::to_string(carrier_frequency_hz) + "Hz";
        result += ")";
        return result;
    }
};

} // namespace dsp
