// SPDX-License-Identifier: MIT

/**
 * @file SarProductChunker.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace graphx::sar {

struct SarChunkerOptions {
    std::uint64_t max_chunk_bytes{0};
    std::string output_prefix{"gotcha_crsd_chunk"};
};

struct SarChunkPlanEntry {
    std::size_t chunk_index{0};
    std::size_t pulse_start{0};
    std::size_t pulse_end{0};
    std::uint64_t estimated_bytes{0};
    std::string output_stem{};
};

struct SarChunkPlan {
    std::vector<SarChunkPlanEntry> chunks{};
    std::vector<std::string> warnings{};
};

/**
 * @class SarProductChunker
 * @brief SarProductChunker class.
 */
class SarProductChunker {
public:
    [[nodiscard]] static SarChunkPlan BuildPlan(
        const NormalizedSarProduct& product,
        const SarChunkerOptions& options) {
        SarChunkPlan plan{};

        const auto pulse_count = product.Shape().pulse_count;
        if (pulse_count == 0) {
            return plan;
        }

        std::size_t chunk_index = 0;
        std::size_t pulse_start = 0;
        std::uint64_t current_bytes = 0;

        for (std::size_t pulse_index = 0; pulse_index < pulse_count; ++pulse_index) {
            const auto pulse_bytes = EstimatePulseBytes(product, pulse_index);
            const bool exceeds_cap =
                options.max_chunk_bytes > 0 && pulse_bytes > options.max_chunk_bytes;
            if (exceeds_cap) {
                plan.warnings.push_back(
                    "pulse_exceeds_max_chunk_bytes:pulse=" + std::to_string(pulse_index) +
                    ",bytes=" + std::to_string(pulse_bytes));
            }

            const bool has_cap = options.max_chunk_bytes > 0;
            const bool would_overflow =
                has_cap && current_bytes > 0 && current_bytes + pulse_bytes > options.max_chunk_bytes;

            if (would_overflow) {
                plan.chunks.push_back(SarChunkPlanEntry{
                    .chunk_index = chunk_index,
                    .pulse_start = pulse_start,
                    .pulse_end = pulse_index - 1,
                    .estimated_bytes = current_bytes,
                    .output_stem = MakeChunkOutputStem(options.output_prefix, chunk_index),
                });
                ++chunk_index;
                pulse_start = pulse_index;
                current_bytes = 0;
            }

            current_bytes += pulse_bytes;
        }

        plan.chunks.push_back(SarChunkPlanEntry{
            .chunk_index = chunk_index,
            .pulse_start = pulse_start,
            .pulse_end = pulse_count - 1,
            .estimated_bytes = current_bytes,
            .output_stem = MakeChunkOutputStem(options.output_prefix, chunk_index),
        });

        return plan;
    }

    [[nodiscard]] static std::uint64_t EstimatePulseBytes(
        const NormalizedSarProduct& product,
        std::size_t pulse_index) {
        std::uint64_t total = 0;
        for (const auto& channel : product.channels) {
            if (pulse_index >= channel.pulses.size()) {
                continue;
            }
            const auto& pulse = channel.pulses[pulse_index];
            total += static_cast<std::uint64_t>(pulse.samples.size()) * 2U * sizeof(float);
        }
        return total;
    }

    [[nodiscard]] static std::string MakeChunkOutputStem(
        const std::string& prefix,
        std::size_t chunk_index) {
        std::ostringstream oss;
        oss << prefix << "_" << std::setfill('0') << std::setw(4) << chunk_index;
        return oss.str();
    }
};

} // namespace graphx::sar
