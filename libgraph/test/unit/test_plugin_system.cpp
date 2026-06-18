/**
 * @file test_plugin_system.cpp
 * @brief Test Plugin System Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include <gtest/gtest.h>
#include "graph/NodeProviderBootstrap.hpp"
#include "graph/NodeProvider.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include <thread>
#include <vector>
#include <memory>

// Define plugin directory if not provided by CMake
#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
#endif

namespace graph::test {

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

/**
 * @brief Test node plugin filename.
 */
std::string TestNodePluginFilename() {
    return std::string("libtest_node") + kSharedLibraryExtension;
}

/**
 * @class MockNodeProvider
 * @brief Mock node provider implementation for GraphX.
 */
class MockNodeProvider final : public graph::INodeProvider {
public:
    std::expected<graph::NodeFacadeAdapter, graph::NodeCreationError>
    CreateNodeExpected(const std::string&) noexcept override {
        return std::unexpected(graph::NodeCreationError::TypeNotFound);
    }

    bool IsNodeTypeAvailable(const std::string& node_type_name) const override {
        return node_type_name == "MockNode";
    }

    std::vector<std::string> GetAvailableNodeTypes() const override {
        return {"MockNode"};
    }
};

}  // namespace

// ===================================================================================
// Test Fixture
// ===================================================================================

/**
 * @class PluginRegistryTest
 * @brief Plugin registry test implementation for GraphX.
 */
class PluginRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = std::make_shared<graph::PluginRegistry>();
    }
    
    void TearDown() override {
        if (registry_) {
            registry_->Clear();
        }
    }
    
    std::shared_ptr<graph::PluginRegistry> registry_;
};

/**
 * @class PluginLoaderTest
 * @brief Plugin loader test implementation for GraphX.
 */
class PluginLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = std::make_shared<graph::PluginRegistry>();
    }
    
    void TearDown() override {
        if (registry_) {
            registry_->Clear();
        }
    }
    
    std::shared_ptr<graph::PluginRegistry> registry_;
};

// ===================================================================================
// PluginRegistry Tests (6 tests)
// ===================================================================================

TEST_F(PluginRegistryTest, ConstructionAndInitialization) {
    // Registry should start empty
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
    EXPECT_EQ(registry_->GetRegisteredNodeTypes().size(), 0);
}

TEST_F(PluginRegistryTest, QueryNonexistentType) {
    // Empty registry should return empty results for queries
    EXPECT_FALSE(registry_->HasNodeType("NonexistentNode"));
    auto type_info = registry_->GetNodeTypeInfo("NonexistentNode");
    EXPECT_FALSE(type_info);
    
    // GetRegisteredNodeTypes should return empty vector
    auto types = registry_->GetRegisteredNodeTypes();
    EXPECT_EQ(types.size(), 0);
}

TEST_F(PluginRegistryTest, UnregisterFromEmptyRegistry) {
    // Unregistering from empty registry should return false
    bool result = registry_->UnregisterNodeType("NonexistentNode");
    EXPECT_FALSE(result);
}

TEST_F(PluginRegistryTest, ClearEmptyRegistry) {
    // Clearing empty registry should not fail
    registry_->Clear();
    
    // Should still be empty
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginRegistryTest, RegisterNodeTypeWithInvalidHandle) {
    auto result = registry_->RegisterNodeTypeExpected(
        "TestNode", "Test node", "/path/to/test.so",
        "create_test", "libstdc++_v1", "1.0.0",
        nullptr,
        nullptr
    );
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::PluginRegistry::PluginRegistryError::InvalidPluginHandle);
    
    // Registry should still be empty
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginRegistryTest, RegisterNodeTypeExpectedReportsInvalidHandle) {
    auto result = registry_->RegisterNodeTypeExpected(
        "TestNode", "Test node", "/path/to/test.so",
        "create_test", "libstdc++_v1", "1.0.0",
        nullptr,
        nullptr
    );

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::PluginRegistry::PluginRegistryError::InvalidPluginHandle);
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginRegistryTest, RegistryStateAfterFailedRegistration) {
    auto result = registry_->RegisterNodeTypeExpected(
        "FailedNode", "Failed node", "/path/to/failed.so",
        "create_failed", "libstdc++_v1", "1.0.0",
        nullptr, nullptr
    );
    ASSERT_FALSE(result);
    
    // Registry should remain empty and consistent
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
    EXPECT_FALSE(registry_->HasNodeType("FailedNode"));
}

