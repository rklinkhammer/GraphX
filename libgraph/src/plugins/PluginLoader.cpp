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

#include "plugins/PluginLoader.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <log4cxx/logger.h>

namespace fs = std::filesystem;

namespace graph {

log4cxx::LoggerPtr PluginLoader::logger_ =
    log4cxx::Logger::getLogger("graph.PluginLoader");

PluginLoader::PluginLoader(const std::string& plugin_directory,
                           std::shared_ptr<PluginRegistry> registry)
    : plugin_directory_(plugin_directory), registry_(registry) {
    
    LOG4CXX_TRACE(logger_, "PluginLoader initialized for directory: " << plugin_directory);
    
    // Verify directory exists
    if (!fs::exists(plugin_directory)) {
        LOG4CXX_WARN(logger_, "Plugin directory does not exist: " << plugin_directory);
        // Don't throw - might be created later or plugins loaded manually
    }
}

std::vector<std::string> PluginLoader::ParsePluginInfo(
    const std::string& info_string) {
    
    std::vector<std::string> parts;
    std::stringstream ss(info_string);
    std::string part;
    
    while (std::getline(ss, part, '|')) {
        // Trim whitespace
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        parts.push_back(part);
    }
    
    return parts;
}

std::string PluginLoader::GetCurrentABITag() const {
    // Determine which C++ standard library we're using
    // This is a compile-time check, but we report at runtime
    
    #ifdef _GLIBCXX_USE_CXX11_ABI
        #if _GLIBCXX_USE_CXX11_ABI
            return "libstdc++_v1";
        #else
            return "libstdc++_v0";
        #endif
    #else
        // Likely libc++ (Clang/macOS)
        return "libc++_v1";
    #endif
}

bool PluginLoader::UnloadPlugin(const std::string& plugin_filename) {
    
    auto it = std::find(loaded_plugins_.begin(), loaded_plugins_.end(), plugin_filename);
    
    if (it == loaded_plugins_.end()) {
        LOG4CXX_WARN(logger_, "Cannot unload plugin (not found): " << plugin_filename);
        return false;
    }
    
    size_t index = std::distance(loaded_plugins_.begin(), it);
    void* handle = plugin_handles_[index];

    if (registry_) {
        registry_->UnregisterNodeTypesForHandle(handle);
    }
    
    if (dlclose(handle) != 0) {
        LOG4CXX_WARN(logger_, "dlclose failed: " << dlerror());
        return false;
    }
    
    loaded_plugins_.erase(it);
    plugin_handles_.erase(plugin_handles_.begin() + index);
    
    LOG4CXX_TRACE(logger_, "Unloaded plugin: " << plugin_filename);
    return true;
}

// ============================================================================
// Phase 5e: Safe Error Handling Implementations
// ============================================================================

std::expected<void, app::error::PluginLoadError>
PluginLoader::LoadPluginSafe(const std::string& plugin_filename) noexcept {
    try {
        LOG4CXX_TRACE(logger_, "Loading plugin: " << plugin_filename);

        if (std::ranges::find(loaded_plugins_, plugin_filename) != loaded_plugins_.end()) {
            LOG4CXX_WARN(logger_, "Plugin already loaded: " << plugin_filename);
            return std::unexpected(app::error::PluginLoadError::AlreadyLoaded);
        }

        const fs::path full_path = fs::path(plugin_directory_) / plugin_filename;
        const std::string full_path_str = full_path.string();

        LOG4CXX_TRACE(logger_, "Full plugin path: " << full_path_str);

        if (!fs::exists(full_path)) {
            LOG4CXX_ERROR(logger_, "Plugin file not found: " << full_path_str);
            return std::unexpected(app::error::PluginLoadError::FileNotFound);
        }

        dlerror();  // Clear any previous error
        void* handle = dlopen(full_path_str.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        
        if (!handle) {
            const char* error = dlerror();
            LOG4CXX_ERROR(logger_, "dlopen failed for " << plugin_filename 
                          << ": " << (error ? error : "unknown error"));
            return std::unexpected(app::error::PluginLoadError::SystemError);
        }

        auto close_on_error = [handle]() noexcept {
            static_cast<void>(dlclose(handle));
        };

        LOG4CXX_TRACE(logger_, "dlopen successful, handle: " << handle);

        dlerror();
        using GetInfoFunc = const char* (*)();
        auto get_info = reinterpret_cast<GetInfoFunc>(dlsym(handle, "plugin_get_info"));
        const char* symbol_error = dlerror();
        
        if (!get_info || symbol_error) {
            LOG4CXX_ERROR(logger_, "Plugin missing plugin_get_info: " << plugin_filename);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::MissingSymbol);
        }

        LOG4CXX_TRACE(logger_, "Found plugin_get_info function");

        const char* info_string = get_info();
        if (!info_string) {
            LOG4CXX_ERROR(logger_, "plugin_get_info returned null");
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::InvalidFormat);
        }

        LOG4CXX_TRACE(logger_, "Plugin info: " << info_string);

        const auto parts = ParsePluginInfo(info_string);
        if (parts.size() < 5) {
            LOG4CXX_ERROR(logger_, "Plugin info has insufficient fields: " << info_string);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::InvalidFormat);
        }

        const std::string type_name = parts[0];
        const std::string description = parts[1];
        const std::string version = parts[2];
        const std::string create_function = parts[3];
        const std::string abi_tag = parts[4];

        LOG4CXX_TRACE(logger_, "Plugin metadata: type=" << type_name 
                      << ", version=" << version << ", ABI=" << abi_tag);

