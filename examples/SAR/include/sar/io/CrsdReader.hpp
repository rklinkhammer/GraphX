#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace graphx::sar {

struct CrsdVectorRecord {
    std::uint64_t vector_index{0};
    std::uint64_t channel_id{0};
    double rcv_time_s{0.0};
    std::array<double, 3> platform_position_m{0.0, 0.0, 0.0};
    std::array<double, 3> platform_velocity_mps{0.0, 0.0, 0.0};
    std::vector<std::complex<float>> signal{};
};

struct CrsdSegmentRecord {
    std::filesystem::path crsd_path{};
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
    std::vector<CrsdVectorRecord> vectors{};
    CrsdVectorRecord first_vector{};
    CrsdVectorRecord last_vector{};
};

struct OrderedCrsdSetReadResult {
    std::vector<CrsdSegmentRecord> segments{};
    std::uint64_t total_vector_count{0};
    std::uint64_t ordered_set_payload_hash{0};
};

struct CrsdReadOptions {
    std::vector<std::filesystem::path> ordered_crsd_paths{};
    std::filesystem::path crsd_directory{};
    std::filesystem::path manifest_path{};
    bool require_contiguous_segment_indices{true};
};

struct CrsdReadResult {
    bool success{false};
    std::string diagnostic{};
    OrderedCrsdSetReadResult value{};
};

class ICrsdReader {
public:
    virtual ~ICrsdReader() = default;
    [[nodiscard]] virtual CrsdReadResult ReadOrderedSet(const CrsdReadOptions& options) const = 0;
};

class CrsdReader final : public ICrsdReader {
public:
    [[nodiscard]] CrsdReadResult ReadOrderedSet(const CrsdReadOptions& options) const override;
};

using CrsdReaderPtr = std::shared_ptr<ICrsdReader>;

} // namespace graphx::sar
