// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "accelgraph/Accelerator.hpp"

namespace accelgraph {

struct DeterministicIqPacket {
    std::vector<float> i_samples;
    std::vector<float> q_samples;
    std::size_t sample_count{0};
    std::uint64_t packet_number{0};
    double sample_rate_hz{48000.0};
    double tone_frequency_hz{1000.0};
    float amplitude{1.0F};
    float phase_radians{0.0F};
};

struct MagnitudeSpectrumPacket {
    std::vector<float> magnitudes;
    std::size_t fft_size{0};
    std::size_t peak_bin{0};
    double peak_frequency_hz{0.0};
    float peak_magnitude{0.0F};
    double sample_rate_hz{0.0};
    std::uint64_t packet_number{0};
    AcceleratorBackend requested_backend{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend{AcceleratorBackend::Cpu};
    bool used_fallback{false};
    std::string fallback_diagnostic;
};

}  // namespace accelgraph