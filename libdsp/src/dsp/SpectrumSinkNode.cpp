#include "dsp/SpectrumSinkNode.hpp"
#include "log4cxx/logger.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <nlohmann/json.hpp>

namespace dsp {

// ============================================================================
// Logger Setup
// ============================================================================

template<typename SampleT, size_t N>
log4cxx::LoggerPtr SpectrumSinkNode<SampleT, N>::log_ =
    log4cxx::Logger::getLogger("dsp.SpectrumSinkNode");

// ============================================================================
// Constructors
// ============================================================================

template<typename SampleT, size_t N>
SpectrumSinkNode<SampleT, N>::SpectrumSinkNode() {
    this->SetName("__unnamed__");
    LOG4CXX_DEBUG(log_, "SpectrumSinkNode constructed (N=" << N << ")");
}

// ============================================================================
// Core Sink Interface
// ============================================================================

template<typename SampleT, size_t N>
bool SpectrumSinkNode<SampleT, N>::Consume(
    const MagnitudePacketType& packet,
    std::integral_constant<std::size_t, 0>) {
    bool should_signal_completion = false;
    {
        std::lock_guard<std::mutex> lock(spectrum_mutex_);
        spectrum_history_.push_back(packet);
        UpdatePeakTracker(packet);
        TrimHistory();
        if (!completion_signaled_) {
            completion_signaled_ = true;
            should_signal_completion = true;
        }
    }

    LOG4CXX_DEBUG(log_, "Spectrum consumed: peak_freq="
                              << packet.peak_frequency_hz << " Hz, peak_mag="
                              << packet.peak_magnitude);
    if (auto* provider = dynamic_cast<CompletionNodeCallback*>(this->GetCallbackProvider());
        should_signal_completion && provider) {
        provider->OnComplete();
    }
    return true;
}

// ============================================================================
// Spectrum Access
// ============================================================================

template<typename SampleT, size_t N>
std::optional<typename SpectrumSinkNode<SampleT, N>::MagnitudePacketType>
SpectrumSinkNode<SampleT, N>::GetLatestSpectrum() const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    if (spectrum_history_.empty()) {
        return std::nullopt;
    }
    return spectrum_history_.back();
}

template<typename SampleT, size_t N>
std::vector<typename SpectrumSinkNode<SampleT, N>::MagnitudePacketType>
SpectrumSinkNode<SampleT, N>::GetSpectrumHistory() const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    return std::vector<MagnitudePacketType>(spectrum_history_.begin(),
                                            spectrum_history_.end());
}

template<typename SampleT, size_t N>
size_t SpectrumSinkNode<SampleT, N>::GetFrameCount() const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    return spectrum_history_.size();
}

// ============================================================================
// Statistics & Analysis
// ============================================================================

template<typename SampleT, size_t N>
typename SpectrumSinkNode<SampleT, N>::SpectrumStatistics
SpectrumSinkNode<SampleT, N>::ComputeStatistics() const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);

    SpectrumStatistics stats;

    if (spectrum_history_.empty()) {
        return stats;
    }

    stats.frame_count = spectrum_history_.size();

    // Latest spectrum stats
    const auto& latest = spectrum_history_.back();
    stats.latest_peak_frequency = latest.peak_frequency_hz;
    stats.latest_peak_magnitude = latest.peak_magnitude;

    // Peak tracking
    stats.max_peak_frequency = peak_tracker_.max_frequency;
    stats.max_peak_magnitude = peak_tracker_.max_magnitude;

    // Iterate through all spectra for averages
    SampleT sum_rms_power = 0;
    stats.max_rms_power = 0;
    stats.min_rms_power = std::numeric_limits<SampleT>::max();

    for (const auto& spectrum : spectrum_history_) {
        // Compute RMS power
        SampleT power = 0;
        for (const auto& mag : spectrum.magnitudes) {
            power += mag * mag;
        }
        power = std::sqrt(power / spectrum.magnitudes.size());

        sum_rms_power += power;
        stats.max_rms_power = std::max(stats.max_rms_power, power);
        stats.min_rms_power = std::min(stats.min_rms_power, power);
    }

    stats.avg_rms_power = sum_rms_power / stats.frame_count;

    // Crest factor (latest packet)
    if (stats.latest_peak_magnitude > 0 && stats.avg_rms_power > 0) {
        stats.crest_factor = stats.latest_peak_magnitude / stats.avg_rms_power;
    }

    // Timestamps
    stats.timestamp_first = spectrum_history_.front().timestamp;
    stats.timestamp_latest = spectrum_history_.back().timestamp;

    // Spectral centroid (from latest spectrum)
    if (!latest.magnitudes.empty()) {
        SampleT numerator = 0;
        SampleT denominator = 0;

        for (size_t i = 0; i < latest.magnitudes.size(); ++i) {
            SampleT freq = latest.BinToFrequency(i);
            numerator += freq * latest.magnitudes[i];
            denominator += latest.magnitudes[i];
        }

        if (denominator > 0) {
            stats.spectral_centroid = numerator / denominator;

            // Spectral spread (standard deviation)
            SampleT variance = 0;
            for (size_t i = 0; i < latest.magnitudes.size(); ++i) {
                SampleT freq = latest.BinToFrequency(i);
                variance += latest.magnitudes[i] *
                           (freq - stats.spectral_centroid) *
                           (freq - stats.spectral_centroid);
            }
            variance /= denominator;
            stats.spectral_spread = std::sqrt(variance);
        }
    }

    return stats;
}

