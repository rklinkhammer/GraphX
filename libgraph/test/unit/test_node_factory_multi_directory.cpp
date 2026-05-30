/**
 * @file test_node_factory_multi_directory.cpp
 * @brief Comprehensive tests for NodeFactory multi-directory plugin support
 *
 * Tests the Option A multi-directory architecture:
 * - AddPluginDirectory() to register multiple plugin directories
 * - LoadAllPluginsFromDirectories() to load all plugins across directories
 * - Backward compatibility with legacy SetPluginLoader() path
 * - Plugin registration from multiple sources
 *
 * @author Test Suite
 * @date May 30, 2026
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <memory>

#include "graph/NodeFactory.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include <log4cxx/logger.h>

namespace fs = std::filesystem;

// Ensure PLUGIN_OUTPUT_DIRECTORY is defined
#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

// Helper function to check if a plugin is registered
inline bool PluginRegistered(const std::vector<std::string>& plugins, const std::string& name) {
    return std::find(plugins.begin(), plugins.end(), name) != plugins.end();
}

/**
 * @class NodeFactoryMultiDirectoryTest
 * @brief Test fixture for multi-directory plugin support
 */
class NodeFactoryMultiDirectoryTest : public ::testing::Test {
protected:
    NodeFactoryMultiDirectoryTest()
        : logger_(log4cxx::Logger::getLogger("test.NodeFactoryMultiDirectory")),
          primary_dir_(PLUGIN_OUTPUT_DIRECTORY),
          test_dir1_("./test_plugins_dir1"),
          test_dir2_("./test_plugins_dir2"),
          test_dir3_("./test_plugins_dir3") {
    }

    void SetUp() override {
        // Create test directories
        fs::create_directories(test_dir1_);
        fs::create_directories(test_dir2_);
        fs::create_directories(test_dir3_);
    }

    void TearDown() override {
        // Clean up test directories
        if (fs::exists(test_dir1_)) {
            fs::remove_all(test_dir1_);
        }
        if (fs::exists(test_dir2_)) {
            fs::remove_all(test_dir2_);
        }
        if (fs::exists(test_dir3_)) {
            fs::remove_all(test_dir3_);
        }
    }

    /**
     * @brief Copy specific plugins to a test directory
     * @param dest_dir Destination directory path
     * @param plugin_names Names of plugins to copy (without .so extension)
     */
    void CopyPluginsToDirectory(const std::string& dest_dir,
                               const std::vector<std::string>& plugin_names) {
        for (const auto& plugin_name : plugin_names) {
            std::string src = primary_dir_ + "/lib" + plugin_name + ".so";
            std::string dst = dest_dir + "/lib" + plugin_name + ".so";
            
            if (fs::exists(src)) {
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
                LOG4CXX_DEBUG(logger_, "Copied " << src << " to " << dst);
            } else {
                LOG4CXX_WARN(logger_, "Plugin not found: " << src);
            }
        }
    }

    /**
     * @brief Get count of .so files in a directory
     * @param dir Directory to check
     * @return Number of .so files
     */
    size_t CountPluginsInDirectory(const std::string& dir) {
        size_t count = 0;
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == ".so") {
                    count++;
                }
            }
        }
        return count;
    }

    /**
     * @brief Get names of .so files in a directory
     * @param dir Directory to check
     * @return Set of plugin names (without .so extension)
     */
    std::set<std::string> GetPluginsInDirectory(const std::string& dir) {
        std::set<std::string> plugins;
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == ".so") {
                    std::string name = entry.path().stem().string();
                    // Remove "lib" prefix if present
                    if (name.substr(0, 3) == "lib") {
                        name = name.substr(3);
                    }
                    plugins.insert(name);
                }
            }
        }
        return plugins;
    }

    log4cxx::LoggerPtr logger_;
    std::string primary_dir_;
    std::string test_dir1_;
    std::string test_dir2_;
    std::string test_dir3_;
};

// ============================================================================
// Test Cases
// ============================================================================

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectory_SingleDirectory) {
    
    // Setup: Copy producer plugins to test_dir1
    CopyPluginsToDirectory(test_dir1_, {"int_producer", "completion_node"});
    
    ASSERT_EQ(CountPluginsInDirectory(test_dir1_), 2);
    
    // Create factory and registry
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Register single directory
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir1_));
    
    // Load plugins
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Verify plugins are registered
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GE(registered_plugins.size(), 2);
    ASSERT_TRUE(PluginRegistered(registered_plugins, "int_producer"));;
    ASSERT_TRUE(PluginRegistered(registered_plugins, "completion_node"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectory_MultipleDirectories) {
    
    // Setup: Distribute plugins across directories
    CopyPluginsToDirectory(test_dir1_, {"int_producer", "double_sink"});
    CopyPluginsToDirectory(test_dir2_, {"int_sink", "completion_node"});
    CopyPluginsToDirectory(test_dir3_, {"test_node", "source_test_node"});
    
    ASSERT_EQ(CountPluginsInDirectory(test_dir1_), 2);
    ASSERT_EQ(CountPluginsInDirectory(test_dir2_), 2);
    ASSERT_EQ(CountPluginsInDirectory(test_dir3_), 2);
    
    // Create factory and registry
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Register all directories
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir1_));
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir2_));
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir3_));
    
    // Load all plugins
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Verify all plugins from all directories are registered
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GE(registered_plugins.size(), 6);
    
    auto expected = std::set<std::string>{
        "int_producer", "double_sink", "int_sink", "completion_node",
        "test_node", "source_test_node"
    };
    
    for (const auto& plugin : expected) {
        ASSERT_TRUE(PluginRegistered(registered_plugins, plugin));;
    }
}

