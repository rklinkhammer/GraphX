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
 * @file PluginInspector.hpp
 * @brief Plugin discovery and capability inspection for GraphX
 *
 * Provides programmatic access to discover installed plugins and query their
 * capabilities (IConfigurable, IDiagnosable, IParameterized interfaces).
 *
 * @author GraphX Team
 * @date 2026-01-06
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <chrono>
#include <ranges>
#include <nlohmann/json.hpp>
#include "core/RangesUtilities.hpp"

namespace graph {

// Forward declarations
class PluginInspector;

/**
 * @struct PluginInfo
 * @brief Metadata about a discovered plugin
 */
struct PluginInfo {
    std::string name;              ///< Plugin name (e.g., "AltitudeFusionNode")
    std::string path;              ///< Full path to plugin file
    std::string version;           ///< Plugin version string
    bool is_loaded = false;        ///< Successfully loaded for inspection
    std::string load_error;        ///< Error message if load failed
};

/**
 * @struct InterfaceCapability
 * @brief Information about a specific interface capability
 */
struct InterfaceCapability {
    std::string name;              ///< Interface name (e.g., "IConfigurable")
    bool supported = false;        ///< Is this interface supported
    std::string description;       ///< Human-readable description
};

/**
 * @struct PluginCapabilities
 * @brief Complete capability information for a single plugin (Phase 2: ranges-optimized)
 *
 * Designed to work efficiently with C++20+ std::ranges for filtering,
 * counting, and analyzing plugin capabilities.
 *
 * **Phase 2 Range Examples**:
 * @code
 *   // Count plugins with specific capability
 *   auto count = ranges::count_if(all_plugins, 
 *       [](const auto& p) { return p.HasIConfigurable(); });
 *   
 *   // Filter compliant plugins
 *   auto compliant = all_plugins 
 *       | views::filter([](const auto& p) { return p.IsCompliant(); })
 *       | ranges::to<std::vector>();
 *   
 *   // Check if any plugin has diagnostic capability
 *   bool has_diagnostic = ranges::any_of(all_plugins,
 *       [](const auto& p) { return p.HasIDiagnosable(); });
 * @endcode
 */
struct PluginCapabilities {
    PluginInfo info;
    std::vector<InterfaceCapability> capabilities;
    nlohmann::json node_descriptor_schema;
    
    /**
     * @brief Check if plugin supports IConfigurable
     * @return true if IConfigurable is supported
     */
    bool HasIConfigurable() const;
    
    /**
     * @brief Check if plugin supports IDiagnosable
     * @return true if IDiagnosable is supported
     */
    bool HasIDiagnosable() const;
    
    /**
     * @brief Check if plugin supports IParameterized
     * @return true if IParameterized is supported
     */
    bool HasIParameterized() const;
    
    /**
     * @brief Check if plugin supports IMetricsCallbackProvider
     * @return true if IMetricsCallbackProvider is supported
     */
    bool HasIMetricsCallback() const;
    
    /**
     * @brief Check if plugin is compliant with base standards
     *
     * Compliant means the plugin supports at least IConfigurable.
     *
     * @return true if plugin is compliant
     */
    bool IsCompliant() const;
    
    /**
     * @brief Convert capabilities to JSON representation
     * @return JSON object with capabilities data
     */
    nlohmann::json ToJson() const;
};

/**
 * @struct ComplianceStats
 * @brief Aggregated compliance statistics for all plugins (Phase 2: ranges-based)
 *
 * Designed for efficient calculation using C++20+ ranges algorithms.
 *
 * **Modern Calculation Pattern** (using ranges::count_if):
 * @code
 *   auto stats = ComplianceStats();
 *   const auto& plugins = inspector->InspectAll();
 *   
 *   stats.total = plugins.size();
 *   stats.configurable = ranges::count_if(plugins,
 *       [](const auto& p) { return p.HasIConfigurable(); });
 *   stats.diagnostic = ranges::count_if(plugins,
 *       [](const auto& p) { return p.HasIDiagnosable(); });
 *   stats.parameterized = ranges::count_if(plugins,
 *       [](const auto& p) { return p.HasIParameterized(); });
 *   stats.metrics_callback = ranges::count_if(plugins,
 *       [](const auto& p) { return p.HasIMetricsCallback(); });
 * @endcode
 *
 * @see RangesUtilities.hpp for CountIf() helper
 */
struct ComplianceStats {
    size_t total = 0;              ///< Total plugins inspected
    size_t configurable = 0;       ///< Plugins with IConfigurable
    size_t diagnostic = 0;         ///< Plugins with IDiagnosable
    size_t parameterized = 0;      ///< Plugins with IParameterized
    size_t metrics_callback = 0;   ///< Plugins with IMetricsCallbackProvider
    