template<typename SampleT, size_t N>
SampleT SpectrumSinkNode<SampleT, N>::GetAveragePowerInBand(
    SampleT freq_low_hz, SampleT freq_high_hz) const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);

    if (spectrum_history_.empty()) {
        return 0;
    }

    const auto& spectrum = spectrum_history_.back();
    SampleT sum_power = 0;
    size_t count = 0;

    for (size_t i = 0; i < spectrum.magnitudes.size(); ++i) {
        SampleT freq = spectrum.BinToFrequency(i);
        if (freq >= freq_low_hz && freq <= freq_high_hz) {
            sum_power += spectrum.magnitudes[i];
            count++;
        }
    }

    return count > 0 ? sum_power / count : 0;
}

template<typename SampleT, size_t N>
bool SpectrumSinkNode<SampleT, N>::HasEnergyInBand(
    SampleT freq_low_hz, SampleT freq_high_hz,
    SampleT magnitude_threshold) const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);

    if (spectrum_history_.empty()) {
        return false;
    }

    const auto& spectrum = spectrum_history_.back();

    for (size_t i = 0; i < spectrum.magnitudes.size(); ++i) {
        SampleT freq = spectrum.BinToFrequency(i);
        if (freq >= freq_low_hz && freq <= freq_high_hz) {
            if (spectrum.magnitudes[i] > magnitude_threshold) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// Configuration
// ============================================================================

template<typename SampleT, size_t N>
void SpectrumSinkNode<SampleT, N>::SetHistoryCapacity(size_t capacity) {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    history_capacity_ = std::max(size_t(1), capacity);
    TrimHistory();
    LOG4CXX_DEBUG(log_, "History capacity set to " << history_capacity_);
}

template<typename SampleT, size_t N>
size_t SpectrumSinkNode<SampleT, N>::GetHistoryCapacity() const {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    return history_capacity_;
}

template<typename SampleT, size_t N>
void SpectrumSinkNode<SampleT, N>::Clear() {
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    spectrum_history_.clear();
    peak_tracker_ = PeakTracker();
    completion_signaled_ = false;
    LOG4CXX_DEBUG(log_, "Spectrum history cleared");
}

template<typename SampleT, size_t N>
void SpectrumSinkNode<SampleT, N>::Configure(const graph::JsonView& cfg) {
    try {
        // Simplified configuration (full JSON parsing deferred)
        LOG4CXX_INFO(log_, "SpectrumSinkNode configured from JSON");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(log_, "SpectrumSinkNode configuration error: "
                                  << e.what());
        throw;
    }
}

// ============================================================================
// IDiagnosable Implementation
// ============================================================================

template<typename SampleT, size_t N>
graph::JsonView SpectrumSinkNode<SampleT, N>::GetDiagnostics() const {
    static thread_local nlohmann::json empty_json = nlohmann::json::object();
    return graph::JsonView(empty_json);
}

// ============================================================================
// IMetricsCallbackProvider Implementation
// ============================================================================

template<typename SampleT, size_t N>
bool SpectrumSinkNode<SampleT, N>::SetMetricsCallback(
    graph::IMetricsCallback* callback) noexcept {
    metrics_callback_ = callback;
    return callback != nullptr;
}

template<typename SampleT, size_t N>
bool SpectrumSinkNode<SampleT, N>::HasMetricsCallback() const noexcept {
    return metrics_callback_ != nullptr;
}

template<typename SampleT, size_t N>
graph::IMetricsCallback* SpectrumSinkNode<SampleT, N>::GetMetricsCallback() const noexcept {
    return metrics_callback_;
}

template<typename SampleT, size_t N>
app::metrics::NodeMetricsSchema SpectrumSinkNode<SampleT, N>::GetNodeMetricsSchema() const noexcept {
    // Return empty schema for now
    // In a full implementation, would describe available metrics
    return app::metrics::NodeMetricsSchema{};
}

// ============================================================================
// Private Helper Methods
// ============================================================================

template<typename SampleT, size_t N>
void SpectrumSinkNode<SampleT, N>::TrimHistory() {
    while (spectrum_history_.size() > history_capacity_) {
        spectrum_history_.pop_front();
    }
}

template<typename SampleT, size_t N>
void SpectrumSinkNode<SampleT, N>::UpdatePeakTracker(
    const MagnitudePacketType& packet) {
    peak_tracker_.max_frequency = std::max(peak_tracker_.max_frequency,
                                          packet.peak_frequency_hz);
    peak_tracker_.max_magnitude = std::max(peak_tracker_.max_magnitude,
                                          packet.peak_magnitude);
}

// ============================================================================
// Explicit Instantiations
// ============================================================================

template class SpectrumSinkNode<float, 256>;
template class SpectrumSinkNode<float, 512>;
template class SpectrumSinkNode<float, 1024>;

}  // namespace dsp
