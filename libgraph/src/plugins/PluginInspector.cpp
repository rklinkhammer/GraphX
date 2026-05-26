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
 * @file PluginInspector.cpp
 * @brief Implementation of PluginInspector
 *
 * @author GraphX Team
 * @date 2026-01-06
 */

#include "plugins/PluginInspector.hpp"
#include "metrics/IMetricsCallback.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <dlfcn.h>
#include <stdexcept>

namespace graph {

namespace fs = std::filesystem;

// ============================================================================
// SemanticVersion Implementation
// ============================================================================

SemanticVersion SemanticVersion::Parse(const std::string& version_string) {
    SemanticVersion version;
    
    // Handle empty string
    if (version_string.empty()) {
        return version; // Default 0.0.0
    }
    
    // Remove 'v' prefix if present
    std::string s = version_string;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s = s.substr(1);
    }
    
    // Parse major.minor.patch
    size_t dot1 = s.find('.');
    size_t dot2 = s.find('.', dot1 + 1);
    
    try {
        if (dot1 != std::string::npos) {
            version.major = std::stoi(s.substr(0, dot1));
            
            if (dot2 != std::string::npos) {
                version.minor = std::stoi(s.substr(dot1 + 1, dot2 - dot1 - 1));
                version.patch = std::stoi(s.substr(dot2 + 1));
            } else {
                version.minor = std::stoi(s.substr(dot1 + 1));
                version.patch = 0;
            }
        } else {
            version.major = std::stoi(s);
        }
    } catch (const std::exception&) {
        // On parse error, return default
        return SemanticVersion();
    }
    
    return version;
}

int SemanticVersion::Compare(const SemanticVersion& other) const {
    if (major != other.major) {
        return major < other.major ? -1 : 1;
    }
    if (minor != other.minor) {
        return minor < other.minor ? -1 : 1;
    }
    if (patch != other.patch) {
        return patch < other.patch ? -1 : 1;
    }
    return 0;
}

bool SemanticVersion::IsCompatible(const SemanticVersion& required_min,
                                   const SemanticVersion& required_max) const {
    return Compare(required_min) >= 0 && Compare(required_max) < 0;
}

std::string SemanticVersion::ToString() const {
    return std::to_string(major) + "." +
           std::to_string(minor) + "." +
           std::to_string(patch);
}

// ============================================================================
// PluginCapabilities Implementation
// ============================================================================

bool PluginCapabilities::HasIConfigurable() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IConfigurable" && cap.supported;
        });
}

bool PluginCapabilities::HasIDiagnosable() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IDiagnosable" && cap.supported;
        });
}

bool PluginCapabilities::HasIParameterized() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IParameterized" && cap.supported;
        });
}

bool PluginCapabilities::HasIMetricsCallback() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IMetricsCallbackProvider" && cap.supported;
        });
}

bool PluginCapabilities::IsCompliant() const {
    return HasIConfigurable();
}

nlohmann::json PluginCapabilities::ToJson() const {
    using json = nlohmann::json;
    
    json j;
    j["name"] = info.name;
    j["path"] = info.path;
    j["version"] = info.version;
    j["is_loaded"] = info.is_loaded;
    
    if (!info.load_error.empty()) {
        j["load_error"] = info.load_error;
    }
    
    j["capabilities"] = json::object();
    j["capabilities"]["configurable"] = HasIConfigurable();
    j["capabilities"]["diagnostic"] = HasIDiagnosable();
    j["capabilities"]["parameterized"] = HasIParameterized();
    j["capabilities"]["metrics_callback"] = HasIMetricsCallback();
    j["compliant"] = IsCompliant();
    
    return j;
}

// ============================================================================
// PluginInspector Implementation
// ============================================================================

PluginInspector::PluginInspector(const std::string& plugin_dir)
    : plugin_dir_(plugin_dir) {
}

PluginInspector::~PluginInspector() = default;

// ========================================================================
// Plugin Discovery
// ========================================================================

