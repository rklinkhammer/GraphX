// MIT License
//
// Copyright (c) 2026 graphlib contributors

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

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

}  // namespace
