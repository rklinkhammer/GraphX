#include <gtest/gtest.h>

#include "sar/io/GraphxCrsdLiteIO.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

class GraphxCrsdLiteIoTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_crsd_lite_io_" + std::to_string(now));
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
        product.collection.product_id = "product-graphx-lite";
        product.collection.collector_name = "GOTCHA";
        product.collection.collection_id = "collection-001";
        product.collection.coordinate_frame = "ecef";
        product.collection.time_basis = "seconds";
        product.collection.source_files = {"a_pulse.mat", "b_pulse.mat"};
        product.collection.provenance_label = "derived_from_gotcha_phase_history";
        product.collection.source_ordering = "manifest";

        graphx::sar::ChannelSignal channel{};
        channel.channel_id = "channel_0";
        channel.waveform.waveform_id = "wf_0";
        channel.waveform.carrier_hz = 9.6e9;
        channel.waveform.bandwidth_hz = 640.0e6;
        channel.waveform.sample_rate_hz = 1.0e9;
        channel.waveform.sample_type = "complex_f32";
        channel.waveform.polarization = "HH";
        channel.waveform.frequency_axis_hz = {9.599e9, 9.600e9, 9.601e9};

        graphx::sar::PulseVector pulse0{};
        pulse0.parameters.vector_index = 0;
        pulse0.parameters.time_seconds = 0.25;
        pulse0.parameters.range_sample_start = 10;
        pulse0.parameters.platform.position_m = {1.0, 2.0, 3.0};
        pulse0.parameters.platform.velocity_mps = {0.1, 0.2, 0.3};
        pulse0.samples = {
            graphx::sar::ComplexSample{.real = 1.0f, .imag = -1.0f},
            graphx::sar::ComplexSample{.real = 2.0f, .imag = -2.0f},
        };

        graphx::sar::PulseVector pulse1{};
        pulse1.parameters.vector_index = 1;
        pulse1.parameters.time_seconds = 1.25;
        pulse1.parameters.range_sample_start = 20;
        pulse1.parameters.platform.position_m = {4.0, 5.0, 6.0};
        pulse1.parameters.platform.velocity_mps = {0.4, 0.5, 0.6};
        pulse1.samples = {
            graphx::sar::ComplexSample{.real = 3.0f, .imag = -3.0f},
            graphx::sar::ComplexSample{.real = 4.0f, .imag = -4.0f},
        };

        channel.pulses.push_back(std::move(pulse0));
        channel.pulses.push_back(std::move(pulse1));
        product.channels.push_back(std::move(channel));
        return product;
    }

    [[nodiscard]] static nlohmann::json ReadJson(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream) << path;
        nlohmann::json value{};
        stream >> value;
        return value;
    }

    std::filesystem::path root_{};
};

TEST_F(GraphxCrsdLiteIoTest, WriterEmitsRequiredFilesAndNonStandardLabels) {
    const auto product = MakeProduct();
    const auto out_dir = Path("lite_out");

    graphx::sar::GraphxCrsdLiteWriter writer;
    const auto write = writer.Write(out_dir, product);
    ASSERT_TRUE(write.success) << write.message;

    const auto signal_path = out_dir / graphx::sar::GraphxCrsdLiteWriter::kSignalFile;
    const auto metadata_path = out_dir / graphx::sar::GraphxCrsdLiteWriter::kMetadataFile;
    const auto index_path = out_dir / graphx::sar::GraphxCrsdLiteWriter::kIndexFile;
    const auto report_path = out_dir / graphx::sar::GraphxCrsdLiteWriter::kConversionReportFile;
    const auto warnings_path = out_dir / graphx::sar::GraphxCrsdLiteWriter::kWarningsLogFile;

    EXPECT_TRUE(std::filesystem::exists(signal_path));
    EXPECT_TRUE(std::filesystem::exists(metadata_path));
    EXPECT_TRUE(std::filesystem::exists(index_path));
    EXPECT_TRUE(std::filesystem::exists(report_path));
    EXPECT_TRUE(std::filesystem::exists(warnings_path));

    const auto metadata = ReadJson(metadata_path);
    const auto index = ReadJson(index_path);
    const auto report = ReadJson(report_path);

    EXPECT_EQ(metadata.at("format"), graphx::sar::GraphxCrsdLiteWriter::kFormatName);
    EXPECT_EQ(metadata.at("label"), graphx::sar::GraphxCrsdLiteWriter::kNonStandardLabel);
    EXPECT_EQ(index.at("label"), graphx::sar::GraphxCrsdLiteWriter::kNonStandardLabel);
    EXPECT_EQ(report.at("label"), graphx::sar::GraphxCrsdLiteWriter::kNonStandardLabel);

    EXPECT_EQ(report.at("provenance"), "derived_from_gotcha_phase_history");
    EXPECT_EQ(report.at("source_ordering"), "manifest");
    EXPECT_EQ(report.at("validation_status"), "ok");

    const auto checksum = graphx::sar::GraphxCrsdLiteWriter::ComputeSignalChecksum(signal_path);
    ASSERT_FALSE(checksum.empty());
    EXPECT_EQ(index.at("signal_checksum_fnv1a64"), checksum);
    ASSERT_TRUE(report.contains("outputs"));
    ASSERT_EQ(report.at("outputs").size(), 1u);
    EXPECT_EQ(report.at("outputs").at(0).at("checksum_fnv1a64"), checksum);
}

