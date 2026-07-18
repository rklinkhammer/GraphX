// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

#include "accelgraph/Accelerator.hpp"
#include "config/Config.hpp"
#include "config/ConfigError.hpp"
#include "config/JsonView.hpp"

namespace accelgraph::fhss {

enum class FHSSFallbackPolicy {
    Strict,
    Allow,
};

struct FHSSAccelConfig {
    AcceleratorBackend backend{AcceleratorBackend::Cpu};
    FHSSFallbackPolicy fallback_policy{FHSSFallbackPolicy::Strict};
    bool strict_fallback{true};
    std::string provider_id{"cpu.default"};
    std::string session_key{"graph.default"};
    int cuda_device_ordinal{0};
};

inline std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline const char* BackendToString(AcceleratorBackend backend) {
    switch (backend) {
        case AcceleratorBackend::Cpu:
            return "cpu";
        case AcceleratorBackend::Metal:
            return "metal";
        case AcceleratorBackend::Cuda:
            return "cuda";
    }
    return "cpu";
}

inline std::optional<AcceleratorBackend> BackendFromString(std::string value) {
    value = ToLower(std::move(value));
    if (value == "cpu") {
        return AcceleratorBackend::Cpu;
    }
    if (value == "metal") {
        return AcceleratorBackend::Metal;
    }
    if (value == "cuda") {
        return AcceleratorBackend::Cuda;
    }
    return std::nullopt;
}

inline const char* FallbackPolicyToString(FHSSFallbackPolicy policy) {
    switch (policy) {
        case FHSSFallbackPolicy::Strict:
            return "strict";
        case FHSSFallbackPolicy::Allow:
            return "allow";
    }
    return "strict";
}

inline std::optional<FHSSFallbackPolicy> FallbackPolicyFromString(std::string value) {
    value = ToLower(std::move(value));
    if (value == "strict") {
        return FHSSFallbackPolicy::Strict;
    }
    if (value == "allow") {
        return FHSSFallbackPolicy::Allow;
    }
    return std::nullopt;
}

inline std::string DefaultProviderIdForBackend(AcceleratorBackend backend) {
    return std::string(BackendToString(backend)) + ".default";
}

inline bool ProviderMatchesBackend(const std::string& provider_id, AcceleratorBackend backend) {
    const std::string backend_prefix = std::string(BackendToString(backend)) + ".";
    const std::string lowered = ToLower(provider_id);
    return lowered.rfind(backend_prefix, 0) == 0;
}

inline std::string NormalizeOrValidateProviderId(const std::optional<std::string>& provider_id,
                                                 AcceleratorBackend backend) {
    if (!provider_id.has_value() || provider_id->empty()) {
        return DefaultProviderIdForBackend(backend);
    }
    if (!ProviderMatchesBackend(*provider_id, backend)) {
        throw graph::ConfigError(
            "FHSS accel config provider_id must match backend prefix (cpu.|metal.|cuda.)");
    }
    return *provider_id;
}

inline constexpr std::array<graph::JsonField, 6> FHSSAccelConfigFields() {
    return {
        graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = false,
                         .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                         .enum_values = std::nullopt,
                         .description = "Requested accelerator backend (cpu, metal, cuda)"},
        graph::JsonField{.name = "fallback_policy", .type = graph::JsonType::String, .required = false,
                         .min = std::nullopt, .max = std::nullopt, .default_value = "strict",
                         .enum_values = std::nullopt,
                         .description = "Fallback policy (strict or allow)"},
        graph::JsonField{.name = "strict_fallback", .type = graph::JsonType::Boolean, .required = false,
                         .min = std::nullopt, .max = std::nullopt, .default_value = "true",
                         .enum_values = std::nullopt,
                         .description = "When true, disallow fallback to cpu"},
        graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = false,
                         .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                         .enum_values = std::nullopt,
                         .description = "Accelerator provider id; defaults from backend"},
        graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = false,
                         .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                         .enum_values = std::nullopt,
                         .description = "Accelerator session key"},
        graph::JsonField{.name = "cuda_device_ordinal", .type = graph::JsonType::Integer, .required = false,
                         .min = 0.0, .max = std::nullopt, .default_value = "0",
                         .enum_values = std::nullopt,
                         .description = "CUDA device ordinal for cuda backend"},
    };
}

inline FHSSAccelConfig ParseFHSSAccelConfig(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        throw graph::ConfigError("FHSS accel config must be a JSON object");
    }

    FHSSAccelConfig parsed{};

    if (json.contains("backend")) {
        if (!json["backend"].is_string()) {
            throw graph::ConfigError("FHSS accel config backend must be a string (cpu|metal|cuda)");
        }
        auto backend = BackendFromString(json["backend"].get<std::string>());
        if (!backend.has_value()) {
            throw graph::ConfigError("FHSS accel config backend must be one of: cpu, metal, cuda");
        }
        parsed.backend = backend.value();
    }

    if (json.contains("strict_fallback")) {
        if (!json["strict_fallback"].is_boolean()) {
            throw graph::ConfigError("FHSS accel config strict_fallback must be boolean");
        }
        parsed.strict_fallback = json["strict_fallback"].get<bool>();
        parsed.fallback_policy = parsed.strict_fallback ? FHSSFallbackPolicy::Strict
                                                        : FHSSFallbackPolicy::Allow;
    }

    if (json.contains("fallback_policy")) {
        if (!json["fallback_policy"].is_string()) {
            throw graph::ConfigError("FHSS accel config fallback_policy must be string (strict|allow)");
        }
        auto policy = FallbackPolicyFromString(json["fallback_policy"].get<std::string>());
        if (!policy.has_value()) {
            throw graph::ConfigError("FHSS accel config fallback_policy must be one of: strict, allow");
        }
        parsed.fallback_policy = policy.value();
        parsed.strict_fallback = (parsed.fallback_policy == FHSSFallbackPolicy::Strict);
    }

    if (json.contains("session_key")) {
        if (!json["session_key"].is_string()) {
            throw graph::ConfigError("FHSS accel config session_key must be a string");
        }
        parsed.session_key = json["session_key"].get<std::string>();
        if (parsed.session_key.empty()) {
            throw graph::ConfigError("FHSS accel config session_key must not be empty");
        }
    }

    if (json.contains("cuda_device_ordinal")) {
        if (!json["cuda_device_ordinal"].is_number_integer()) {
            throw graph::ConfigError("FHSS accel config cuda_device_ordinal must be an integer >= 0");
        }
        parsed.cuda_device_ordinal = json["cuda_device_ordinal"].get<int>();
        if (parsed.cuda_device_ordinal < 0) {
            throw graph::ConfigError("FHSS accel config cuda_device_ordinal must be >= 0");
        }
    }

    std::optional<std::string> provider;
    if (json.contains("provider_id")) {
        if (!json["provider_id"].is_string()) {
            throw graph::ConfigError("FHSS accel config provider_id must be a string");
        }
        provider = json["provider_id"].get<std::string>();
    }
    parsed.provider_id = NormalizeOrValidateProviderId(provider, parsed.backend);

    return parsed;
}

}  // namespace accelgraph::fhss
