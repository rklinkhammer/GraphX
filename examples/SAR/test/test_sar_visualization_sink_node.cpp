#include <gtest/gtest.h>

#include "sar/SarVisualizationSinkNode.hpp"

#include "gpu/accel/types/AccelTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

std::uint64_t EncodeToken(std::uint64_t sequence_id,
                          std::uint32_t tile_id,
                          std::size_t byte_count,
                          std::uint32_t stream_id,
                          sar::SarFrameMarker marker) {
    const auto marker_bits = static_cast<std::uint64_t>(marker) & 0x3u;
    const auto tile_bits = static_cast<std::uint64_t>(tile_id) & 0xFFFu;
    const auto sequence_bits = sequence_id & 0xFFFFFFu;
    const auto byte_bits = static_cast<std::uint64_t>(byte_count) & 0xFFFFu;
    const auto stream_bits = static_cast<std::uint64_t>(stream_id) & 0x3FFu;

    return marker_bits |
           (tile_bits << 2u) |
           (sequence_bits << 14u) |
           (byte_bits << 38u) |
           (stream_bits << 54u);
}

graph::gpu::accel::HostPinnedBufferView MakeView(std::uint64_t sequence_id,
                                                 std::uint32_t tile_id,
                                                 sar::SarFrameMarker marker,
                                                 std::size_t byte_count = 16u,
                                                 std::uint32_t stream_id = 0u) {
    graph::gpu::accel::HostPinnedBufferView msg{};
    msg.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        EncodeToken(sequence_id, tile_id, byte_count, stream_id, marker)));
    msg.bytes = static_cast<std::uint64_t>(byte_count);
    msg.dtype = graph::gpu::accel::DataType::Float32;
    msg.layout = sar::MakeAccelVectorLayout(
        static_cast<std::uint64_t>(std::max<std::size_t>(1u, byte_count / sizeof(float))));
    msg.allocator_id = 3;
    return msg;
}

TEST(SarVisualizationSinkNodeTest, WritesPgmArtifactWhenEnabled) {
    const auto output_dir =
        std::filesystem::temp_directory_path() / "graphx_sar_viz_test_pgm";
    std::filesystem::remove_all(output_dir);

    sar::SarVisualizationSinkNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"output_dir", output_dir.string()},
        {"format", "pgm"},
        {"normalize", true},
        {"file_prefix", "tile"},
    };
    node.Configure(graph::JsonView(cfg_json));

    auto msg = MakeView(5, 2, sar::SarFrameMarker::Data, 16u);

    auto out = node.Transfer(
        msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(node.artifact_count(), 1u);

    const auto output_path = output_dir / "tile_seq5_tile2.pgm";
    ASSERT_TRUE(std::filesystem::exists(output_path));

    std::ifstream in(output_path);
    ASSERT_TRUE(in.is_open());
    std::string first_line;
    std::getline(in, first_line);
    EXPECT_EQ(first_line, "P2");

    std::filesystem::remove_all(output_dir);
}

TEST(SarVisualizationSinkNodeTest, DisabledModeDoesNotWriteArtifacts) {
    const auto output_dir =
        std::filesystem::temp_directory_path() / "graphx_sar_viz_test_disabled";
    std::filesystem::remove_all(output_dir);

    sar::SarVisualizationSinkNode node;
    nlohmann::json cfg_json = {
        {"enabled", false},
        {"output_dir", output_dir.string()},
        {"format", "csv"},
        {"normalize", false},
        {"file_prefix", "tile"},
    };
    node.Configure(graph::JsonView(cfg_json));

    auto msg = MakeView(1, 0, sar::SarFrameMarker::Data, 4u);

    auto out = node.Transfer(
        msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(node.artifact_count(), 0u);
    EXPECT_FALSE(std::filesystem::exists(output_dir / "tile_seq1_tile0.csv"));

    std::filesystem::remove_all(output_dir);
}

} // namespace