TEST_F(GraphxCrsdLiteIoTest, ReaderRoundTripsNormalizedProductAndPulseOrdering) {
    const auto product = MakeProduct();
    const auto out_dir = Path("lite_roundtrip");

    graphx::sar::GraphxCrsdLiteWriter writer;
    ASSERT_TRUE(writer.Write(out_dir, product).success);

    graphx::sar::GraphxCrsdLiteReader reader;
    const auto read = reader.Read(out_dir);
    ASSERT_TRUE(read.success) << read.message;

    const auto& roundtrip = read.product;
    EXPECT_EQ(roundtrip.collection.product_id, product.collection.product_id);
    EXPECT_EQ(roundtrip.collection.collection_id, product.collection.collection_id);
    EXPECT_EQ(roundtrip.collection.provenance_label, product.collection.provenance_label);
    EXPECT_EQ(roundtrip.collection.source_ordering, product.collection.source_ordering);

    ASSERT_EQ(roundtrip.channels.size(), 1u);
    const auto& channel = roundtrip.Channel(0);
    EXPECT_EQ(channel.waveform.waveform_id, "wf_0");
    EXPECT_EQ(channel.waveform.frequency_axis_hz.size(), 3u);

    ASSERT_EQ(channel.pulses.size(), 2u);
    EXPECT_EQ(channel.pulses[0].parameters.vector_index, 0u);
    EXPECT_EQ(channel.pulses[1].parameters.vector_index, 1u);
    EXPECT_DOUBLE_EQ(channel.pulses[0].parameters.time_seconds, 0.25);
    EXPECT_DOUBLE_EQ(channel.pulses[1].parameters.time_seconds, 1.25);

    EXPECT_FLOAT_EQ(channel.pulses[0].samples[0].real, 1.0f);
    EXPECT_FLOAT_EQ(channel.pulses[0].samples[0].imag, -1.0f);
    EXPECT_FLOAT_EQ(channel.pulses[1].samples[1].real, 4.0f);
    EXPECT_FLOAT_EQ(channel.pulses[1].samples[1].imag, -4.0f);
}

TEST_F(GraphxCrsdLiteIoTest, ReaderRejectsChecksumMismatch) {
    const auto product = MakeProduct();
    const auto out_dir = Path("lite_corrupt");

    graphx::sar::GraphxCrsdLiteWriter writer;
    ASSERT_TRUE(writer.Write(out_dir, product).success);

    std::ofstream signal(out_dir / graphx::sar::GraphxCrsdLiteWriter::kSignalFile,
                         std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(signal.good());
    signal.seekp(0, std::ios::beg);
    const float replacement = 99.0f;
    signal.write(reinterpret_cast<const char*>(&replacement), sizeof(replacement));
    signal.flush();

    graphx::sar::GraphxCrsdLiteReader reader;
    const auto read = reader.Read(out_dir);
    EXPECT_FALSE(read.success);
    EXPECT_EQ(read.message, "signal_checksum_mismatch");
}

} // namespace
