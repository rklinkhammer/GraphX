// MIT License
/// @file core/PluginReflection.hpp
/// @brief C++26 Reflection wrapper for plugin metadata generation and discovery

//
// Copyright (c) 2025 Robert Klinkhammer
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

#include <string_view>
#include <type_traits>
#include <concepts>
#include <optional>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>

// Feature detection for C++26 std::reflect
#if __cplusplus >= 202600 && __has_include(<meta>)
    #include <meta>
    #define GDASHBOARD_HAS_REFLECTION 1
#else
    #define GDASHBOARD_HAS_REFLECTION 0
#endif

// ============================================================================
// Plugin Reflection Infrastructure (C++26 Reflection Foundation)
// ============================================================================
// Provides compile-time introspection of plugins and their capabilities.
// Enables zero-cost abstraction for metadata generation and discovery.
// ============================================================================

namespace app::reflection {

using json = nlohmann::json;

/**
 * @struct CapabilityMetadata
 * @brief Compile-time metadata for a single plugin capability
 *
 * Describes what a plugin can do without runtime overhead.
 * Generated through C++26 reflection or explicit specialization.
 */
struct CapabilityMetadata {
    std::string_view name;              ///< Capability name (e.g., "IConfigurable")
    std::string_view description;       ///< Human-readable description
    bool is_required = false;           ///< Is this capability required for basic operation?
    bool is_experimental = false;       ///< Is this capability experimental/optional?
    std::string_view version_introduced;///< Version when this capability was added
    std::string_view min_version;       ///< Minimum plugin version supporting this
};

/**
 * @struct PluginMetadata
 * @brief Compile-time metadata for a plugin
 *
 * Aggregates all plugin information: name, version, capabilities, etc.
 * Generated from C++26 reflection or manual specialization.
 */
struct PluginMetadata {
    std::string_view name;              ///< Plugin name
    std::string_view version;           ///< Plugin version (semantic versioning)
    std::string_view description;       ///< Human-readable description
    std::string_view author;            ///< Plugin author/organization
    std::string_view license;           ///< License identifier (e.g., "MIT")
    std::span<const CapabilityMetadata> capabilities;  ///< All supported capabilities
};

// ============================================================================
// Reflection Concepts for Capability Detection
// ============================================================================

/**
 * @concept HasIConfigurable
 * @brief Type implements IConfigurable interface (Phase 3: Reflection-based)
 *
 * Plugins with this capability can be configured via JSON.
 *
 * @note Uses C++26 reflection for detection in future; currently uses SFINAE
 */
template<typename T>
concept HasIConfigurable = requires(T t) {
    { t.Configure(std::declval<const json&>()) } -> std::convertible_to<void>;
};

/**
 * @concept HasIDiagnosable
 * @brief Type implements IDiagnosable interface
 *
 * Plugins with this capability can provide diagnostic information.
 */
template<typename T>
concept HasIDiagnosable = requires(T t) {
    { t.GetDiagnostics() } -> std::convertible_to<json>;
};

/**
 * @concept HasIParameterized
 * @brief Type implements IParameterized interface
 *
 * Plugins with this capability expose tunable parameters.
 */
template<typename T>
concept HasIParameterized = requires(T t) {
    { t.GetParameters() } -> std::convertible_to<std::vector<std::string>>;
};

/**
 * @concept HasIMetricsCallback
 * @brief Type implements metrics callback provider interface
 *
 * Plugins with this capability expose metrics via callbacks.
 */
template<typename T>
concept HasIMetricsCallback = requires(T t) {
    { t.OnMetricsAvailable() } -> std::convertible_to<void>;
};

// ============================================================================
// Reflection-Based Plugin Introspection
// ============================================================================

/**
 * @brief Get capability metadata for a plugin type (Phase 3: Reflection)
 *
 * Uses C++26 reflection (when available) or manual capabilities to build
 * metadata. Can be specialized for specific plugin types.
 *
 * @tparam Plugin The plugin type to introspect
 * @return Array of CapabilityMetadata for all supported capabilities
 *
 * **Phase 3 C++26 Pattern**:
 * @code
 *   template<>
 *   consteval auto GetCapabilities<MyPlugin>() {
 *       using reflect::reflect_t;
 *       // Use reflection to detect capabilities from interface implementations
 *       constexpr auto members = nonstatic_data_members_of(reflect_t<MyPlugin>);
 *       // Build capability list from members
 *   }
 * @endcode
 */
template<typename Plugin>
/**
 * @brief Get capabilities.
 * @return Result of the operation.
 */
consteval std::span<const CapabilityMetadata> GetCapabilities();

/**
 * @brief Get plugin metadata (Phase 3: Reflection Foundation)
 *
 * Aggregates all plugin information into a single metadata structure.
 * Must be specialized for each plugin type.
 *
 * @tparam Plugin The plugin type to introspect
 * @return PluginMetadata with all plugin information
 *
 * **Usage**:
 * @code
 *   template<>
 *   consteval PluginMetadata GetPluginMetadata<MyPlugin>() {
 *       return {
 *           .name = "MyPlugin",
 *           .version = "1.0.0",
 *           .description = "Does something useful",
 *           .author = "Author Name",
 *           .license = "MIT",
 *           .capabilities = GetCapabilities<MyPlugin>()
 *       };
 *   }
 * @endcode
 */
template<typename Plugin>
/**
 * @brief Get plugin metadata.
 * @return Result of the operation.
 */
consteval PluginMetadata GetPluginMetadata();

// ============================================================================
// Plugin Reflection Wrapper (Phase 3: Compile-time Introspection)
// ============================================================================

/**
 * @class PluginReflectionWrapper
 * @brief Wraps a plugin with reflection capabilities
 *
 * Provides compile-time and runtime access to plugin metadata without
 * modifying the original plugin class. Uses reflection for zero-cost abstraction.
 *
 * @tparam Plugin The plugin type to wrap
 *
 * **Phase 3 Usage Pattern**:
 * @code
 *   class MyPlugin {
 *   public:
 *       void Configure(const json& config);
 *       json GetDiagnostics() const;
 *   };
 *
 *   // At compile-time, generate metadata
 *   constexpr auto metadata = GetPluginMetadata<MyPlugin>();
 *
 *   // At runtime, use wrapper for safe access
 *   PluginReflectionWrapper<MyPlugin> wrapper(plugin_instance);
 *   
 *   auto caps = wrapper.GetCapabilities();
 *   if (wrapper.SupportsCapability("IConfigurable")) {
 *       wrapper.Configure(config);
 *   }
 * @endcode
 */
template<typename Plugin>
/**
 * @class PluginReflectionWrapper
 * @brief Plugin reflection wrapper implementation for GraphX.
 */
class PluginReflectionWrapper {
public:
    /**
     * @brief Construct wrapper around plugin instance
     *
     * @param plugin Reference to the plugin instance
     */
    explicit PluginReflectionWrapper(Plugin& plugin) 
        : plugin_(plugin) {}