std::vector<PluginInfo> PluginInspector::DiscoverPlugins() {
    std::vector<PluginInfo> plugins;
    
    
    
    try {
        if (!fs::exists(plugin_dir_)) {
            
            std::cerr << "Warning: Plugin directory does not exist: "
                      << plugin_dir_ << std::endl;
            return plugins;
        }
        
        
        
        // Scan directory for plugin files
        for (const auto& entry : fs::directory_iterator(plugin_dir_)) {
            try {
                if (!entry.is_regular_file()) {
                    
                    continue;
                }
                
                // Check file extension
                auto path = entry.path();
                auto ext = path.extension().string();
                
                
                
                bool is_plugin = false;
#ifdef _WIN32
                is_plugin = (ext == ".dll");
#elif defined(__APPLE__)
                is_plugin = (ext == ".dylib" || ext == ".so");
#else  // Linux
                is_plugin = (ext == ".so");
#endif
                
                
                
                if (!is_plugin) {
                    
                    continue;
                }
                
                // Extract plugin name (filename without extension)
                auto filename = path.stem().string();
                
                // Remove "lib" prefix if present
                if (filename.size() > 3 && filename.substr(0, 3) == "lib") {
                    filename = filename.substr(3);
                }
                
                PluginInfo info;
                info.name = filename;
                info.path = path.string();
                info.is_loaded = false;
                
                // Extract metadata from plugin (version, description)
                info = ExtractPluginMetadata(info);
                
                plugins.push_back(info);
            } catch (const std::exception& e) {
                
                continue;
            }
        }
        
        
        
        // Sort by name for consistent ordering
        std::sort(plugins.begin(), plugins.end(),
            [](const PluginInfo& a, const PluginInfo& b) {
                return a.name < b.name;
            });
    } catch (const std::exception& e) {
        
        std::cerr << "Error discovering plugins: " << e.what() << std::endl;
    }
    
    return plugins;
}

// ========================================================================
// Plugin Inspection
// ========================================================================

PluginCapabilities PluginInspector::InspectPlugin(const std::string& name) {
    auto discovered = DiscoverPlugins();
    
    // Find the plugin
    auto it = std::find_if(discovered.begin(), discovered.end(),
        [&name](const PluginInfo& info) {
            return info.name == name;
        });
    
    if (it == discovered.end()) {
        throw std::runtime_error("Plugin not found: " + name);
    }
    
    return InspectLoadedPlugin(*it);
}

std::vector<PluginCapabilities> PluginInspector::InspectAll() {
    // Check cache validity
    if (cache_info_.enabled && !cached_capabilities_.empty() && cache_info_.IsValid()) {
        cache_info_.cache_hits++;
        return cached_capabilities_;
    }
    
    cache_info_.cache_misses++;
    
    std::vector<PluginCapabilities> results;
    
    auto discovered = DiscoverPlugins();
    
    
    for (const auto& info : discovered) {
        
        try {
            results.push_back(InspectLoadedPlugin(info));
        } catch (const std::exception& e) {
            
            // Add error result but continue
            PluginCapabilities error_result;
            error_result.info = info;
            error_result.info.is_loaded = false;
            error_result.info.load_error = std::string(e.what());
            results.push_back(error_result);
        }
    }
    
    
    cached_capabilities_ = results;
    cache_info_.last_cached = std::chrono::system_clock::now();
    
    return results;
}

PluginCapabilities PluginInspector::InspectLoadedPlugin(const PluginInfo& info) {
    PluginCapabilities result;
    result.info = info;
    
    // For now, we'll mark all loaded plugins as having basic capabilities
    // since we can't reliably detect them without full initialization
    
    InterfaceCapability config;
    config.name = "IConfigurable";
    config.supported = true;  // Assume compliant for now
    config.description = "Supports JSON configuration";
    result.capabilities.push_back(config);
    
    InterfaceCapability diag;
    diag.name = "IDiagnosable";
    diag.supported = true;  // Assume supported for now
    diag.description = "Provides runtime diagnostics";
    result.capabilities.push_back(diag);
    
    InterfaceCapability param;
    param.name = "IParameterized";
    param.supported = true;  // Assume supported for now
    param.description = "Supports parameter introspection";
    result.capabilities.push_back(param);
    
    InterfaceCapability metrics;
    metrics.name = "IMetricsCallbackProvider";
    metrics.supported = true;  // Nodes can opt-in to metrics
    metrics.description = "Publishes metrics events (phase transitions, state changes)";
    result.capabilities.push_back(metrics);
    
    result.info.is_loaded = true;
    return result;
}

// ========================================================================
// Compliance Analysis
// ========================================================================

