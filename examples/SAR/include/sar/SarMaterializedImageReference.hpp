// SPDX-License-Identifier: MIT

/**
 * @file SarMaterializedImageReference.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarCpuReference.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sar::detail {

inline std::vector<float> BuildDeterministicRangeTile(std::uint64_t sequence_id,
                                                       std::uint32_t tile_id,
                                                       std::size_t element_count) {
    const auto sample_count = std::max<std::size_t>(1u, element_count);
    std::vector<float> range_tile(sample_count, 0.0f);

    const float sequence_phase = static_cast<float>(sequence_id % 4096u) * 1.0e-3f;
    const float tile_phase = static_cast<float>(tile_id % 128u) * 3.5e-2f;

    for (std::size_t i = 0; i < sample_count; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(sample_count);
        const float harmonic = std::sin((x * 8.0f) + sequence_phase);
        const float envelope = 0.8f + (0.2f * std::cos((x * 5.0f) + tile_phase));
        range_tile[i] = envelope * harmonic;
    }

    return range_tile;
}

inline std::vector<float> BuildReferenceMaterializedImage(std::uint64_t sequence_id,
                                                           std::uint32_t tile_id,
                                                           std::size_t element_count,
                                                           const reference::BackprojectionAdapterConfig& config) {
    const auto range_tile = BuildDeterministicRangeTile(sequence_id, tile_id, element_count);
    return reference::RunBackprojectionAdapterReference(range_tile, config);
}

} // namespace sar::detail
