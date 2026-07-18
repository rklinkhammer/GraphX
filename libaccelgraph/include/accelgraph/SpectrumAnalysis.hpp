// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

enum class SpectrumRequestedBackend {
    Cpu,
    Metal,
    Cuda,
    Auto,
};

enum class SpectrumFallbackPolicy {
    Strict,
    Allow,
};

struct SpectrumAnalysisRequest {
    std::span<const float> interleaved_iq;
    std::size_t complex_sample_count{0};
    double sample_rate_hz{48000.0};
    std::size_t output_bins{0};
    SpectrumRequestedBackend requested_backend{SpectrumRequestedBackend::Auto};
    SpectrumFallbackPolicy fallback_policy{SpectrumFallbackPolicy::Strict};
};

struct SpectrumAnalysisResult {
    std::vector<float> magnitudes;
    std::size_t peak_bin{0};
    double peak_frequency_hz{0.0};
    AcceleratorBackend execution_backend{AcceleratorBackend::Cpu};
    bool fallback_used{false};
    std::string fallback_reason{"none"};
};

class ISpectrumAnalysisOperation {
public:
    virtual ~ISpectrumAnalysisOperation() = default;

    [[nodiscard]] virtual std::expected<SpectrumAnalysisResult, AcceleratorError>
    Analyze(const SpectrumAnalysisRequest& request) = 0;
};

}  // namespace accelgraph
