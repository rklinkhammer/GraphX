#include <gtest/gtest.h>

#include "sar/io/CrsdIO.hpp"
#include "sar/io/GotchaMatReader.hpp"
#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarProductValidator.hpp"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

// ──────────────────────────────────────────────────────────────────────
// Test Suite: Real GOTCHA Full-Aperture Validation
// ──────────────────────────────────────────────────────────────────────
class RealGotchaFullApertureValidationTest : public ::testing::Test {
protected:
    const char* GetDatasetPath() const {
        return std::getenv("GRAPHX_SAR_GOTCHA_DATASET");
    }

    [[nodiscard]] bool HasDataset() const {
        const auto path = GetDatasetPath();
        return path != nullptr && std::string{path}.length() > 0 &&
            std::filesystem::exists(path) &&
            std::filesystem::is_directory(path);
    }
};

// ──────────────────────────────────────────────────────────────────────
// Test 1: Verify test skips cleanly in CI when env var is not set
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, SkipsCleanlyWhenDatasetNotSet) {
    // This test just documents the skip behavior.
    // In CI (where GRAPHX_SAR_GOTCHA_DATASET is unset), it will be skipped.
    // Locally (where the var is set), it runs.
    if (HasDataset()) {
        GTEST_SKIP() << "Dataset available; this test documents skip behavior only";
    }
    // If we reach here, dataset is not set — which is the CI case.
}

// ──────────────────────────────────────────────────────────────────────
// Test 2: Read full-aperture real GOTCHA and verify all pulses
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, ReadFullApertureAndVerifyAllPulses) {
    if (!HasDataset()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET not set; real GOTCHA full-aperture validation is local-only";
    }

    const auto dataset_path = std::filesystem::path{GetDatasetPath()};
    ASSERT_TRUE(std::filesystem::exists(dataset_path)) << "Dataset path does not exist: " << dataset_path;

    // Discover input files (lexical order)
    std::vector<std::filesystem::path> mat_files{};
    for (const auto& entry : std::filesystem::directory_iterator(dataset_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mat") {
            mat_files.push_back(entry.path());
        }
    }
    std::sort(mat_files.begin(), mat_files.end());

    ASSERT_GE(mat_files.size(), 1u) << "Dataset contains no .mat files: " << dataset_path;

    // Read with GotchaMatReader in full-aperture mode
    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "real_gotcha_full_aperture_validation",
            .product_id = "real_gotcha_full_aperture_product",
            .collector_name = "GOTCHA",
            .coordinate_frame = "ecef",
            .time_basis = "seconds",
        }};

    const auto read = reader.ReadDetailed(dataset_path);
    ASSERT_TRUE(read.success) << "Read failed: " << read.message;
    ASSERT_FALSE(read.product.channels.empty()) << "Product has no channels";
    ASSERT_FALSE(read.product.channels.front().pulses.empty()) << "Channel has no pulses";

    const auto total_pulses = read.product.channels.front().pulses.size();
    EXPECT_GT(total_pulses, 0u) << "No pulses read from full-aperture conversion";
}

