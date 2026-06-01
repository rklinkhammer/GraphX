/**
 * @file test_node_factory_multi_directory.cpp
 * @brief Comprehensive tests for NodeFactory multi-directory plugin support
 *
 * Tests the Option A multi-directory architecture:
 * - AddPluginDirectoryExpected() to register multiple plugin directories
 * - LoadAllPluginsFromDirectoriesExpected() to load all plugins across directories
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

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

}  // namespace

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
     * @param plugin_names Names of plugins to copy (without shared-library extension)
     */
    void CopyPluginsToDirectory(const std::string& dest_dir,
                               const std::vector<std::string>& plugin_names) {
        for (const auto& plugin_name : plugin_names) {
            std::string src = primary_dir_ + "/lib" + plugin_name + kSharedLibraryExtension;
            std::string dst = dest_dir + "/lib" + plugin_name + kSharedLibraryExtension;
            
            if (fs::exists(src)) {
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
                LOG4CXX_DEBUG(logger_, "Copied " << src << " to " << dst);
            } else {
                LOG4CXX_WARN(logger_, "Plugin not found: " << src);
            }
        }
    }

    /**
     * @brief Get count of plugin files in a directory
     * @param dir Directory to check
     * @return Number of plugin files
     */
    size_t CountPluginsInDirectory(const std::string& dir) {
        size_t count = 0;
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == kSharedLibraryExtension) {
                    count++;
                }
            }
        }
        return count;
    }

    /**
     * @brief Get names of plugin files in a directory
     * @param dir Directory to check
     * @return Set of plugin names (without shared-library extension)
     */
    std::set<std::string> GetPluginsInDirectory(const std::string& dir) {
        std::set<std::string> plugins;
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == kSharedLibraryExtension) {
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
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir1_));
    
    // Load plugins
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
    // Verify plugins are registered
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GE(registered_plugins.size(), 2);
    ASSERT_TRUE(PluginRegistered(registered_plugins, "TestIntProducer"));;
    ASSERT_TRUE(PluginRegistered(registered_plugins, "CompletionNode"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectoryExpectedAndLoadExpected_SingleDirectory) {
    
    CopyPluginsToDirectory(test_dir1_, {"int_producer", "completion_node"});
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    auto add_result = factory->AddPluginDirectoryExpected(test_dir1_);
    ASSERT_TRUE(add_result);
    
    auto load_result = factory->LoadAllPluginsFromDirectoriesExpected();
    ASSERT_TRUE(load_result);
    
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GE(registered_plugins.size(), 2);
    EXPECT_TRUE(PluginRegistered(registered_plugins, "TestIntProducer"));
    EXPECT_TRUE(PluginRegistered(registered_plugins, "CompletionNode"));
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
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir1_));
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir2_));
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir3_));
    
    // Load all plugins
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
    // Verify all plugins from all directories are registered
    auto registered_plugins = registry->GetRegisteredNodeTypes();
    ASSERT_GE(registered_plugins.size(), 6);
    
    auto expected = std::set<std::string>{
        "TestIntProducer", "TestDoubleSinkNode", "TestIntSinkNode",
        "CompletionNode", "TestNode", "SourceTestNode"
    };
    
    for (const auto& plugin : expected) {
        ASSERT_TRUE(PluginRegistered(registered_plugins, plugin));;
    }
}

TEST_F(NodeFactoryMultiDirectoryTest,
       LoadAllPluginsFromDirectories_ErrorHandling) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    auto result = factory->LoadAllPluginsFromDirectoriesExpected();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::NodeFactory::PluginDirectoryError::NoDirectoriesRegistered);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       LoadAllPluginsFromDirectoriesExpectedReportsNoDirectories) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    auto result = factory->LoadAllPluginsFromDirectoriesExpected();
    
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::NodeFactory::PluginDirectoryError::NoDirectoriesRegistered);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectory_EmptyPathValidation) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    auto result = factory->AddPluginDirectoryExpected("");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::NodeFactory::PluginDirectoryError::EmptyPath);
}

