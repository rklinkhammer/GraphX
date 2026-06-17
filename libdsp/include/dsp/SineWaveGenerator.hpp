/**
 * @file SineWaveGenerator.hpp
 * @brief GraphX source file.
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

/*!
 * @file dsp/SineWaveGenerator.hpp
 * @brief Native sine wave signal generator implementing DataGeneratorBase
 * @author GraphX Contributors
 * @date 2025
 * @license MIT
 */

#pragma once

#include <cmath>
#include <optional>
#include <chrono>
#include <log4cxx/logger.h>
#include "graph/DataGeneratorBase.hpp"
#include "dsp/IqPacket.hpp"

namespace dsp {

/**
 * @class SineWaveGenerator
 * @brief Generates complex IQ samples from a sine wave signal
 *
 * Produces continuous IQ (In-phase/Quadrature) samples representing a sine wave
 * at a specified frequency, amplitude, and sample rate. Samples are aggregated
 * into fixed-size packets.
 *
 * **Key Features:**
 * - Generates arbitrary frequency sine waves
 * - Configurable amplitude and sample rate
 * - Produces fixed-size IqPacket with N samples per Produce() call
 * - Timestamps packets for timeline tracking
 * - Infinite stream (never exhausted)
 *
 * **Template Parameters:**
 * - SampleT: Scalar type for IQ components (float or double)
 * - N: Samples per packet
 *
 * **Configuration:**
 * - frequency_hz: Sine frequency in Hz
 * - amplitude: Peak amplitude (default 1.0)
 * - sample_rate_hz: Sampling rate in Hz
 * - phase_offset: Initial phase in radians
 *
 * **Mathematical Model:**
 * ```
 * I(t) = amplitude * sin(2π * frequency * t + phase)
 * Q(t) = amplitude * cos(2π * frequency * t + phase)
 * sample = I(t) + j*Q(t)
 * ```
 *
 * **Example Usage:**
 * @code
 * // Create generator for 1 kHz sine at 48 kHz sample rate, 256 samples/packet
 * dsp::SineWaveGenerator<float, 256> gen;
 * gen.SetFrequency(1000.0);         // 1 kHz
 * gen.SetAmplitude(0.5);
 * gen.SetSampleRate(48000.0);
 * gen.SetPhaseOffset(0.0);
 *
 * // Get 256-sample packet
 * auto packet = gen.Produce(0);  // packet.samples has 256 complex values
 * @endcode
 *
 * @see IqPacket for packet structure
 * @see DataGeneratorBase for interface definition
 */
template<typename SampleT, size_t N>
class SineWaveGenerator : public graph::DataGeneratorBase<IqPacket<SampleT, N>> {
private:
    static log4cxx::LoggerPtr logger_;

    // Signal parameters
    double frequency_hz_;           ///< Sine frequency in Hz
    SampleT amplitude_;             ///< Peak amplitude
    double sample_rate_hz_;         ///< Sample rate in Hz
    double phase_offset_;           ///< Initial phase offset in radians

    // State
    uint64_t sample_index_;         ///< Total samples generated
    uint64_t packet_number_;        ///< Packet counter
    std::chrono::nanoseconds last_timestamp_;  ///< Timestamp of last packet

public:
    /// Constructor with default parameters
    SineWaveGenerator()
        : frequency_hz_(1000.0),
          amplitude_(1.0),
          sample_rate_hz_(48000.0),
          phase_offset_(0.0),
          sample_index_(0),
          packet_number_(0),
          last_timestamp_(0) {
        LOG4CXX_TRACE(logger_, "SineWaveGenerator constructed");
    }

    /// Virtual destructor for proper cleanup
    virtual ~SineWaveGenerator() = default;

    /// @brief Generate next packet of IQ samples
    /// @param index Packet index (unused, uses internal sample counter)
    /// @return IqPacket with N complex sine samples
    std::optional<IqPacket<SampleT, N>> Produce(size_t /*index*/) override {
        auto packet = IqPacket<SampleT, N>();
        packet.sample_rate_hz = sample_rate_hz_;
        packet.packet_number = packet_number_++;
        packet.timestamp = std::chrono::system_clock::now();
        last_timestamp_ = packet.GetTimestampNanos();

        // Generate N samples
        for (size_t i = 0; i < N; ++i) {
            double t = static_cast<double>(sample_index_++) / sample_rate_hz_;
            double phase = 2.0 * M_PI * frequency_hz_ * t + phase_offset_;

            // Generate IQ components
            SampleT i_component = amplitude_ * static_cast<SampleT>(std::sin(phase));
            SampleT q_component = amplitude_ * static_cast<SampleT>(std::cos(phase));

            packet.samples[i] = Complex<SampleT>(i_component, q_component);
        }

        LOG4CXX_TRACE(logger_, "SineWaveGenerator produced packet " << packet.packet_number
                              << " with " << N << " samples");

        return packet;
    }

    /// @brief Check if generator is exhausted (always false - infinite stream)
    bool IsExhausted() const override {
        return false;
    }

    /// @brief Get timestamp of last packet
    std::chrono::nanoseconds GetLastTimestamp() const override {
        return last_timestamp_;
    }

    // Configuration accessors

    /// @brief Set sine wave frequency
    void SetFrequency(double hz) {
        frequency_hz_ = hz;
        LOG4CXX_DEBUG(logger_, "Frequency set to " << hz << " Hz");
    }

    /// @brief Get current frequency
    double GetFrequency() const { return frequency_hz_; }

    /// @brief Set signal amplitude
    void SetAmplitude(SampleT amplitude) {
        amplitude_ = amplitude;
        LOG4CXX_DEBUG(logger_, "Amplitude set to " << amplitude);
    }

    /// @brief Get current amplitude
    SampleT GetAmplitude() const { return amplitude_; }

    /// @brief Set sample rate
    void SetSampleRate(double hz) {
        sample_rate_hz_ = hz;
        LOG4CXX_DEBUG(logger_, "Sample rate set to " << hz << " Hz");
    }

    /// @brief Get current sample rate
    double GetSampleRate() const { return sample_rate_hz_; }

    /// @brief Set phase offset in radians
    void SetPhaseOffset(double radians) {
        phase_offset_ = radians;
        LOG4CXX_DEBUG(logger_, "Phase offset set to " << radians << " rad");
    }

    /// @brief Get current phase offset
    double GetPhaseOffset() const { return phase_offset_; }

    /// @brief Reset sample counter to zero
    void Reset() {
        sample_index_ = 0;
        packet_number_ = 0;
        LOG4CXX_DEBUG(logger_, "SineWaveGenerator reset");
    }

    /// @brief Get total samples generated
    uint64_t GetTotalSamplesGenerated() const { return sample_index_; }

    /// @brief Get total packets generated
    uint64_t GetTotalPacketsGenerated() const { return packet_number_; }

    /// @brief Convert to string for debugging
    std::string ToString() const {
        return "SineWaveGenerator<" + std::string(typeid(SampleT).name()) + ", " +
               std::to_string(N) + ">(freq=" + std::to_string(frequency_hz_) +
               "Hz, amp=" + std::to_string(amplitude_) + ", rate=" +
               std::to_string(sample_rate_hz_) + "Hz)";
    }
};

// Static logger initialization
template<typename SampleT, size_t N>
log4cxx::LoggerPtr SineWaveGenerator<SampleT, N>::logger_ =
    log4cxx::Logger::getLogger("dsp.SineWaveGenerator");

} // namespace dsp
