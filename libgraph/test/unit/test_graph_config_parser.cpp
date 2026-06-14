#include <gtest/gtest.h>

#include "graph/GraphConfigParser.hpp"

#include <array>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

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

TEST(GraphConfigParserExpectedTest, ParseSafeParsesResolverControls) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "resolver_graph",
      "execution_backend": "metal",
      "backend_fallback_policy": "allow_fallback",
      "resolver_diagnostics": true,
      "edge_contract": "accel-token",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"},
        {"id": "sink_1", "type": "SinkTestNode"}
      ],
      "edges": [{
        "source_node_id": "source_1",
        "source_port": 0,
        "target_node_id": "sink_1",
        "target_port": 0
      }]
    })");

    ASSERT_TRUE(result);
    EXPECT_EQ(result->resolver.execution_backend, "metal");
    EXPECT_EQ(result->resolver.backend_fallback_policy, "allow_fallback");
    EXPECT_TRUE(result->resolver.resolver_diagnostics);
    EXPECT_EQ(result->resolver.edge_contract, "accel-token");
}

TEST(GraphConfigParserExpectedTest, ParseSafeParsesResolverMappings) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "resolver_mapping_graph",
      "execution_backend": "metal",
      "resolver_mappings": [{
        "intent_type": "RangeCompressionNode",
        "input_token_type": "HostPinnedBufferView",
        "output_token_type": "DeviceBufferView",
        "variants": [
          {
            "backend": "metal",
            "concrete_type": "SarRangeCompressionNodeMetal"
          },
          {
            "backend": "stub",
            "concrete_type": "RangeCompressionNode"
          }
        ]
      }],
      "nodes": [
        {"id": "source_1", "type": "RangeCompressionNode"}
      ],
      "edges": []
    })");

    ASSERT_TRUE(result);
    ASSERT_EQ(result->resolver_mappings.size(), 1u);
    const auto& mapping = result->resolver_mappings.front();
    EXPECT_EQ(mapping.intent_type, "RangeCompressionNode");
    EXPECT_EQ(mapping.input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(mapping.output_token_type, "DeviceBufferView");
    ASSERT_EQ(mapping.variants.size(), 2u);
    EXPECT_EQ(mapping.variants[0].backend, "metal");
    EXPECT_EQ(mapping.variants[0].concrete_type, "SarRangeCompressionNodeMetal");
    EXPECT_EQ(mapping.variants[1].backend, "stub");
    EXPECT_EQ(mapping.variants[1].concrete_type, "RangeCompressionNode");
}

