/**
 * @file SineSignalNode.hpp
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

/**
 * @file dsp/SineSignalNode.hpp
 * @brief Sine wave signal generator as a graph framework node
 *
 * Implements a DSP node that generates sine wave IQ samples at configurable
 * frequency, amplitude, and sample rate. The first production node to use
 * DataProducerWithNotification directly (not CSV-based injection).
 *
 * @author GraphX Contributors
 * @date 2025
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "graph/DataProducerWithNotification.hpp"
#include "graph/Message.hpp"
#include "graph/IConfigurable.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "dsp/SineWaveGenerator.hpp"
#include "dsp/IqPacket.hpp"
#include "sensor/SensorClassificationType.hpp"
#include "metrics/NodeMetricsSchema.hpp"

namespace dsp {

/**
 * @class SineSignalNode
 * @brief Sine wave signal producer node for DSP processing
 *
 * Generates sine wave IQ samples at configurable frequency, amplitude, and
 * sample rate. Demonstrates the pattern for DSP/signal generator nodes that
 * produce data internally (not from external injection).
 *
 * **Architecture:**
 * - Extends DataProducerWithNotification (first production use of this base)
 * - Template parameter N: IQ packet size (default 256 samples)
 * - Port 0: IqSamples - outputs Message<IqPacket<float, N>>
 * - Port 1: Notify - outputs message::CompletionSignal
 * - Uses SineWaveGenerator<float, N> for internal data generation
 *
 * **Usage:**
 * @code
 *   auto node = std::make_unique<dsp::SineSignalNode<256>>();
 *   node->SetName("sine_1");
 *   node->Init();    // Set up listener ports
 *   node->Start();   // Begin sample generation
 * @endcode
 */
template<size_t N = 256>
/**
 * @class SineSignalNode
 * @brief Sine signal node implementation for GraphX.
 */
