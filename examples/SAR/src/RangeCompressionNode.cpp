#include "sar/RangeCompressionNode.hpp"

#include "config/ConfigError.hpp"
#include "dsp/FFTManager.hpp"
#include "dsp/IqPacket.hpp"

#include <algorithm>
#include <array>
#include <complex>

namespace sar {

namespace {

template <std::size_t N>
void FillPacketFromInput(const SarPulseBlockMessage& input, dsp::IqPacket<float, N>& packet, double sample_rate_hz) {
    packet.packet_number = input.envelope.sequence_id;
    packet.sample_rate_hz = sample_rate_hz;
    packet.carrier_frequency_hz = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        if (i < input.iq_samples.size()) {
            packet.samples[i] = input.iq_samples[i];
        } else {
            packet.samples[i] = std::complex<float>(0.0f, 0.0f);
        }
    }
}

template <std::size_t N>
SarPulseBlockMessage CompressWithManager(const SarPulseBlockMessage& input,
                                         const RangeCompressionConfig& config) {
    static dsp::FFTManager<float, N> manager(1, config.sample_rate_hz, dsp::WindowType::HANN);
    manager.SetAccumulationCount(1);
    manager.SetSampleRate(config.sample_rate_hz);
    manager.SetWindowType(dsp::WindowType::HANN);

    dsp::IqPacket<float, N> packet;
    FillPacketFromInput(input, packet, config.sample_rate_hz);

    auto fft_result = manager.ProcessPacket(packet);

    SarPulseBlockMessage out = input;
    out.iq_samples.assign(input.iq_samples.size(), SarIqSample(0.0f, 0.0f));

    if (!fft_result.has_value()) {
        out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);
        return out;
    }

    const auto& magnitudes = fft_result->magnitudes;
    const std::size_t limit = std::min(out.iq_samples.size(), magnitudes.size());
    for (std::size_t i = 0; i < limit; ++i) {
        out.iq_samples[i] = SarIqSample(magnitudes[i] * config.gain, 0.0f);
    }

    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);
    return out;
}

SarPulseBlockMessage CompressFallback(const SarPulseBlockMessage& input,
                                      const RangeCompressionConfig& config) {
    SarPulseBlockMessage out = input;
    for (auto& sample : out.iq_samples) {
        sample = SarIqSample(std::abs(sample) * config.gain, 0.0f);
    }
    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);
    return out;
}

} // namespace

RangeCompressionNode::RangeCompressionNode(RangeCompressionConfig config)
    : config_(config) {}

std::optional<SarPulseBlockMessage> RangeCompressionNode::Transfer(
    const SarPulseBlockMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled || input.envelope.marker == SarFrameMarker::EndOfStream) {
        return input;
    }

    return CompressWithFft(input);
}

void RangeCompressionNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("enabled")) {
        auto value = cfg.TryGetBool("enabled");
        if (!value) {
            throw value.error();
        }
        config.enabled = value.value();
    }

    if (cfg.Contains("gain")) {
        auto value = cfg.TryGetFloat("gain");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("gain must be >= 0");
        }
        config.gain = value.value();
    }

    if (cfg.Contains("sample_rate_hz")) {
        auto value = cfg.TryGetFloat("sample_rate_hz");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("sample_rate_hz must be > 0");
        }
        config.sample_rate_hz = value.value();
    }

    SetConfig(config);
}

graph::JsonView RangeCompressionNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    parameters_cache_["gain"] = config_.gain;
    parameters_cache_["sample_rate_hz"] = config_.sample_rate_hz;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView RangeCompressionNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> RangeCompressionNode::GetParameterNames() const {
    return {
        "enabled",
        "gain",
        "sample_rate_hz",
    };
}

void RangeCompressionNode::SetConfig(const RangeCompressionConfig& config) {
    config_ = config;
}

const RangeCompressionConfig& RangeCompressionNode::GetConfig() const noexcept {
    return config_;
}

SarPulseBlockMessage RangeCompressionNode::CompressWithFft(const SarPulseBlockMessage& input) const {
    const auto sample_count = input.iq_samples.size();
    if (sample_count == 256u) {
        return CompressWithManager<256>(input, config_);
    }
    if (sample_count == 512u) {
        return CompressWithManager<512>(input, config_);
    }
    if (sample_count == 1024u) {
        return CompressWithManager<1024>(input, config_);
    }

    // Fall back to deterministic magnitude-only compression for unsupported FFT sizes.
    return CompressFallback(input, config_);
}

} // namespace sar