    /**
     * @brief Get compile-time metadata for this plugin type
     *
     * @return PluginMetadata with all plugin information
     */
    static consteval PluginMetadata GetMetadata() {
        return GetPluginMetadata<Plugin>();
    }

    /**
     * @brief Get all capabilities supported by this plugin
     *
     * @return Span of CapabilityMetadata for each capability
     */
    static consteval std::span<const CapabilityMetadata> GetCapabilities() {
        return GetPluginMetadata<Plugin>().capabilities;
    }

    /**
     * @brief Check if plugin supports a specific capability
     *
     * Uses reflection to determine capability support.
     *
     * @param capability_name Name of the capability (e.g., "IConfigurable")
     * @return true if capability is supported
     */
    bool SupportsCapability(std::string_view capability_name) const {
        for (const auto& cap : GetCapabilities()) {
            if (cap.name == capability_name) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get plugin name
     *
     * @return Plugin name from metadata
     */
    static std::string_view GetName() {
        return GetMetadata().name;
    }

    /**
     * @brief Get plugin version
     *
     * @return Plugin version from metadata
     */
    static std::string_view GetVersion() {
        return GetMetadata().version;
    }

    /**
     * @brief Get plugin description
     *
     * @return Plugin description from metadata
     */
    static std::string_view GetDescription() {
        return GetMetadata().description;
    }

    /**
     * @brief Get capability count
     *
     * @return Number of capabilities supported by this plugin
     */
    static constexpr size_t GetCapabilityCount() {
        return GetCapabilities().size();
    }

    /**
     * @brief Check if plugin has a required capability
     *
     * @param capability_name Name of required capability
     * @return true if plugin supports it, false otherwise
     */
    bool HasRequiredCapability(std::string_view capability_name) const {
        for (const auto& cap : GetCapabilities()) {
            if (cap.name == capability_name && cap.is_required) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Convert metadata to JSON representation
     *
     * **Phase 3 Export Pattern**:
     * @code
     *   auto json_metadata = wrapper.GetMetadataAsJson();
     *   // Returns JSON with plugin info, capabilities, versions, etc.
     * @endcode
     *
     * @return JSON object with plugin metadata
     */
    json GetMetadataAsJson() const {
        const auto& metadata = GetMetadata();
        json result;
        result["name"] = std::string(metadata.name);
        result["version"] = std::string(metadata.version);
        result["description"] = std::string(metadata.description);
        result["author"] = std::string(metadata.author);
        result["license"] = std::string(metadata.license);
        
        json capabilities_json = json::array();
        for (const auto& cap : metadata.capabilities) {
            json cap_obj;
            cap_obj["name"] = std::string(cap.name);
            cap_obj["description"] = std::string(cap.description);
            cap_obj["required"] = cap.is_required;
            cap_obj["experimental"] = cap.is_experimental;
            cap_obj["version_introduced"] = std::string(cap.version_introduced);
            cap_obj["min_version"] = std::string(cap.min_version);
            capabilities_json.push_back(cap_obj);
        }
        result["capabilities"] = capabilities_json;
        
        return result;
    }

private:
    Plugin& plugin_;  ///< Reference to wrapped plugin instance
};

// ============================================================================
// Runtime Plugin Registry (Phase 3: Discovery)
// ============================================================================

/**
 * @class PluginRegistry
 * @brief Registry for dynamically discovering plugins and their metadata
 *
 * Maintains a collection of plugins with reflection-based metadata.
 * Enables discovery of capabilities without manual configuration.
 *
 * **Phase 3 Discovery Pattern**:
 * @code
 *   PluginRegistry registry;
 *   
 *   // Register plugins (can happen dynamically)
 *   MyPlugin plugin;
 *   registry.Register<MyPlugin>(plugin);
 *   
 *   // Discover capabilities
 *   auto configurable_plugins = registry.FindPluginsByCapability("IConfigurable");
 *   for (const auto& metadata : configurable_plugins) {
 *       // Process plugins with this capability
 *   }
 * @endcode
 */
/**
 * @class PluginRegistry
 * @brief Plugin registry implementation for GraphX.
 */
class PluginRegistry {
public:
    /**
     * @brief Register a plugin with reflection metadata
     *
     * @tparam Plugin The plugin type to register
     * @param plugin Reference to plugin instance
     */
    template<typename Plugin>
    void Register(Plugin& plugin) {
        const auto metadata = GetPluginMetadata<Plugin>();
        const auto it = std::find_if(entries_.begin(), entries_.end(),
            [&metadata](const RegisteredPlugin& entry) {
                return entry.metadata.name == metadata.name;
            });

        if (it != entries_.end()) {
            it->metadata = metadata;
            it->instance = static_cast<void*>(std::addressof(plugin));
            return;
        }

        entries_.push_back(RegisteredPlugin{
            .metadata = metadata,
            .instance = static_cast<void*>(std::addressof(plugin))
        });
    }

    /**
     * @brief Find all plugins supporting a capability
     *
     * @param capability_name Name of the capability to search for
     * @return Vector of plugin metadata for plugins with this capability
     */
    std::vector<PluginMetadata> FindPluginsByCapability(std::string_view capability_name) const {
        std::vector<PluginMetadata> matching;

        for (const auto& entry : entries_) {
            const auto& capabilities = entry.metadata.capabilities;
            const bool has_capability = std::any_of(capabilities.begin(), capabilities.end(),
                [capability_name](const CapabilityMetadata& capability) {
                    return capability.name == capability_name;
                });

            if (has_capability) {
                matching.push_back(entry.metadata);
            }
        }

        return matching;
    }

    /**
     * @brief Get all registered plugins
     *
     * @return Vector of metadata for all registered plugins
     */
    std::vector<PluginMetadata> GetAllPlugins() const {
        std::vector<PluginMetadata> all;
        all.reserve(entries_.size());

        for (const auto& entry : entries_) {
            all.push_back(entry.metadata);
        }

        return all;
    }

private:
    struct RegisteredPlugin {
        PluginMetadata metadata;
        void* instance = nullptr;
    };

    std::vector<RegisteredPlugin> entries_;
};

}  // namespace app::reflection

// ============================================================================
// Phase 3 Reflection Patterns & Best Practices
// ============================================================================
//
// **Pattern 1: Plugin Capability Detection (Compile-time)**
// ```cpp
// if constexpr (HasIConfigurable<MyPlugin>) {
//     // Plugin definitely supports IConfigurable
// }
// ```
//
// **Pattern 2: Plugin Metadata Export (Phase 3)**
// ```cpp
// constexpr auto metadata = GetPluginMetadata<MyPlugin>();
// // Use metadata for code generation, documentation, schema generation
// ```
//
// **Pattern 3: Dynamic Capability Checking (Runtime)**
// ```cpp
// PluginReflectionWrapper<MyPlugin> wrapper(plugin);
// if (wrapper.SupportsCapability("IConfigurable")) {
//     // Safe to use Configure()
// }
// ```
//
// **Pattern 4: Plugin Discovery (Phase 3 Future)**
// ```cpp
// PluginRegistry registry;
// auto configurable = registry.FindPluginsByCapability("IConfigurable");
// // Process all plugins with this capability
// ```
//
// **Benefits**:
// - Zero runtime cost for compile-time metadata (consteval)
// - Concepts provide compile-time capability checking
// - Reflection enables automatic schema/code generation
// - Wrapper provides safe runtime access
// - Backward compatible with existing plugins
//
