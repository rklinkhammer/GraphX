#pragma once

#include "IqPacket.hpp"
#include "MagnitudePacket.hpp"
#include "WindowFunctions.hpp"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <vector>

namespace dsp {

/**
 * @brief Snapshot of FFT metrics at a point in time
 *
 * Copyable struct containing atomic values read at a specific moment.
 */
struct FFTMetricsSnapshot {
    uint64_t packets_input{0};
    uint64_t ffts_computed{0};
    uint64_t packets_accumulated{0};
    uint64_t packets_dropped{0};
    uint64_t batch_flushes{0};
    uint64_t total_fft_time_ns{0};
    uint64_t total_accumulate_time_ns{0};
    uint64_t max_accumulation_depth{0};
    uint64_t current_accumulation{0};
    uint64_t window_config_changes{0};
    uint64_t accumulation_config_changes{0};

    /**
     * @brief Get average FFT computation time in microseconds
     */
    [[nodiscard]] double GetAverageFFTTime_us() const {
        if (ffts_computed == 0) return 0.0;
        return static_cast<double>(total_fft_time_ns) / (1000.0 * ffts_computed);
    }

    /**
     * @brief Get FFT throughput in FFTs per second
     */
    [[nodiscard]] double GetFFTThroughput_per_second() const {
        if (total_fft_time_ns == 0 || ffts_computed == 0) return 0.0;
        return 1e9 * static_cast<double>(ffts_computed) / static_cast<double>(total_fft_time_ns);
    }

    /**
     * @brief Get packet accumulation efficiency (packets in / packets used)
     */
    [[nodiscard]] double GetAccumulationEfficiency() const {
        if (packets_accumulated == 0) return 0.0;
        return static_cast<double>(packets_accumulated) / static_cast<double>(packets_input);
    }

    /**
     * @brief Get packet drop rate
     */
    [[nodiscard]] double GetPacketDropRate() const {
        if (packets_input == 0) return 0.0;
        return static_cast<double>(packets_dropped) / static_cast<double>(packets_input);
    }
};

/**
 * @brief Thread-safe metrics for FFT processing
 *
 * Uses atomic operations for non-blocking access.
 * Follows ActiveQueue pattern for consistent metrics tracking.
 */
struct FFTMetrics {
    // Non-copyable due to atomic members
    FFTMetrics() = default;
    FFTMetrics(const FFTMetrics&) = delete;
    FFTMetrics& operator=(const FFTMetrics&) = delete;
    FFTMetrics(FFTMetrics&&) = delete;
    FFTMetrics& operator=(FFTMetrics&&) = delete;

    // Input/Output counters
    std::atomic<uint64_t> packets_input{0};        ///< IqPackets received
    std::atomic<uint64_t> ffts_computed{0};        ///< Successful FFTs computed
    std::atomic<uint64_t> packets_accumulated{0};  ///< Total packets processed through accumulator
    std::atomic<uint64_t> packets_dropped{0};      ///< Dropped due to sequence gaps
    std::atomic<uint64_t> batch_flushes{0};        ///< Number of batch flushes (Reset or explicit)

    // Timing metrics (nanoseconds)
    std::atomic<uint64_t> total_fft_time_ns{0};    ///< Cumulative FFT computation time
    std::atomic<uint64_t> total_accumulate_time_ns{0};  ///< Cumulative accumulation buffer fill time

    // Capacity tracking
    std::atomic<uint64_t> max_accumulation_depth{0};   ///< Peak number of packets buffered
    std::atomic<uint64_t> current_accumulation{0};     ///< Current packets in accumulator

    // Diagnostics
    std::atomic<uint64_t> window_config_changes{0};    ///< Number of window type reconfigurations
    std::atomic<uint64_t> accumulation_config_changes{0};  ///< Number of accumulation count changes

};

/**
 * @brief Core FFT processor for IQ packet analysis
 *
 * Implements packet accumulation, windowing, FFT computation, and peak detection.
 * Uses direct DFT algorithm suitable for N ≤ 1024 samples.
 *
 * @tparam SampleT Floating point type (float or double)
 * @tparam N FFT size (must match IqPacket size)
 *
 * Example:
 * ```cpp
 * dsp::FFTManager<float, 256> fft_mgr(4, 48000.0, WindowType::HANN);
 *
 * for (const auto& iq_packet : input_stream) {
 *     auto result = fft_mgr.ProcessPacket(iq_packet);
 *     if (result) {
 *         std::cout << "Peak at " << result->peak_frequency_hz << " Hz\n";
 *     }
 * }
 * ```
 */
template<typename SampleT, size_t N>
class FFTManager {
public:
    /**
     * @brief Construct FFT manager
     *
     * @param accumulation_count Number of IqPackets to accumulate before processing (1-16)
     * @param sample_rate_hz Sample rate in Hz (for frequency axis calculation)
     * @param window_type Window function to apply (default Hann)
     */
    explicit FFTManager(size_t accumulation_count = 1,
                       double sample_rate_hz = 48000.0,
                       WindowType window_type = WindowType::HANN)
        : accumulation_count_(accumulation_count),
          sample_rate_hz_(sample_rate_hz),
          window_type_(window_type),
          first_packet_(true),
          last_packet_number_(0) {
        if (accumulation_count < 1) accumulation_count_ = 1;
        if (accumulation_count > 16) accumulation_count_ = 16;

        // Pre-generate window coefficients
        window_coeffs_ = WindowFunctions::GenerateArray<SampleT, N>(window_type_);

        // Set window type metric
        metrics_.window_config_changes.store(0, std::memory_order_release);
    }

