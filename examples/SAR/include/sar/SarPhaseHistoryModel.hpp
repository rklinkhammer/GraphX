// SPDX-License-Identifier: MIT

/**
 * @file SarPhaseHistoryModel.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace sar {

enum class SarPhaseHistoryOwnership : std::uint8_t {
    OwnedHostBuffer,
    SharedImmutableView,
    DeviceBackedView,
};

enum class SarPhaseHistoryControlMarker : std::uint8_t {
    Data,
    Watermark,
    EndOfStream,
};

enum class SarPhaseHistorySampleFormat : std::uint8_t {
    ComplexFloat32Interleaved,
};

struct SarPhaseHistoryBufferLayout {
    std::uint32_t rank{2};
    std::array<std::uint64_t, 4> shape{0u, 0u, 0u, 0u};
    std::array<std::uint64_t, 4> stride{0u, 0u, 0u, 0u};
};

struct SarPhaseHistoryVector {
    std::uint64_t vector_index{0};
    std::uint64_t channel_id{0};
    double rcv_time_s{0.0};
    std::array<double, 3> platform_position_m{0.0, 0.0, 0.0};
    std::array<double, 3> platform_velocity_mps{0.0, 0.0, 0.0};
    std::vector<std::complex<float>> samples{};
    std::uint64_t sample_payload_hash{0};
};

struct SarPhaseHistorySegment {
    std::uint64_t segment_index{0};
    std::uint64_t channel_id{0};
    std::uint64_t global_vector_start{0};
    std::uint64_t vector_count{0};
    std::uint64_t samples_per_vector{0};
    double carrier_hz{0.0};
    double sample_rate_hz{0.0};
    std::uint64_t payload_hash{0};
    std::uint64_t first_vector_hash{0};
    std::uint64_t last_vector_hash{0};
    std::vector<SarPhaseHistoryVector> vectors{};
};

// Split/merge metadata contract for parallel focused-image formation.
struct SarAperturePartition {
    std::uint64_t partition_id{0};
    std::uint64_t partition_count{0};
    std::uint64_t global_vector_start{0};
    std::uint64_t vector_count{0};
    std::uint64_t ordering_key{0};
    std::uint64_t input_boundary_hash{0};
    std::uint64_t output_boundary_hash{0};
};

struct SarAperturePartitionScheme {
    std::vector<SarAperturePartition> partitions{};
    std::uint64_t expected_partition_count{0};
    std::uint64_t merge_ordering_key{0};
};

struct SarPhaseHistoryApertureFrame {
    std::vector<SarPhaseHistorySegment> segments{};
    std::uint64_t total_vector_count{0};
    std::uint64_t samples_per_vector{0};
    double carrier_hz{0.0};
    double sample_rate_hz{0.0};
    std::uint64_t ordered_set_payload_hash{0};
    std::uint64_t split_boundary_input_hash{0};
    std::uint64_t split_boundary_output_hash{0};
    SarPhaseHistoryOwnership ownership{SarPhaseHistoryOwnership::OwnedHostBuffer};
    SarPhaseHistorySampleFormat sample_format{SarPhaseHistorySampleFormat::ComplexFloat32Interleaved};
    SarPhaseHistoryBufferLayout layout{};
    SarPhaseHistoryControlMarker control_marker{SarPhaseHistoryControlMarker::Data};
    SarAperturePartitionScheme partition_scheme{};
};

struct SarPhaseHistoryControlMessage {
    SarControlToken control{};
    SarPhaseHistoryApertureFrame frame{};
};

} // namespace sar
