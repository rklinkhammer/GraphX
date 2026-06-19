/**
 * @file SpectrumSinkNode.hpp
 * @brief Spectrum Sink Node DSP support.
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
#include <chrono>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <log4cxx/logger.h>

#include "graph/NamedNodes.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/ICompletionCallback.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "metrics/NodeMetricsSchema.hpp"

namespace dsp {

/**
 * @class SpectrumSinkNode
 * @brief Sink node for power spectrum analysis and visualization
 *
 * Template parameters:
 * @tparam SampleT Floating point type (float or double)
 * @tparam N FFT size from input MagnitudePacket
 *
 * **Input Port 0:** ControlToken<MagnitudePacket<SampleT, N>> - Power spectrum from CpuSpectrumDftNode
 *
 * **Features:**
 * - Captures and buffers spectrum data with configurable history
 * - Peak frequency/magnitude tracking with peak hold
 * - RMS power computation across frequency bins
 * - Spectral statistics (centroid, spread, dynamics)
 * - Thread-safe concurrent access
 *
 * **Typical Usage:**
 * ```
 * CpuSpectrumDftNode -> Transfer(IqPacket) -> MagnitudePacket
 *                                         |
 *                                         v
 *                            SpectrumSinkNode -> Consume()
 * ```
 */
/**
 * @class SpectrumSinkNode
 * @brief Spectrum Sink Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
template<typename SampleT = float, size_t N = 256>
class SpectrumSinkNode
    : public graph::NamedSinkNode<
        SpectrumSinkNode<SampleT, N>,
        graph::gpu::accel::ControlToken<MagnitudePacket<SampleT, N>>>,
      public graph::IConfigurable,
      public graph::IDiagnosable,
      public graph::IMetricsCallbackProvider,
      public graph::CompletionCallbackProvider {
public:
    // ========================================================================
    // Type Aliases
    // ========================================================================

    using MagnitudePacketType = MagnitudePacket<SampleT, N>;
    using MagnitudeTokenType = graph::gpu::accel::ControlToken<MagnitudePacketType>;
    using SpectrumArray = std::array<SampleT, N>;

    // ========================================================================
    // Statistics Structure
    // ========================================================================

    /**
     * @brief Statistics computed from captured spectra
     *
     * Aggregates metrics useful for spectrum analysis and visualization.
     */
    /**
     * @struct SpectrumStatistics
     * @brief Spectrum Statistics data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    struct SpectrumStatistics {
        /// Number of spectra captured
        size_t frame_count = 0;

        /// Latest peak frequency (Hz)
        SampleT latest_peak_frequency = 0;

        /// Latest peak magnitude (linear)
        SampleT latest_peak_magnitude = 0;

        /// Maximum peak frequency observed (Hz)
        SampleT max_peak_frequency = 0;

        /// Maximum peak magnitude observed (linear)
        SampleT max_peak_magnitude = 0;

        /// Average RMS power across all spectra (linear)
        SampleT avg_rms_power = 0;

        /// Maximum RMS power observed (linear)
        SampleT max_rms_power = 0;

        /// Minimum RMS power observed (linear)
        SampleT min_rms_power = std::numeric_limits<SampleT>::max();

        /// Spectral centroid (center of mass in frequency domain, Hz)
        SampleT spectral_centroid = 0;

        /// Spectral spread (standard deviation of power distribution, Hz)
        SampleT spectral_spread = 0;

        /// Crest factor (peak magnitude / RMS power)
        SampleT crest_factor = 0;

        /// Timestamp of first spectrum captured
        std::chrono::system_clock::time_point timestamp_first;

        /// Timestamp of latest spectrum captured
        std::chrono::system_clock::time_point timestamp_latest;
    };

    // ========================================================================
    // Constructors & Lifecycle
    // ========================================================================

    /**
     * @brief Construct a SpectrumSinkNode
     *
     * Initializes with default configuration:
     * - Spectrum history: 100 frames
     * - Peak hold time: disabled
     * - RMS power averaging: enabled
     */