ComplianceStats PluginInspector::GetComplianceStats() {
    if (cached_capabilities_.empty()) {
        InspectAll();  // Populate cache
    }
    
    // Phase 2: Modern C++26 ranges approach using count_if
    // Cleaner and more expressive than manual loop iteration
    using namespace std::ranges;
    
    ComplianceStats stats;
    stats.total = cached_capabilities_.size();
    stats.configurable = count_if(cached_capabilities_,
        [](const auto& cap) { return cap.HasIConfigurable(); });
    stats.diagnostic = count_if(cached_capabilities_,
        [](const auto& cap) { return cap.HasIDiagnosable(); });
    stats.parameterized = count_if(cached_capabilities_,
        [](const auto& cap) { return cap.HasIParameterized(); });
    stats.metrics_callback = count_if(cached_capabilities_,
        [](const auto& cap) { return cap.HasIMetricsCallback(); });
    
    return stats;
}

std::vector<std::string> PluginInspector::GetNonCompliantPlugins() {
    if (cached_capabilities_.empty()) {
        InspectAll();  // Populate cache
    }
    
    // Phase 2: Modern ranges approach - filter and transform
    // Collects names of non-compliant plugins using ranges composition
    using namespace std::ranges;
    
    return cached_capabilities_
        | views::filter([](const auto& cap) { return !cap.IsCompliant(); })
        | views::transform([](const auto& cap) { return cap.info.name; })
        | to<std::vector>();
}

// ========================================================================
// Output Formatting
// ========================================================================

std::string PluginInspector::FormatTable(
    const std::vector<PluginCapabilities>& plugins) {
    
    std::stringstream ss;
    
    // Header
    ss << "Name                         Config  Diag  Param  Metrics  Compliant\n";
    ss << "--------------------------------------------------------------------------\n";
    
    // Rows
    for (const auto& plugin : plugins) {
        // Plugin name (left-aligned, 28 chars)
        std::string name = plugin.info.name;
        if (name.length() > 28) {
            name = name.substr(0, 25) + "...";
        }
        name.resize(28, ' ');
        
        ss << name;
        ss << (plugin.HasIConfigurable() ? "Y" : "N");
        ss << std::string(7, ' ');
        ss << (plugin.HasIDiagnosable() ? "Y" : "N");
        ss << std::string(6, ' ');
        ss << (plugin.HasIParameterized() ? "Y" : "N");
        ss << std::string(7, ' ');
        ss << (plugin.HasIMetricsCallback() ? "Y" : "N");
        ss << std::string(9, ' ');
        ss << (plugin.IsCompliant() ? "Y" : "N");
        ss << "\n";
    }
    
    // Summary
    ComplianceStats stats = GetComplianceStats();
    ss << "\nSummary: " << stats.total << " plugins, "
       << stats.configurable << " configurable, "
       << stats.metrics_callback << " with metrics";
    
    return ss.str();
}

std::string PluginInspector::FormatJson(
    const std::vector<PluginCapabilities>& plugins) {
    
    using json = nlohmann::json;
    
    auto stats = GetComplianceStats();
    
    json output;
    output["metadata"]["timestamp"] = "2026-01-06T00:00:00Z";
    output["metadata"]["plugin_directory"] = plugin_dir_;
    output["metadata"]["total_plugins"] = stats.total;
    
    output["summary"]["configurable"] = stats.configurable;
    output["summary"]["diagnostic"] = stats.diagnostic;
    output["summary"]["parameterized"] = stats.parameterized;
    output["summary"]["compliance_percentage"] = stats.CompliancePercentage();
    
    output["plugins"] = json::array();
    for (const auto& plugin : plugins) {
        output["plugins"].push_back(plugin.ToJson());
    }
    
    return output.dump(2);
}

std::string PluginInspector::FormatCsv(
    const std::vector<PluginCapabilities>& plugins) {
    
    std::stringstream ss;
    
    // Header
    ss << "Name,Path,Configurable,Diagnostic,Parameterized,Compliant\n";
    
    // Rows
    for (const auto& plugin : plugins) {
        ss << EscapeCsvField(plugin.info.name) << ","
           << EscapeCsvField(plugin.info.path) << ","
           << (plugin.HasIConfigurable() ? "true" : "false") << ","
           << (plugin.HasIDiagnosable() ? "true" : "false") << ","
           << (plugin.HasIParameterized() ? "true" : "false") << ","
           << (plugin.IsCompliant() ? "true" : "false") << "\n";
    }
    
    return ss.str();
}