        const std::string current_abi = GetCurrentABITag();
        if (abi_tag != current_abi) {
            LOG4CXX_ERROR(logger_, "ABI mismatch for " << type_name 
                          << ": plugin uses " << abi_tag 
                          << " but application uses " << current_abi);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::VersionMismatch);
        }

        LOG4CXX_TRACE(logger_, "ABI validation passed");

        dlerror();
        using GetApiVersionFunc = int (*)();
        auto get_api_version = reinterpret_cast<GetApiVersionFunc>(
            dlsym(handle, "plugin_api_version"));
        int plugin_api_version = 1;
        if (get_api_version) {
            plugin_api_version = get_api_version();
            LOG4CXX_TRACE(logger_, "Plugin API version: " << plugin_api_version);
        } else {
            LOG4CXX_TRACE(logger_, "Plugin missing plugin_api_version, assuming v1");
            dlerror();
        }

        using namespace graph::plugins;
        if (!IsVersionCompatible(plugin_api_version)) {
            const std::string version_msg = GetVersionMessage(plugin_api_version);
            LOG4CXX_ERROR(logger_, "Version mismatch for " << type_name 
                          << ": " << version_msg);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::VersionMismatch);
        }

        LOG4CXX_TRACE(logger_, "Version validation passed");

        dlerror();
        using GetFacadeFunc = const NodeFacade* (*)();
        auto get_facade = reinterpret_cast<GetFacadeFunc>(dlsym(handle, "plugin_get_facade"));
        symbol_error = dlerror();

        if (!get_facade || symbol_error) {
            LOG4CXX_ERROR(logger_, "Plugin missing plugin_get_facade: " << type_name);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::MissingSymbol);
        }

        const NodeFacade* facade = get_facade();
        if (!facade) {
            LOG4CXX_ERROR(logger_, "plugin_get_facade returned null");
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::InvalidFormat);
        }

        LOG4CXX_TRACE(logger_, "Retrieved NodeFacade from plugin");

        if (!registry_) {
            LOG4CXX_ERROR(logger_, "Cannot register plugin without PluginRegistry");
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::InitializationFailed);
        }

        auto registration = registry_->RegisterNodeTypeExpected(
            type_name,
            description,
            full_path_str,
            create_function,
            abi_tag,
            version,
            handle,
            facade
        );
        if (!registration) {
            LOG4CXX_ERROR(logger_, "Failed to register node type: " << type_name);
            close_on_error();
            return std::unexpected(app::error::PluginLoadError::InitializationFailed);
        }

        loaded_plugins_.push_back(plugin_filename);
        plugin_handles_.push_back(handle);

        LOG4CXX_TRACE(logger_, "Successfully loaded plugin: " << plugin_filename 
                     << " with node type: " << type_name);
        return {};
    } catch (const std::filesystem::filesystem_error& e) {
        LOG4CXX_ERROR(logger_, "LoadPluginSafe filesystem error: " << e.what());
        return std::unexpected(app::error::PluginLoadError::SystemError);
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "LoadPluginSafe unexpected exception: " << e.what());
        return std::unexpected(app::error::PluginLoadError::Unknown);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "LoadPluginSafe unknown exception");
        return std::unexpected(app::error::PluginLoadError::Unknown);
    }
}

std::expected<size_t, app::error::PluginLoadError>
PluginLoader::LoadAllPluginsSafe() noexcept {
    try {
        if (!fs::exists(plugin_directory_)) {
            LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe: Directory not found - " 
                          << plugin_directory_);
            return std::unexpected(app::error::PluginLoadError::FileNotFound);
        }
        
        LOG4CXX_TRACE(logger_, "LoadAllPluginsSafe: Loading plugins from " 
                      << plugin_directory_);
        
        size_t loaded_count = 0;
        size_t failed_count = 0;
        
        for (const auto& entry : fs::directory_iterator(plugin_directory_)) {
            if (entry.path().extension() == ".so" ||
                entry.path().extension() == ".dylib") {

                auto result = LoadPluginSafe(entry.path().filename().string());
                if (result) {
                    loaded_count++;
                } else {
                    LOG4CXX_WARN(logger_, "LoadAllPluginsSafe: Failed to load plugin "
                                 << entry.path().filename() << " - "
                                 << app::error::ErrorMessage(result.error()));
                    failed_count++;
                }
            }
        }
        
        LOG4CXX_TRACE(logger_, "LoadAllPluginsSafe: Complete - "
                      << loaded_count << " loaded, " << failed_count << " failed");
        
        return loaded_count;
        
    } catch (const std::filesystem::filesystem_error& e) {
        LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe filesystem error: " << e.what());
        return std::unexpected(app::error::PluginLoadError::SystemError);
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe exception: " << e.what());
        return std::unexpected(app::error::PluginLoadError::SystemError);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe unknown exception");
        return std::unexpected(app::error::PluginLoadError::Unknown);
    }
}

PluginLoader::~PluginLoader() {
    LOG4CXX_TRACE(logger_, "PluginLoader destructor: releasing bookkeeping for "
                  << plugin_handles_.size() << " plugin handles");

    // Do not dlclose() here. PluginRegistry/NodeFactory can retain function and
    // facade pointers into these libraries through shutdown, and plugin-level
    // static destructors can also depend on process-wide logging state. Explicit
    // unloading remains available through UnloadPlugin().
    plugin_handles_.clear();
    loaded_plugins_.clear();
}

}  // namespace graph
