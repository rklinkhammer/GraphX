// MIT License
/// @file PluginLoader.hpp
/// @brief Plugin system support for PluginLoader

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

#pragma once

#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginVersion.hpp"
#include "config/Errors.hpp"
#include <string>
#include <vector>
#include <memory>
#include <expected>
#include <log4cxx/logger.h>

namespace graph {

// ============================================================================
// PluginLoader - Dynamic plugin loading and registration
// ============================================================================

/// @class PluginLoader
/// @brief Loads plugin .so/.dylib files and registers their node types
///
/// PluginLoader handles:
/// - dlopen() to load plugin libraries
/// - dlsym() to resolve plugin_get_info() function
/// - Parsing plugin metadata
/// - Registering discovered node types with PluginRegistry
/// - Error handling and logging
///
/// Usage:
/// @code
/// PluginLoader loader("./plugins", registry);
/// auto result = loader.LoadPluginSafe("libsensor_node.so");
/// // or
/// auto count = loader.LoadAllPluginsSafe();
/// @endcode
class PluginLoader {
private:
    // ========================================================================
    // Private Members and Methods
    // ========================================================================
    
    static log4cxx::LoggerPtr logger_;
    
    std::string plugin_directory_;
    std::shared_ptr<PluginRegistry> registry_;
    std::vector<std::string> loaded_plugins_;
    std::vector<void*> plugin_handles_;

    /// @brief Parse plugin metadata string format
    /// @param info_string Raw string from plugin_get_info() function
    /// @return Vector of parsed field strings, empty if parse fails
    /// 
    /// Expected format: "NodeType|Description|Version|CreateFunctionName|ABITag"
    std::vector<std::string> ParsePluginInfo(const std::string& info_string);

    /// @brief Get the ABI tag for the current C++ standard library
    /// @return "libstdc++_v1" for GCC/Clang on Linux, "libc++_v1" for Clang on macOS/BSD
    std::string GetCurrentABITag() const;

public:
    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /// @brief Construct a plugin loader
    /// @param plugin_directory Directory containing .so/.dylib files
    /// @param registry Shared pointer to PluginRegistry for registration
    PluginLoader(const std::string& plugin_directory,
                 std::shared_ptr<PluginRegistry> registry);

    // ========================================================================
    // Plugin Loading Operations
    // ========================================================================

    /// @brief Load a single plugin file safely with expected<> error handling
    /// @param plugin_filename Filename only (e.g., "libsensor_node.so")
    /// @return expected with void on success, PluginLoadError on failure
    /// 
    /// Error mapping:
    /// - FileNotFound: dlopen failed, file not found
    /// - SystemError: dlopen failed for other system reasons
    /// - MissingSymbol: plugin_get_info or plugin_get_facade not found
    /// - InvalidFormat: Plugin info format invalid or metadata incomplete
    /// - VersionMismatch: ABI tag incompatible with application
    /// - InitializationFailed: Registration with PluginRegistry failed
    /// - Unknown: Unexpected error, details in logs
    ///
    /// Example:
    /// @code
    /// auto result = loader.LoadPluginSafe("libsensor_node.so");
    /// if (result) {
    ///     // Plugin loaded successfully
    /// } else {
    ///     std::string error = app::error::ErrorMessage(result.error());
    ///     LOG_ERROR("Failed to load plugin: " << error);
    /// }
    /// @endcode
    ///
    /// @note All plugin loading is logged via log4cxx (TRACE and ERROR levels)
    /// @note Caller must use [[nodiscard]] warning if ignoring result
    [[nodiscard]] std::expected<void, app::error::PluginLoadError>
    LoadPluginSafe(const std::string& plugin_filename) noexcept;

    /// @brief Load all plugins from the plugin directory safely
    /// @return expected with count of successfully loaded plugins on success,
    ///         PluginLoadError on failure (returns directory not found error)
    ///
    /// Individual plugin failures are logged as warnings but don't affect the overall
    /// operation.
    ///
    /// Returns:
    /// - Success: size_t count of plugins successfully loaded
    /// - Failure: PluginLoadError if directory doesn't exist
    ///
    /// Example:
    /// @code
    /// auto result = loader.LoadAllPluginsSafe();
    /// if (result) {
    ///     size_t count = result.value();
    ///     LOG_INFO("Loaded " << count << " plugins");
    /// } else {
    ///     LOG_ERROR("Failed to load plugins: "
    ///               << app::error::ErrorMessage(result.error()));
    /// }
    /// @endcode
    ///
    /// @note Individual plugin errors are logged at WARN level but don't fail overall
    /// @note All plugin loading is logged via log4cxx (TRACE and ERROR levels)
    [[nodiscard]] std::expected<size_t, app::error::PluginLoadError>
    LoadAllPluginsSafe() noexcept;

    // ========================================================================
    // Query and Management Operations
    // ========================================================================

    /// @brief Get the registry used by this loader
    /// @return Shared pointer to PluginRegistry
    std::shared_ptr<PluginRegistry> GetRegistry() const {
        return registry_;
    }

    /// @brief Get list of successfully loaded plugin files
    /// @return Vector of plugin filenames that were loaded
    const std::vector<std::string>& GetLoadedPlugins() const {
        return loaded_plugins_;
    }

    /// @brief Get count of loaded plugins
    /// @return Number of successfully loaded plugin files
    size_t GetLoadedPluginCount() const {
        return loaded_plugins_.size();
    }

    /// @brief Unload a plugin (close its handle)
    /// @param plugin_filename Filename of plugin to unload
    /// @return true if plugin was unloaded, false if not found
    ///
    /// Calls dlclose() on the plugin. The node types remain registered
    /// but new instances cannot be created.
    bool UnloadPlugin(const std::string& plugin_filename);

    /// @brief Destructor - releases loader bookkeeping without unloading libraries
    ///
    /// PluginRegistry and RegisteredNodeProvider may still hold function/facade pointers into
    /// loaded libraries during shutdown. Use UnloadPlugin() for explicit unload.
    ~PluginLoader();
};

}  // namespace graph
