// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace accelgraph {

struct IqFrameToken {
    std::vector<float> interleaved_iq;
    std::size_t complex_sample_count{0};
    double sample_rate_hz{48000.0};
    double tone_frequency_hz{1000.0};
    std::uint64_t frame_index{0};
};

struct SpectrumFrameToken {
    std::vector<float> magnitudes;
    std::size_t fft_size{0};
    std::size_t peak_bin{0};
    double peak_frequency_hz{0.0};
    std::string requested_backend{"cpu"};
    std::string execution_backend{"cpu"};
    bool fallback_used{false};
    std::string fallback_reason{"none"};
};

}  // namespace accelgraph
