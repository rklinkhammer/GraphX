/**
 * @file test_plugin_reflection.cpp
 * @brief Test Plugin Reflection Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

TEST(PluginReflectionMetadataTest, MetadataRemainsCompileTimeOnly) {
    constexpr auto alpha =
        app::reflection::GetPluginMetadata<app::reflection::ReflectionPluginAlpha>();
    constexpr auto beta =
        app::reflection::GetPluginMetadata<app::reflection::ReflectionPluginBeta>();

    static_assert(alpha.name == "ReflectionPluginAlpha");
    static_assert(alpha.capabilities.size() == 2u);
    static_assert(beta.name == "ReflectionPluginBeta");
    static_assert(beta.capabilities.size() == 1u);

    EXPECT_EQ(alpha.capabilities.front().name, "IConfigurable");
    EXPECT_EQ(beta.capabilities.front().name, "IDiagnosable");
}

}  // namespace