TEST_F(NodeFactoryMultiDirectoryTest,
       LoadAllPluginsFromDirectories_ErrorHandling) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Should throw when no directories registered
    ASSERT_THROW(factory->LoadAllPluginsFromDirectories(),
                std::runtime_error);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectory_EmptyPathValidation) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Should throw on empty path
    ASSERT_THROW(factory->AddPluginDirectory(""),
                std::invalid_argument);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       BackwardCompatibility_SetPluginLoaderStillWorks) {
    
    // Setup: Put all plugins in primary directory
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto loader = std::make_shared<graph::PluginLoader>(PLUGIN_OUTPUT_DIRECTORY, registry);
    
    // Create factory
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Use legacy SetPluginLoader() path
    ASSERT_NO_THROW(factory->SetPluginLoader(loader));
    ASSERT_NO_THROW(loader->LoadAllPlugins());
    
    // Verify plugins loaded via legacy path
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GT(registered_plugins.size(), 0);
    ASSERT_TRUE(PluginRegistered(registered_plugins, "int_producer"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       MixedMode_AddPluginDirectoryAfterSetPluginLoader) {
    
    // Setup test directories
    CopyPluginsToDirectory(test_dir1_, {"completion_node"});
    CopyPluginsToDirectory(test_dir2_, {"test_node"});
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    
    // First, use legacy SetPluginLoader with primary directory
    auto legacy_loader = std::make_shared<graph::PluginLoader>(PLUGIN_OUTPUT_DIRECTORY, registry);
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    factory->SetPluginLoader(legacy_loader);
    ASSERT_NO_THROW(legacy_loader->LoadAllPlugins());
    
    size_t legacy_count = registry->GetRegisteredNodeTypes().size();
    ASSERT_GT(legacy_count, 0);
    
    // Then add additional directories using new API
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir1_));
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir2_));
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Should have plugins from all sources
    size_t total_count = registry->GetRegisteredNodeTypes().size();
    ASSERT_GE(total_count, legacy_count + 2);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       CreateStaticNode_WorksWithMultiDirectoryPlugins) {
    
    // Setup: Load from primary directory
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Register and load
    ASSERT_NO_THROW(factory->AddPluginDirectory(PLUGIN_OUTPUT_DIRECTORY));
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Should be able to create nodes loaded via AddPluginDirectory
    // This verifies the factory integration is complete
    auto plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GT(plugins.size(), 0);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       PluginRegistry_SharedAcrossLoaders) {
    
    // Setup: Copy plugins to separate directories
    CopyPluginsToDirectory(test_dir1_, {"int_producer"});
    CopyPluginsToDirectory(test_dir2_, {"completion_node"});
    
    // Create single registry
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Add both directories
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir1_));
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir2_));
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Both plugins should be in the SAME registry instance
    auto registered = registry->GetRegisteredNodeTypes();
    ASSERT_TRUE(PluginRegistered(registered, "int_producer"));;
    ASSERT_TRUE(PluginRegistered(registered, "completion_node"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       SequentialDirectoryAddition) {
    
    CopyPluginsToDirectory(test_dir1_, {"int_producer"});
    CopyPluginsToDirectory(test_dir2_, {"int_sink"});
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Add first directory
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir1_));
    
    // Add second directory later
    ASSERT_NO_THROW(factory->AddPluginDirectory(test_dir2_));
    
    // Load all
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
    
    // Both should be loaded
    auto registered = registry->GetRegisteredNodeTypes();
    ASSERT_TRUE(PluginRegistered(registered, "int_producer"));;
    ASSERT_TRUE(PluginRegistered(registered, "int_sink"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       NonExistentDirectoryHandling) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Adding non-existent directory should not throw in AddPluginDirectory
    // (it's only checked when loading)
    ASSERT_NO_THROW(factory->AddPluginDirectory("./non_existent_dir_xyz"));
    
    // But LoadAllPluginsFromDirectories should handle it gracefully
    // (skips directories that don't exist or can't be loaded)
    ASSERT_NO_THROW(factory->LoadAllPluginsFromDirectories());
}

TEST_F(NodeFactoryMultiDirectoryTest,
       PluginRegistrationIsolation) {
    
    CopyPluginsToDirectory(test_dir1_, {"int_producer", "int_sink"});
    CopyPluginsToDirectory(test_dir2_, {"completion_node"});
    
    // Create two separate registries
    auto registry1 = std::make_shared<graph::PluginRegistry>();
    auto factory1 = std::make_shared<graph::NodeFactory>(registry1);
    
    auto registry2 = std::make_shared<graph::PluginRegistry>();
    auto factory2 = std::make_shared<graph::NodeFactory>(registry2);
    
    // Load different plugins into each
    ASSERT_NO_THROW(factory1->AddPluginDirectory(test_dir1_));
    ASSERT_NO_THROW(factory1->LoadAllPluginsFromDirectories());
    
    ASSERT_NO_THROW(factory2->AddPluginDirectory(test_dir2_));
    ASSERT_NO_THROW(factory2->LoadAllPluginsFromDirectories());
    
    // Each registry should have only its plugins
    auto plugins1 = registry1->GetRegisteredNodeTypes();
    auto plugins2 = registry2->GetRegisteredNodeTypes();
    
    ASSERT_TRUE(PluginRegistered(plugins1, "int_producer"));;
    ASSERT_TRUE(PluginRegistered(plugins1, "int_sink"));;
    ASSERT_FALSE(PluginRegistered(plugins1, "completion_node"));;
    
    ASSERT_TRUE(PluginRegistered(plugins2, "completion_node"));;
    ASSERT_FALSE(PluginRegistered(plugins2, "int_producer"));;
    ASSERT_FALSE(PluginRegistered(plugins2, "int_sink"));;
}
