/**
 * @file test_plugin_inspector.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 graphlib contributors

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "config/SchemaGenerator.hpp"
#include "graph/NodeDescriptor.hpp"
#include "graph/NodeMetadataService.hpp"
#include "plugins/PluginInspector.hpp"

namespace {

namespace fs = std::filesystem;

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#if defined(_WIN32)
constexpr const char* kPluginExtension = ".dll";
#else
constexpr const char* kPluginExtension = ".so";
#endif

/**
 * @class StubInspectorDescriptorProvider
 * @brief Stub inspector descriptor provider implementation for GraphX.
 */
class StubInspectorDescriptorProvider final : public graph::INodeDescriptorProvider {
public:
    graph::NodeDescriptor BuildRuntimeDescriptor(
        graph::RuntimeNodeDescriptorRequest request) const override {
        auto descriptor = graph::BuildRuntimeNodeDescriptor(
            std::move(request.seed),
            request.parameterized,
            std::move(request.input_ports),
            std::move(request.output_ports));
        descriptor.name = "provider_overridden_name";
        return descriptor;
    }
};

/**
 * @class StubInspectorSchemaProvider
 * @brief Stub inspector schema provider implementation for GraphX.
 */
class StubInspectorSchemaProvider final : public graph::INodeDescriptorSchemaProvider {
public:
    nlohmann::json BuildSchema(const graph::NodeDescriptor& descriptor) const override {
        auto schema = graph::GenerateNodeDescriptorSchema(descriptor);
        schema["name"] = "schema_overridden_name";
        return schema;
    }
};

/**
 * @class StubInspectorMetadataService
 * @brief Stub inspector metadata service implementation for GraphX.
 */
class StubInspectorMetadataService final : public graph::INodeMetadataService {
public:
    const graph::INodeDescriptorProvider& DescriptorProvider() const override {
        return descriptor_provider_;
    }

    const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return schema_provider_;
    }

private:
    StubInspectorDescriptorProvider descriptor_provider_;
    StubInspectorSchemaProvider schema_provider_;
};

/**
 * @class PluginInspectorTest
 * @brief Plugin inspector test implementation for GraphX.
 */
class PluginInspectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        temp_dir_ = fs::temp_directory_path() / ("graphx_plugin_inspector_" + std::to_string(now));
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

/**
 * @brief Create plugin file.
 * @param stem Parameter for create plugin file.
 * @param bytes Parameter for create plugin file.
 */
    fs::path CreatePluginFile(const std::string& stem, const std::vector<char>& bytes) const {
        const fs::path plugin_path = temp_dir_ / ("lib" + stem + kPluginExtension);
        std::ofstream out(plugin_path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return plugin_path;
    }

    fs::path temp_dir_;
};

TEST_F(PluginInspectorTest, DiscoverPluginsExtractsVersionFromBinaryMetadata) {
    std::vector<char> payload = {
        static_cast<char>(0x7f), 'E', 'L', 'F',
        ' ', 'G', 'r', 'a', 'p', 'h', 'X',
        ' ', 'v', '9', '.', '8', '.', '7'
    };
    CreatePluginFile("alpha", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "alpha");
    EXPECT_EQ(plugins[0].version, "9.8.7");
}

TEST_F(PluginInspectorTest, DiscoverPluginsFallsBackToFilenameVersion) {
    std::vector<char> payload = {'n', 'o', 'n', '-', 'b', 'i', 'n', 'a', 'r', 'y'};
    CreatePluginFile("beta_v2.4.1", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "beta_v2.4.1");
    EXPECT_EQ(plugins[0].version, "2.4.1");
}

TEST_F(PluginInspectorTest, DiscoverPluginsDefaultsVersionWhenNoMetadataFound) {
    std::vector<char> payload = {'x', 'y', 'z'};
    CreatePluginFile("gamma", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "gamma");
    EXPECT_EQ(plugins[0].version, "1.0.0");
}

TEST_F(PluginInspectorTest, DiscoverPluginsAcceptsUppercaseVPrefixedSemver) {
    std::vector<char> payload = {
        static_cast<char>(0x7f), 'E', 'L', 'F',
        ' ', 'V', '4', '.', '5', '.', '6', ' '
    };
    CreatePluginFile("delta", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "delta");
    EXPECT_EQ(plugins[0].version, "4.5.6");
}

TEST_F(PluginInspectorTest, DiscoverPluginsPrefersPatchVersionOverTwoPartVersion) {
    std::vector<char> payload = {
        static_cast<char>(0x7f), 'E', 'L', 'F',
        ' ', '1', '.', '2', ' ',
        'x', ' ',
        'v', '3', '.', '4', '.', '5', ' '
    };
    CreatePluginFile("epsilon", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "epsilon");
    EXPECT_EQ(plugins[0].version, "3.4.5");
}

