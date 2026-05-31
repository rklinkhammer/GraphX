#pragma once

#include "dsp/FFTManager.hpp"
#include "dsp/IqPacket.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "dsp/WindowFunctions.hpp"

#include "graph/NamedNodes.hpp"
#include "graph/Message.hpp"
#include "graph/IConfigurable.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "config/JsonView.hpp"
#include "metrics/NodeMetricsSchema.hpp"

#include <memory>
#include <optional>
#include <expected>
#include <deque>
#include <nlohmann/json.hpp>

namespace dsp {

/**
 * @enum FFTConfigError
 * @brief Error codes for FFT node configuration (C++26)
 *
 * Used with std::expected<void, FFTConfigError> for type-safe configuration.
 */
enum class FFTConfigError {
    /// Invalid accumulation count (must be 1-16)
    InvalidAccumulationCount,
    
    /// Unknown or invalid window function name
    InvalidWindowType,
    
    /// Invalid sample rate (must be positive)
    InvalidSampleRate,
    
    /// Missing required configuration parameter
    MissingParameter,
    
    /// Unexpected JSON structure or type
    InvalidJSONStructure,
    
    /// Other configuration error
    UnknownError
};

/**
 * @brief Graph-native FFT processor node
 *
 * Wraps FFTManager<SampleT, N> as a NamedInteriorNode for graph integration.
 * Processes IqPacket input through FFT to produce MagnitudePacket output.
 *
 * **Architecture:**
 * ```
 * IqPacket<float, N> Input
 *   ↓
 * [Accumulator: 1-16 packets]
 *   ↓
 * [Window function: Hann/Hamming/Blackman]
 *   ↓
 * [DFT computation: O(N²)]
 *   ↓
 * [Peak detection & frequency conversion]
 *   ↓
 * MagnitudePacket<float, N/2> Output
 * ```
 *
 * **Port 0 (Input):** IqPacket<float, N> - Complex IQ samples
 * **Port 0 (Output):** MagnitudePacket<float, N> - Power spectrum
 *
 * **Configuration Parameters:**
 * - accumulation_count (1-16): Number of packets to accumulate
 * - window_type (rectangular|hann|hamming|blackman): Window function
 * - sample_rate_hz (double): Sample rate for frequency axis
 *
 * **Metrics:**
 * - packets_input: IqPackets received
 * - ffts_computed: Successful FFT operations
 * - packets_accumulated: Total packets in accumulator
 * - packets_dropped: Dropped due to gaps
 * - peak_frequencies: Peak detection results history
 *
 * @tparam SampleT Floating point type (float or double)
 * @tparam N FFT size (input packet size)
 */
template<typename SampleT = float, size_t N = 256>
class FFTNode : public graph::NamedInteriorNode<
                    graph::TypeList<graph::message::Message>,
                    graph::TypeList<MagnitudePacket<SampleT, N>>,
                    FFTNode<SampleT, N>>,
                public graph::IConfigurable,
                public graph::IDiagnosable,
                public graph::IMetricsCallbackProvider {
public:
    // ========================================================================
    // Type Aliases
    // ========================================================================

    using IqPacketType = IqPacket<SampleT, N>;
    using IqMessageType = graph::message::Message;
    using MagnitudePacketType = MagnitudePacket<SampleT, N>;
    using FFTManagerType = FFTManager<SampleT, N>;

    // ========================================================================
    // Constructors & Lifecycle
    // ========================================================================

    /**
     * @brief Default constructor
     *
     * Initializes with default FFT configuration:
     * - Accumulation: 1 packet
     * - Window: Hann
     * - Sample rate: 48 kHz
     */
    explicit FFTNode();

    /**
     * @brief Constructor with configuration
     *
     * @param accumulation_count Number of packets to accumulate (1-16)
     * @param sample_rate_hz Sample rate in Hz
     * @param window_type Window function type
     */
    explicit FFTNode(size_t accumulation_count, double sample_rate_hz,
                     WindowType window_type = WindowType::HANN);

    /**
     * @brief Virtual destructor
     */
    virtual ~FFTNode() = default;

    // ========================================================================
    // Core Transfer Method
    // ========================================================================

    /**
     * @brief Process IqPacket through FFT processor
     *
     * Accumulates packets until accumulation_count is reached, then:
     * 1. Applies window function
     * 2. Computes FFT (Direct DFT)
     * 3. Detects peak magnitude and frequency
     * 4. Returns MagnitudePacket
     *
     * @param packet Input IqPacket
     * @return MagnitudePacket when FFT computation completes, std::nullopt while accumulating
     */
    std::optional<MagnitudePacketType> Transfer(
        const IqMessageType& packet,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    // ========================================================================
    // Configuration Methods
    // ========================================================================

    /**
     * @brief Set accumulation packet count
     *
     * @param count Number of packets (clamped to 1-16)
     */
    void SetAccumulationCount(size_t count);

    /**
     * @brief Set window function type
     *
     * @param window_type New window type
     */
    void SetWindowType(WindowType window_type);

    /**
     * @brief Set sample rate (for frequency axis)
     *
     * @param sample_rate_hz Sample rate in Hz
     */
    void SetSampleRate(double sample_rate_hz);

    /**
     * @brief Flush pending accumulated packets
     *
     * Forces processing of partially-accumulated batch.
     * Useful for end-of-stream processing.
     */
    void Flush();

    /**
     * @brief Reset accumulator state
     *
     * Clears pending packets without processing.
     */
    void Reset();

    // ========================================================================
    // Metrics Access
    // ========================================================================

    /**
     * @brief Get FFT metrics snapshot
     */
    FFTMetricsSnapshot GetMetricsSnapshot() const {
        if (fft_manager_) {
            return fft_manager_->GetMetrics();
        }
        return FFTMetricsSnapshot{};
    }

    /**
     * @brief Get number of packets processed
     */
    uint64_t GetPacketsProcessed() const {
        auto metrics = GetMetricsSnapshot();
        return metrics.packets_input;
    }

    /**
     * @brief Get number of FFTs computed
     */
    uint64_t GetFFTsComputed() const {
        auto metrics = GetMetricsSnapshot();
        return metrics.ffts_computed;
    }

    /**
     * @brief Get number of pending packets in accumulator
     */
    size_t GetPendingPackets() const {
        if (fft_manager_) {
            return fft_manager_->GetPendingPackets();
        }
        return 0;
    }

    /**
     * @brief Get current accumulation count
     */
    size_t GetAccumulationCount() const {
        if (fft_manager_) {
            return fft_manager_->GetAccumulationCount();
        }
        return 1;
    }

    /**
     * @brief Get current window type
     */
    WindowType GetWindowType() const {
        if (fft_manager_) {
            return fft_manager_->GetWindowType();
        }
        return WindowType::HANN;
    }

    /**
     * @brief Get current sample rate
     */
    double GetSampleRate() const {
        if (fft_manager_) {
            return fft_manager_->GetSampleRate();
        }
        return 48000.0;
    }

    // ========================================================================
    // IConfigurable Implementation
    // ========================================================================

    /**
     * @brief Configure FFT node with JSON parameters
     *
     * Supported parameters:
     * - accumulation_count (integer, default 1): Packets to accumulate (1-16)
     * - window_type (string, default "hann"): Window function
     *   - "rectangular", "hann", "hamming", "blackman"
     * - sample_rate_hz (number, default 48000): Sample rate for frequency axis
     *
     * @param cfg JSON configuration view
     * @throws ConfigError if configuration is invalid
     */
    void Configure(const graph::JsonView& cfg) override;

    /**
     * @brief Configure FFT node with JSON parameters (C++26 - Type-Safe)
     *
     * Type-safe alternative to Configure() that throws ConfigError.
     * Returns std::expected<void, FFTConfigError> for composable error handling.
     *
     * Supported parameters:
     * - accumulation_count (integer, default 1): Packets to accumulate (1-16)
     * - window_type (string, default "hann"): Window function
     * - sample_rate_hz (number, default 48000): Sample rate for frequency axis
     *
     * Example:
     * ```cpp
     * auto result = node.ConfigureExpected(cfg);
     * if (!result) {
     *     switch (result.error()) {
     *         case FFTConfigError::InvalidAccumulationCount:
     *             LOG_ERROR("Accumulation must be 1-16");
     *             break;
     *         case FFTConfigError::InvalidWindowType:
     *             LOG_ERROR("Unknown window type");
     *             break;
     *     }
     * }
     * ```
     *
     * @param cfg JSON configuration view
     * @return Success (empty expected<void>) or FFTConfigError
     */
    std::expected<void, FFTConfigError> ConfigureExpected(const graph::JsonView& cfg) noexcept;

    // ========================================================================
    // IDiagnosable Implementation
    // ========================================================================

    /**
     * @brief Get FFT node diagnostics
     *
     * Returns JSON with:
     * - accumulation_count: Current accumulation setting
     * - window_type: Current window function
     * - sample_rate_hz: Current sample rate
     * - pending_packets: Packets awaiting FFT
     * - ffts_computed: Total FFT operations
     * - packets_processed: Total input packets
     *
     * @return JsonView with diagnostic information
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
    bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override;

    /**
     * @brief Check if metrics callback is installed
     *
     * @return true if callback is currently set
     */
    bool HasMetricsCallback() const noexcept override;

    /**
     * @brief Get the installed metrics callback
     *
     * @return Pointer to callback or nullptr if not installed
     */
    graph::IMetricsCallback* GetMetricsCallback() const noexcept override;

    /**
     * @brief Get metrics schema for this node
     *
     * @return Node metrics schema describing available metrics
     */
    app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override;

private:
    // Core FFT processor
    std::unique_ptr<FFTManagerType> fft_manager_;

    // Peak frequency history (for diagnostics)
    static constexpr size_t PEAK_HISTORY_SIZE = 100;
    std::deque<SampleT> peak_frequency_history_;

    // Metrics callback handler (may be nullptr)
    graph::IMetricsCallback* metrics_callback_{nullptr};

    // Helper methods
    void TrimPeakHistory();
};

// Explicit instantiations (common sizes)
extern template class FFTNode<float, 256>;
extern template class FFTNode<float, 512>;
extern template class FFTNode<float, 1024>;

}  // namespace dsp