std::string PluginInspector::FormatMarkdown(
    const std::vector<PluginCapabilities>& plugins) {
    
    std::stringstream ss;
    
    ss << "# GraphX Plugin Capabilities\n\n";
    ss << "**Generated:** 2026-01-06\n";
    ss << "**Total Plugins:** " << plugins.size() << "\n\n";
    
    auto stats = GetComplianceStats();
    ss << "## Summary\n\n";
    ss << "| Plugin | Configurable | Diagnostic | Parameterized | Compliant |\n";
    ss << "|--------|:------------:|:----------:|:-------------:|:---------:|\n";
    
    for (const auto& plugin : plugins) {
        ss << "| " << plugin.info.name << " | "
           << (plugin.HasIConfigurable() ? "Y" : "N") << " | "
           << (plugin.HasIDiagnosable() ? "Y" : "N") << " | "
           << (plugin.HasIParameterized() ? "Y" : "N") << " | "
           << (plugin.IsCompliant() ? "Y" : "N") << " |\n";
    }
    
    ss << "\n## Statistics\n\n";
    ss << "- **Total:** " << stats.total << "\n";
    ss << "- **IConfigurable:** " << stats.configurable << "/"
       << stats.total << " (" << static_cast<int>(stats.CompliancePercentage())
       << "%)\n";
    ss << "- **IDiagnosable:** " << stats.diagnostic << "/"
       << stats.total << "\n";
    ss << "- **IParameterized:** " << stats.parameterized << "/"
       << stats.total << "\n";
    
    return ss.str();
}

// ========================================================================
// Validation
// ========================================================================