TEST_F(PluginInspectorTest, DiscoverPluginsRejectsEmbeddedAlnumVersionTokens) {
    std::vector<char> payload = {
        static_cast<char>(0x7f), 'E', 'L', 'F',
        'a', '1', '.', '2', '.', '3', ' ',
        'v', '9', '.', '8', '.', '7', 'z'
    };
    CreatePluginFile("zeta", payload);

    graph::PluginInspector inspector(temp_dir_.string());
    const auto plugins = inspector.DiscoverPlugins();

    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].name, "zeta");
    EXPECT_EQ(plugins[0].version, "1.0.0");
}

TEST_F(PluginInspectorTest, InspectPluginDetectsMetricsCapabilityFromFacade) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const auto source = inspector.InspectPlugin("source_test_node");
    const auto test = inspector.InspectPlugin("test_node");

    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;
    ASSERT_TRUE(test.info.is_loaded) << test.info.load_error;

    EXPECT_TRUE(source.HasIMetricsCallback());
    EXPECT_FALSE(test.HasIMetricsCallback());
}

TEST_F(PluginInspectorTest, InspectPluginReflectsFacadeCallbackPresence) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const auto source = inspector.InspectPlugin("source_test_node");
    const auto test = inspector.InspectPlugin("test_node");

    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;
    ASSERT_TRUE(test.info.is_loaded) << test.info.load_error;

    EXPECT_TRUE(source.HasIConfigurable());
    EXPECT_FALSE(source.HasIDiagnosable());
    EXPECT_TRUE(source.HasIParameterized());

    EXPECT_FALSE(test.HasIConfigurable());
    EXPECT_FALSE(test.HasIDiagnosable());
    EXPECT_FALSE(test.HasIParameterized());
}

TEST_F(PluginInspectorTest, InspectPluginJsonIncludesNodeDescriptorSchema) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const auto source = inspector.InspectPlugin("source_test_node");
    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;

    const auto json = source.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));

    const auto& schema = json["node_descriptor_schema"];
    EXPECT_TRUE(schema.is_object());
    EXPECT_TRUE(schema.contains("name"));
    EXPECT_TRUE(schema.contains("type"));
    EXPECT_TRUE(schema.contains("lifecycle_state"));
    EXPECT_TRUE(schema.contains("supports_configuration"));
    EXPECT_TRUE(schema.contains("config_fields"));
    EXPECT_TRUE(schema.contains("inputs"));
    EXPECT_TRUE(schema.contains("outputs"));
    EXPECT_TRUE(schema["supports_configuration"].is_boolean());
    EXPECT_TRUE(schema["config_fields"].is_array());
    EXPECT_TRUE(schema["inputs"].is_array());
    EXPECT_TRUE(schema["outputs"].is_array());

    if (!schema["config_fields"].empty()) {
        const auto& field = schema["config_fields"][0];
        EXPECT_TRUE(field.contains("name"));
        EXPECT_TRUE(field.contains("type"));
        EXPECT_TRUE(field.contains("required"));
    }
}

TEST_F(PluginInspectorTest, InspectSinkPluginJsonIncludesTypedRequiredConfigField) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const auto sink = inspector.InspectPlugin("sink_test_node");
    ASSERT_TRUE(sink.info.is_loaded) << sink.info.load_error;

    const auto json = sink.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));

    const auto& schema = json["node_descriptor_schema"];
    ASSERT_TRUE(schema.contains("supports_configuration"));
    ASSERT_TRUE(schema["supports_configuration"].is_boolean());
    EXPECT_TRUE(schema["supports_configuration"].get<bool>());

    ASSERT_TRUE(schema.contains("config_fields"));
    ASSERT_TRUE(schema["config_fields"].is_array());
    ASSERT_EQ(schema["config_fields"].size(), 1u);

    const auto& field = schema["config_fields"][0];
    EXPECT_EQ(field.value("name", ""), "expected_message_count");
    EXPECT_EQ(field.value("type", ""), "integer");
    ASSERT_TRUE(field.contains("required"));
    EXPECT_TRUE(field["required"].get<bool>());
}