TEST(GraphConfigParserExpectedTest, ParseSafeParsesSarAccelTokenResolverMappings) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "sar_resolver_mapping_graph",
      "edge_contract": "accel-token",
      "execution_backend": "metal",
      "resolver_mappings": [{
        "intent_type": "SarBackprojectionTransformNode",
        "input_token_type": "SarAccelControlToken",
        "output_token_type": "SarAccelControlToken",
        "variants": [
          {
            "backend": "metal",
            "concrete_type": "SarBackprojectionTransformNode"
          },
          {
            "backend": "stub",
            "concrete_type": "SarBackprojectionTransformNode"
          }
        ]
      }],
      "nodes": [
        {"id": "bp", "type": "SarBackprojectionTransformNode"}
      ],
      "edges": []
    })");

    ASSERT_TRUE(result);
    EXPECT_EQ(result->resolver.edge_contract, "accel-token");
    ASSERT_EQ(result->resolver_mappings.size(), 1u);
    const auto& mapping = result->resolver_mappings.front();
    EXPECT_EQ(mapping.intent_type, "SarBackprojectionTransformNode");
    EXPECT_EQ(mapping.input_token_type, "SarAccelControlToken");
    EXPECT_EQ(mapping.output_token_type, "SarAccelControlToken");
}

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsResolverMappingWithoutVariants) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "bad_resolver_mapping_graph",
      "resolver_mappings": [{
        "intent_type": "RangeCompressionNode",
        "variants": []
      }],
      "nodes": [
        {"id": "source_1", "type": "RangeCompressionNode"}
      ],
      "edges": []
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(GraphConfigParserExpectedTest, ParseSafeAppliesResolverDefaults) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "resolver_defaults_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": []
    })");

    ASSERT_TRUE(result);
    EXPECT_EQ(result->resolver.execution_backend, "auto");
    EXPECT_EQ(result->resolver.backend_fallback_policy, "strict");
    EXPECT_TRUE(result->resolver.resolver_diagnostics);
    EXPECT_TRUE(result->resolver.edge_contract.empty());
}

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsUnknownExecutionBackend) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "bad_backend_graph",
      "execution_backend": "vulkan",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": []
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsUnknownFallbackPolicy) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "bad_fallback_graph",
      "backend_fallback_policy": "maybe",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": []
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsUnknownEdgeContract) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "bad_edge_contract_graph",
      "edge_contract": "payload-copy",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": []
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(GraphConfigParserExpectedTest, ParseSafePreservesDeclaredPayloadContract) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "payload_contract_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"},
        {"id": "sink_1", "type": "SinkTestNode"}
      ],
      "edges": [{
        "source_node_id": "source_1",
        "source_port": 0,
        "target_node_id": "sink_1",
        "target_port": 0,
        "payload_contract": "HostPinnedBufferView"
      }]
    })");

    ASSERT_TRUE(result);
    ASSERT_EQ(result->edges.size(), 1u);
    EXPECT_EQ(result->edges[0].payload_contract, "HostPinnedBufferView");
}

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsLegacySarPayloadContractForAccelTokenGraph) {
  // Legacy-name literals are retained only for negative validation coverage.
    const std::array<const char*, 7> legacy_payload_contracts = {
        "SarPulseBlockMessage",
        "SarRangeTileMessage",
        "SarImageTileMessage",
        "SarDeviceLeaseMessage",
        "SarTransferTicketMessage",
        "  SarRangeTileMessage  ",
        "sardeviceleasemessage",
    };

    for (const auto* payload_contract : legacy_payload_contracts) {
        SCOPED_TRACE(payload_contract);

        nlohmann::json config = {
            {"name", "legacy_payload_contract_graph"},
            {"edge_contract", "accel-token"},
            {"nodes",
             {{{"id", "source_1"}, {"type", "SourceTestNode"}},
              {{"id", "sink_1"}, {"type", "SinkTestNode"}}}},
            {"edges",
             {{{"source_node_id", "source_1"},
               {"source_port", 0},
               {"target_node_id", "sink_1"},
               {"target_port", 0},
               {"payload_contract", payload_contract}}}},
        };

        const auto result = graph::config::GraphConfigParser::ParseSafe(config.dump());
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
    }
}

TEST(GraphConfigParserExpectedTest,
     ParseSafeRejectsLegacyResolverMappingTokenTypeForAccelTokenGraph) {
    const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
      "name": "legacy_mapping_contract_graph",
      "edge_contract": "accel-token",
      "resolver_mappings": [{
        "intent_type": "RangeCompressionNode",
        "input_token_type": "SarPulseBlockMessage",
        "output_token_type": "SarAccelControlToken",
        "variants": [
          {"backend": "metal", "concrete_type": "RangeCompressionNode"}
        ]
      }],
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"}
      ],
      "edges": []
    })");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
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

TEST(GraphConfigParserExpectedTest, ParseSafeRejectsEmptyNodeId) {
  const auto result = graph::config::GraphConfigParser::ParseSafe(R"({
    "name": "empty_id_graph",
    "nodes": [
      {"id": "", "type": "SourceTestNode"}
    ],
    "edges": []
  })");

  ASSERT_TRUE(result);
  const auto validation = graph::config::GraphConfigParser::Validate(result.value());
  EXPECT_FALSE(validation.valid);
}
