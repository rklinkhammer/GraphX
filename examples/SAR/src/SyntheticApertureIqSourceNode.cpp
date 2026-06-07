#include "sar/SyntheticApertureIqSourceNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
#include <cmath>

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

} // namespace

SyntheticApertureIqSourceNode::SyntheticApertureIqSourceNode(
    SyntheticApertureIqSourceConfig config)
    : config_(config) {}

std::optional<SarPulseBlockMessage> SyntheticApertureIqSourceNode::Produce(
    std::integral_constant<std::size_t, 0>) {
    if (eos_emitted_) {
        return std::nullopt;
    }

    if (next_sequence_id_ >= config_.total_pulses) {
        eos_emitted_ = true;
        return MakeEndOfStreamMessage();
    }

    SarPulseBlockMessage out = MakeDataMessage();
    ++next_sequence_id_;
    return out;
}

void SyntheticApertureIqSourceNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("stream_id")) {
        auto value = cfg.TryGetInt("stream_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("stream_id must be >= 0");
        }
        config.stream_id = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("total_pulses")) {
        auto value = cfg.TryGetInt("total_pulses");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("total_pulses must be > 0");
        }
        config.total_pulses = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("samples_per_pulse")) {
        auto value = cfg.TryGetInt("samples_per_pulse");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("samples_per_pulse must be > 0");
        }
        config.samples_per_pulse = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("backend_id")) {
        auto value = cfg.TryGetInt("backend_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("backend_id must be >= 0");
        }
        config.backend_id = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    if (cfg.Contains("moving_target_enabled")) {
        auto value = cfg.TryGetBool("moving_target_enabled");
        if (!value) {
            throw value.error();
        }
        config.moving_target_enabled = value.value();
    }

    if (cfg.Contains("target_initial_range_m")) {
        auto value = cfg.TryGetFloat("target_initial_range_m");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("target_initial_range_m must be > 0");
        }
        config.target_initial_range_m = value.value();
    }

    if (cfg.Contains("target_closing_velocity_mps")) {
        auto value = cfg.TryGetFloat("target_closing_velocity_mps");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("target_closing_velocity_mps must be >= 0");
        }
        config.target_closing_velocity_mps = value.value();
    }

    if (cfg.Contains("pulse_interval_s")) {
        auto value = cfg.TryGetFloat("pulse_interval_s");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0.0f) {
            throw graph::ConfigError("pulse_interval_s must be > 0");
        }
        config.pulse_interval_s = value.value();
    }

    if (cfg.Contains("target_reflectivity")) {
        auto value = cfg.TryGetFloat("target_reflectivity");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("target_reflectivity must be >= 0");
        }
        config.target_reflectivity = value.value();
    }

    SetConfig(config);
}

graph::JsonView SyntheticApertureIqSourceNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["stream_id"] = config_.stream_id;
    parameters_cache_["total_pulses"] = config_.total_pulses;
    parameters_cache_["samples_per_pulse"] = config_.samples_per_pulse;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    parameters_cache_["moving_target_enabled"] = config_.moving_target_enabled;
    parameters_cache_["target_initial_range_m"] = config_.target_initial_range_m;
    parameters_cache_["target_closing_velocity_mps"] = config_.target_closing_velocity_mps;
    parameters_cache_["pulse_interval_s"] = config_.pulse_interval_s;
    parameters_cache_["target_reflectivity"] = config_.target_reflectivity;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SyntheticApertureIqSourceNode::GetParameterDescription(
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

std::vector<std::string> SyntheticApertureIqSourceNode::GetParameterNames() const {
    return {
        "stream_id",
        "total_pulses",
        "samples_per_pulse",
        "backend_id",
        "backend",
        "moving_target_enabled",
        "target_initial_range_m",
        "target_closing_velocity_mps",
        "pulse_interval_s",
        "target_reflectivity",
    };
}

void SyntheticApertureIqSourceNode::Reset() {
    next_sequence_id_ = 0;
    eos_emitted_ = false;
}

void SyntheticApertureIqSourceNode::SetConfig(const SyntheticApertureIqSourceConfig& config) {
    config_ = config;
    Reset();
}

const SyntheticApertureIqSourceConfig& SyntheticApertureIqSourceNode::GetConfig() const noexcept {
    return config_;
}

SarIqSample SyntheticApertureIqSourceNode::MakeSample(
    std::uint64_t sequence_id,
    std::uint32_t sample_index) const {
    const float seq = static_cast<float>(sequence_id);
    const float idx = static_cast<float>(sample_index);

    float real = seq + (idx * 0.001f);
    float imag = (seq * 0.25f) - (idx * 0.0015f);

    if (config_.moving_target_enabled) {
        const float pulse_time_s = seq * config_.pulse_interval_s;
        const float range_m = std::max(
            1.0f,
            config_.target_initial_range_m - (config_.target_closing_velocity_mps * pulse_time_s));
        const float normalized_range = config_.target_initial_range_m / range_m;
        const float amp = 1.0f + ((normalized_range - 1.0f) * config_.target_reflectivity);
        const float phase = (range_m * 0.0005f) + (idx * 0.01f);

        real += amp * std::cos(phase);
        imag += 0.5f * amp * std::sin(phase);
    }

    return SarIqSample(real, imag);
}

SarPulseBlockMessage SyntheticApertureIqSourceNode::MakeDataMessage() const {
    SarPulseBlockMessage out{};
    out.envelope.sequence_id = next_sequence_id_;
    out.envelope.stream_id = config_.stream_id;
    out.envelope.tile_id = 0;
    out.envelope.tile_count = 1;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::Data;
    out.envelope.synthetic = true;

    out.buffer.buffer_id = next_sequence_id_;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;

    out.iq_samples.reserve(config_.samples_per_pulse);
    for (std::uint32_t i = 0; i < config_.samples_per_pulse; ++i) {
        out.iq_samples.push_back(MakeSample(next_sequence_id_, i));
    }
    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);

    return out;
}

SarPulseBlockMessage SyntheticApertureIqSourceNode::MakeEndOfStreamMessage() const {
    SarPulseBlockMessage out{};
    out.envelope.sequence_id = next_sequence_id_;
    out.envelope.stream_id = config_.stream_id;
    out.envelope.tile_id = 0;
    out.envelope.tile_count = 1;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::EndOfStream;
    out.envelope.synthetic = true;

    out.buffer.buffer_id = next_sequence_id_;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    out.buffer.byte_count = 0;

    return out;
}

} // namespace sar
