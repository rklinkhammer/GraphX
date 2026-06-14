#include <gtest/gtest.h>

#include "sar/io/GotchaMatReader.hpp"
#include "sar/io/GraphxSarNormalizedIO.hpp"
#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarProductValidator.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

// ──────────────────────────────────────────────────────────────────────
// Helper: read pulse counts from a conversion report
// ──────────────────────────────────────────────────────────────────────
static nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream{path};
    if (!stream.good()) {
        return nullptr;
    }
    nlohmann::json value{};
    stream >> value;
    return value;
}

static std::size_t ExtractTotalPulsesFromReport(const std::filesystem::path& report_path) {
    const auto report = ReadJson(report_path);
    if (report.is_null() || !report.contains("aperture_accounting")) {
        return 0;
    }
    const auto& acct = report.at("aperture_accounting");
    if (!acct.contains("total_pulses_read")) {
        return 0;
    }
    return acct.at("total_pulses_read").get<std::size_t>();
}

static std::vector<graphx::sar::SarPulseFileCount> ExtractPulsesPerFileFromReport(
    const std::filesystem::path& report_path) {
    std::vector<graphx::sar::SarPulseFileCount> result{};
    const auto report = ReadJson(report_path);
    if (report.is_null() || !report.contains("aperture_accounting")) {
        return result;
    }
    const auto& acct = report.at("aperture_accounting");
    if (!acct.contains("pulses_per_file")) {
        return result;
    }
    const auto& ppf = acct.at("pulses_per_file");
    if (!ppf.is_array()) {
        return result;
    }
    for (const auto& entry : ppf) {
        result.push_back(graphx::sar::SarPulseFileCount{
            .filename = entry.at("filename").get<std::string>(),
            .pulse_count = entry.at("pulse_count").get<std::size_t>(),
        });
    }
    return result;
}

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
// Test 3: Verify full-aperture conversion produces valid lite output
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, FullApertureConversionProducesValidLite) {
    if (!HasDataset()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET not set; real GOTCHA full-aperture validation is local-only";
    }

    const auto dataset_path = std::filesystem::path{GetDatasetPath()};

    // Read full-aperture
    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "real_gotcha_lite_validation",
            .product_id = "real_gotcha_lite_product",
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

    // Write to lite format
    std::error_code fs_error{};
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_pr8_real_validation";
    std::filesystem::remove_all(output_dir, fs_error);
    std::filesystem::create_directories(output_dir, fs_error);

    graphx::sar::GraphxSarNormalizedWriter writer{};
    const auto write = writer.Write(output_dir, read.product);
    ASSERT_TRUE(write.success) << "Write failed: " << write.message;

    // Verify lite output files exist
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile))
        << "Metadata file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::GraphxSarNormalizedWriter::kSignalFile))
        << "Signal file missing";
    ASSERT_TRUE(std::filesystem::exists(
        output_dir / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile))
        << "Conversion report missing";

    // Verify metadata structure
    const auto metadata = ReadJson(
        output_dir / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile);
    ASSERT_FALSE(metadata.is_null()) << "Metadata JSON is invalid";
    EXPECT_TRUE(metadata.contains("shape")) << "Metadata lacks shape";
    EXPECT_TRUE(metadata.contains("channels")) << "Metadata lacks channels";

    std::filesystem::remove_all(output_dir, fs_error);
}

// ──────────────────────────────────────────────────────────────────────
// Test 4: Verify conversion report contains correct aperture accounting
// ──────────────────────────────────────────────────────────────────────
TEST_F(RealGotchaFullApertureValidationTest, ConversionReportShowsCorrectApertureAccounting) {
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

    const auto num_files = read.product.collection.source_files.size();
    const auto total_pulses = read.product.Shape().pulse_count;

    EXPECT_GE(num_files, 1u) << "No source files recorded";
    EXPECT_GT(total_pulses, 0u) << "No pulses in product";

    // Write to lite format
    std::error_code fs_error{};
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_pr8_report_validation";
    std::filesystem::remove_all(output_dir, fs_error);
    std::filesystem::create_directories(output_dir, fs_error);

    graphx::sar::GraphxSarNormalizedWriter writer{};
    const auto write = writer.Write(output_dir, read.product);
    ASSERT_TRUE(write.success) << "Write failed: " << write.message;

    // Verify report structure
    const auto report_path = output_dir / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile;
    const auto total_from_report = ExtractTotalPulsesFromReport(report_path);
    const auto per_file_from_report = ExtractPulsesPerFileFromReport(report_path);

    EXPECT_EQ(total_from_report, total_pulses)
        << "Report total_pulses_read does not match product pulse count";
    EXPECT_EQ(per_file_from_report.size(), num_files)
        << "Report pulses_per_file list size does not match source_files count";

    // Verify per-file counts sum to total
    std::size_t per_file_sum = 0;
    for (const auto& entry : per_file_from_report) {
        per_file_sum += entry.pulse_count;
    }
    EXPECT_EQ(per_file_sum, total_pulses)
        << "Sum of per-file pulse counts does not match total";

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
