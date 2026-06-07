#include <gtest/gtest.h>

#include "sar/SarVisualizationSinkNode.hpp"

#include <filesystem>
#include <fstream>

namespace {

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

    sar::SarImageTileMessage msg{};
    msg.envelope.sequence_id = 5;
    msg.envelope.tile_id = 2;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.width = 2;
    msg.height = 2;
    msg.pixels = {0.0f, 1.0f, 2.0f, 3.0f};

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

    sar::SarImageTileMessage msg{};
    msg.envelope.sequence_id = 1;
    msg.envelope.tile_id = 0;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.width = 1;
    msg.height = 1;
    msg.pixels = {42.0f};

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
