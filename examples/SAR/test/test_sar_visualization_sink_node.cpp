#include <gtest/gtest.h>

#include "sar/SarVisualizationSinkNode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

sar::SarAccelControlToken MakeView(std::uint64_t sequence_id,
                                   std::uint32_t tile_id,
                                   sar::SarFrameMarker marker,
                                   std::size_t byte_count = 16u,
                                   std::uint32_t stream_id = 0u) {
    sar::SarAccelControlToken msg{};
    msg.token_id = (sequence_id << 16u) | static_cast<std::uint64_t>(tile_id);
    msg.sidecar.sequence_id = sequence_id;
    msg.sidecar.stream_id = stream_id;
    msg.sidecar.tile_id = tile_id;
    msg.sidecar.marker = marker;
    msg.sidecar.payload_byte_count = byte_count;
    msg.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(msg.token_id + 1u));
    msg.host_view.bytes = static_cast<std::uint64_t>(byte_count);
    msg.host_view.dtype = graph::gpu::accel::DataType::Float32;
    msg.host_view.layout = sar::MakeAccelVectorLayout(
        static_cast<std::uint64_t>(std::max<std::size_t>(1u, byte_count / sizeof(float))));
    msg.host_view.allocator_id = 3;
    msg.has_host_view = true;
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

TEST(SarVisualizationSinkNodeTest, ArtifactOutputIsDeterministicForRepeatedInputs) {
    const auto output_dir =
        std::filesystem::temp_directory_path() / "graphx_sar_viz_test_deterministic";
    std::filesystem::remove_all(output_dir);

    sar::SarVisualizationSinkNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"output_dir", output_dir.string()},
        {"format", "csv"},
        {"normalize", false},
        {"file_prefix", "tile"},
    };
    node.Configure(graph::JsonView(cfg_json));

    const auto msg = MakeView(9, 3, sar::SarFrameMarker::Data, 8u, 1u);

    auto first = node.Transfer(
        msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());

    auto second = node.Transfer(
        msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(node.artifact_count(), 2u);

    const auto output_path = output_dir / "tile_seq9_tile3.csv";
    ASSERT_TRUE(std::filesystem::exists(output_path));

    std::ifstream in(output_path);
    ASSERT_TRUE(in.is_open());
    std::ostringstream contents;
    contents << in.rdbuf();
    EXPECT_EQ(contents.str(), "0,1\n");

    std::filesystem::remove_all(output_dir);
}

} // namespace
