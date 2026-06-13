#include <gtest/gtest.h>

#include "sar/io/SarProductChunker.hpp"

#include <vector>

namespace {

[[nodiscard]] graphx::sar::NormalizedSarProduct MakeChunkingProduct(
    const std::vector<std::size_t>& sample_counts) {
    graphx::sar::NormalizedSarProduct product{};
    product.collection.product_id = "chunker-product";
    product.collection.coordinate_frame = "ecef";
    product.collection.time_basis = "seconds";

    graphx::sar::ChannelSignal channel{};
    channel.channel_id = "channel_0";
    channel.waveform.waveform_id = "wf_0";
    channel.waveform.sample_rate_hz = 1.0;

    for (std::size_t pulse_index = 0; pulse_index < sample_counts.size(); ++pulse_index) {
        graphx::sar::PulseVector pulse{};
        pulse.parameters.vector_index = pulse_index;
        pulse.parameters.time_seconds = static_cast<double>(pulse_index);
        for (std::size_t sample_index = 0; sample_index < sample_counts[pulse_index]; ++sample_index) {
            pulse.samples.push_back(graphx::sar::ComplexSample{
                .real = static_cast<float>(sample_index),
                .imag = static_cast<float>(-static_cast<int>(sample_index)),
            });
        }
        channel.pulses.push_back(std::move(pulse));
    }

    product.channels.push_back(std::move(channel));
    return product;
}

TEST(SarProductChunkerTest, SplitsByMaxOutputSizeWithoutSplittingPulses) {
    const auto product = MakeChunkingProduct({2, 4, 3});

    const auto plan = graphx::sar::SarProductChunker::BuildPlan(
        product,
        graphx::sar::SarChunkerOptions{
            .max_chunk_bytes = 40,
            .output_prefix = "gotcha_crsd_chunk",
        });

    ASSERT_EQ(plan.chunks.size(), 3u);

    EXPECT_EQ(plan.chunks[0].pulse_start, 0u);
    EXPECT_EQ(plan.chunks[0].pulse_end, 0u);
    EXPECT_EQ(plan.chunks[0].output_stem, "gotcha_crsd_chunk_0000");

    EXPECT_EQ(plan.chunks[1].pulse_start, 1u);
    EXPECT_EQ(plan.chunks[1].pulse_end, 1u);
    EXPECT_EQ(plan.chunks[1].output_stem, "gotcha_crsd_chunk_0001");

    EXPECT_EQ(plan.chunks[2].pulse_start, 2u);
    EXPECT_EQ(plan.chunks[2].pulse_end, 2u);
    EXPECT_EQ(plan.chunks[2].output_stem, "gotcha_crsd_chunk_0002");
}

TEST(SarProductChunkerTest, EmitsWarningWhenSinglePulseExceedsMaxOutputSize) {
    const auto product = MakeChunkingProduct({12, 2});

    const auto plan = graphx::sar::SarProductChunker::BuildPlan(
        product,
        graphx::sar::SarChunkerOptions{
            .max_chunk_bytes = 64,
            .output_prefix = "gotcha_crsd_chunk",
        });

    ASSERT_EQ(plan.chunks.size(), 2u);
    ASSERT_FALSE(plan.warnings.empty());
    EXPECT_NE(plan.warnings[0].find("pulse_exceeds_max_chunk_bytes"), std::string::npos);
    EXPECT_EQ(plan.chunks[0].pulse_start, 0u);
    EXPECT_EQ(plan.chunks[0].pulse_end, 0u);
}

TEST(SarProductChunkerTest, OutputNamesAreDeterministic) {
    EXPECT_EQ(
        graphx::sar::SarProductChunker::MakeChunkOutputStem("gotcha_crsd_chunk", 0),
        "gotcha_crsd_chunk_0000");
    EXPECT_EQ(
        graphx::sar::SarProductChunker::MakeChunkOutputStem("gotcha_crsd_chunk", 15),
        "gotcha_crsd_chunk_0015");
}

} // namespace