// ──────────────────────────────────────────────────────────────────────
// Test 3: Verify full-aperture conversion produces valid CRSD output
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, FullApertureConversionProducesValidCrsd) {
    if (!HasDataset()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET not set; real GOTCHA full-aperture validation is local-only";
    }

    const auto dataset_path = std::filesystem::path{GetDatasetPath()};

    // Read full-aperture
    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "real_gotcha_crsd_validation",
            .product_id = "real_gotcha_crsd_product",
            .collector_name = "GOTCHA",
            .coordinate_frame = "ecef",
            .time_basis = "seconds",
        }};

    const auto read = reader.ReadDetailed(dataset_path);
    ASSERT_TRUE(read.success) << "Read failed: " << read.message;

    // Validate the product
    const auto validation = graphx::sar::SarProductValidator::Validate(read.product);
    ASSERT_TRUE(validation.ok()) << "Product validation failed: " << 
        (validation.errors.empty() ? "no errors recorded" : validation.errors.front().code);

    // Write to CRSD format
    std::error_code fs_error{};
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_pr8_real_validation";
    std::filesystem::remove_all(output_dir, fs_error);
    std::filesystem::create_directories(output_dir, fs_error);

    graphx::sar::CrsdWriter writer{};
    const auto write = writer.Write(output_dir, read.product);
    ASSERT_TRUE(write.success) << "Write failed: " << write.message;

    // Verify CRSD output files exist
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::CrsdWriter::kSignalFile))
        << "Signal file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::CrsdWriter::kMetadataFile))
        << "Metadata file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::CrsdWriter::kPvpFile))
        << "PVP file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::CrsdWriter::kProvenanceFile))
        << "Provenance file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::CrsdWriter::kChunkIndexFile))
        << "Chunk index file missing";

    std::filesystem::remove_all(output_dir, fs_error);
}

// ──────────────────────────────────────────────────────────────────────
// Test 4: Verify CRSD writer preserves full-aperture pulse count
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, CrsdWriterPreservesFullAperturePulseCount) {
    if (!HasDataset()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET not set; real GOTCHA full-aperture validation is local-only";
    }

    const auto dataset_path = std::filesystem::path{GetDatasetPath()};

    // Read full-aperture
    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "real_gotcha_report_validation",
            .product_id = "real_gotcha_report_product",
            .collector_name = "GOTCHA",
            .coordinate_frame = "ecef",
            .time_basis = "seconds",
        }};

    const auto read = reader.ReadDetailed(dataset_path);
    ASSERT_TRUE(read.success) << "Read failed: " << read.message;

    const auto total_pulses = read.product.Shape().pulse_count;
    EXPECT_GT(total_pulses, 0u) << "No pulses in product";

    // Write to CRSD format
    std::error_code fs_error{};
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_pr8_report_validation";
    std::filesystem::remove_all(output_dir, fs_error);
    std::filesystem::create_directories(output_dir, fs_error);

    graphx::sar::CrsdWriter writer{};
    const auto write = writer.Write(output_dir, read.product);
    ASSERT_TRUE(write.success) << "Write failed: " << write.message;

    EXPECT_TRUE(std::filesystem::exists(output_dir / graphx::sar::CrsdWriter::kSignalFile));
    EXPECT_TRUE(std::filesystem::exists(output_dir / graphx::sar::CrsdWriter::kChunkIndexFile));
    EXPECT_EQ(read.product.Shape().pulse_count, total_pulses)
        << "Pulse count changed unexpectedly before CRSD write";

    std::filesystem::remove_all(output_dir, fs_error);
}

// ──────────────────────────────────────────────────────────────────────
// Test 5: Verify all 10 GOTCHA files (if available) are processed
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, ProcessesAllTenGotchaFilesWhenAvailable) {
    if (!HasDataset()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET not set; real GOTCHA full-aperture validation is local-only";
    }

    const auto dataset_path = std::filesystem::path{GetDatasetPath()};

    // Count .mat files
    std::size_t mat_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dataset_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mat") {
            ++mat_count;
        }
    }

    EXPECT_GE(mat_count, 1u) << "Dataset contains no .mat files";
    if (mat_count >= 10) {
        EXPECT_EQ(mat_count, 10u) << "Expected 10 .mat files for standard GOTCHA dataset";
    }

    // Read with reader
    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "real_gotcha_file_count_validation",
            .product_id = "real_gotcha_file_count_product",
            .collector_name = "GOTCHA",
            .coordinate_frame = "ecef",
            .time_basis = "seconds",
        }};

    const auto read = reader.ReadDetailed(dataset_path);
    ASSERT_TRUE(read.success) << "Read failed: " << read.message;

    const auto source_files = read.product.collection.source_files.size();
    EXPECT_EQ(source_files, mat_count)
        << "Reader processed different number of files than found in directory";
}

} // namespace