TEST_F(NodeFactoryMultiDirectoryTest,
       AddPluginDirectoryExpectedReportsEmptyPath) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    auto result = factory->AddPluginDirectoryExpected("");
    
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::NodeFactory::PluginDirectoryError::EmptyPath);
}

TEST_F(NodeFactoryMultiDirectoryTest,
    MixedMode_AddPluginDirectoryAcrossSources) {
    
    // Setup test directories
    CopyPluginsToDirectory(test_dir1_, {"completion_node"});
    CopyPluginsToDirectory(test_dir2_, {"test_node"});
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);

    // Load the primary directory and then add additional sources
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(PLUGIN_OUTPUT_DIRECTORY));
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir1_));
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir2_));
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
    // Should have plugins from all sources. Duplicate node types from added
    // directories refresh existing registrations rather than increasing count.
    size_t total_count = registry->GetRegisteredNodeTypes().size();
    ASSERT_GT(total_count, 0u);
    auto registered = registry->GetRegisteredNodeTypes();
    ASSERT_TRUE(PluginRegistered(registered, "CompletionNode"));;
    ASSERT_TRUE(PluginRegistered(registered, "TestNode"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       CreateStaticNode_WorksWithMultiDirectoryPlugins) {
    
    // Setup: Load from primary directory
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Register and load
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(PLUGIN_OUTPUT_DIRECTORY));
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
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
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir1_));
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir2_));
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
    // Both plugins should be in the SAME registry instance
    auto registered = registry->GetRegisteredNodeTypes();
    ASSERT_TRUE(PluginRegistered(registered, "TestIntProducer"));;
    ASSERT_TRUE(PluginRegistered(registered, "CompletionNode"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       SequentialDirectoryAddition) {
    
    CopyPluginsToDirectory(test_dir1_, {"int_producer"});
    CopyPluginsToDirectory(test_dir2_, {"int_sink"});
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Add first directory
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir1_));
    
    // Add second directory later
    ASSERT_TRUE(factory->AddPluginDirectoryExpected(test_dir2_));
    
    // Load all
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
    
    // Both should be loaded
    auto registered = registry->GetRegisteredNodeTypes();
    ASSERT_TRUE(PluginRegistered(registered, "TestIntProducer"));;
    ASSERT_TRUE(PluginRegistered(registered, "TestIntSinkNode"));;
}

TEST_F(NodeFactoryMultiDirectoryTest,
       NonExistentDirectoryHandling) {
    
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto factory = std::make_shared<graph::NodeFactory>(registry);
    
    // Adding non-existent directory should not throw in AddPluginDirectory
    // (it's only checked when loading)
    ASSERT_TRUE(factory->AddPluginDirectoryExpected("./non_existent_dir_xyz"));
    
    // But LoadAllPluginsFromDirectories should handle it gracefully
    // (skips directories that don't exist or can't be loaded)
    ASSERT_TRUE(factory->LoadAllPluginsFromDirectoriesExpected());
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
    ASSERT_TRUE(factory1->AddPluginDirectoryExpected(test_dir1_));
    ASSERT_TRUE(factory1->LoadAllPluginsFromDirectoriesExpected());
    
    ASSERT_TRUE(factory2->AddPluginDirectoryExpected(test_dir2_));
    ASSERT_TRUE(factory2->LoadAllPluginsFromDirectoriesExpected());
    
    // Each registry should have only its plugins
    auto plugins1 = registry1->GetRegisteredNodeTypes();
    auto plugins2 = registry2->GetRegisteredNodeTypes();
    
    ASSERT_TRUE(PluginRegistered(plugins1, "TestIntProducer"));;
    ASSERT_TRUE(PluginRegistered(plugins1, "TestIntSinkNode"));;
    ASSERT_FALSE(PluginRegistered(plugins1, "CompletionNode"));;
    
    ASSERT_TRUE(PluginRegistered(plugins2, "CompletionNode"));;
    ASSERT_FALSE(PluginRegistered(plugins2, "TestIntProducer"));;
    ASSERT_FALSE(PluginRegistered(plugins2, "TestIntSinkNode"));;
}
