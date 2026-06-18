/**
 * @file NodeProviderBootstrap.cpp
 * @brief Node Provider Bootstrap Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#include "graph/NodeProviderBootstrap.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include "graph/RegisteredNodeProvider.hpp"
#include "graph/NodeProvider.hpp"
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <system_error>
#include <filesystem>
#include <chrono>
#include <sstream>

namespace app {

// ============================================================================
// Static Initialization
// ============================================================================

static log4cxx::LoggerPtr logger_ = log4cxx::Logger::getLogger("NodeProviderBootstrap");

namespace {

/**
 * @brief Join plugin directories.
 * @param plugin_directories Parameter for join plugin directories.
 */
std::string JoinPluginDirectories(const std::vector<std::string>& plugin_directories) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < plugin_directories.size(); ++i) {
        if (i > 0) {
            joined << ";";
        }
        joined << plugin_directories[i];
    }
    return joined.str();
}

} // namespace

// ============================================================================
std::expected<NodeProviderBootstrap::ProviderBootstrapResult, NodeProviderBootstrap::ProviderBootstrapError>
NodeProviderBootstrap::CreateProviderExpected(const std::string& plugin_directory) noexcept {
    return CreateProviderExpected(std::vector<std::string>{plugin_directory});
}

// ============================================================================
std::expected<NodeProviderBootstrap::ProviderBootstrapResult, NodeProviderBootstrap::ProviderBootstrapError>
NodeProviderBootstrap::CreateProviderExpected(const std::vector<std::string>& plugin_directories) noexcept {
    LOG4CXX_TRACE(logger_, "Bootstrapping node provider with plugin directory: "
                          << JoinPluginDirectories(plugin_directories));

    ProviderBootstrapDiagnostics diagnostics{};
    diagnostics.plugin_directory = JoinPluginDirectories(plugin_directories);
    diagnostics.plugin_directories = plugin_directories;

    if (plugin_directories.empty()) {
        LOG4CXX_ERROR(logger_, "No plugin directories provided");
        return std::unexpected(ProviderBootstrapError::InvalidPluginDirectory);
    }

    // Step 1: Create PluginRegistry shared by every loader.
    std::shared_ptr<graph::PluginRegistry> registry;
    try {
        registry = std::make_shared<graph::PluginRegistry>();
        LOG4CXX_TRACE(logger_, "Created PluginRegistry successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create PluginRegistry: " << e.what());
        return std::unexpected(ProviderBootstrapError::RegistryCreationFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to create PluginRegistry: unknown error");
        return std::unexpected(ProviderBootstrapError::Unknown);
    }

    std::vector<std::shared_ptr<graph::PluginLoader>> loaders;
    loaders.reserve(plugin_directories.size());

    for (const auto& plugin_directory : plugin_directories) {
        if (plugin_directory.empty()) {
            LOG4CXX_ERROR(logger_, "Plugin directory cannot be empty");
            return std::unexpected(ProviderBootstrapError::InvalidPluginDirectory);
        }

        std::error_code fs_error;
        const bool plugin_directory_exists = std::filesystem::exists(plugin_directory, fs_error);
        if (fs_error) {
            LOG4CXX_ERROR(logger_, "Unable to inspect plugin directory: "
                                 << plugin_directory << " - " << fs_error.message());
            return std::unexpected(ProviderBootstrapError::InvalidPluginDirectory);
        }

        if (!plugin_directory_exists) {
            LOG4CXX_WARN(logger_, "Plugin directory does not exist: "
                                << plugin_directory << ". Proceeding with empty registry for this directory.");
        } else if (!std::filesystem::is_directory(plugin_directory, fs_error)) {
            if (fs_error) {
                LOG4CXX_ERROR(logger_, "Unable to inspect plugin path: "
                                     << plugin_directory << " - " << fs_error.message());
                return std::unexpected(ProviderBootstrapError::InvalidPluginDirectory);
            }
            LOG4CXX_ERROR(logger_, "Plugin path exists but is not a directory: "
                                 << plugin_directory);
            return std::unexpected(ProviderBootstrapError::InvalidPluginDirectory);
        }

        std::shared_ptr<graph::PluginLoader> loader;
        try {
            loader = std::make_shared<graph::PluginLoader>(plugin_directory, registry);
            loaders.push_back(loader);
            LOG4CXX_TRACE(logger_, "Created PluginLoader successfully for " << plugin_directory);
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to create PluginLoader: " << e.what());
            return std::unexpected(ProviderBootstrapError::LoaderCreationFailed);
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Failed to create PluginLoader: unknown error");
            return std::unexpected(ProviderBootstrapError::Unknown);
        }

        if (plugin_directory_exists) {
            auto loaded = loader->LoadAllPluginsSafe();
            if (loaded) {
                diagnostics.discovered_count += loaded->discovered_count;
                diagnostics.loaded_count += loaded->loaded_count;
                diagnostics.failed_count += loaded->failed_count;
                diagnostics.scan_duration += loaded->load_duration;
            } else {
                LOG4CXX_WARN(logger_, "Plugin loading encountered errors in "
                                      << plugin_directory << ": "
                                      << app::error::ErrorMessage(loaded.error())
                                      << ". Some plugins may not be available.");
            }
        } else {
            LOG4CXX_WARN(logger_, "Plugin directory does not exist, skipping plugin loading: "
                                << plugin_directory);
        }
    }

    // Step 3: Create RegisteredNodeProvider with loaded registry
    const auto init_start = std::chrono::steady_clock::now();
    std::shared_ptr<graph::RegisteredNodeProvider> provider;
    try {
        provider = std::make_shared<graph::RegisteredNodeProvider>(registry);
        LOG4CXX_TRACE(logger_, "Created RegisteredNodeProvider successfully");

        provider->Initialize();
        LOG4CXX_TRACE(logger_, "Initialized RegisteredNodeProvider with plugins");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create or initialize RegisteredNodeProvider: "
                             << e.what());
        return std::unexpected(ProviderBootstrapError::ProviderCreationFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to create or initialize RegisteredNodeProvider: unknown error");
        return std::unexpected(ProviderBootstrapError::Unknown);
    }
    diagnostics.init_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - init_start);

    auto lifetime = std::make_shared<ProviderBootstrapHandle>();
    lifetime->plugin_registry = registry;
    lifetime->loaders = loaders;
    if (!loaders.empty()) {
        lifetime->loader = loaders.front();
    }

    LOG4CXX_TRACE(logger_, "Provider bootstrap completed. discovered="
                         << diagnostics.discovered_count
                         << ", loaded=" << diagnostics.loaded_count
                         << ", failed=" << diagnostics.failed_count
                         << ", scan_ms=" << diagnostics.scan_duration.count()
                         << ", init_ms=" << diagnostics.init_duration.count());

    return ProviderBootstrapResult{.provider = provider, .diagnostics = diagnostics, .lifetime = lifetime};
}

