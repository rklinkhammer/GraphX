/**
 * @file test_plugin_reflection.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 graphlib contributors

#include <array>

#include <gtest/gtest.h>

#include "core/PluginReflection.hpp"

namespace app::reflection {

struct ReflectionPluginAlpha {};
struct ReflectionPluginBeta {};

inline constexpr std::array<CapabilityMetadata, 2> kAlphaCapabilities{{
    CapabilityMetadata{
        .name = "IConfigurable",
        .description = "Supports JSON configuration",
        .is_required = true,
        .is_experimental = false,
        .version_introduced = "1.0.0",
        .min_version = "1.0.0"
    },
    CapabilityMetadata{
        .name = "IMetricsCallbackProvider",
        .description = "Publishes metrics callbacks",
        .is_required = false,
        .is_experimental = false,
        .version_introduced = "1.1.0",
        .min_version = "1.1.0"
    }
}};

inline constexpr std::array<CapabilityMetadata, 1> kBetaCapabilities{{
    CapabilityMetadata{
        .name = "IDiagnosable",
        .description = "Provides diagnostics output",
        .is_required = false,
        .is_experimental = false,
        .version_introduced = "1.0.0",
        .min_version = "1.0.0"
    }
}};

template<>
consteval std::span<const CapabilityMetadata> GetCapabilities<ReflectionPluginAlpha>() {
    return kAlphaCapabilities;
}

template<>
consteval std::span<const CapabilityMetadata> GetCapabilities<ReflectionPluginBeta>() {
    return kBetaCapabilities;
}

template<>
consteval PluginMetadata GetPluginMetadata<ReflectionPluginAlpha>() {
    return PluginMetadata{
        .name = "ReflectionPluginAlpha",
        .version = "1.2.0",
        .description = "Alpha plugin",
        .author = "GraphX",
        .license = "MIT",
        .capabilities = GetCapabilities<ReflectionPluginAlpha>()
    };
}

template<>
consteval PluginMetadata GetPluginMetadata<ReflectionPluginBeta>() {
    return PluginMetadata{
        .name = "ReflectionPluginBeta",
        .version = "1.0.0",
        .description = "Beta plugin",
        .author = "GraphX",
        .license = "MIT",
        .capabilities = GetCapabilities<ReflectionPluginBeta>()
    };
}

}  // namespace app::reflection

namespace {

TEST(PluginReflectionRegistryTest, RegisterAndListPlugins) {
    app::reflection::PluginRegistry registry;
    app::reflection::ReflectionPluginAlpha alpha;
    app::reflection::ReflectionPluginBeta beta;

    registry.Register(alpha);
    registry.Register(beta);

    const auto all_plugins = registry.GetAllPlugins();
    ASSERT_EQ(all_plugins.size(), 2u);

    EXPECT_EQ(all_plugins[0].name, "ReflectionPluginAlpha");
    EXPECT_EQ(all_plugins[1].name, "ReflectionPluginBeta");
}

TEST(PluginReflectionRegistryTest, FindPluginsByCapabilityReturnsMatchingPlugins) {
    app::reflection::PluginRegistry registry;
    app::reflection::ReflectionPluginAlpha alpha;
    app::reflection::ReflectionPluginBeta beta;

    registry.Register(alpha);
    registry.Register(beta);

    const auto configurable = registry.FindPluginsByCapability("IConfigurable");
    ASSERT_EQ(configurable.size(), 1u);
    EXPECT_EQ(configurable[0].name, "ReflectionPluginAlpha");

    const auto diagnosable = registry.FindPluginsByCapability("IDiagnosable");
    ASSERT_EQ(diagnosable.size(), 1u);
    EXPECT_EQ(diagnosable[0].name, "ReflectionPluginBeta");
}

TEST(PluginReflectionRegistryTest, RegisterUpdatesExistingMetadataByPluginName) {
    app::reflection::PluginRegistry registry;
    app::reflection::ReflectionPluginAlpha alpha_first;
    app::reflection::ReflectionPluginAlpha alpha_second;

    registry.Register(alpha_first);
    registry.Register(alpha_second);

    const auto all_plugins = registry.GetAllPlugins();
    ASSERT_EQ(all_plugins.size(), 1u);
    EXPECT_EQ(all_plugins[0].name, "ReflectionPluginAlpha");
}

}  // namespace
