#include <gtest/gtest.h>

#include "sar/io/SarIoUtilities.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class SarIoUtilitiesTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("sar_io_utils_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    std::filesystem::path root_{};
};

TEST_F(SarIoUtilitiesTest, BuildsGotchaOutputIndexSchemaWithRequiredFields) {
    const auto json = graphx::sar::SarIoUtilities::BuildGotchaOutputIndexJson(
        graphx::sar::GotchaOutputIndexBuildInput{
            .schema = "graphx.sar.gotcha_sar_normalized_index.v1",
            .collection_id = "collection-001",
            .source_files = {"a.mat", "b.mat"},
            .source_ordering = "manifest",
            .provenance = "derived_from_gotcha_phase_history",
            .outputs = {
                graphx::sar::SarOutputSummary{
                    .output_name = "gotcha_sar_normalized_chunk_0000.graphx-sar-normalized",
                    .checksum_fnv1a64 = "0x0000000000000001",
                    .pulse_start = 0,
                    .pulse_end = 7,
                    .pulse_count = 8,
                    .channel_count = 1,
                    .max_sample_count = 512,
                },
            },
            .frequency_axis_hz = {9.599e9, 9.600e9},
            .assumptions = {"non_standard_intermediate_format"},
            .warnings = {"first warning"},
        });

    EXPECT_EQ(json.at("schema"), "graphx.sar.gotcha_sar_normalized_index.v1");
    EXPECT_EQ(json.at("collection_id"), "collection-001");
    EXPECT_EQ(json.at("source_ordering"), "manifest");
    ASSERT_TRUE(json.contains("outputs"));
    ASSERT_EQ(json.at("outputs").size(), 1u);
    EXPECT_EQ(json.at("outputs").at(0).at("pulse_range").at("start"), 0);
    EXPECT_EQ(json.at("outputs").at(0).at("pulse_range").at("end"), 7);
    EXPECT_EQ(json.at("outputs").at(0).at("sample_shape").at("channel_count"), 1);
    ASSERT_TRUE(json.contains("frequency_metadata"));
    ASSERT_TRUE(json.at("frequency_metadata").contains("frequency_axis_hz"));
}

TEST_F(SarIoUtilitiesTest, BuildsConversionReportSchemaWithValidationStatusAndChecksums) {
    const auto json = graphx::sar::SarIoUtilities::BuildConversionReportJson(
        graphx::sar::ConversionReportBuildInput{
            .format = "graphx-sar-normalized",
            .label = "NON-STANDARD",
            .selected_mode = "graphx-sar-normalized",
            .validation_status = "ok",
            .provenance = "derived_from_gotcha_phase_history",
            .source_ordering = "manifest",
            .assumptions = {"non_standard_intermediate_format"},
            .warnings = {"warning-a"},
            .outputs = {
                graphx::sar::SarOutputSummary{
                    .output_name = "signal.bin",
                    .checksum_fnv1a64 = "0x0000000000000002",
                    .pulse_start = 0,
                    .pulse_end = 1,
                    .pulse_count = 2,
                    .channel_count = 1,
                    .max_sample_count = 2,
                },
            },
            .metadata_file = "metadata.json",
            .index_file = "index.json",
        });

    EXPECT_EQ(json.at("schema"), "graphx.sar.conversion_report.v1");
    EXPECT_EQ(json.at("validation_status"), "ok");
    EXPECT_EQ(json.at("provenance"), "derived_from_gotcha_phase_history");
    EXPECT_EQ(json.at("outputs").at(0).at("checksum_fnv1a64"), "0x0000000000000002");
    EXPECT_EQ(json.at("outputs").at(0).at("pulse_range").at("start"), 0);
    EXPECT_EQ(json.at("outputs").at(0).at("sample_shape").at("max_sample_count"), 2);
}

TEST_F(SarIoUtilitiesTest, WarningsLogBehaviorIsDeterministic) {
    const auto with_warnings = Path("warnings_with_entries.log");
    ASSERT_TRUE(graphx::sar::SarIoUtilities::WriteWarningsLog(with_warnings, {"w1", "w2"}));

    std::ifstream with_warnings_stream{with_warnings};
    ASSERT_TRUE(with_warnings_stream.good());
    std::string with_warnings_text{
        std::istreambuf_iterator<char>(with_warnings_stream),
        std::istreambuf_iterator<char>()};
    EXPECT_EQ(with_warnings_text, "w1\nw2\n");

    const auto empty_warnings = Path("warnings_empty.log");
    ASSERT_TRUE(graphx::sar::SarIoUtilities::WriteWarningsLog(empty_warnings, {}));

    std::ifstream empty_warnings_stream{empty_warnings};
    ASSERT_TRUE(empty_warnings_stream.good());
    std::string empty_warnings_text{
        std::istreambuf_iterator<char>(empty_warnings_stream),
        std::istreambuf_iterator<char>()};
    EXPECT_EQ(empty_warnings_text, "none\n");
}

} // namespace
