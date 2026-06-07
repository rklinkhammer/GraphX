#include <gtest/gtest.h>

#include "graph/JsonDynamicGraphLoader.hpp"
#include "graph/NodeProviderBootstrap.hpp"
#include "graph/NodeMetadataService.hpp"
#include "graph/RegisteredNodeProvider.hpp"
#include "config/SchemaGenerator.hpp"
#include "test/AdvancedTestNodes.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

class StubLoaderDescriptorSchemaProvider final : public graph::INodeDescriptorSchemaProvider {
public:
  nlohmann::json BuildSchema(const graph::NodeDescriptor&) const override {
    return nlohmann::json{
      {"name", "stub_loader_descriptor"},
      {"type", "stub_loader_type"},
      {"description", ""},
      {"lifecycle_state", 0},
      {"supports_configuration", false},
      {"config_fields", nlohmann::json::array()},
      {"inputs", nlohmann::json::array({
        {
          {"index", 0},
          {"name", "__schema_override_port__"},
          {"payload", "integer"},
          {"direction", "input"}
        }
      })},
      {"outputs", nlohmann::json::array()}
    };
  }
};

class StubLoaderMetadataService final : public graph::INodeMetadataService {
public:
  const graph::INodeDescriptorProvider& DescriptorProvider() const override {
    return graph::GetDefaultNodeDescriptorProvider();
  }

  const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
    return schema_provider_;
  }

private:
  StubLoaderDescriptorSchemaProvider schema_provider_;
};

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

