// SPDX-License-Identifier: MIT

/**
 * @file RangeCompressionNode.cpp
 * @brief GraphX source file.
 */

#include "sar/RangeCompressionNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"

#include <chrono>
#include <string>

namespace sar {

namespace {

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

std::optional<SarControlToken> RangeCompressionNode::Transfer(
    const SarControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled || input.sidecar.marker == SarFrameMarker::EndOfStream) {
        return input;
    }

    const auto stage_start = runtime::SteadyClock::now();
    auto out = input;
    out.sidecar.stage_timings.range_compression_time_us += runtime::ElapsedUs(stage_start);
    return out;
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

} // namespace sar