/**
 * @brief Spectrum sink node.
 * @return Result of the operation.
 */
    explicit SpectrumSinkNode();

    /**
     * @brief Virtual destructor
     */
    virtual ~SpectrumSinkNode() = default;

    // ========================================================================
    // Core Sink Interface
    // ========================================================================

    /**
     * @brief Consume a token-wrapped MagnitudePacket from CpuSpectrumDftNode
     *
     * Called by GraphX framework when spectrum data arrives.
     * Stores spectrum in buffer and updates peak tracking.
     *
     * @param packet The token-wrapped MagnitudePacket to consume
     * @param integral_constant Port identifier (always 0)
     * @return true to continue consuming
     *
     * @thread_safety This method is thread-safe.
     */
    bool Consume(const MagnitudeTokenType& packet,
                 std::integral_constant<std::size_t, 0>) override;

    // ========================================================================
    // Spectrum Access
    // ========================================================================

    /**
     * @brief Get the latest captured spectrum
     *
     * @return Latest MagnitudePacket, or nullopt if none captured
     *
     * @thread_safety This method is thread-safe.
     */
/**
 * @brief Get latest spectrum.
 * @return Result of the operation.
 */
    std::optional<MagnitudePacketType> GetLatestSpectrum() const;

    /**
     * @brief Get spectrum history (FIFO buffer)
     *
     * @return Vector of captured spectra in chronological order
     *
     * @thread_safety This method is thread-safe.
     */
/**
 * @brief Get spectrum history.
 * @return Result of the operation.
 */
    std::vector<MagnitudePacketType> GetSpectrumHistory() const;

    /**
     * @brief Get count of captured spectra
     *
     * @return Number of MagnitudePackets captured
     *
     * @thread_safety This method is thread-safe for read.
     */
/**
 * @brief Get frame count.
 * @return Result of the operation.
 */
    size_t GetFrameCount() const;

    // ========================================================================
    // Statistics & Analysis
    // ========================================================================

    /**
     * @brief Compute statistics from captured spectra
     *
     * Analyzes all buffered spectra and computes aggregate statistics.
     *
     * @return SpectrumStatistics with computed values
     *
     * @thread_safety This method is thread-safe.
     * @complexity O(n*m) where n = frame count, m = bins per spectrum
     */
/**
 * @brief Compute statistics.
 * @return Result of the operation.
 */
    SpectrumStatistics ComputeStatistics() const;

    /**
     * @brief Get average power in frequency band
     *
     * Computes mean magnitude across specified frequency range.
     *
     * @param freq_low_hz Lower frequency bound (Hz)
     * @param freq_high_hz Upper frequency bound (Hz)
     * @return Average magnitude in band, or 0 if no data
     *
     * @thread_safety This method is thread-safe.
     */
/**
 * @brief Get average power in band.
 * @param freq_low_hz Parameter for get average power in band.
 * @param freq_high_hz Parameter for get average power in band.
 * @return Result of the operation.
 */
    SampleT GetAveragePowerInBand(SampleT freq_low_hz, SampleT freq_high_hz) const;

    /**
     * @brief Check if frequency contains dominant energy
     *
     * Returns true if peak magnitude within specified band is > threshold.
     *
     * @param freq_low_hz Lower frequency bound (Hz)
     * @param freq_high_hz Upper frequency bound (Hz)
     * @param magnitude_threshold Power threshold (linear)
     * @return true if band contains significant energy
     *
     * @thread_safety This method is thread-safe.
     */
    bool HasEnergyInBand(SampleT freq_low_hz, SampleT freq_high_hz,
                         SampleT magnitude_threshold) const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set spectrum history buffer size
     *
     * @param capacity Number of spectra to keep in history (1-1000)
     *
     * @thread_safety This method is thread-safe. May trim history if
     * capacity is reduced.
     */
/**
 * @brief Set history capacity.
 * @param capacity Parameter for set history capacity.
 */
    void SetHistoryCapacity(size_t capacity);

    /**
     * @brief Get current history buffer capacity
     *
     * @return Maximum number of spectra in history
     */