    /**
     * @brief Calculate compliance percentage
     *
     * Returns the percentage of plugins that are IConfigurable-compliant.
     *
     * @return Percentage (0-100), or 0 if no plugins
     */
    double CompliancePercentage() const {
        return total > 0 ? (configurable * 100.0 / total) : 0.0;
    }
};

/**
 * @struct CacheInfo
 * @brief Caching statistics and configuration
 */
struct CacheInfo {
    bool enabled = true;                                    ///< Is caching enabled
    std::chrono::milliseconds ttl{5 * 60 * 1000};          ///< Cache time-to-live (5 minutes default)
    size_t cache_hits = 0;                                  ///< Number of cache hits
    size_t cache_misses = 0;                                ///< Number of cache misses
    std::chrono::system_clock::time_point last_cached;     ///< Last time cache was populated
    
    /**
     * @brief Check if cache is still valid
     * @return true if cache TTL hasn't expired
     */
    bool IsValid() const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cached);
        return age < ttl;
    }
    
    /**
     * @brief Calculate cache hit ratio
     * @return Hit ratio (0.0 to 1.0), or 0 if no accesses
     */
    double GetHitRatio() const {
        auto total = cache_hits + cache_misses;
        return total > 0 ? (static_cast<double>(cache_hits) / total) : 0.0;
    }
};

/**
 * @struct SemanticVersion
 * @brief Semantic version with major.minor.patch format
 *
 * Supports parsing and comparison of semantic versions.
 */
struct SemanticVersion {
    int major = 0;     ///< Major version number
    int minor = 0;     ///< Minor version number
    int patch = 0;     ///< Patch version number
    
    /**
     * @brief Parse semantic version from string
     *
     * Supports formats: "1.2.3", "v1.2.3", "1.2"
     *
     * @param version_string Version string to parse
     * @return Parsed semantic version
     */
    static SemanticVersion Parse(const std::string& version_string);
    
    /**
     * @brief Compare two versions
     *
     * @param other Version to compare against
     * @return -1 if this < other, 0 if equal, 1 if this > other
     */
    int Compare(const SemanticVersion& other) const;
    
    /**
     * @brief Check if this version is compatible with a required version
     *
     * Compatible means: this >= required_min AND this < required_max
     *
     * @param required_min Minimum required version (inclusive)
     * @param required_max Maximum required version (exclusive)
     * @return true if version is compatible
     */
    bool IsCompatible(const SemanticVersion& required_min,
                      const SemanticVersion& required_max) const;
    
    /**
     * @brief Convert version to string format
     * @return Version string like "1.2.3"
     */
    std::string ToString() const;
};

/**
 * @class PluginInspector
 * @brief Main plugin discovery and inspection engine (Phase 2: ranges-integrated)
 *
 * Discovers installed plugins and inspects their capabilities.
 * Provides multiple output formats (table, JSON, CSV, markdown).
 *
 * **Phase 2 C++26 Ranges Integration**:
 * The PluginInspector is designed to work seamlessly with std::ranges algorithms
 * for filtering, counting, and analyzing plugins.
 *
 * @code
 *   PluginInspector inspector("/usr/local/lib/graphx");
 *   auto all_plugins = inspector.InspectAll();
 *   
 *   // Count compliant plugins using ranges
 *   auto compliant_count = ranges::count_if(all_plugins,
 *       [](const auto& p) { return p.IsCompliant(); });
 *   
 *   // Filter plugins with diagnostic capability
 *   auto with_diagnostics = all_plugins
 *       | views::filter([](const auto& p) { 
 *           return p.HasIDiagnosable(); 
 *       })
 *       | ranges::to<std::vector>();
 *   
 *   // Check if any plugin has metrics callback
 *   bool has_metrics = ranges::any_of(all_plugins,
 *       [](const auto& p) { return p.HasIMetricsCallback(); });
 * @endcode
 *
 * @see RangesUtilities.hpp for helper functions
 * @note Non-copyable to prevent accidental duplication of plugin loading
 */