TEST_F(PluginInspectorTest, ConfigurableTestPluginsExposeTypedRequiredConfigFields) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const std::vector<std::string> configurable_plugins = {
        "source_test_node",
        "sink_test_node"
    };

    for (const auto& plugin_name : configurable_plugins) {
        const auto plugin = inspector.InspectPlugin(plugin_name);
        ASSERT_TRUE(plugin.info.is_loaded) << plugin_name << ": " << plugin.info.load_error;

        const auto json = plugin.ToJson();
        ASSERT_TRUE(json.contains("node_descriptor_schema")) << plugin_name;

        const auto& schema = json["node_descriptor_schema"];
        ASSERT_TRUE(schema.contains("supports_configuration")) << plugin_name;
        ASSERT_TRUE(schema["supports_configuration"].is_boolean()) << plugin_name;
        EXPECT_TRUE(schema["supports_configuration"].get<bool>()) << plugin_name;

        ASSERT_TRUE(schema.contains("config_fields")) << plugin_name;
        ASSERT_TRUE(schema["config_fields"].is_array()) << plugin_name;
        ASSERT_FALSE(schema["config_fields"].empty()) << plugin_name;

        for (const auto& field : schema["config_fields"]) {
            ASSERT_TRUE(field.is_object()) << plugin_name;
            ASSERT_TRUE(field.contains("name")) << plugin_name;
            ASSERT_TRUE(field["name"].is_string()) << plugin_name;
            EXPECT_FALSE(field["name"].get<std::string>().empty()) << plugin_name;

            ASSERT_TRUE(field.contains("type")) << plugin_name;
            ASSERT_TRUE(field["type"].is_string()) << plugin_name;
            EXPECT_FALSE(field["type"].get<std::string>().empty()) << plugin_name;

            ASSERT_TRUE(field.contains("required")) << plugin_name;
            EXPECT_TRUE(field["required"].is_boolean()) << plugin_name;
        }
    }
}

TEST_F(PluginInspectorTest, InspectPluginUsesInjectedDescriptorProvider) {
/**
 * @class DescriptorOnlyMetadataService
 * @brief Descriptor only metadata service implementation for GraphX.
 */
    class DescriptorOnlyMetadataService final : public graph::INodeMetadataService {
    public:
        const graph::INodeDescriptorProvider& DescriptorProvider() const override {
            return descriptor_provider_;
        }

        const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
            return graph::GetDefaultNodeDescriptorSchemaProvider();
        }

    private:
        StubInspectorDescriptorProvider descriptor_provider_;
    } metadata_service;

    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY, &metadata_service);

    const auto source = inspector.InspectPlugin("source_test_node");
    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;

    const auto json = source.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));

    const auto& schema = json["node_descriptor_schema"];
    ASSERT_TRUE(schema.contains("name"));
    EXPECT_EQ(schema["name"], "provider_overridden_name");
}

TEST_F(PluginInspectorTest, InspectPluginUsesInjectedDescriptorSchemaProvider) {
/**
 * @class SchemaOnlyMetadataService
 * @brief Schema only metadata service implementation for GraphX.
 */
    class SchemaOnlyMetadataService final : public graph::INodeMetadataService {
    public:
        const graph::INodeDescriptorProvider& DescriptorProvider() const override {
            return graph::GetDefaultNodeDescriptorProvider();
        }

        const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
            return schema_provider_;
        }

    private:
        StubInspectorSchemaProvider schema_provider_;
    } metadata_service;

    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY, &metadata_service);

    const auto source = inspector.InspectPlugin("source_test_node");
    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;

    const auto json = source.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));

    const auto& schema = json["node_descriptor_schema"];
    ASSERT_TRUE(schema.contains("name"));
    EXPECT_EQ(schema["name"], "schema_overridden_name");
}

TEST_F(PluginInspectorTest, InspectPluginUsesInjectedMetadataService) {
    StubInspectorMetadataService metadata_service;
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY, &metadata_service);

    const auto source = inspector.InspectPlugin("source_test_node");
    ASSERT_TRUE(source.info.is_loaded) << source.info.load_error;

    const auto json = source.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));
    const auto& schema = json["node_descriptor_schema"];

    ASSERT_TRUE(schema.contains("name"));
    EXPECT_EQ(schema["name"], "schema_overridden_name");
}

TEST_F(PluginInspectorTest, InspectOptionalConfigPluginHasNoRequiredConfigFields) {
    graph::PluginInspector inspector(PLUGIN_OUTPUT_DIRECTORY);

    const auto optional_node = inspector.InspectPlugin("optional_config_test_node");
    ASSERT_TRUE(optional_node.info.is_loaded) << optional_node.info.load_error;

    const auto json = optional_node.ToJson();
    ASSERT_TRUE(json.contains("node_descriptor_schema"));

    const auto& schema = json["node_descriptor_schema"];
    ASSERT_TRUE(schema.contains("supports_configuration"));
    ASSERT_TRUE(schema["supports_configuration"].is_boolean());
    EXPECT_TRUE(schema["supports_configuration"].get<bool>());

    ASSERT_TRUE(schema.contains("config_fields"));
    ASSERT_TRUE(schema["config_fields"].is_array());
    EXPECT_TRUE(schema["config_fields"].empty());
}

}  // namespace