    /**
     * @brief Process a single IqPacket
     *
     * Accumulates packets until accumulation_count is reached, then:
     * 1. Merges all accumulated samples
     * 2. Applies window function
     * 3. Computes FFT (via DFT)
     * 4. Detects peak
     * 5. Returns MagnitudePacket
     *
     * @param packet Input IqPacket
     * @return MagnitudePacket on FFT completion, std::nullopt while accumulating
     */
    [[nodiscard]] std::optional<MagnitudePacket<SampleT, N>> ProcessPacket(
        const IqPacket<SampleT, N>& packet) {
        // Step 1: Track input
        metrics_.packets_input.fetch_add(1, std::memory_order_acq_rel);

        // Step 2: Check for sequence gaps
        if (!first_packet_) {
            uint64_t expected = last_packet_number_ + 1;
            if (packet.packet_number > expected) {
                uint64_t gap = packet.packet_number - expected;
                metrics_.packets_dropped.fetch_add(gap, std::memory_order_acq_rel);
            }
        }
        first_packet_ = false;
        last_packet_number_ = packet.packet_number;

        // Step 3: Accumulate
        accumulator_.push_back(packet);
        metrics_.packets_accumulated.fetch_add(1, std::memory_order_acq_rel);

        // Update current accumulation depth
        uint64_t current = accumulator_.size();
        metrics_.current_accumulation.store(current, std::memory_order_release);

        // Track maximum depth
        uint64_t max_depth = metrics_.max_accumulation_depth.load(std::memory_order_acquire);
        while (current > max_depth) {
            if (metrics_.max_accumulation_depth.compare_exchange_weak(
                    max_depth, current, std::memory_order_acq_rel)) {
                break;
            }
        }

        // Step 4: Check if ready
        if (current < accumulation_count_) {
            return std::nullopt;  // Not ready yet
        }

        // Step 5: Merge samples
        std::vector<std::complex<SampleT>> combined;
        combined.reserve(N * accumulation_count_);
        for (const auto& pkt : accumulator_) {
            for (const auto& sample : pkt.samples) {
                combined.push_back(sample);
            }
        }

        // Step 6: Apply window function (truncate to merged size if needed)
        size_t window_size = std::min(combined.size(), N * accumulation_count_);
        for (size_t i = 0; i < window_size; ++i) {
            size_t window_idx = i % N;
            combined[i] *= window_coeffs_[window_idx];
        }

        // Step 7: Compute FFT
        auto magnitudes = ComputeDFT(combined);

        // Step 8: Detect peak
        auto [peak_bin, peak_mag] = FindPeak(magnitudes);
        SampleT peak_freq = BinToFrequency(peak_bin, combined.size());

        // Step 9: Build result packet
        MagnitudePacket<SampleT, N> result;
        result.timestamp = packet.timestamp;
        result.packet_number = packet.packet_number;
        result.num_accumulated_packets = accumulator_.size();
        result.sample_rate_hz = sample_rate_hz_;
        result.peak_bin = peak_bin;
        result.peak_magnitude = peak_mag;
        result.peak_frequency_hz = peak_freq;
        result.window_type = static_cast<size_t>(window_type_);
        result.scallop_loss_db = static_cast<SampleT>(
            WindowFunctions::GetScallopLoss_dB(window_type_));

        // Copy magnitudes to output (fill with zeros if mismatch)
        std::fill(result.magnitudes.begin(), result.magnitudes.end(), SampleT{0});
        size_t copy_count = std::min(magnitudes.size(), result.magnitudes.size());
        std::copy(magnitudes.begin(), magnitudes.begin() + copy_count,
                  result.magnitudes.begin());

        result.valid = IsValidPacket(result);

        // Step 10: Reset for next batch
        accumulator_.clear();
        metrics_.current_accumulation.store(0, std::memory_order_release);
        metrics_.ffts_computed.fetch_add(1, std::memory_order_acq_rel);

        return result;
    }

