#pragma once

#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarIoUtilities.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

struct CrsdWriterOptions {
    std::vector<std::string> assumptions{
        "standards_targeted_crsd_metadata",
        "standards_targeted_signal_array",
        "standards_targeted_pvp",
    };
    std::vector<std::string> warnings{};
};

class CrsdWriter final : public ISarWriter {
public:
    explicit CrsdWriter(CrsdWriterOptions options = {})
        : options_(std::move(options)) {}

    [[nodiscard]] SarWriteResult Write(
        const std::filesystem::path& output_directory,
        const NormalizedSarProduct& product) const override {
        (void)output_directory;
        if (!product.HasRequiredFields()) {
            return SarWriteResult{
                .success = false,
                .message = "missing_required_fields",
            };
        }

        return SarWriteResult{
            .success = false,
            .message = kUnavailableMessage,
        };
    }

    [[nodiscard]] static std::string ComputeSignalChecksum(
        const std::filesystem::path& signal_path) {
        return SarIoUtilities::ComputeFileChecksumFNV1a64(signal_path);
    }

    static constexpr const char* kFormatName = "crsd";
    static constexpr const char* kStandardsTargetedLabel = "STANDARDS-TARGETED";
    static constexpr const char* kSignalFile = "signal.bin";
    static constexpr const char* kMetadataFile = "metadata.json";
    static constexpr const char* kPvpFile = "pvp.json";
    static constexpr const char* kProvenanceFile = "provenance.json";
    static constexpr const char* kChunkIndexFile = "chunk_index.json";
    static constexpr const char* kMetadataSchema = "graphx.sar.crsd.metadata.v1";
    static constexpr const char* kPvpSchema = "graphx.sar.crsd.pvp.v1";
    static constexpr const char* kProvenanceSchema = "graphx.sar.crsd.provenance.v1";
    static constexpr const char* kChunkIndexSchema = "graphx.sar.crsd.chunk_index.v1";
    static constexpr const char* kUnavailableMessage =
        "crsd_writer_unavailable:valid_sarpy_openable_crsd_writer_not_implemented";

private:
    CrsdWriterOptions options_{};
};

} // namespace graphx::sar