// ============================================================================
std::expected<std::vector<std::string>, NodeProviderBootstrap::ProviderBootstrapError>
NodeProviderBootstrap::GetAvailableNodeTypesExpected(
    const std::shared_ptr<graph::INodeProvider>& provider) noexcept {
    
    if (!provider) {
        LOG4CXX_ERROR(logger_, "GetAvailableNodeTypes called with null provider");
        return std::unexpected(ProviderBootstrapError::NullProvider);
    }
    
    LOG4CXX_TRACE(logger_, "Querying available node types from provider");
    
    try {
        auto node_types = provider->GetAvailableNodeTypes();
        LOG4CXX_TRACE(logger_, "Found " << node_types.size() 
                             << " available node types");
        
        if (!node_types.empty()) {
            std::string types_str;
            for (size_t i = 0; i < node_types.size(); ++i) {
                if (i > 0) types_str += ", ";
                types_str += node_types[i];
            }
            LOG4CXX_TRACE(logger_, "Available types: " << types_str);
        }
        
        return node_types;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to get available node types: " << e.what());
        return std::unexpected(ProviderBootstrapError::QueryFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to get available node types: unknown error");
        return std::unexpected(ProviderBootstrapError::Unknown);
    }
}

// ============================================================================
std::expected<bool, NodeProviderBootstrap::ProviderBootstrapError>
NodeProviderBootstrap::IsNodeTypeAvailableExpected(
    const std::shared_ptr<graph::INodeProvider>& provider,
    const std::string& type_name) noexcept {
    
    if (!provider) {
        LOG4CXX_ERROR(logger_, "IsNodeTypeAvailable called with null provider");
        return std::unexpected(ProviderBootstrapError::NullProvider);
    }
    
    if (type_name.empty()) {
        LOG4CXX_WARN(logger_, "IsNodeTypeAvailable called with empty type_name");
        return false;
    }
    
    LOG4CXX_TRACE(logger_, "Checking availability of node type: " << type_name);
    
    try {
        bool available = provider->IsNodeTypeAvailable(type_name);
        
        if (available) {
            LOG4CXX_TRACE(logger_, "Node type '" << type_name << "' is available");
        } else {
            LOG4CXX_TRACE(logger_, "Node type '" << type_name << "' is not available");
        }
        
        return available;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to check node type availability: " 
                             << e.what());
        return std::unexpected(ProviderBootstrapError::QueryFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to check node type availability: unknown error");
        return std::unexpected(ProviderBootstrapError::Unknown);
    }
}

}  // namespace app
