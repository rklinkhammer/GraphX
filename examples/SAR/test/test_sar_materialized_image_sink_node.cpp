#include <gtest/gtest.h>

#include "gpu/accel/types/AccelTypes.hpp"
#include "sar/SarAccelTokenImagePayloadStore.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

std::uint64_t EncodeToken(std::uint32_t marker,
                          std::uint32_t tile_id,
                          std::uint64_t sequence_id,
                          std::uint32_t byte_count,
                          std::uint32_t stream_id) {
    const auto marker_bits = static_cast<std::uint64_t>(marker & 0x3u);
    const auto tile_bits = static_cast<std::uint64_t>(tile_id & 0xFFFu);
    const auto sequence_bits = static_cast<std::uint64_t>(sequence_id & 0xFFFFFFu);
    const auto byte_bits = static_cast<std::uint64_t>(byte_count & 0xFFFFu);
    const auto stream_bits = static_cast<std::uint64_t>(stream_id & 0x3FFu);

    return marker_bits |
           (tile_bits << 2u) |
           (sequence_bits << 14u) |
           (byte_bits << 38u) |
           (stream_bits << 54u);
}

graph::gpu::accel::HostPinnedBufferView MakeHostView(std::uint64_t token, std::uint64_t bytes) {
    graph::gpu::accel::HostPinnedBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::Metal;
    view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
    view.bytes = bytes;
    view.dtype = graph::gpu::accel::DataType::Float32;
    view.layout.rank = 1;
    view.layout.shape[0] = std::max<std::uint64_t>(1u, bytes / sizeof(float));
    view.layout.stride[0] = 1;
    view.allocator_id = 1;
    return view;
}

} // namespace

TEST(SarMaterializedImageSinkNodeTest, DisabledModePassesThroughWithoutCapture) {
    sar::SarMaterializedImageSinkNode sink;

    const auto token = EncodeToken(0u, 3u, 11u, 64u, 1u);
    const auto input = MakeHostView(token, 64u);

    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->host_ptr, input.host_ptr);
    EXPECT_EQ(sink.capture_count(), 0u);
    EXPECT_FALSE(sink.has_materialized_image());
}

TEST(SarMaterializedImageSinkNodeTest, NonDataMarkersDoNotCaptureImage) {
    sar::SarMaterializedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));

    const auto eos_token = EncodeToken(2u, 3u, 11u, 0u, 1u);
    const auto eos_input = MakeHostView(eos_token, sizeof(float));

    auto out = sink.Transfer(
        eos_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(sink.capture_count(), 0u);
    EXPECT_FALSE(sink.has_materialized_image());
}

TEST(SarMaterializedImageSinkNodeTest, MissingPayloadDoesNotCaptureImage) {
    sar::SarMaterializedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));

    constexpr std::uint64_t sequence_id = 17u;
    constexpr std::uint32_t tile_id = 5u;
    constexpr std::uint32_t bytes = 64u;

    const auto token = EncodeToken(0u, tile_id, sequence_id, bytes, 2u);
    const auto input = MakeHostView(token, bytes);

    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(sink.capture_count(), 0u);
    EXPECT_FALSE(sink.has_materialized_image());
}

TEST(SarMaterializedImageSinkNodeTest, ConsumesStoredPayloadWhenAvailable) {
    sar::SarMaterializedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));

    constexpr std::uint64_t sequence_id = 21u;
    constexpr std::uint32_t tile_id = 7u;
    constexpr std::uint32_t bytes = 64u;

    const auto token = EncodeToken(0u, tile_id, sequence_id, bytes, 4u);
    const std::vector<float> payload{3.5f, 2.5f, 1.5f, 0.5f};
    sar::detail::StoreAccelTokenImagePayload(
        token,
        sar::detail::AccelTokenImagePayload{
            .sequence_id = sequence_id,
            .tile_id = tile_id,
            .pixels = payload,
        });

    const auto input = MakeHostView(token, bytes);
    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(sink.capture_count(), 1u);
    ASSERT_TRUE(sink.has_materialized_image());

    const auto metadata = sink.last_capture_metadata();
    EXPECT_EQ(metadata.sequence_id, sequence_id);
    EXPECT_EQ(metadata.tile_id, tile_id);
    EXPECT_EQ(metadata.element_count, payload.size());

    const auto image = sink.last_materialized_image();
    ASSERT_EQ(image.size(), payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        EXPECT_NEAR(image[i], payload[i], 1.0e-7f);
    }
}
