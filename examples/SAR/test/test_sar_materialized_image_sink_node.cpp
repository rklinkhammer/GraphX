#include <gtest/gtest.h>

#include "sar/SarAccelTokenImagePayloadStore.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

sar::SarAccelControlToken MakeHostToken(std::uint64_t token,
                                        std::uint64_t sequence_id,
                                        std::uint32_t tile_id,
                                        std::uint32_t marker,
                                        std::uint64_t bytes) {
    sar::SarAccelControlToken msg{};
    msg.token_id = token;
    msg.sidecar.sequence_id = sequence_id;
    msg.sidecar.tile_id = tile_id;
    msg.sidecar.marker = static_cast<sar::SarFrameMarker>(marker);
    msg.sidecar.payload_byte_count = bytes;
    msg.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(token + 1u));
    msg.host_view.bytes = bytes;
    msg.host_view.dtype = graph::gpu::accel::DataType::Float32;
    msg.host_view.layout.rank = 1;
    msg.host_view.layout.shape[0] = std::max<std::uint64_t>(1u, bytes / sizeof(float));
    msg.host_view.layout.stride[0] = 1;
    msg.host_view.allocator_id = 1;
    msg.has_host_view = true;
    return msg;
}

} // namespace

TEST(SarMaterializedImageSinkNodeTest, DisabledModePassesThroughWithoutCapture) {
    sar::SarMaterializedImageSinkNode sink;

    const auto input = MakeHostToken(1001u, 11u, 3u, 0u, 64u);

    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->token_id, input.token_id);
    EXPECT_EQ(sink.capture_count(), 0u);
    EXPECT_FALSE(sink.has_materialized_image());
}

TEST(SarMaterializedImageSinkNodeTest, NonDataMarkersDoNotCaptureImage) {
    sar::SarMaterializedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));

    const auto eos_input = MakeHostToken(1002u, 11u, 3u, 2u, sizeof(float));

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

    const auto input = MakeHostToken(1003u, sequence_id, tile_id, 0u, bytes);

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

    const auto token = 1004u;
    const std::vector<float> payload{3.5f, 2.5f, 1.5f, 0.5f};
    sar::detail::StoreAccelTokenImagePayload(
        token,
        sar::detail::AccelTokenImagePayload{
            .sequence_id = sequence_id,
            .tile_id = tile_id,
            .pixels = payload,
        });

    const auto input = MakeHostToken(token, sequence_id, tile_id, 0u, bytes);
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
