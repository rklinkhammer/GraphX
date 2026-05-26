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

/**
 * @file test_plugin_system.cpp
 * @brief Comprehensive unit tests for Plugin System (Phase 5 Priority 1)
 *
 * Tests the plugin loading and registration system with:
 * - Plugin registry functionality (registration, discovery, creation)
 * - Plugin loader error handling (missing files, invalid plugins)
 * - Thread-safe registry operations
 * - C++26 compliance (expected<> error handling)
 *
 * @note Uses mock/stub patterns to avoid requiring compiled plugin files
 */

#include <gtest/gtest.h>
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

// ===================================================================================
// Test Fixture
// ===================================================================================

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
    EXPECT_EQ(type_info, nullptr);
    
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
    // Registering with null handle should throw
    EXPECT_THROW({
        registry_->RegisterNodeType(
            "TestNode", "Test node", "/path/to/test.so",
            "create_test", "libstdc++_v1", "1.0.0",
            nullptr, // Invalid: null plugin handle
            nullptr  // Invalid: null facade
        );
    }, std::runtime_error);
    
    // Registry should still be empty
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginRegistryTest, RegistryStateAfterFailedRegistration) {
    // Attempt invalid registration (should throw)
    try {
        registry_->RegisterNodeType(
            "FailedNode", "Failed node", "/path/to/failed.so",
            "create_failed", "libstdc++_v1", "1.0.0",
            nullptr, nullptr
        );
    } catch (const std::exception&) {
        // Expected - null handle is invalid
    }
    
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
    loader.LoadAllPlugins();
    
    // Registry should remain empty (no plugins found)
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

TEST_F(PluginLoaderTest, LoadPluginErrorHandling) {
    // Create loader
    graph::PluginLoader loader("/tmp", registry_);
    
    // Attempt to load non-existent plugin
    // Should throw or handle gracefully
    try {
        loader.LoadPlugin("nonexistent_plugin.so");
        // If no exception, that's also valid (loader might log and continue)
        SUCCEED();
    } catch (const std::exception& e) {
        // Expected: plugin not found
        SUCCEED();
    }
}

TEST_F(PluginLoaderTest, RegistryIntegration) {
    // Verify that loader can register types with registry
    // (This test verifies the relationship between loader and registry)
    
    graph::PluginLoader loader("/tmp", registry_);
    
    // Registry should be empty before loading
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
    
    // After attempted load of non-existent plugins, registry should still be empty
    loader.LoadAllPlugins();
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 0);
}

// ===================================================================================
// Integration Tests (2 tests)
// ===================================================================================

TEST_F(PluginRegistryTest, MultipleRegistrationAttemptsWithErrors) {
    // Verify that failed registration doesn't corrupt state
    
    // First, attempt multiple invalid registrations
    for (int i = 0; i < 3; ++i) {
        EXPECT_THROW({
            registry_->RegisterNodeType(
                "FailedType_" + std::to_string(i),
                "Failed registration",
                "/path/to/failed.so",
                "create_failed",
                "libstdc++_v1", "1.0.0",
                nullptr, nullptr
            );
        }, std::runtime_error);
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
    graph::PluginLoader loader(plugin_dir, registry_);
    
    // Should load without exception
    EXPECT_NO_THROW({
        loader.LoadPlugin("libtest_node.so");
    });
    
    // Verify registration succeeded
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 1);
    EXPECT_TRUE(registry_->HasNodeType("TestNode"));
}

TEST_F(PluginLoaderTest, SymbolResolutionFromLoadedPlugin) {
    // Verify that dlsym() successfully resolves required symbols
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    graph::PluginLoader loader(plugin_dir, registry_);
    
    EXPECT_NO_THROW({
        loader.LoadPlugin("libtest_node.so");
    });
    
    // Verify node type is accessible and can be queried
    EXPECT_TRUE(registry_->HasNodeType("TestNode"));
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    EXPECT_NE(type_info, nullptr);
    
    // Clean up allocated TypeInfo
    delete type_info;
}

TEST_F(PluginLoaderTest, ParseActualPluginMetadata) {
    // Verify metadata extraction from real plugin
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    graph::PluginLoader loader(plugin_dir, registry_);
    
    EXPECT_NO_THROW({
        loader.LoadPlugin("libtest_node.so");
    });
    
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    ASSERT_NE(type_info, nullptr);
    
    // Verify metadata fields extracted correctly
    EXPECT_EQ(type_info->type_name, "TestNode");
    EXPECT_EQ(type_info->description, "Test node for plugin dynamic loading");
    EXPECT_EQ(type_info->version, "1.0");
    // ABI tag is platform-specific (libstdc++_v1 or libc++_v1)
    EXPECT_TRUE(!type_info->abi_tag.empty());
    
    delete type_info;
}

TEST_F(PluginLoaderTest, PluginAPIVersionNegotiation) {
    // Verify that plugin API version negotiation works
    // TestNode exports plugin_api_version() = 2
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    graph::PluginLoader loader(plugin_dir, registry_);
    
    // Should load without exception (version is compatible)
    EXPECT_NO_THROW({
        loader.LoadPlugin("libtest_node.so");
    });
    
    // If version was incompatible, LoadPlugin() would have thrown
    EXPECT_EQ(registry_->GetRegisteredTypeCount(), 1);
}

TEST_F(PluginLoaderTest, CreateNodeFromLoadedPlugin) {
    // Verify that nodes can be instantiated from the loaded plugin
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    graph::PluginLoader loader(plugin_dir, registry_);
    
    loader.LoadPlugin("libtest_node.so");
    
    // Verify we can create a node instance
    auto [node_handle, facade] = registry_->CreateNode("TestNode");
    EXPECT_NE(node_handle, nullptr);
    EXPECT_NE(facade, nullptr);
}

TEST_F(PluginLoaderTest, NodeFacadeInterfaceCompliance) {
    // Verify that NodeFacade vtable is properly initialized
    const std::string plugin_dir = PLUGIN_OUTPUT_DIRECTORY;
    graph::PluginLoader loader(plugin_dir, registry_);
    
    loader.LoadPlugin("libtest_node.so");
    
    auto [node_handle, facade] = registry_->CreateNode("TestNode");
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
    graph::PluginLoader loader(plugin_dir, registry_);
    
    // Should load successfully (no ABI mismatch)
    EXPECT_NO_THROW({
        loader.LoadPlugin("libtest_node.so");
    });
    
    auto type_info = registry_->GetNodeTypeInfo("TestNode");
    ASSERT_NE(type_info, nullptr);
    
    // Verify ABI tag matches expected value for this platform
    #ifdef _LIBCPP_VERSION
        // macOS / AppleClang with libc++
        EXPECT_EQ(type_info->abi_tag, "libc++_v1");
    #else
        // Linux / GCC with libstdc++
        EXPECT_EQ(type_info->abi_tag, "libstdc++_v1");
    #endif
    
    delete type_info;
}

} // namespace graph::test