/**
 * @brief Get history capacity.
 * @return Result of the operation.
 */
    size_t GetHistoryCapacity() const;

    /**
     * @brief Clear all captured spectra
     *
     * @thread_safety This method is thread-safe.
     */
/**
 * @brief Clear.
 */
    void Clear();

    /**
     * @brief Configure node from JSON
     *
     * Supported parameters:
     * - "history_capacity" (integer, default 100): Spectrum buffer size
     *
     * @param cfg JSON configuration view
     * @throws ConfigError if configuration is invalid
     */
/**
 * @brief Configure.
 * @param cfg Parameter for configure.
 */
    void Configure(const graph::JsonView& cfg) override;

    // ========================================================================
    // IDiagnosable Implementation
    // ========================================================================

    /**
     * @brief Get spectrum sink node diagnostics
     *
     * Returns JSON with:
     * - frame_count: Number of spectra captured
     * - history_capacity: Current buffer capacity
     * - latest_peak_frequency: Latest peak in Hz
     * - latest_peak_magnitude: Latest peak magnitude
     * - max_peak_frequency: Maximum peak seen
     * - max_peak_magnitude: Maximum magnitude seen
     *
     * @return JsonView with diagnostic information
     */
/**
 * @brief Get diagnostics.
 * @return Result of the operation.
 */
    graph::JsonView GetDiagnostics() const override;

    // ========================================================================
    // IMetricsCallbackProvider Implementation
    // ========================================================================

    /**
     * @brief Set the metrics callback for this node
     *
     * @param callback Pointer to metrics callback handler
     * @return true if callback was successfully set
     */
/**
 * @brief Set metrics callback.
 * @param callback Parameter for set metrics callback.
 * @return Result of the operation.
 */
    bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override;

    /**
     * @brief Check if metrics callback is installed
     *
     * @return true if callback is currently set
     */
/**
 * @brief Has metrics callback.
 * @return Result of the operation.
 */
    bool HasMetricsCallback() const noexcept override;

    /**
     * @brief Get the installed metrics callback
     *
     * @return Pointer to callback or nullptr if not installed
     */
/**
 * @brief Get metrics callback.
 * @return Result of the operation.
 */
    graph::IMetricsCallback* GetMetricsCallback() const noexcept override;

    /**
     * @brief Get metrics schema for this node
     *
     * @return Node metrics schema describing available metrics
     */
/**
 * @brief Get node metrics schema.
 * @return Result of the operation.
 */
    app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override;

private:
    // ========================================================================
    // State Management
    // ========================================================================

    mutable std::mutex spectrum_mutex_;
    std::deque<MagnitudePacketType> spectrum_history_;
    static constexpr size_t DEFAULT_HISTORY_CAPACITY = 100;
    size_t history_capacity_ = DEFAULT_HISTORY_CAPACITY;
    bool completion_signaled_{false};

    // Peak tracking
    /**
     * @struct PeakTracker
     * @brief Peak Tracker data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    struct PeakTracker {
        SampleT max_frequency = 0;
        SampleT max_magnitude = 0;
    } peak_tracker_;

    // Metrics callback handler (may be nullptr)
    graph::IMetricsCallback* metrics_callback_{nullptr};

    // Logger
    static log4cxx::LoggerPtr log_;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Trim history to capacity if needed
     *
     * Called after new spectrum is added. Assumes spectrum_mutex_ is held.
     */
/**
 * @brief Trim history.
 */
    void TrimHistory();

    /**
     * @brief Update peak tracker with new spectrum
     *
     * Assumes spectrum_mutex_ is held.
     */
/**
 * @brief Update peak tracker.
 * @param packet Parameter for update peak tracker.
 */
    void UpdatePeakTracker(const MagnitudePacketType& packet);
};

// Explicit instantiations for common sizes
extern template class SpectrumSinkNode<float, 256>;
extern template class SpectrumSinkNode<float, 512>;
extern template class SpectrumSinkNode<float, 1024>;

}  // namespace dsp
