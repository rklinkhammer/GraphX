#include <gtest/gtest.h>

#include "graph/GraphConfigParser.hpp"

#include <filesystem>
#include <fstream>

namespace {

constexpr const char* kValidConfig = R"json(
{
  "name": "test_graph",
  "description": "expected-first parser test",
  "num_threads": 2,
  "deadlock_detection": {
    "enabled": false,
    "timeout_ms": 250
  },
  "nodes": [
    {
      "id": "source_1",
      "type": "SourceTestNode",
      "name": "source"
    },
    {
      "id": "sink_1",
      "type": "SinkTestNode"
    }
  ],
  "edges": [
    {
      "source_node_id": "source_1",
      "source_port": 0,
      "target_node_id": "sink_1",
      "target_port": "0",
      "buffer_size": 64,
      "backpressure_enabled": true
    }
  ]
}
)json";

}  // namespace

TEST(GraphConfigParserExpectedTest, ParseSafeParsesValidGraphConfig) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(kValidConfig);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->name, "test_graph");
    EXPECT_EQ(result->num_threads, 2u);
    EXPECT_FALSE(result->deadlock_detection.enabled);
    EXPECT_EQ(result->deadlock_detection.timeout_ms, 250u);
    ASSERT_EQ(result->nodes.size(), 2u);
    EXPECT_EQ(result->nodes[0].id, "source_1");
    EXPECT_EQ(result->nodes[0].type, "SourceTestNode");
    ASSERT_EQ(result->edges.size(), 1u);
    EXPECT_EQ(result->edges[0].source_port, 0u);
    EXPECT_EQ(result->edges[0].target_port, 0u);
    EXPECT_TRUE(result->edges[0].source_port_name.empty());
    EXPECT_TRUE(result->edges[0].target_port_name.empty());
    EXPECT_EQ(result->edges[0].buffer_size, 64u);
}

  TEST(GraphConfigParserExpectedTest, ParseSafeParsesNamedPorts) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "named_ports_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"},
        {"id": "sink_1", "type": "SinkTestNode"}
      ],
      "edges": [{
        "source_node_id": "source_1",
        "source_port_name": "Output0",
        "target_node_id": "sink_1",
        "target_port_name": "Input0"
      }]
    })");

    ASSERT_TRUE(result);
    ASSERT_EQ(result->edges.size(), 1u);
    EXPECT_EQ(result->edges[0].source_port_name, "Output0");
    EXPECT_EQ(result->edges[0].target_port_name, "Input0");
  }

  TEST(GraphConfigParserExpectedTest, ParseSafeTreatsStringPortTokensAsNamesWhenNotNumeric) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "string_token_ports_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"},
        {"id": "sink_1", "type": "SinkTestNode"}
      ],
      "edges": [{
        "source_node_id": "source_1",
        "source_port": "Output0",
        "target_node_id": "sink_1",
        "target_port": "Input0"
      }]
    })");

    ASSERT_TRUE(result);
    ASSERT_EQ(result->edges.size(), 1u);
    EXPECT_EQ(result->edges[0].source_port_name, "Output0");
    EXPECT_EQ(result->edges[0].target_port_name, "Input0");
  }

TEST(GraphConfigParserExpectedTest, ParseSafeReportsInvalidJson) {
    const auto result = graph::config::GraphConfigParser::ParseSafe("{not json}");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::InvalidFormat);
}

TEST(GraphConfigParserExpectedTest, ParseSafeReportsMissingRequiredNodes) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({"name":"missing_nodes"})");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::MissingRequired);
}

TEST(GraphConfigParserExpectedTest, ParseSafeReportsNodeTypeMismatch) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
        "name": "bad_node",
        "nodes": [{"id": "node_1", "type": 42}]
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::TypeMismatch);
}

TEST(GraphConfigParserExpectedTest, ParseSafeReportsTopLevelTypeMismatch) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"([{"nodes": []}])");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::TypeMismatch);
}

TEST(GraphConfigParserExpectedTest, ParseSafeReportsNegativePortOutOfRange) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
        "name": "bad_edge",
        "nodes": [
            {"id": "source_1", "type": "SourceTestNode"},
            {"id": "sink_1", "type": "SinkTestNode"}
        ],
        "edges": [{
            "source_node_id": "source_1",
            "source_port": -1,
            "target_node_id": "sink_1",
            "target_port": 0
        }]
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::OutOfRange);
}

TEST(GraphConfigParserExpectedTest, ParseFileSafeReportsFileNotFound) {
    const auto missing_path =
        (std::filesystem::temp_directory_path() / "graphx_missing_config_parser_test.json").string();

    const auto result = graph::config::GraphConfigParser::ParseFileSafe(missing_path);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::FileNotFound);
}

TEST(GraphConfigParserExpectedTest, ParseSafeReportsMissingGraphSection) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({"name":"missing_nodes"})");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::MissingRequired);
}
