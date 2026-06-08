#include "sar/RangeCompressionNode.hpp"

#include "config/ConfigError.hpp"
#include "dsp/FFTManager.hpp"
#include "dsp/IqPacket.hpp"
#include "sar/SarCpuReference.hpp"

#include <algorithm>
#include <array>
#include <complex>
#include <string>

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

RangeCompressionMode ParseMode(const std::string& value) {
    if (value == "fft_magnitude") {
        return RangeCompressionMode::FftMagnitude;
    }
    if (value == "matched_filter") {
        return RangeCompressionMode::MatchedFilter;
    }
    throw graph::ConfigError("mode must be one of: fft_magnitude, matched_filter");
}

RangeCompressionOutput ParseOutput(const std::string& value) {
    if (value == "magnitude") {
        return RangeCompressionOutput::Magnitude;
    }
    if (value == "complex") {
        return RangeCompressionOutput::Complex;
    }
    throw graph::ConfigError("output must be one of: magnitude, complex");
}

std::string ModeToString(RangeCompressionMode mode) {
    switch (mode) {
        case RangeCompressionMode::FftMagnitude:
            return "fft_magnitude";
        case RangeCompressionMode::MatchedFilter:
            return "matched_filter";
    }
    return "fft_magnitude";
}

std::string OutputToString(RangeCompressionOutput output) {
    switch (output) {
        case RangeCompressionOutput::Magnitude:
            return "magnitude";
        case RangeCompressionOutput::Complex:
            return "complex";
    }
    return "magnitude";
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

    if (config_.mode == RangeCompressionMode::MatchedFilter) {
        return CompressWithMatchedFilter(input);
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

    if (cfg.Contains("mode")) {
        auto value = cfg.TryGetString("mode");
        if (!value) {
            throw value.error();
        }
        config.mode = ParseMode(value.value());
    }

    if (cfg.Contains("output")) {
        auto value = cfg.TryGetString("output");
        if (!value) {
            throw value.error();
        }
        config.output = ParseOutput(value.value());
    }

    if (cfg.Contains("bandwidth_hz")) {
        auto value = cfg.TryGetFloat("bandwidth_hz");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("bandwidth_hz must be > 0");
        }
        config.bandwidth_hz = value.value();
    }

    if (cfg.Contains("chirp_duration_s")) {
        auto value = cfg.TryGetFloat("chirp_duration_s");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("chirp_duration_s must be > 0");
        }
        config.chirp_duration_s = value.value();
    }

    if (cfg.Contains("range_origin_m")) {
        auto value = cfg.TryGetFloat("range_origin_m");
        if (!value) {
            throw value.error();
        }
        config.range_origin_m = value.value();
    }

    if (cfg.Contains("range_spacing_m")) {
        auto value = cfg.TryGetFloat("range_spacing_m");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("range_spacing_m must be > 0");
        }
        config.range_spacing_m = value.value();
    }

    if (config.mode == RangeCompressionMode::MatchedFilter) {
        if (!cfg.Contains("sample_rate_hz") || !cfg.Contains("bandwidth_hz") ||
            !cfg.Contains("chirp_duration_s") || !cfg.Contains("range_spacing_m")) {
            throw graph::ConfigError(
                "matched_filter mode requires sample_rate_hz, bandwidth_hz, chirp_duration_s, and range_spacing_m");
        }
    }

    SetConfig(config);
}

graph::JsonView RangeCompressionNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    parameters_cache_["gain"] = config_.gain;
    parameters_cache_["sample_rate_hz"] = config_.sample_rate_hz;
    parameters_cache_["mode"] = ModeToString(config_.mode);
    parameters_cache_["output"] = OutputToString(config_.output);
    parameters_cache_["bandwidth_hz"] = config_.bandwidth_hz;
    parameters_cache_["chirp_duration_s"] = config_.chirp_duration_s;
    parameters_cache_["range_origin_m"] = config_.range_origin_m;
    parameters_cache_["range_spacing_m"] = config_.range_spacing_m;
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
        "mode",
        "output",
        "bandwidth_hz",
        "chirp_duration_s",
        "range_origin_m",
        "range_spacing_m",
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

SarPulseBlockMessage RangeCompressionNode::CompressWithMatchedFilter(
    const SarPulseBlockMessage& input) const {
    sar::reference::ChirpReferenceConfig reference_config{};
    reference_config.sample_count = static_cast<std::uint32_t>(input.iq_samples.size());
    reference_config.sample_rate_hz = config_.sample_rate_hz;
    reference_config.bandwidth_hz = config_.bandwidth_hz;
    reference_config.chirp_duration_s = config_.chirp_duration_s;
    reference_config.range_origin_m = config_.range_origin_m;
    reference_config.range_spacing_m = config_.range_spacing_m;

    const auto reference_chirp = sar::reference::GenerateLinearFmChirp(reference_config);
    std::vector<std::complex<double>> received;
    received.reserve(input.iq_samples.size());
    for (const auto& sample : input.iq_samples) {
        received.emplace_back(static_cast<double>(sample.real()), static_cast<double>(sample.imag()));
    }

    const auto compressed =
        sar::reference::MatchedFilterRangeCompress(received, reference_chirp);

    SarPulseBlockMessage out = input;
    out.iq_samples.assign(input.iq_samples.size(), SarIqSample(0.0f, 0.0f));
    for (std::size_t i = 0; i < compressed.size(); ++i) {
        if (config_.output == RangeCompressionOutput::Complex) {
            out.iq_samples[i] = SarIqSample(
                static_cast<float>(compressed[i].real() * config_.gain),
                static_cast<float>(compressed[i].imag() * config_.gain));
        } else {
            out.iq_samples[i] =
                SarIqSample(static_cast<float>(std::abs(compressed[i]) * config_.gain), 0.0f);
        }
    }
    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);
    return out;
}

} // namespace sar