    /**
     * @brief Flush any pending accumulated packets (compute partial FFT if any)
     *
     * Useful for end-of-stream processing when fewer than accumulation_count_
     * packets remain. Resets internal state after flush.
     *
     * @return MagnitudePacket if packets are pending, std::nullopt if accumulator empty
     */
    [[nodiscard]] std::optional<MagnitudePacket<SampleT, N>> Flush() {
        if (accumulator_.empty()) {
            return std::nullopt;
        }

        // Merge samples from accumulated packets
        std::vector<std::complex<SampleT>> combined;
        combined.reserve(N * accumulator_.size());
        for (const auto& pkt : accumulator_) {
            for (const auto& sample : pkt.samples) {
                combined.push_back(sample);
            }
        }

        // Apply window function (truncate to merged size if needed)
        size_t window_size = std::min(combined.size(), N * accumulation_count_);
        for (size_t i = 0; i < window_size; ++i) {
            size_t window_idx = i % N;
            combined[i] *= window_coeffs_[window_idx];
        }

        // Compute FFT
        auto magnitudes = ComputeDFT(combined);

        // Detect peak
        auto [peak_bin, peak_mag] = FindPeak(magnitudes);
        SampleT peak_freq = BinToFrequency(peak_bin, combined.size());

        // Build result packet
        MagnitudePacket<SampleT, N> result;
        result.timestamp = accumulator_.back().timestamp;
        result.packet_number = last_packet_number_;
        result.num_accumulated_packets = accumulator_.size();
        result.sample_rate_hz = sample_rate_hz_;
        result.peak_bin = peak_bin;
        result.peak_magnitude = peak_mag;
        result.peak_frequency_hz = peak_freq;
        result.window_type = static_cast<size_t>(window_type_);
        result.scallop_loss_db = static_cast<SampleT>(
            WindowFunctions::GetScallopLoss_dB(window_type_));

        // Copy magnitudes to output (fill with zeros if mismatch)
        std::fill(result.magnitudes.begin(), result.magnitudes.end(), SampleT{0});
        size_t copy_count = std::min(magnitudes.size(), result.magnitudes.size());
        std::copy(magnitudes.begin(), magnitudes.begin() + copy_count,
                  result.magnitudes.begin());

        result.valid = IsValidPacket(result);

        // Clear accumulator and update metrics
        accumulator_.clear();
        metrics_.current_accumulation.store(0, std::memory_order_release);
        metrics_.ffts_computed.fetch_add(1, std::memory_order_acq_rel);
        metrics_.batch_flushes.fetch_add(1, std::memory_order_acq_rel);

        return result;
    }

    /**
     * @brief Reset state and clear accumulator
     *
     * Clears pending packets without processing.
     * Does not affect configuration or metrics.
     */
    void Reset() {
        accumulator_.clear();
        first_packet_ = true;
        last_packet_number_ = 0;
        metrics_.batch_flushes.fetch_add(1, std::memory_order_acq_rel);
        metrics_.current_accumulation.store(0, std::memory_order_release);
    }

