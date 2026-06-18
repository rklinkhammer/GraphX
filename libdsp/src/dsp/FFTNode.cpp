// SPDX-License-Identifier: MIT

/**
 * @file FFTNode.cpp
 * @brief Fftnode DSP support.
 *
 * @details Provides DSP implementation unit backing the public signal-processing API. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include "dsp/FFTNode.hpp"
#include "log4cxx/logger.h"
#include <nlohmann/json.hpp>

namespace dsp {

// Logger
static auto logger = log4cxx::LoggerPtr(log4cxx::Logger::getLogger("dsp.FFTNode"));

// ============================================================================
// Constructors
// ============================================================================

template<typename SampleT, size_t N>
FFTNode<SampleT, N>::FFTNode()
    : fft_manager_(std::make_unique<FFTManagerType>(1, 48000.0, WindowType::HANN)) {
    this->SetName("__unnamed__");
    LOG4CXX_DEBUG(logger, "FFTNode constructed with defaults (N=" << N << ")");
}

template<typename SampleT, size_t N>
FFTNode<SampleT, N>::FFTNode(size_t accumulation_count, double sample_rate_hz,
                            WindowType window_type)
    : fft_manager_(std::make_unique<FFTManagerType>(accumulation_count, sample_rate_hz,
                                                     window_type)) {
    this->SetName("__unnamed__");
    LOG4CXX_DEBUG(logger, "FFTNode constructed (acc=" << accumulation_count << ", sr="
                                                      << sample_rate_hz << ")");
}

// ============================================================================
// Core Transfer Method
// ============================================================================

template<typename SampleT, size_t N>
std::optional<typename FFTNode<SampleT, N>::MagnitudePacketType>
FFTNode<SampleT, N>::Transfer(const IqMessageType& packet,
                              std::integral_constant<std::size_t, 0>,
                              std::integral_constant<std::size_t, 0>) {
    if (!fft_manager_) {
        LOG4CXX_WARN(logger, "FFTNode.Transfer called but FFTManager not initialized");
        return std::nullopt;
    }

    auto result = fft_manager_->ProcessPacket(packet.template get<IqPacketType>());

    if (result) {
        // Record peak frequency in history
        peak_frequency_history_.push_back(result->peak_frequency_hz);
        TrimPeakHistory();
    }

    return result;
}

// ============================================================================
// Configuration Methods
// ============================================================================

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::SetAccumulationCount(size_t count) {
    if (fft_manager_) {
        fft_manager_->SetAccumulationCount(count);
        LOG4CXX_DEBUG(logger, "FFTNode accumulation count set to " << count);
    }
}

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::SetWindowType(WindowType window_type) {
    if (fft_manager_) {
        fft_manager_->SetWindowType(window_type);
        LOG4CXX_DEBUG(logger, "FFTNode window type set to " << WindowFunctions::ToString(window_type));
    }
}

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::SetSampleRate(double sample_rate_hz) {
    if (fft_manager_) {
        fft_manager_->SetSampleRate(sample_rate_hz);
        LOG4CXX_DEBUG(logger, "FFTNode sample rate set to " << sample_rate_hz << " Hz");
    }
}

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::Flush() {
    if (fft_manager_) {
        // Note: Flush() returns std::optional, we discard the result here
        (void)fft_manager_->Flush();
        LOG4CXX_DEBUG(logger, "FFTNode accumulator flushed");
    }
}

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::Reset() {
    if (fft_manager_) {
        fft_manager_->Reset();
        peak_frequency_history_.clear();
        LOG4CXX_DEBUG(logger, "FFTNode reset");
    }
}

// ============================================================================
// IConfigurable Implementation
// ============================================================================

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::Configure(const graph::JsonView& cfg) {
    try {
        // Parse accumulation_count if present
        if (cfg.Raw().contains("accumulation_count")) {
            auto acc_count_result = cfg.TryGetInt("accumulation_count", -1);
            if (!acc_count_result) {
                throw acc_count_result.error();
            }
            int acc_count = acc_count_result.value();
            if (acc_count > 0) {
                SetAccumulationCount(acc_count);
            }
        }

        // Parse window_type if present
        if (cfg.Raw().contains("window_type")) {
            auto window_result = cfg.TryGetString("window_type", "hann");
            if (!window_result) {
                throw window_result.error();
            }
            std::string window_str = window_result.value();
            auto window_type_opt = WindowFunctions::Parse(window_str);
            if (window_type_opt.has_value()) {
                SetWindowType(window_type_opt.value());
            }
        }

        // Parse sample_rate_hz if present
        if (cfg.Raw().contains("sample_rate_hz")) {
            auto sample_rate_result = cfg.TryGetFloat("sample_rate_hz", 48000.0f);
            if (!sample_rate_result) {
                throw sample_rate_result.error();
            }
            float sample_rate_f = sample_rate_result.value();
            SetSampleRate(static_cast<double>(sample_rate_f));
        }

        LOG4CXX_INFO(logger, "FFTNode configured from JSON");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger, "FFTNode configuration error: " << e.what());
        throw;
    }
}

// ============================================================================
// C++26 Configuration with Error Handling
// ============================================================================

template<typename SampleT, size_t N>
std::expected<void, FFTConfigError> FFTNode<SampleT, N>::ConfigureExpected(
    const graph::JsonView& cfg) noexcept {
    try {
        // Parse accumulation_count if present
        if (cfg.Raw().contains("accumulation_count")) {
            auto acc_count_result = cfg.TryGetInt("accumulation_count", -1);
            if (!acc_count_result) {
                LOG4CXX_WARN(logger, "Invalid accumulation_count type");
                return std::unexpected(FFTConfigError::InvalidAccumulationCount);
            }
            int acc_count = acc_count_result.value();
            if (acc_count <= 0 || acc_count > 16) {
                LOG4CXX_WARN(logger, "Invalid accumulation_count: " << acc_count);
                return std::unexpected(FFTConfigError::InvalidAccumulationCount);
            }
            SetAccumulationCount(acc_count);
        }

        // Parse window_type if present
        if (cfg.Raw().contains("window_type")) {
            auto window_result = cfg.TryGetString("window_type", "hann");
            if (!window_result) {
                LOG4CXX_WARN(logger, "Invalid window_type field");
                return std::unexpected(FFTConfigError::InvalidWindowType);
            }
            std::string window_str = window_result.value();
            auto window_type_opt = WindowFunctions::Parse(window_str);
            if (!window_type_opt.has_value()) {
                LOG4CXX_WARN(logger, "Invalid window_type: " << window_str);
                return std::unexpected(FFTConfigError::InvalidWindowType);
            }
            SetWindowType(window_type_opt.value());
        }

        // Parse sample_rate_hz if present
        if (cfg.Raw().contains("sample_rate_hz")) {
            auto sample_rate_result = cfg.TryGetFloat("sample_rate_hz", 48000.0f);
            if (!sample_rate_result) {
                LOG4CXX_WARN(logger, "Invalid sample_rate_hz field");
                return std::unexpected(FFTConfigError::InvalidSampleRate);
            }
            float sample_rate_f = sample_rate_result.value();
            if (sample_rate_f <= 0.0f) {
                LOG4CXX_WARN(logger, "Invalid sample_rate_hz: " << sample_rate_f);
                return std::unexpected(FFTConfigError::InvalidSampleRate);
            }
            SetSampleRate(static_cast<double>(sample_rate_f));
        }

        LOG4CXX_INFO(logger, "FFTNode configured from JSON (expected)");
        return {};  // Success: return empty expected<void>
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger, "FFTNode::ConfigureExpected() - unexpected error: " << e.what());
        return std::unexpected(FFTConfigError::UnknownError);
    }
}

// ============================================================================
// IDiagnosable Implementation
// ============================================================================

template<typename SampleT, size_t N>
graph::JsonView FFTNode<SampleT, N>::GetDiagnostics() const {
    static thread_local nlohmann::json empty_json = nlohmann::json::object();
    return graph::JsonView(empty_json);
}

// ============================================================================
// IMetricsCallbackProvider Implementation
// ============================================================================

template<typename SampleT, size_t N>
bool FFTNode<SampleT, N>::SetMetricsCallback(
    graph::IMetricsCallback* callback) noexcept {
    metrics_callback_ = callback;
    return callback != nullptr;
}

template<typename SampleT, size_t N>
bool FFTNode<SampleT, N>::HasMetricsCallback() const noexcept {
    return metrics_callback_ != nullptr;
}

template<typename SampleT, size_t N>
graph::IMetricsCallback* FFTNode<SampleT, N>::GetMetricsCallback() const noexcept {
    return metrics_callback_;
}

template<typename SampleT, size_t N>
app::metrics::NodeMetricsSchema FFTNode<SampleT, N>::GetNodeMetricsSchema() const noexcept {
    // Return empty schema for now
    // In a full implementation, would describe available metrics
    return app::metrics::NodeMetricsSchema{};
}

// ============================================================================
// Private Helper Methods
// ============================================================================

template<typename SampleT, size_t N>
void FFTNode<SampleT, N>::TrimPeakHistory() {
    while (peak_frequency_history_.size() > PEAK_HISTORY_SIZE) {
        peak_frequency_history_.pop_front();
    }
}

// ============================================================================
// Explicit Instantiations
// ============================================================================

template class FFTNode<float, 256>;
template class FFTNode<float, 512>;
template class FFTNode<float, 1024>;

}  // namespace dsp
