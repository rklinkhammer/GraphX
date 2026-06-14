#include <gtest/gtest.h>

#include "sar/io/SarProductValidator.hpp"

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

graphx::sar::NormalizedSarProduct MakeValidProduct(
    std::size_t channel_count = 2,
    std::size_t pulse_count = 3,
    std::size_t sample_count = 4) {
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
        channel.waveform.carrier_hz = 9.6e9;
        channel.waveform.bandwidth_hz = 640.0e6;
        channel.waveform.sample_rate_hz = 1.0e9;
        channel.waveform.sample_type = "complex_f32";
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
                    .real = static_cast<float>((channel_index * 100) + (pulse_index * 10) + sample_index),
                    .imag = -static_cast<float>(sample_index),
                });
            }

            channel.pulses.push_back(std::move(pulse));
        }

        product.channels.push_back(std::move(channel));
    }

    return product;
}

std::vector<std::string> IssueSummaries(const graphx::sar::SarValidationResult& result) {
    std::vector<std::string> summaries{};
    for (const auto& error : result.errors) {
        summaries.push_back(error.code + "|" + error.path + "|" + error.message);
    }
    return summaries;
}

} // namespace

TEST(SarProductValidatorTest, AcceptsValidNormalizedProduct) {
    const auto product = MakeValidProduct();

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(SarProductValidatorTest, ReportsMetadataCompletenessErrorsDeterministically) {
    auto product = MakeValidProduct();
    product.collection.product_id.clear();
    product.channels[1].channel_id.clear();
    product.channels[1].waveform.waveform_id.clear();

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "missing_required_field|collection.product_id|required normalized SAR product field is missing",
                                         "missing_required_field|channels[1].channel_id|required normalized SAR product field is missing",
                                         "missing_required_field|channels[1].waveform.waveform_id|required normalized SAR product field is missing",
                                     }));
}

TEST(SarProductValidatorTest, ReportsNaNAndInfInMetadataAndSamples) {
    auto product = MakeValidProduct();
    product.reference_geometry.scene_center_m[2] = std::numeric_limits<double>::quiet_NaN();
    product.channels[0].waveform.bandwidth_hz = std::numeric_limits<double>::infinity();
    product.channels[1].pulses[2].parameters.platform.velocity_mps[0] =
        -std::numeric_limits<double>::infinity();
    product.channels[1].pulses[2].samples[3].imag = std::numeric_limits<float>::quiet_NaN();

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "non_finite_value|reference_geometry.scene_center_m[2]|value must be finite",
                                         "non_finite_value|channels[0].waveform.bandwidth_hz|value must be finite",
                                         "non_finite_value|channels[1].pulses[2].parameters.platform.velocity_mps[0]|value must be finite",
                                         "frequency_metadata_mismatch|channels[1].waveform|frequency metadata must match the first channel",
                                         "non_finite_value|channels[1].pulses[2].samples[3].imag|value must be finite",
                                     }));
}

TEST(SarProductValidatorTest, ReportsPulseOrderingErrors) {
    auto product = MakeValidProduct();
    product.channels[0].pulses[1].parameters.vector_index = 99;
    product.channels[0].pulses[2].parameters.time_seconds = 0.0005;

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "pulse_order_mismatch|channels[0].pulses[1].parameters.vector_index|pulse vector_index must match its zero-based position in the channel",
                                         "pulse_time_order_mismatch|channels[0].pulses[2].parameters.time_seconds|pulse time_seconds must be monotonically nondecreasing within a channel",
                                     }));
}

TEST(SarProductValidatorTest, ReportsShapeConsistencyErrors) {
    auto product = MakeValidProduct();
    product.channels[1].pulses.pop_back();
    product.channels[0].pulses[2].samples.pop_back();

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "shape_mismatch|channels[0].pulses[2].samples|pulse sample count does not match the first pulse",
                                         "shape_mismatch|channels[1].pulses|channel pulse count does not match the first channel",
                                     }));
}

TEST(SarProductValidatorTest, ReportsUnsupportedSampleTypeAndMissingSamples) {
    auto product = MakeValidProduct();
    product.channels[0].waveform.sample_type = "complex_f64";
    product.channels[1].pulses[0].samples.clear();

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "missing_required_field|channels[1].pulses[0].samples|required normalized SAR product field is missing",
                                         "shape_mismatch|channels[1].pulses[0].samples|pulse sample count does not match the first pulse",
                                         "unsupported_sample_type|channels[0].waveform.sample_type|normalized SAR product samples must be complex_f32",
                                     }));
}

TEST(SarProductValidatorTest, ReportsPulseCountConsistencyErrors) {
    auto product = MakeValidProduct(1, 3, 4);
    product.collection.expected_pulse_count = 5;
    product.collection.source_files = {"subData01.mat", "subData02.mat", "subData03.mat", "subData04.mat"};

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "pulse_count_mismatch|collection.expected_pulse_count|shape pulse count does not match collection.expected_pulse_count",
                                         "pulse_count_inconsistent|collection.source_files|pulse count is smaller than source file count",
                                     }));
}

TEST(SarProductValidatorTest, ReportsFrequencyMetadataConsistencyErrors) {
    auto product = MakeValidProduct(2, 3, 4);
    product.channels[0].waveform.frequency_axis_hz = {9.599e9, 9.600e9, 9.601e9};
    product.channels[1].waveform.frequency_axis_hz = {9.601e9, 9.600e9, 9.599e9};
    product.channels[1].waveform.carrier_hz = 9.7e9;

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "frequency_axis_not_strictly_increasing|channels[1].waveform.frequency_axis_hz|frequency_axis_hz must be strictly increasing",
                                         "frequency_metadata_mismatch|channels[1].waveform|frequency metadata must match the first channel",
                                         "frequency_axis_mismatch|channels[1].waveform.frequency_axis_hz|frequency_axis_hz must match the first channel",
                                     }));
}

TEST(SarProductValidatorTest, ReportsPulseFileMetadataCompletenessAndOrderingErrors) {
    auto product = MakeValidProduct(1, 3, 4);
    product.collection.source_files = {"subData01.mat", "subData02.mat"};

    product.channels[0].pulses[0].parameters.source_file_index = 0;
    product.channels[0].pulses[0].parameters.source_pulse_index = 0;
    product.channels[0].pulses[1].parameters.source_file_index = 1;
    product.channels[0].pulses[1].parameters.source_pulse_index = 0;
    product.channels[0].pulses[2].parameters.source_file_index = 1;
    product.channels[0].pulses[2].parameters.source_pulse_index = 0;

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "pulse_file_sequence_mismatch|channels[0].pulses[2].parameters|(source_file_index, source_pulse_index) must be strictly increasing in vector order",
                                     }));
}

TEST(SarProductValidatorTest, ReportsGeometryCompletenessErrors) {
    auto product = MakeValidProduct(1, 2, 4);
    product.channels[0].pulses[0].parameters.platform.position_m = {0.0, 0.0, 0.0};

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(IssueSummaries(result), std::vector<std::string>({
                                         "geometry_incomplete|channels[0].pulses[0].parameters.platform.position_m|platform.position_m must be populated for every pulse",
                                     }));
}

TEST(SarProductValidatorTest, EmitsInformationalWarningForPerPulsePlatformVariation) {
    auto product = MakeValidProduct(1, 3, 4);

    const auto result = graphx::sar::SarProductValidator::Validate(product);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_EQ(result.warnings[0].code, "platform_state_varies_per_pulse");
    EXPECT_EQ(result.warnings[0].path, "channels[0]");
}
