// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_io.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/io/CrsdIO.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef SARPY_VALIDATE_CRSD_TOOL_PATH
#define SARPY_VALIDATE_CRSD_TOOL_PATH "tools/sarpy/validate_crsd.py"
#endif

namespace {

class CrsdIoTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_crsd_io_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    [[nodiscard]] static graphx::sar::NormalizedSarProduct MakeProduct() {
        graphx::sar::NormalizedSarProduct product{};
        product.collection.product_id = "product-crsd";
        product.collection.collector_name = "GOTCHA";
        product.collection.collection_id = "collection-crsd-001";
        product.collection.coordinate_frame = "ecef";
        product.collection.time_basis = "seconds";
        product.collection.source_files = {"pulse_a.mat", "pulse_b.mat"};
        product.collection.provenance_label = "derived_from_gotcha_phase_history";
        product.collection.source_ordering = "lexical";

        graphx::sar::ChannelSignal channel{};
        channel.channel_id = "channel_0";
        channel.waveform.waveform_id = "wf_0";
        channel.waveform.carrier_hz = 9.6e9;
        channel.waveform.bandwidth_hz = 640.0e6;
        channel.waveform.sample_rate_hz = 1.0e9;
        channel.waveform.sample_type = "complex_f32";
        channel.waveform.polarization = "HH";
        channel.waveform.frequency_axis_hz = {9.599e9, 9.600e9};

        graphx::sar::PulseVector pulse0{};
        pulse0.parameters.vector_index = 0;
        pulse0.parameters.time_seconds = 0.0;
        pulse0.parameters.range_sample_start = 0;
        pulse0.parameters.platform.position_m = {1.0, 2.0, 3.0};
        pulse0.parameters.platform.velocity_mps = {0.1, 0.2, 0.3};
        pulse0.samples = {
            graphx::sar::ComplexSample{.real = 1.0f, .imag = -1.0f},
            graphx::sar::ComplexSample{.real = 2.0f, .imag = -2.0f},
        };

        graphx::sar::PulseVector pulse1{};
        pulse1.parameters.vector_index = 1;
        pulse1.parameters.time_seconds = 1.0;
        pulse1.parameters.range_sample_start = 2;
        pulse1.parameters.platform.position_m = {4.0, 5.0, 6.0};
        pulse1.parameters.platform.velocity_mps = {0.4, 0.5, 0.6};
        pulse1.samples = {
            graphx::sar::ComplexSample{.real = 3.0f, .imag = -3.0f},
            graphx::sar::ComplexSample{.real = 4.0f, .imag = -4.0f},
        };

        channel.pulses.push_back(std::move(pulse0));
        channel.pulses.push_back(std::move(pulse1));
        product.channels.push_back(std::move(channel));
        product.reference_geometry.scene_center_m = {0.0, 0.0, 0.0};
        product.reference_geometry.reference_platform = product.channels.front().pulses.front().parameters.platform;
        return product;
    }

    [[nodiscard]] static std::string ShellQuote(const std::filesystem::path& path) {
        std::string raw = path.string();
        std::string quoted{"'"};
        for (const char ch : raw) {
            if (ch == '\'') {
                quoted += "'\\''";
            } else {
                quoted += ch;
            }
        }
        quoted += "'";
        return quoted;
    }

    std::filesystem::path root_{};
};

TEST_F(CrsdIoTest, WriterProducesSarpyOpenableCrsdProduct) {
    const auto out_dir = Path("crsd_out");
    const auto product = MakeProduct();

    graphx::sar::CrsdWriter writer;
    const auto write = writer.Write(out_dir, product);
    ASSERT_TRUE(write.success) << write.message;
    EXPECT_TRUE(std::filesystem::exists(out_dir / graphx::sar::CrsdWriter::kSignalFile));
    EXPECT_TRUE(std::filesystem::exists(out_dir / graphx::sar::CrsdWriter::kMetadataFile));
    EXPECT_TRUE(std::filesystem::exists(out_dir / graphx::sar::CrsdWriter::kPvpFile));
    EXPECT_TRUE(std::filesystem::exists(out_dir / graphx::sar::CrsdWriter::kProvenanceFile));
    EXPECT_TRUE(std::filesystem::exists(out_dir / graphx::sar::CrsdWriter::kChunkIndexFile));

    const auto validation_report = Path("sarpy_validation.json");
    const auto command =
        std::string{"python3 "} + SARPY_VALIDATE_CRSD_TOOL_PATH +
        " validate --input-crsd " +
        ShellQuote(out_dir / graphx::sar::CrsdWriter::kSignalFile) +
        " --output-json " + ShellQuote(validation_report) +
        " >/dev/null 2>&1";
    EXPECT_EQ(std::system(command.c_str()), 0);
    EXPECT_TRUE(std::filesystem::exists(validation_report));
}

TEST_F(CrsdIoTest, WriterFailsForMissingRequiredFields) {
    graphx::sar::NormalizedSarProduct invalid{};
    const auto out_dir = Path("crsd_invalid");

    graphx::sar::CrsdWriter writer;
    const auto write = writer.Write(out_dir, invalid);
    EXPECT_FALSE(write.success);
    EXPECT_EQ(write.message, "missing_required_fields");
    EXPECT_FALSE(std::filesystem::exists(out_dir));
}

} // namespace
