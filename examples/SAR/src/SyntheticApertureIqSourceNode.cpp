#include "sar/SyntheticApertureIqSourceNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
#include <atomic>

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

std::uint64_t NextOpaqueTokenId() {
    static std::atomic<std::uint64_t> next_id{1u};
    return next_id.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

SyntheticApertureIqSourceNode::SyntheticApertureIqSourceNode(
    SyntheticApertureIqSourceConfig config)
    : config_(config) {}

std::optional<SarAccelControlToken> SyntheticApertureIqSourceNode::Produce(
    std::integral_constant<std::size_t, 0>) {
    if (eos_emitted_) {
        return std::nullopt;
    }

    if (next_sequence_id_ >= config_.total_pulses) {
        eos_emitted_ = true;
        return MakeEndOfStreamToken();
    }

    SarAccelControlToken out = MakeDataToken();
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

SarAccelControlToken SyntheticApertureIqSourceNode::MakeDataToken() const {
    SarAccelControlToken out{};
    out.token_id = NextOpaqueTokenId();
    out.sidecar.sequence_id = next_sequence_id_;
    out.sidecar.batch_id = config_.stream_id;
    out.sidecar.aperture_id = next_sequence_id_;
    out.sidecar.pulse_range_start = next_sequence_id_;
    out.sidecar.pulse_range_count = 1;
    out.sidecar.stream_id = config_.stream_id;
    out.sidecar.tile_id = 0;
    out.sidecar.tile_count = 1;
    out.sidecar.backend_id = config_.backend_id;
    out.sidecar.backend = config_.backend;
    out.sidecar.marker = SarFrameMarker::Data;
    out.sidecar.synthetic = true;
    out.sidecar.payload_byte_count =
        static_cast<std::size_t>(config_.samples_per_pulse) * sizeof(float);

    auto& host_view = out.host_view;
    const auto backend = ToAccelBackendKind(config_.backend);
    host_view.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    // host_ptr is opaque transport metadata only. SAR identity derives from sidecar.
    host_view.host_ptr = runtime::OpaqueHostPointer();
    host_view.bytes = static_cast<std::uint64_t>(out.sidecar.payload_byte_count);
    host_view.dtype = graph::gpu::accel::DataType::Float32;
    host_view.layout =
        MakeAccelVectorLayout(static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, config_.samples_per_pulse)));
    host_view.allocator_id = static_cast<std::uint64_t>(config_.backend_id) + 1u;
    out.has_host_view = true;
    return out;
}

SarAccelControlToken SyntheticApertureIqSourceNode::MakeEndOfStreamToken() const {
    SarAccelControlToken out{};
    out.token_id = NextOpaqueTokenId();
    out.sidecar.sequence_id = next_sequence_id_;
    out.sidecar.batch_id = config_.stream_id;
    out.sidecar.aperture_id = next_sequence_id_;
    out.sidecar.pulse_range_start = next_sequence_id_;
    out.sidecar.pulse_range_count = 0;
    out.sidecar.stream_id = config_.stream_id;
    out.sidecar.tile_id = 0;
    out.sidecar.tile_count = 1;
    out.sidecar.backend_id = config_.backend_id;
    out.sidecar.backend = config_.backend;
    out.sidecar.marker = SarFrameMarker::EndOfStream;
    out.sidecar.synthetic = true;
    out.sidecar.payload_byte_count = 0u;

    auto& host_view = out.host_view;
    const auto backend = ToAccelBackendKind(config_.backend);
    host_view.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    // host_ptr is opaque transport metadata only. SAR identity derives from sidecar.
    host_view.host_ptr = runtime::OpaqueHostPointer();
    host_view.bytes = static_cast<std::uint64_t>(sizeof(float));
    host_view.dtype = graph::gpu::accel::DataType::Float32;
    host_view.layout = MakeAccelVectorLayout(1u);
    host_view.allocator_id = static_cast<std::uint64_t>(config_.backend_id) + 1u;
    out.has_host_view = true;
    return out;
}

} // namespace sar