// ===================================================================================
// PluginLoader Tests (4 tests)
// ===================================================================================

TEST_F(PluginLoaderTest, ConstructionWithValidDirectory) {
    // Create loader with a valid (existing) directory
    // Using /tmp which should exist on all systems
    graph::PluginLoader loader("/tmp", registry_);
    
    // Loader should be constructed (no exception)
    // Registry should remain empty until plugins are loaded
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginLoaderTest, LoadAllPluginsFromEmptyDirectory) {
    // Create loader for /tmp (which typically has no .so/.dylib files for our plugins)
    graph::PluginLoader loader("/tmp", registry_);
    
    // Loading should succeed but find no plugins
    ASSERT_TRUE(loader.LoadAllPluginsSafe());
    
    // Registry should remain empty (no plugins found)
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginLoaderTest, LoadPluginErrorHandling) {
    // Create loader
    graph::PluginLoader loader("/tmp", registry_);
    
    auto result = loader.LoadPluginSafe("nonexistent_plugin.so");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::PluginLoadError::FileNotFound);
}

TEST_F(PluginLoaderTest, LoadPluginSafeReturnsFileNotFoundForMissingPlugin) {
    graph::PluginLoader loader("/tmp", registry_);

    const auto result = loader.LoadPluginSafe("nonexistent_plugin.so");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::PluginLoadError::FileNotFound);
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginLoaderTest, RegistryIntegration) {
    // Verify that loader can register types with registry
    // (This test verifies the relationship between loader and registry)
    
    graph::PluginLoader loader("/tmp", registry_);
    
    // Registry should be empty before loading
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
    
    // After attempted load of non-existent plugins, registry should still be empty
    ASSERT_TRUE(loader.LoadAllPluginsSafe());
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

// ===================================================================================
// Integration Tests (2 tests)
// ===================================================================================

TEST_F(PluginRegistryTest, MultipleRegistrationAttemptsWithErrors) {
    // Verify that failed registration doesn't corrupt state
    
    // First, attempt multiple invalid registrations
    for (int i = 0; i < 3; ++i) {
        auto result = registry_->RegisterNodeTypeExpected(
            "FailedType_" + std::to_string(i),
            "Failed registration",
            "/path/to/failed.so",
            "create_failed",
            "libstdc++_v1", "1.0.0",
            nullptr, nullptr
        );
        ASSERT_FALSE(result);
    }
    
    // Registry should still be empty and functional
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
    
    // Clear should work without issues
    registry_->Clear();
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginLoaderTest, PluginLoaderConstruction) {
    // Loader should construct successfully with valid directory
    graph::PluginLoader loader("/tmp", registry_);
    
    // Registry starts empty
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

// ===================================================================================
// Dynamic Loading Integration Tests (7 tests) - Phase 5.1.X Enhancement
// ===================================================================================
// These tests validate real dynamic loading with the compiled TestNode plugin
// The TestNode plugin provides a concrete example of a plugin that can be loaded
// via dlopen/dlsym and demonstrates the full plugin lifecycle

TEST_F(PluginLoaderTest, LoadActualTestNodePlugin) {
    // Load the actual compiled test_node plugin
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    // Verify registration succeeded
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 1);
    EXPECT_TRUE(registry_->HasNodeType("TestNode"));
}

TEST_F(PluginLoaderTest, LoadPluginSafeLoadsActualTestNodePlugin) {
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);

    const auto result = loader.LoadPluginSafe(plugin_filename);

    ASSERT_TRUE(result);
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 1);
    EXPECT_TRUE(registry_->HasNodeType("TestNode"));
}