class SineSignalNode : public graph::DataProducerWithNotification<
    SineSignalNode<N>,
    SineWaveGenerator<float, N>,
    IqPacket<float, N>,
    IqPacket<float, N>,
    graph::message::CompletionSignal,
    int, 0>,
                       public graph::IConfigurable,
                       public graph::IParameterized,
                       public graph::IDiagnosable,
                       public graph::IMetricsCallbackProvider {

public:
    /**
     * @brief Construct a SineSignalNode with default configuration
     *
     * Creates a sine signal generator with:
     * - sample_interval: 10ms (100 Hz)
     * - sample_ignore: 0 (all samples produced)
     * - Frequency: 1000 Hz (default from SineWaveGenerator)
     * - Amplitude: 1.0 (default from SineWaveGenerator)
     * - Sample Rate: 48000 Hz (default from SineWaveGenerator)
     * - Packet size: N samples per packet (template parameter)
     */
    SineSignalNode()
        : graph::DataProducerWithNotification<
            SineSignalNode<N>,
            SineWaveGenerator<float, N>,
            IqPacket<float, N>,
            IqPacket<float, N>,
            graph::message::CompletionSignal,
            int,0>(
                std::make_unique<SineWaveGenerator<float, N>>(),
                std::chrono::microseconds(10000),  // 10ms default interval
                0) {  // No samples ignored by default
        this->SetName("__unnamed__");
    }

    virtual ~SineSignalNode() = default;

    /// @brief Port name constant for IQ samples output
    static constexpr char kIqSamplesPort[] = "IqSamples";

    /// @brief Port name constant for completion notification output
    static constexpr char kNotifyPort[] = "Notify";

    /**
     * @brief Port specifications tuple
     *
     * Defines the two output ports:
     * - Port 0: IqSamples - Message<IqPacket<float, N>>
     * - Port 1: Notify - CompletionSignal
     */
    using Ports = std::tuple<
        graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output,
            kIqSamplesPort, graph::PayloadList<IqPacket<float, N>>>,
        graph::PortSpec<1, ::graph::message::CompletionSignal, graph::PortDirection::Output,
            kNotifyPort, graph::PayloadList<>>>;

protected:
    /**
     * @brief Create completion notification when generator is exhausted
     *
     * Called by DataProducerWithNotification when the generator runs out
     * of data. Returns a CompletionSignal with appropriate metadata.
     *
     * @return CompletionSignal with exhaustion reason and sample count
     */
    graph::message::CompletionSignal CreateNotification() const override {
        return graph::message::CompletionSignal(
            graph::message::CompletionSignal::Reason::CSV_DATA_EXHAUSTED,
            this->GetName(),
            this->GetLastGeneratorTimestamp(),
            this->GetTotalSamplesGenerated(),
            "Sine wave generation complete"
        );
    }

    // ========================================================================
    // IConfigurable Implementation
    // ========================================================================

    /**
     * @brief Configure sine signal generator from JSON
     *
     * Supported parameters:
     * - "frequency_hz" (number): Signal frequency in Hz
     * - "amplitude" (number): Signal amplitude [0, 1]
     * - "sample_rate_hz" (number): Sample rate in Hz
     *
     * @param cfg JSON configuration view
     * @throws ConfigError if configuration is invalid
     */
/**
 * @brief Configure.
 * @param cfg Parameter for configure.
 */
    void Configure(const graph::JsonView& cfg) override;

/**
 * @brief Get parameters.
 * @return Result of the operation.
 */
    graph::JsonView GetParameters() const override;
/**
 * @brief Get parameter description.
 * @param param_name Parameter for get parameter description.
 * @return Result of the operation.
 */
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
/**
 * @brief Get parameter names.
 * @return Result of the operation.
 */
    std::vector<std::string> GetParameterNames() const override;

    // ========================================================================
    // IDiagnosable Implementation
    // ========================================================================

    /**
     * @brief Get diagnostics from sine signal generator
     *
     * Returns JSON with:
     * - frequency_hz: Current frequency setting
     * - amplitude: Current amplitude setting
     * - sample_rate_hz: Current sample rate
     * - samples_generated: Total packets generated
     * - packet_size: Samples per packet (N)
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
    /// Metrics callback handler (may be nullptr)
    graph::IMetricsCallback* metrics_callback_{nullptr};
};

// ============================================================================
// Inline Implementations
// ============================================================================

template<size_t N>
inline void SineSignalNode<N>::Configure(const graph::JsonView& cfg) {
    auto* generator = this->GetGenerator();
    if (!generator) {
        return;
    }

    if (cfg.Contains("frequency_hz")) {
        auto value = cfg.TryGetFloat("frequency_hz", static_cast<float>(generator->GetFrequency()));
        if (!value) {
            throw value.error();
        }
        generator->SetFrequency(static_cast<double>(value.value()));
    }

    if (cfg.Contains("amplitude")) {
        auto value = cfg.TryGetFloat("amplitude", generator->GetAmplitude());
        if (!value) {
            throw value.error();
        }
        generator->SetAmplitude(value.value());
    }

    if (cfg.Contains("sample_rate_hz")) {
        auto value = cfg.TryGetFloat("sample_rate_hz", static_cast<float>(generator->GetSampleRate()));
        if (!value) {
            throw value.error();
        }
        generator->SetSampleRate(static_cast<double>(value.value()));
    }
}

template<size_t N>
inline graph::JsonView SineSignalNode<N>::GetParameters() const {
    static thread_local nlohmann::json params;
    const auto* generator = this->GetGenerator();
    params = nlohmann::json::object({
        {"frequency_hz", generator ? generator->GetFrequency() : 1000.0},
        {"amplitude", generator ? generator->GetAmplitude() : 1.0f},
        {"sample_rate_hz", generator ? generator->GetSampleRate() : 48000.0}
    });
    return graph::JsonView(params);
}

template<size_t N>
inline graph::JsonView SineSignalNode<N>::GetParameterDescription(
    const std::string& param_name) const {
    static thread_local nlohmann::json description;
    if (param_name == "frequency_hz") {
        description = {
            {"type", "number"},
            {"required", false},
            {"description", "Complex sine frequency in Hz"}
        };
    } else if (param_name == "amplitude") {
        description = {
            {"type", "number"},
            {"required", false},
            {"description", "Complex sine amplitude"}
        };
    } else if (param_name == "sample_rate_hz") {
        description = {
            {"type", "number"},
            {"required", false},
            {"description", "Sample rate in Hz"}
        };
    } else {
        description = nlohmann::json::object();
    }
    return graph::JsonView(description);
}

template<size_t N>
inline std::vector<std::string> SineSignalNode<N>::GetParameterNames() const {
    return {"frequency_hz", "amplitude", "sample_rate_hz"};
}

template<size_t N>
inline graph::JsonView SineSignalNode<N>::GetDiagnostics() const {
    static thread_local nlohmann::json empty_json = nlohmann::json::object();
    return graph::JsonView(empty_json);
}

template<size_t N>
inline bool SineSignalNode<N>::SetMetricsCallback(
    graph::IMetricsCallback* callback) noexcept {
    metrics_callback_ = callback;
    return callback != nullptr;
}

template<size_t N>
inline bool SineSignalNode<N>::HasMetricsCallback() const noexcept {
    return metrics_callback_ != nullptr;
}

template<size_t N>
inline graph::IMetricsCallback* SineSignalNode<N>::GetMetricsCallback() const noexcept {
    return metrics_callback_;
}

template<size_t N>
inline app::metrics::NodeMetricsSchema SineSignalNode<N>::GetNodeMetricsSchema() const noexcept {
    // Return empty schema for now
    // In a full implementation, would describe available metrics
    return app::metrics::NodeMetricsSchema{};
}

} // namespace dsp