class PluginInspector {
public:
    /**
     * @brief Construct plugin inspector
     *
     * @param plugin_dir Directory containing plugins
     *                    (default: /usr/local/lib/graphx)
     */
    explicit PluginInspector(
        const std::string& plugin_dir = "/usr/local/lib/graphx");
    
    /**
     * @brief Destructor
     */
    ~PluginInspector();
    
    // Non-copyable
    PluginInspector(const PluginInspector&) = delete;
    PluginInspector& operator=(const PluginInspector&) = delete;
    
    // Movable
    PluginInspector(PluginInspector&&) noexcept = default;
    PluginInspector& operator=(PluginInspector&&) noexcept = default;
    
    // ========================================================================
    // Plugin Discovery
    // ========================================================================
    
    /**
     * @brief Discover all available plugins
     *
     * Scans the plugin directory for plugin files (.so, .dylib, .dll).
     * This is a fast, non-invasive operation that doesn't load plugins.
     *
     * @return Vector of discovered plugin metadata
     */
    std::vector<PluginInfo> DiscoverPlugins();
    
    // ========================================================================
    // Plugin Inspection
    // ========================================================================
    
    /**
     * @brief Inspect a specific plugin for capabilities
     *
     * Loads the plugin and queries its interfaces.
     *
     * @param name Plugin name (as returned by DiscoverPlugins)
     * @return Capabilities of the plugin, or error status
     *
     * @throws std::runtime_error if plugin cannot be loaded
     */
    PluginCapabilities InspectPlugin(const std::string& name);
    
    /**
     * @brief Inspect all discovered plugins
     *
     * Discovers and inspects all plugins in the plugin directory.
     * This is the main convenience method for full inspection.
     *
     * @return Vector of all plugin capabilities
     */
    std::vector<PluginCapabilities> InspectAll();
    
    // ========================================================================
    // Compliance Analysis
    // ========================================================================
    
    /**
     * @brief Get compliance statistics for all plugins
     *
     * @return Aggregated statistics (total, configurable, diagnostic, etc.)
     */
    ComplianceStats GetComplianceStats();
    
    /**
     * @brief Get list of non-compliant plugins
     *
     * Non-compliant means the plugin doesn't support IConfigurable.
     *
     * @return Names of non-compliant plugins
     */
    std::vector<std::string> GetNonCompliantPlugins();
    
    // ========================================================================
    // Output Formatting
    // ========================================================================
    
    /**
     * @brief Format plugins as an ASCII table
     *
     * Generates a human-readable table with Unicode box drawing.
     *
     * @param plugins Vector of plugin capabilities to format
     * @return Formatted table string
     */
    std::string FormatTable(const std::vector<PluginCapabilities>& plugins);
    
    /**
     * @brief Format plugins as JSON
     *
     * Generates valid JSON with complete capability information.
     *
     * @param plugins Vector of plugin capabilities to format
     * @return JSON-formatted string
     */
    std::string FormatJson(const std::vector<PluginCapabilities>& plugins);
    
    /**
     * @brief Format plugins as CSV
     *
     * Generates RFC 4180 compliant CSV with proper escaping.
     *
     * @param plugins Vector of plugin capabilities to format
     * @return CSV-formatted string
     */
    std::string FormatCsv(const std::vector<PluginCapabilities>& plugins);
    
    /**
     * @brief Format plugins as Markdown
     *
     * Generates GitHub-compatible markdown with tables and sections.
     *
     * @param plugins Vector of plugin capabilities to format
     * @return Markdown-formatted string
     */
    std::string FormatMarkdown(const std::vector<PluginCapabilities>& plugins);
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    /**
     * @brief Validate a plugin against requirements
     *
     * Checks if a plugin supports all required interfaces.
     *
     * @param name Plugin name
     * @param requirements Interface names to require
     *                     (e.g., {"IConfigurable", "IDiagnosable"})
     * @return true if plugin meets all requirements
     *
     * @throws std::runtime_error if plugin not found
     */
    bool ValidatePlugin(
        const std::string& name,
        const std::vector<std::string>& requirements);
    
