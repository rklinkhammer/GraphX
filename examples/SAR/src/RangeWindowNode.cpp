#include "sar/RangeWindowNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"

#include <chrono>

namespace sar {

namespace {

} // namespace

RangeWindowNode::RangeWindowNode(RangeWindowConfig config)
    : config_(config) {}

std::optional<SarAccelControlToken> RangeWindowNode::Transfer(
    const SarAccelControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    SarAccelControlToken out = input;
    if (!config_.enabled || input.sidecar.marker == SarFrameMarker::EndOfStream) {
        return out;
    }

    const auto stage_start = runtime::SteadyClock::now();
    out.sidecar.stage_timings.range_window_time_us += runtime::ElapsedUs(stage_start);
    return out;
}

void RangeWindowNode::Configure(const graph::JsonView& cfg) {
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

    SetConfig(config);
}

graph::JsonView RangeWindowNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    parameters_cache_["gain"] = config_.gain;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView RangeWindowNode::GetParameterDescription(
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

std::vector<std::string> RangeWindowNode::GetParameterNames() const {
    return {
        "enabled",
        "gain",
    };
}

void RangeWindowNode::SetConfig(const RangeWindowConfig& config) {
    config_ = config;
}

const RangeWindowConfig& RangeWindowNode::GetConfig() const noexcept {
    return config_;
}

} // namespace sar