TEST_F(PluginLoaderTest, LoadPluginSafeReportsInitializationFailureWithoutRegistry) {
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, nullptr);

    const auto result = loader.LoadPluginSafe(plugin_filename);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::error::PluginLoadError::InitializationFailed);
}

TEST_F(PluginLoaderTest, SymbolResolutionFromLoadedPlugin) {
    // Verify that dlsym() successfully resolves required symbols
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    // Verify node type is accessible and can be queried
    EXPECT_TRUE(registry_->HasNodeType("TestNode"));
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    EXPECT_TRUE(type_info);
}

TEST_F(PluginLoaderTest, ParseActualPluginMetadata) {
    // Verify metadata extraction from real plugin
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    ASSERT_TRUE(type_info);
    
    // Verify metadata fields extracted correctly
    EXPECT_EQ(type_info->type_name, "TestNode");
    EXPECT_EQ(type_info->description, "Test node for plugin dynamic loading");
    EXPECT_EQ(type_info->version, "1.0");
    // ABI tag is platform-specific (libstdc++_v1 or libc++_v1)
    EXPECT_TRUE(!type_info->abi_tag.empty());
}

TEST_F(PluginLoaderTest, PluginAPIRequiredVersionMatchesCurrent) {
    // Verify that loaded plugins match the current required plugin API version.
    // TestNode exports plugin_api_version() = 2
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));

    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 1);
}

TEST_F(PluginLoaderTest, CreateNodeFromLoadedPlugin) {
    // Verify that nodes can be instantiated from the loaded plugin
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    // Verify we can create a node instance
    auto created = registry_->CreateNodeExpected("TestNode");
    ASSERT_TRUE(created);
    auto [node_handle, facade] = *created;
    EXPECT_NE(node_handle, nullptr);
    EXPECT_NE(facade, nullptr);
}

TEST_F(PluginLoaderTest, CreateNodeExpectedFromLoadedPlugin) {
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);

    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));

    auto result = registry_->CreateNodeExpected("TestNode");
    ASSERT_TRUE(result);
    auto [node_handle, facade] = *result;
    EXPECT_NE(node_handle, nullptr);
    EXPECT_NE(facade, nullptr);
}

TEST_F(PluginLoaderTest, CreateNodeExpectedReportsMissingType) {
    auto result = registry_->CreateNodeExpected("MissingNode");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::PluginRegistry::PluginRegistryError::TypeNotRegistered);
}

TEST(NodeProviderBootstrapExpectedTest, CreateProviderExpectedCreatesEmptyProviderForMissingDirectory) {
    auto result = app::NodeProviderBootstrap::CreateProviderExpected(
        "/tmp/graphx_provider_bootstrap_missing_plugin_dir_for_expected_test"
    );

    ASSERT_TRUE(result);
    EXPECT_NE(result->provider, nullptr);
    ASSERT_NE(result->lifetime, nullptr);
    EXPECT_NE(result->lifetime->loader, nullptr);
}

TEST(NodeProviderBootstrapExpectedTest, CreateProviderExpectedReportsDiagnosticsAndLifetime) {
    auto result = app::NodeProviderBootstrap::CreateProviderExpected(PLUGIN_OUTPUT_DIRECTORY);

    ASSERT_TRUE(result);
    EXPECT_NE(result->provider, nullptr);
    EXPECT_NE(result->lifetime, nullptr);
    EXPECT_NE(result->lifetime->plugin_registry, nullptr);
    EXPECT_NE(result->lifetime->loader, nullptr);
    EXPECT_EQ(result->diagnostics.plugin_directory, PLUGIN_OUTPUT_DIRECTORY);
    EXPECT_GE(result->diagnostics.discovered_count, result->diagnostics.loaded_count);
    EXPECT_EQ(result->diagnostics.discovered_count,
              result->diagnostics.loaded_count + result->diagnostics.failed_count);
    EXPECT_GE(result->diagnostics.scan_duration.count(), 0);
    EXPECT_GE(result->diagnostics.init_duration.count(), 0);
}

