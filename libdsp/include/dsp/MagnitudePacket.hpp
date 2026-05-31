#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace dsp {

/**
 * @brief Output packet containing power spectrum from FFT analysis
 *
 * Represents the frequency-domain result of FFT processing on accumulated
 * IqPacket data. Contains magnitude spectrum (Nyquist bins), peak detection
 * results, and metadata for trace and analysis.
 *
 * @tparam SampleT Sample data type (typically float or double)
 * @tparam N FFT size (input packet size; output is N/2 due to Nyquist theorem)
 */
template<typename SampleT, size_t N>
struct MagnitudePacket {
    // Spectrum data
    std::array<SampleT, N / 2> magnitudes;     ///< Linear magnitude spectrum (Nyquist bins)

    // Metadata
    std::chrono::system_clock::time_point timestamp;
    uint64_t packet_number{0};                 ///< Sequence number from original IqPacket
    uint64_t num_accumulated_packets{1};       ///< How many IqPackets accumulated for this output
    double sample_rate_hz{48000.0};            ///< Sample rate in Hz (for frequency axis)
    bool valid{true};                          ///< Data validity flag (false if processing error)

    // Peak detection results
    size_t peak_bin{0};                        ///< Index of bin with maximum magnitude
    SampleT peak_magnitude{0};                 ///< Maximum magnitude value
    SampleT peak_frequency_hz{0};              ///< Frequency of peak (Hz)

    // Diagnostics
    size_t window_type{0};                     ///< Window function used (0=Rect, 1=Hann, 2=Hamming, 3=Blackman)
    SampleT scallop_loss_db{-1.0f};            ///< Estimated scallop loss from windowing (dB)

    /**
     * @brief Check validity of packet
     * @return true if all magnitude values are finite (not NaN/Inf)
     */
    [[nodiscard]] bool IsValid() const {
        if (!valid) return false;
        for (const auto& mag : magnitudes) {
            if (!std::isfinite(mag) || mag < 0) {
                return false;
            }
        }
        return std::isfinite(peak_magnitude) && peak_magnitude >= 0;
    }

    /**
     * @brief Convert bin index to frequency
     * @param bin Bin index (0 to N/2-1)
     * @return Frequency in Hz
     */
    [[nodiscard]] SampleT BinToFrequency(size_t bin) const {
        if (bin >= magnitudes.size()) return 0;
        return (static_cast<SampleT>(bin) * static_cast<SampleT>(sample_rate_hz)) /
               static_cast<SampleT>(N);
    }

    /**
     * @brief Convert frequency to bin index
     * @param freq_hz Frequency in Hz
     * @return Closest bin index
     */
    [[nodiscard]] size_t FrequencyToBin(SampleT freq_hz) const {
        if (sample_rate_hz <= 0) return 0;
        size_t bin = static_cast<size_t>(
            (freq_hz * static_cast<SampleT>(N)) / static_cast<SampleT>(sample_rate_hz));
        return (bin < magnitudes.size()) ? bin : magnitudes.size() - 1;
    }

    /**
     * @brief Get magnitude at specific frequency
     * @param freq_hz Frequency in Hz
     * @return Linear magnitude at that frequency
     */
    [[nodiscard]] SampleT MagnitudeAt(SampleT freq_hz) const {
        size_t bin = FrequencyToBin(freq_hz);
        return (bin < magnitudes.size()) ? magnitudes[bin] : SampleT{0};
    }

    /**
     * @brief Get top N peaks in spectrum
     * @param count Number of peaks to return
     * @return Vector of (frequency, magnitude) pairs sorted by magnitude descending
     */
    [[nodiscard]] std::vector<std::pair<SampleT, SampleT>> GetTopPeaks(size_t count = 3) const {
        std::vector<std::pair<SampleT, SampleT>> peaks;
        for (size_t i = 0; i < magnitudes.size(); ++i) {
            peaks.emplace_back(BinToFrequency(i), magnitudes[i]);
        }
        // Sort by magnitude descending
        std::sort(peaks.begin(), peaks.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        if (peaks.size() > count) {
            peaks.resize(count);
        }
        return peaks;
    }

    /**
     * @brief Compute RMS (root mean square) of spectrum
     * @return RMS magnitude value
     */
    [[nodiscard]] SampleT ComputeRMS() const {
        SampleT sum_sq{0};
        for (const auto& mag : magnitudes) {
            sum_sq += mag * mag;
        }
        return std::sqrt(sum_sq / static_cast<SampleT>(magnitudes.size()));
    }

    /**
     * @brief Find bins within ±bandwidth_hz of peak frequency
     * @param bandwidth_hz Half-width bandwidth
     * @return Sum of magnitudes in bandwidth
     */
    [[nodiscard]] SampleT GetBandwidthPower(SampleT bandwidth_hz) const {
        size_t lower_bin = FrequencyToBin(peak_frequency_hz - bandwidth_hz);
        size_t upper_bin = FrequencyToBin(peak_frequency_hz + bandwidth_hz);

        SampleT power{0};
        for (size_t i = lower_bin; i <= upper_bin && i < magnitudes.size(); ++i) {
            power += magnitudes[i];
        }
        return power;
    }
};

}  // namespace dsp