    /**
     * @brief Reconfigure accumulation count
     *
     * @param count New accumulation count (clamped to 1-16)
     */
    void SetAccumulationCount(size_t count) {
        if (count < 1) count = 1;
        if (count > 16) count = 16;
        if (count != accumulation_count_) {
            accumulation_count_ = count;
            metrics_.accumulation_config_changes.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    /**
     * @brief Reconfigure window function
     *
     * @param window_type New window type
     */
    void SetWindowType(WindowType window_type) {
        if (window_type != window_type_) {
            window_type_ = window_type;
            window_coeffs_ = WindowFunctions::GenerateArray<SampleT, N>(window_type_);
            metrics_.window_config_changes.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    /**
     * @brief Set sample rate (for frequency axis calculation)
     *
     * @param sample_rate_hz Sample rate in Hz
     */
    void SetSampleRate(double sample_rate_hz) {
        sample_rate_hz_ = sample_rate_hz;
    }

    /**
     * @brief Get current accumulation count
     */
    [[nodiscard]] size_t GetAccumulationCount() const {
        return accumulation_count_;
    }

    /**
     * @brief Get current window type
     */
    [[nodiscard]] WindowType GetWindowType() const {
        return window_type_;
    }

    /**
     * @brief Get sample rate
     */
    [[nodiscard]] double GetSampleRate() const {
        return sample_rate_hz_;
    }

    /**
     * @brief Get metrics snapshot (atomic read of all counters)
     */
    [[nodiscard]] FFTMetricsSnapshot GetMetrics() const {
        return FFTMetricsSnapshot{
            .packets_input = metrics_.packets_input.load(std::memory_order_acquire),
            .ffts_computed = metrics_.ffts_computed.load(std::memory_order_acquire),
            .packets_accumulated = metrics_.packets_accumulated.load(std::memory_order_acquire),
            .packets_dropped = metrics_.packets_dropped.load(std::memory_order_acquire),
            .batch_flushes = metrics_.batch_flushes.load(std::memory_order_acquire),
            .total_fft_time_ns = metrics_.total_fft_time_ns.load(std::memory_order_acquire),
            .total_accumulate_time_ns = metrics_.total_accumulate_time_ns.load(std::memory_order_acquire),
            .max_accumulation_depth = metrics_.max_accumulation_depth.load(std::memory_order_acquire),
            .current_accumulation = metrics_.current_accumulation.load(std::memory_order_acquire),
            .window_config_changes = metrics_.window_config_changes.load(std::memory_order_acquire),
            .accumulation_config_changes = metrics_.accumulation_config_changes.load(std::memory_order_acquire),
        };
    }

    /**
     * @brief Get number of packets pending in accumulator
     */
    [[nodiscard]] size_t GetPendingPackets() const {
        return accumulator_.size();
    }

private:
    // Configuration
    size_t accumulation_count_;
    double sample_rate_hz_;
    WindowType window_type_;
    std::array<SampleT, N> window_coeffs_;

    // State
    std::vector<IqPacket<SampleT, N>> accumulator_;
    bool first_packet_;
    uint64_t last_packet_number_;

    // Metrics
    mutable FFTMetrics metrics_;

    /**
     * @brief Compute Direct Fourier Transform (DFT)
     *
     * O(N²) algorithm suitable for N ≤ 1024.
     * Returns magnitude spectrum (positive frequencies only).
     *
     * @param samples Input samples (accumulated and windowed)
     * @return Magnitude spectrum (N/2 bins)
     */
    [[nodiscard]] std::vector<SampleT> ComputeDFT(
        const std::vector<std::complex<SampleT>>& samples) const {
        size_t num_samples = samples.size();
        size_t num_bins = num_samples / 2;
        std::vector<SampleT> magnitudes(num_bins, SampleT{0});

        constexpr SampleT TWO_PI = static_cast<SampleT>(6.283185307179586);

        for (size_t k = 0; k < num_bins; ++k) {
            std::complex<SampleT> acc{0, 0};
            for (size_t n = 0; n < num_samples; ++n) {
                SampleT angle = TWO_PI * static_cast<SampleT>(k * n) /
                                static_cast<SampleT>(num_samples);
                std::complex<SampleT> w{std::cos(angle), -std::sin(angle)};
                acc += samples[n] * w;
            }
            magnitudes[k] = std::abs(acc) / static_cast<SampleT>(num_samples);
        }

        return magnitudes;
    }

    /**
     * @brief Find peak magnitude and its bin index
     *
     * @param magnitudes Magnitude spectrum
     * @return Pair of (peak_bin, peak_magnitude)
     */
    [[nodiscard]] std::pair<size_t, SampleT> FindPeak(
        const std::vector<SampleT>& magnitudes) const {
        if (magnitudes.empty()) {
            return {0, SampleT{0}};
        }

        size_t peak_bin = 0;
        SampleT peak_value = magnitudes[0];

        for (size_t i = 1; i < magnitudes.size(); ++i) {
            if (magnitudes[i] > peak_value) {
                peak_value = magnitudes[i];
                peak_bin = i;
            }
        }

        return {peak_bin, peak_value};
    }

    /**
     * @brief Convert bin index to frequency
     *
     * @param bin Bin index
     * @param num_samples Total number of samples used in FFT
     * @return Frequency in Hz
     */
    [[nodiscard]] SampleT BinToFrequency(size_t bin, size_t num_samples) const {
        if (num_samples == 0) return SampleT{0};
        return (static_cast<SampleT>(bin) * static_cast<SampleT>(sample_rate_hz_)) /
               static_cast<SampleT>(num_samples);
    }

    /**
     * @brief Validate output packet
     *
     * @param packet Packet to validate
     * @return true if all magnitudes are finite and non-negative
     */
    [[nodiscard]] bool IsValidPacket(const MagnitudePacket<SampleT, N>& packet) const {
        for (const auto& mag : packet.magnitudes) {
            if (!std::isfinite(mag) || mag < 0) {
                return false;
            }
        }
        return std::isfinite(packet.peak_magnitude) && packet.peak_magnitude >= 0;
    }
};

}  // namespace dsp