bool PluginInspector::ValidatePlugin(
    const std::string& name,
    const std::vector<std::string>& requirements) {
    
    auto cap = InspectPlugin(name);
    
    for (const auto& req : requirements) {
        if (req == "IConfigurable" && !cap.HasIConfigurable()) {
            return false;
        }
        if (req == "IDiagnosable" && !cap.HasIDiagnosable()) {
            return false;
        }
        if (req == "IParameterized" && !cap.HasIParameterized()) {
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> PluginInspector::ValidateAll(
    const std::vector<std::string>& requirements) {
    
    std::vector<std::string> failures;
    
    auto plugins = DiscoverPlugins();
    for (const auto& plugin : plugins) {
        if (!ValidatePlugin(plugin.name, requirements)) {
            failures.push_back(plugin.name);
        }
    }
    
    return failures;
}

// ========================================================================
// Configuration
// ========================================================================

void PluginInspector::SetPluginDirectory(const std::string& plugin_dir) {
    plugin_dir_ = plugin_dir;
    ClearCache();  // Invalidate cache when directory changes
}

std::string PluginInspector::GetPluginDirectory() const {
    return plugin_dir_;
}

// ========================================================================
// Helper Methods
// ========================================================================

std::string PluginInspector::EscapeCsvField(const std::string& field) {
    // Check if field needs quoting
    if (field.find(',') == std::string::npos &&
        field.find('"') == std::string::npos &&
        field.find('\n') == std::string::npos) {
        return field;
    }
    
    // Quote and escape
    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";  // RFC 4180: double quotes
        } else {
            escaped += c;
        }
    }
    escaped += "\"";
    return escaped;
}

bool PluginInspector::HasInterface(void* node,
                                   const std::string& interface_name) {
    // TODO: Implement dynamic_cast-based detection if RTTI is enabled
    // For now, relies on QueryCapabilities function
    (void)node;
    (void)interface_name;
    return false;
}

// ========================================================================
// Metadata Extraction
// ========================================================================

PluginInfo PluginInspector::ExtractPluginMetadata(const PluginInfo& info) {
    PluginInfo enhanced = info;
    
    // Try to extract version from ELF headers
    std::string elf_version = ExtractVersionFromELF(info.path);
    if (!elf_version.empty()) {
        enhanced.version = elf_version;
    } else {
        // Fall back to filename parsing
        enhanced.version = ParseVersionFromFilename(info.name);
    }
    
    return enhanced;
}

std::string PluginInspector::ExtractVersionFromELF(const std::string& plugin_path) {
    // For now, return empty string - in a production system, would parse ELF headers
    // This requires <elf.h> or Mach-O headers for macOS
    // Simple approach: look for semantic version pattern in binary comments
    
    // TODO: Implement actual ELF header parsing with:
    // - Read file first 4 bytes (ELF magic: 0x7f, 'E', 'L', 'F')
    // - Parse section headers to find .comment or custom sections
    // - Extract version string from comment section
    // - Parse version string into version field
    
    (void)plugin_path;
    return "";  // Placeholder: return empty to indicate extraction failed
}

std::string PluginInspector::ExtractDescriptionFromELF(const std::string& plugin_path) {
    // Similar to version extraction, would parse ELF custom sections
    // For now, return empty string
    
    (void)plugin_path;
    return "";  // Placeholder: return empty to indicate extraction failed
}

std::string PluginInspector::ParseVersionFromFilename(const std::string& filename) {
    // Try to extract version pattern like v1.0.0 or 1.0.0-alpha from filename
    // Format: nodename or nodename_v1.0.0
    
    size_t underscore = filename.find('_');
    if (underscore == std::string::npos) {
        // No version in filename, use default
        return "1.0.0";
    }
    
    std::string version_part = filename.substr(underscore + 1);
    
    // Remove 'v' prefix if present
    if (!version_part.empty() && version_part[0] == 'v') {
        version_part = version_part.substr(1);
    }
    
    // Validate it looks like a version (contains dots)
    if (version_part.find('.') != std::string::npos) {
        return version_part;
    }
    
    return "1.0.0";  // Default version
}

// ========================================================================
// Cache Management
// ========================================================================

CacheInfo PluginInspector::GetCacheInfo() const {
    return cache_info_;
}

void PluginInspector::SetCachingEnabled(bool enabled) {
    cache_info_.enabled = enabled;
    if (!enabled) {
        ClearCache();
    }
}

void PluginInspector::SetCacheTTL(std::chrono::milliseconds ttl) {
    cache_info_.ttl = ttl;
}

void PluginInspector::ClearCache() {
    cached_capabilities_.clear();
    cache_info_.cache_hits = 0;
    cache_info_.cache_misses = 0;
    cache_info_.last_cached = std::chrono::system_clock::time_point();
}

// ============================================================================
// Version Compatibility Checking
// ============================================================================

bool PluginInspector::CheckVersionCompatibility(
    const std::string& plugin_name,
    const std::string& min_version,
    const std::string& max_version) {
    
    // Get the plugin's version
    SemanticVersion plugin_version = GetPluginVersion(plugin_name);
    SemanticVersion min_ver = SemanticVersion::Parse(min_version);
    SemanticVersion max_ver = SemanticVersion::Parse(max_version);
    
    return plugin_version.IsCompatible(min_ver, max_ver);
}

SemanticVersion PluginInspector::GetPluginVersion(const std::string& plugin_name) {
    auto capabilities = InspectAll();
    
    // Phase 2: Modern ranges approach - find_if for locating plugins
    auto it = std::ranges::find_if(capabilities,
        [&plugin_name](const auto& cap) { return cap.info.name == plugin_name; });
    
    if (it != capabilities.end()) {
        return SemanticVersion::Parse(it->info.version);
    }
    
    throw std::runtime_error("Plugin not found: " + plugin_name);
}

std::vector<std::string> PluginInspector::FindCompatiblePlugins(
    const std::string& min_version,
    const std::string& max_version) {
    
    SemanticVersion min_ver = SemanticVersion::Parse(min_version);
    SemanticVersion max_ver = SemanticVersion::Parse(max_version);
    
    auto capabilities = InspectAll();
    
    // Phase 2: Modern ranges approach - filter and transform compatible plugins
    using namespace std::ranges;
    return capabilities
        | views::filter([&min_ver, &max_ver](const auto& cap) {
            SemanticVersion plugin_version = SemanticVersion::Parse(cap.info.version);
            return plugin_version.IsCompatible(min_ver, max_ver);
        })
        | views::transform([](const auto& cap) { return cap.info.name; })
        | to<std::vector>();
}

SemanticVersion PluginInspector::GetNewestVersion(const std::string& plugin_name) {
    auto capabilities = InspectAll();
    
    // Phase 2: Modern ranges approach - filter by name, then find max version
    using namespace std::ranges;
    auto versions = capabilities
        | views::filter([&plugin_name](const auto& cap) { return cap.info.name == plugin_name; })
        | views::transform([](const auto& cap) { return SemanticVersion::Parse(cap.info.version); })
        | to<std::vector>();
    
    if (versions.empty()) {
        throw std::runtime_error("Plugin not found: " + plugin_name);
    }
    
    // Use custom comparator since SemanticVersion has Compare() method but no operator<
    return *max_element(versions.begin(), versions.end(),
        [](const auto& a, const auto& b) { return a.Compare(b) < 0; });
}

} // namespace graph::
