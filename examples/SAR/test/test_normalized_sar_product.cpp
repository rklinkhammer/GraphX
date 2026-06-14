#include <gtest/gtest.h>

#include "sar/io/NormalizedSarProduct.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

graphx::sar::NormalizedSarProduct MakeProduct(
    std::size_t channel_count,
    std::size_t pulse_count,
    std::size_t sample_count) {
    graphx::sar::NormalizedSarProduct product{};
    product.collection.product_id = "product-001";
    product.collection.collector_name = "collector";
    product.collection.collection_id = "collection-001";
    product.collection.coordinate_frame = "ecef";
    product.collection.time_basis = "relative";
    product.reference_geometry.scene_center_m = {100.0, 200.0, 300.0};
    product.reference_geometry.reference_platform.position_m = {10.0, 20.0, 30.0};
    product.reference_geometry.reference_platform.velocity_mps = {1.0, 2.0, 3.0};

    for (std::size_t channel_index = 0; channel_index < channel_count; ++channel_index) {
        graphx::sar::ChannelSignal channel{};
        channel.channel_id = "channel-" + std::to_string(channel_index);
        channel.waveform.waveform_id = "waveform-" + std::to_string(channel_index);
        channel.waveform.carrier_hz = 9.6e9 + static_cast<double>(channel_index);
        channel.waveform.bandwidth_hz = 640.0e6;
        channel.waveform.sample_rate_hz = 1.0e9;
        channel.waveform.polarization = "HH";

        for (std::size_t pulse_index = 0; pulse_index < pulse_count; ++pulse_index) {
            graphx::sar::PulseVector pulse{};
            pulse.parameters.vector_index = static_cast<std::uint64_t>(pulse_index);
            pulse.parameters.time_seconds = static_cast<double>(pulse_index) * 0.001;
            pulse.parameters.range_sample_start = 1000 + static_cast<std::uint64_t>(pulse_index);
            pulse.parameters.platform.position_m = {
                static_cast<double>(pulse_index),
                static_cast<double>(channel_index),
                42.0,
            };
            pulse.parameters.platform.velocity_mps = {7.0, 8.0, 9.0};

            for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index) {
                pulse.samples.push_back(graphx::sar::ComplexSample{
                    .real = static_cast<float>((pulse_index * 100) + (channel_index * 10) + sample_index),
                    .imag = -static_cast<float>(sample_index),
                });
            }

            channel.pulses.push_back(std::move(pulse));
        }

        product.channels.push_back(std::move(channel));
    }

    return product;
}

class CapturingReader final : public graphx::sar::ISarReader {
public:
    explicit CapturingReader(graphx::sar::NormalizedSarProduct product)
        : product_(std::move(product)) {}

    graphx::sar::SarReadResult Read(const std::filesystem::path& path) const override {
        return graphx::sar::SarReadResult{
            .success = true,
            .message = path.string(),
            .product = product_,
        };
    }

private:
    graphx::sar::NormalizedSarProduct product_{};
};

class CapturingWriter final : public graphx::sar::ISarWriter {
public:
    graphx::sar::SarWriteResult Write(
        const std::filesystem::path& path,
        const graphx::sar::NormalizedSarProduct& product) const override {
        observed_path = path;
        observed_product_id = product.collection.product_id;
        observed_shape = product.Shape();
        return graphx::sar::SarWriteResult{.success = true, .message = "written"};
    }

    mutable std::filesystem::path observed_path{};
    mutable std::string observed_product_id{};
    mutable graphx::sar::ProductShape observed_shape{};
};

} // namespace

TEST(NormalizedSarProductTest, ShapeReportsPulseChannelAndSampleDimensions) {
    const auto product = MakeProduct(2, 3, 4);

    const auto shape = product.Shape();
    EXPECT_EQ(shape.channel_count, 2u);
    EXPECT_EQ(shape.pulse_count, 3u);
    EXPECT_EQ(shape.max_sample_count, 4u);
    EXPECT_FALSE(product.Empty());
}

TEST(NormalizedSarProductTest, IndexesSignalAsPulseChannelSample) {
    auto product = MakeProduct(2, 3, 4);

    EXPECT_EQ(product.Sample(2, 1, 3).real, 213.0f);
    EXPECT_EQ(product.Sample(2, 1, 3).imag, -3.0f);

    product.Sample(2, 1, 3) = graphx::sar::ComplexSample{.real = 12.5f, .imag = -4.25f};

    EXPECT_EQ(product.Channel(1).pulses.at(2).samples.at(3).real, 12.5f);
    EXPECT_EQ(product.Pulse(2, 1).samples.at(3).imag, -4.25f);
    EXPECT_THROW((void)product.Sample(3, 0, 0), std::out_of_range);
    EXPECT_THROW((void)product.Sample(0, 2, 0), std::out_of_range);
    EXPECT_THROW((void)product.Sample(0, 0, 4), std::out_of_range);
}