    /**
     * @brief Validate all plugins against requirements
     *
     * @param requirements Interface names to require
     * @return Names of plugins that don't meet requirements
     */
    std::vector<std::string> ValidateAll(
        const std::vector<std::string>& requirements);
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set the plugin directory
     *
     * @param plugin_dir Path to plugin directory
     */
    void SetPluginDirectory(const std::string& plugin_dir);
    
    /**
     * @brief Get the current plugin directory
     *
     * @return Path to plugin directory
     */
    std::string GetPluginDirectory() const;
    
    // ========================================================================
    // Cache Management
    // ========================================================================
    
    /**
     * @brief Get current cache information
     *
     * @return Cache statistics and configuration
     */
    CacheInfo GetCacheInfo() const;
    
    /**
     * @brief Enable or disable caching
     *
     * @param enabled true to enable caching, false to disable
     */
    void SetCachingEnabled(bool enabled);
    
    /**
     * @brief Set cache time-to-live
     *
     * @param ttl Cache validity duration in milliseconds
     */
    void SetCacheTTL(std::chrono::milliseconds ttl);
    
    /**
     * @brief Clear the plugin inspection cache
     *
     * Forces the next InspectAll() call to re-inspect all plugins.
     */
    void ClearCache();
    
    // ========================================================================
    // Metadata Extraction
    // ========================================================================
    
    /**
     * @brief Extract plugin metadata from ELF/Mach-O headers
     *
     * Attempts to extract version and description from plugin binary headers.
     * Falls back to filename parsing if headers are unavailable.
     *
     * @param info Plugin info to enhance with metadata
     * @return Enhanced plugin info with extracted metadata
     */
    PluginInfo ExtractPluginMetadata(const PluginInfo& info);
    
    // ========================================================================
    // Version Compatibility Checking
    // ========================================================================
    
    /**
     * @brief Check if a specific plugin version is compatible
     *
     * Verifies that the plugin's version falls within the required range.
     *
     * @param plugin_name Name of plugin to check
     * @param min_version Minimum required version (inclusive)
     * @param max_version Maximum required version (exclusive)
     * @return true if plugin version is compatible
     *
     * @throws std::runtime_error if plugin not found
     */
    bool CheckVersionCompatibility(
        const std::string& plugin_name,
        const std::string& min_version,
        const std::string& max_version);
    
    /**
     * @brief Get the version of a specific plugin
     *
     * @param plugin_name Name of plugin
     * @return Plugin version as semantic version
     *
     * @throws std::runtime_error if plugin not found
     */
    SemanticVersion GetPluginVersion(const std::string& plugin_name);
    
    /**
     * @brief Find all plugins compatible with a version requirement
     *
     * @param min_version Minimum required version (inclusive)
     * @param max_version Maximum required version (exclusive)
     * @return Names of compatible plugins
     */
    std::vector<std::string> FindCompatiblePlugins(
        const std::string& min_version,
        const std::string& max_version);
    
    /**
     * @brief Find the newest version of a plugin
     *
     * Useful for determining latest available version.
     *
     * @param plugin_name Name of plugin to check
     * @return Newest version available
     *
     * @throws std::runtime_error if plugin not found
     */
    SemanticVersion GetNewestVersion(const std::string& plugin_name);

private:
    std::string plugin_dir_;
    std::vector<PluginCapabilities> cached_capabilities_;
    CacheInfo cache_info_;
    
    // Helper methods for inspection
    PluginCapabilities InspectLoadedPlugin(const PluginInfo& info);
    
    // Helper methods for metadata extraction
    std::string ExtractVersionFromELF(const std::string& plugin_path);
    std::string ExtractDescriptionFromELF(const std::string& plugin_path);
    std::string ParseVersionFromFilename(const std::string& filename);
    
    // Helper methods for interface detection
    bool HasInterface(void* node, const std::string& interface_name);
    
    // Helper methods for formatting
    std::string EscapeCsvField(const std::string& field);
};

} // namespace graph