TEST(JsonDynamicGraphLoaderExpectedTest, LoadEdgesSafeParsesNamedPorts) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_named_ports_graph",
      "nodes": [
        {"id": "source_1", "type": "SourceTestNode"},
        {"id": "sink_1", "type": "SinkTestNode"}
      ],
      "edges": [
        {
          "source_node_id": "source_1",
          "source_port_name": "Output0",
          "target_node_id": "sink_1",
          "target_port_name": "Input0"
        }
      ]
    }
    )json");

    const auto result = graph::config::JsonDynamicGraphLoader::LoadEdgesSafe(path.string());

    std::filesystem::remove(path);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(result->front().source_port_name, "Output0");
    EXPECT_EQ(result->front().target_port_name, "Input0");
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

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeReportsNullProvider) {
    const auto path = WriteTempGraphConfig(kValidGraphConfig);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(path.string(), nullptr);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsUnknownPortConfigKey) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_invalid_port_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "port_config": {
            "__missing_port__": {"enabled": true}
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsPortConfigEvenWhenSchemaProviderAcceptsKeys) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_schema_provider_override",
      "num_threads": 1,
      "nodes": [
        {
          "id": "test_1",
          "type": "TestNode",
          "port_config": {
            "__schema_override_port__": {"enabled": true}
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    StubLoaderMetadataService metadata_service;
    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(),
        provider_bundle->provider,
      &metadata_service);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeUsesConfigNameOrIdForNodeNames) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_name_resolution",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "name": "source_alpha",
          "node_config": {
            "message_count": 1
          }
        },
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {
            "expected_message_count": 1
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 2u);

    EXPECT_EQ((*result)[0]->GetName(), "source_alpha");
    EXPECT_EQ((*result)[0]->GetType(), "SourceTestNode");
    EXPECT_EQ((*result)[1]->GetName(), "sink_1");
    EXPECT_EQ((*result)[1]->GetType(), "SinkTestNode");
    EXPECT_NE((*result)[0]->GetName(), (*result)[0]->GetType());
    EXPECT_NE((*result)[1]->GetName(), (*result)[1]->GetType());
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsNonObjectNodeConfig) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_invalid_node_config_type",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": 42
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsNodeConfigForNonConfigurableNode) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_non_configurable_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "test_1",
          "type": "TestNode",
          "node_config": {
            "sample_rate_hz": 100
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsOmittedNodeConfigForNonConfigurableNode) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_non_configurable_node_config_omitted",
      "num_threads": 1,
      "nodes": [
        {
          "id": "test_1",
          "type": "TestNode"
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 1u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsNullNodeConfigForNonConfigurableNode) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_non_configurable_node_config_null",
      "num_threads": 1,
      "nodes": [
        {
          "id": "test_1",
          "type": "TestNode",
          "node_config": null
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 1u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsMixedNonConfigurableNullAndMissingRequiredTypedConfig) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_mixed_nonconfigurable_null_and_missing_required",
      "num_threads": 1,
      "nodes": [
        {
          "id": "test_1",
          "type": "TestNode",
          "node_config": null
        },
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {}
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsMixedMissingRequiredTypedConfigIndependentOfOrder) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_mixed_missing_required_order_variant",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {}
        },
        {
          "id": "test_1",
          "type": "TestNode",
          "node_config": null
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsMissingRequiredNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_missing_required_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {}
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsWrongTypeNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_wrong_type_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {
            "message_count": "many"
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsValidTypedNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_valid_typed_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {
            "message_count": 5
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 1u);

    auto configured_source = (*result)[0]->GetNode<test::SourceTestNode>();
    ASSERT_NE(configured_source, nullptr);
    EXPECT_EQ(configured_source->GetMessageCount(), 5u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsUnknownTypedNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_unknown_typed_node_config_field",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {
            "message_count": 5,
            "extra_field": true
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsNullTypedNodeConfigWithRequiredField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_null_typed_node_config_required_field",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": null
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsMissingRequiredSinkNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_missing_required_sink_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {}
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsWrongTypeSinkNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_wrong_type_sink_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {
            "expected_message_count": "many"
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsValidTypedSinkNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_valid_typed_sink_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {
            "expected_message_count": 5
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 1u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsUnknownTypedSinkNodeConfigField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_unknown_typed_sink_node_config_field",
      "num_threads": 1,
      "nodes": [
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {
            "expected_message_count": 5,
            "unexpected": "value"
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsNullTypedSinkNodeConfigWithRequiredField) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_null_typed_sink_node_config_required_field",
      "num_threads": 1,
      "nodes": [
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": null
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsNullNodeConfigWithoutRequiredFields) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_null_optional_node_config",
      "num_threads": 1,
      "nodes": [
        {
          "id": "optional_1",
          "type": "OptionalConfigTestNode",
          "node_config": null
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 1u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsUnknownObjectForOptionalConfigNode) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_optional_node_unknown_object",
      "num_threads": 1,
      "nodes": [
        {
          "id": "optional_1",
          "type": "OptionalConfigTestNode",
          "node_config": {
            "unexpected": 1
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeAcceptsMixedNodeConfigCategories) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_mixed_node_config_categories_valid",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {
            "message_count": 4
          }
        },
        {
          "id": "sink_1",
          "type": "SinkTestNode",
          "node_config": {
            "expected_message_count": 4
          }
        },
        {
          "id": "optional_1",
          "type": "OptionalConfigTestNode",
          "node_config": null
        },
        {
          "id": "test_1",
          "type": "TestNode"
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
    ASSERT_EQ(result->size(), 4u);
}

TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeRejectsMixedCategoriesWhenNonConfigurableHasNodeConfig) {
    const auto path = WriteTempGraphConfig(R"json(
    {
      "name": "loader_mixed_node_config_categories_invalid_non_configurable",
      "num_threads": 1,
      "nodes": [
        {
          "id": "source_1",
          "type": "SourceTestNode",
          "node_config": {
            "message_count": 4
          }
        },
        {
          "id": "optional_1",
          "type": "OptionalConfigTestNode",
          "node_config": null
        },
        {
          "id": "test_1",
          "type": "TestNode",
          "node_config": {
            "unexpected": true
          }
        }
      ],
      "edges": []
    }
    )json");

    auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(provider_bundle);

    const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
        path.string(), provider_bundle->provider);

    std::filesystem::remove(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed);
}

    TEST(JsonDynamicGraphLoaderExpectedTest, LoadNodesSafeNodeConfigModeMatrixByCategory) {
      struct Case {
        std::string name;
        std::string node_block;
        bool expect_success;
      };

      const std::vector<Case> cases = {
        // Required typed config (SourceTestNode)
        {
          .name = "required_source_omitted",
          .node_block = R"json({"id":"n1","type":"SourceTestNode"})json",
          .expect_success = false,
        },
        {
          .name = "required_source_null",
          .node_block = R"json({"id":"n1","type":"SourceTestNode","node_config":null})json",
          .expect_success = false,
        },
        {
          .name = "required_source_object_valid",
          .node_block = R"json({"id":"n1","type":"SourceTestNode","node_config":{"message_count":3}})json",
          .expect_success = true,
        },
        {
          .name = "required_source_object_invalid",
          .node_block = R"json({"id":"n1","type":"SourceTestNode","node_config":{"message_count":"bad"}})json",
          .expect_success = false,
        },

        // Optional configurable with no required fields (OptionalConfigTestNode)
        {
          .name = "optional_node_omitted",
          .node_block = R"json({"id":"n1","type":"OptionalConfigTestNode"})json",
          .expect_success = true,
        },
        {
          .name = "optional_node_null",
          .node_block = R"json({"id":"n1","type":"OptionalConfigTestNode","node_config":null})json",
          .expect_success = true,
        },
        {
          .name = "optional_node_object_valid_empty",
          .node_block = R"json({"id":"n1","type":"OptionalConfigTestNode","node_config":{}})json",
          .expect_success = true,
        },
        {
          .name = "optional_node_object_invalid_unknown",
          .node_block = R"json({"id":"n1","type":"OptionalConfigTestNode","node_config":{"unknown":1}})json",
          .expect_success = false,
        },

        // Non-configurable (TestNode)
        {
          .name = "nonconfig_node_omitted",
          .node_block = R"json({"id":"n1","type":"TestNode"})json",
          .expect_success = true,
        },
        {
          .name = "nonconfig_node_null",
          .node_block = R"json({"id":"n1","type":"TestNode","node_config":null})json",
          .expect_success = true,
        },
        {
          .name = "nonconfig_node_object_valid_not_allowed",
          .node_block = R"json({"id":"n1","type":"TestNode","node_config":{}})json",
          .expect_success = false,
        },
        {
          .name = "nonconfig_node_object_invalid",
          .node_block = R"json({"id":"n1","type":"TestNode","node_config":{"sample_rate_hz":100}})json",
          .expect_success = false,
        },
      };

      auto provider_bundle = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);
      ASSERT_TRUE(provider_bundle);

      for (const auto& test_case : cases) {
        const std::string config =
          "{\n"
          "  \"name\": \"loader_matrix_" + test_case.name + "\",\n"
          "  \"num_threads\": 1,\n"
          "  \"nodes\": [\n"
          "    " + test_case.node_block + "\n"
          "  ],\n"
          "  \"edges\": []\n"
          "}\n";

        const auto path = WriteTempGraphConfig(config);
        const auto result = graph::config::JsonDynamicGraphLoader::LoadNodesSafe(
          path.string(), provider_bundle->provider);
        std::filesystem::remove(path);

        if (test_case.expect_success) {
          EXPECT_TRUE(result) << test_case.name << " error=" << static_cast<int>(result.error());
          if (result) {
            EXPECT_EQ(result->size(), 1u) << test_case.name;
          }
        } else {
          EXPECT_FALSE(result) << test_case.name;
          if (!result) {
            EXPECT_EQ(result.error(), app::error::ConfigError::ValidationFailed)
              << test_case.name;
          }
        }
      }
    }

