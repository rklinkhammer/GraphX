#include <gtest/gtest.h>

#include "graph/JsonDynamicGraphLoader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

std::filesystem::path WriteTempGraphConfig(std::string_view content) {
    const auto path = std::filesystem::temp_directory_path() /
        ("graphx_json_dynamic_loader_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".json");

    std::ofstream file(path);
    file << content;
    return path;
}

constexpr const char* kValidGraphConfig = R"json(
{
  "name": "loader_test_graph",
  "num_threads": 1,
  "nodes": [
    {"id": "source_1", "type": "SourceTestNode"},
    {"id": "sink_1", "type": "SinkTestNode"}
  ],
  "edges": [
    {
      "source_node_id": "source_1",
      "source_port": 0,
      "target_node_id": "sink_1",
      "target_port": 0
    }
  ]
}
)json";

}  // namespace

TEST(JsonDynamicGraphLoaderExpectedTest, LoadEdgesSafeReturnsParsedEdges) {
    const auto path = WriteTempGraphConfig(kValidGraphConfig);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadEdgesSafe(path.string());

    std::filesystem::remove(path);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(result->front().source_node_id, "source_1");
    EXPECT_EQ(result->front().target_node_id, "sink_1");
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadEdgesSafeReportsMissingFile) {
    const auto missing_path =
        (std::filesystem::temp_directory_path() / "graphx_missing_dynamic_loader_test.json").string();

    const auto result = graph::config::JsonDynamicGraphLoader::LoadEdgesSafe(missing_path);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::FileNotFound);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadEdgesSafeReportsValidationFailure) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "invalid_loader_test_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": [
        {
          "source_node_id": "source_1",
          "source_port": 0,
          "target_node_id": "missing_sink",
          "target_port": 0
        }
      ]
    }
    )json");

    const auto result = graph::config::JsonDynamicGraphLoader::LoadEdgesSafe(path.string());

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeReportsNullFactory) {
    const auto path = WriteTempGraphConfig(kValidGraphConfig);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(path.string(), nullptr);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}