TEST(NodeProviderBootstrapExpectedTest, CreateProviderExpectedRejectsFilePath) {
    const std::string plugin_file =
        std::string(PLUGIN_OUTPUT_DIRECTORY) + "/" + TestNodePluginFilename();

    auto result = app::NodeProviderBootstrap::CreateProviderExpected(plugin_file);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), app::NodeProviderBootstrap::ProviderBootstrapError::InvalidPluginDirectory);
}

TEST(NodeProviderBootstrapExpectedTest, QueryExpectedReportsNullProvider) {
    auto types = app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(nullptr);
    ASSERT_FALSE(types);
    EXPECT_EQ(types.error(), app::NodeProviderBootstrap::ProviderBootstrapError::NullProvider);

    auto available = app::NodeProviderBootstrap::IsNodeTypeAvailableExpected(nullptr, "TestNode");
    ASSERT_FALSE(available);
    EXPECT_EQ(available.error(), app::NodeProviderBootstrap::ProviderBootstrapError::NullProvider);
}

TEST(NodeProviderBootstrapExpectedTest, QueryExpectedUsesNodeProviderInterface) {
    std::shared_ptr<graph::INodeProvider> provider = std::make_shared<MockNodeProvider>();

    auto types = app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(provider);
    ASSERT_TRUE(types);
    ASSERT_EQ(types->size(), 1u);
    EXPECT_EQ(types->front(), "MockNode");

    auto available = app::NodeProviderBootstrap::IsNodeTypeAvailableExpected(provider, "MockNode");
    ASSERT_TRUE(available);
    EXPECT_TRUE(*available);

    auto missing = app::NodeProviderBootstrap::IsNodeTypeAvailableExpected(provider, "MissingNode");
    ASSERT_TRUE(missing);
    EXPECT_FALSE(*missing);
}

TEST_F(PluginLoaderTest, UnloadPluginUnregistersNodeTypes) {
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);

    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    ASSERT_TRUE(registry_->HasNodeType("TestNode"));

    EXPECT_TRUE(loader.UnloadPlugin(plugin_filename));
    EXPECT_FALSE(registry_->HasNodeType("TestNode"));
    auto created = registry_->CreateNodeExpected("TestNode");
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error(), graph::PluginRegistry::PluginRegistryError::TypeNotRegistered);
}

TEST_F(PluginLoaderTest, NodeFacadeInterfaceCompliance) {
    // Verify that NodeFacade vtable is properly initialized
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    auto created = registry_->CreateNodeExpected("TestNode");
    ASSERT_TRUE(created);
    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);
    
    // Verify essential facade methods are implemented (non-null function pointers)
    // Required lifecycle methods
    EXPECT_NE(facade->Init, nullptr);
    EXPECT_NE(facade->Start, nullptr);
    EXPECT_NE(facade->Stop, nullptr);
    EXPECT_NE(facade->Join, nullptr);
    
    // Metadata and introspection methods
    EXPECT_NE(facade->GetInputPortCount, nullptr);
    EXPECT_NE(facade->GetOutputPortCount, nullptr);
    EXPECT_NE(facade->GetInputPortMetadata, nullptr);
    EXPECT_NE(facade->GetOutputPortMetadata, nullptr);
}

TEST_F(PluginLoaderTest, ABICompatibilityValidation) {
    // Verify that ABI compatibility is properly detected
    // TestNode plugin should be compiled with same ABI as the application
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    const std::string plugin_filename = TestNodePluginFilename();
    graph::PluginLoader loader(plugin_dir, registry_);
    
    ASSERT_TRUE(loader.LoadPluginSafe(plugin_filename));
    
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    ASSERT_TRUE(type_info);
    
    // Verify ABI tag matches expected value for this platform
    #ifdef _LIBCPP_VERSION
        // macOS / AppleClang with libc++
        EXPECT_EQ(type_info->abi_tag, "libc++_v1");
    #else
        // Linux / GCC with libstdc++
        EXPECT_EQ(type_info->abi_tag, "libstdc++_v1");
    #endif
}

} // namespace graph::test