TEST(NormalizedSarProductTest, PreservesCollectionWaveformGeometryAndPerVectorMetadata) {
    const auto product = MakeProduct(2, 3, 4);

    EXPECT_EQ(product.collection.product_id, "product-001");
    EXPECT_EQ(product.collection.coordinate_frame, "ecef");
    EXPECT_EQ(product.collection.time_basis, "relative");
    EXPECT_EQ(product.reference_geometry.scene_center_m[0], 100.0);
    EXPECT_EQ(product.reference_geometry.reference_platform.velocity_mps[2], 3.0);

    const auto& channel = product.Channel(1);
    EXPECT_EQ(channel.channel_id, "channel-1");
    EXPECT_EQ(channel.waveform.waveform_id, "waveform-1");
    EXPECT_EQ(channel.waveform.sample_rate_hz, 1.0e9);
    EXPECT_EQ(channel.waveform.polarization, "HH");

    const auto& pulse = product.Pulse(2, 1);
    EXPECT_EQ(pulse.parameters.vector_index, 2u);
    EXPECT_EQ(pulse.parameters.range_sample_start, 1002u);
    EXPECT_DOUBLE_EQ(pulse.parameters.time_seconds, 0.002);
    EXPECT_EQ(pulse.parameters.platform.position_m[1], 1.0);
}

TEST(NormalizedSarProductTest, ReportsMissingRequiredFieldsDeterministically) {
    graphx::sar::NormalizedSarProduct product{};

    auto missing = product.MissingRequiredFields();
    EXPECT_EQ(missing, std::vector<std::string>({
                           "collection.product_id",
                           "collection.coordinate_frame",
                           "collection.time_basis",
                           "channels",
                       }));
    EXPECT_FALSE(product.HasRequiredFields());

    product = MakeProduct(1, 1, 1);
    EXPECT_TRUE(product.HasRequiredFields());

    product.channels[0].waveform.sample_rate_hz = 0.0;
    product.channels[0].pulses[0].samples.clear();

    missing = product.MissingRequiredFields();
    EXPECT_EQ(missing, std::vector<std::string>({
                           "channels[0].waveform.sample_rate_hz",
                           "channels[0].pulses[0].samples",
                       }));
}

TEST(NormalizedSarProductTest, SupportsFullAperturePulseFileMetadataFields) {
    auto product = MakeProduct(1, 5, 3);
    product.collection.source_files = {
        "subData01.mat",
        "subData02.mat",
    };
    product.collection.expected_pulse_count = 5;

    product.channels[0].pulses[0].parameters.source_file_index = 0;
    product.channels[0].pulses[0].parameters.source_pulse_index = 0;
    product.channels[0].pulses[1].parameters.source_file_index = 0;
    product.channels[0].pulses[1].parameters.source_pulse_index = 1;
    product.channels[0].pulses[2].parameters.source_file_index = 1;
    product.channels[0].pulses[2].parameters.source_pulse_index = 0;
    product.channels[0].pulses[3].parameters.source_file_index = 1;
    product.channels[0].pulses[3].parameters.source_pulse_index = 1;
    product.channels[0].pulses[4].parameters.source_file_index = 1;
    product.channels[0].pulses[4].parameters.source_pulse_index = 2;

    const auto shape = product.Shape();
    EXPECT_EQ(shape.pulse_count, 5u);
    ASSERT_TRUE(product.collection.expected_pulse_count.has_value());
    EXPECT_EQ(*product.collection.expected_pulse_count, 5u);
    ASSERT_TRUE(product.channels[0].pulses[4].parameters.source_file_index.has_value());
    ASSERT_TRUE(product.channels[0].pulses[4].parameters.source_pulse_index.has_value());
    EXPECT_EQ(*product.channels[0].pulses[4].parameters.source_file_index, 1u);
    EXPECT_EQ(*product.channels[0].pulses[4].parameters.source_pulse_index, 2u);
}

TEST(NormalizedSarProductTest, ReaderAndWriterInterfacesUseNormalizedProductBoundary) {
    const auto input = MakeProduct(1, 2, 3);
    const CapturingReader reader{input};

    const auto read = reader.Read("input.product");
    ASSERT_TRUE(read.success);
    EXPECT_EQ(read.message, "input.product");
    EXPECT_EQ(read.product.Shape().pulse_count, 2u);
    EXPECT_EQ(read.product.Shape().channel_count, 1u);
    EXPECT_EQ(read.product.Sample(1, 0, 2).real, 102.0f);

    const CapturingWriter writer{};
    const auto write = writer.Write("output.product", read.product);
    ASSERT_TRUE(write.success);
    EXPECT_EQ(write.message, "written");
    EXPECT_EQ(writer.observed_path, std::filesystem::path{"output.product"});
    EXPECT_EQ(writer.observed_product_id, "product-001");
    EXPECT_EQ(writer.observed_shape.max_sample_count, 3u);
}
