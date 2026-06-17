/**
 * @file PluginInspector.cpp
 * @brief GraphX source file.
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

/**
 * @file PluginInspector.cpp
 * @brief Implementation of PluginInspector
 *
 * @author GraphX Team
 * @date 2026-01-06
 */

#include "plugins/PluginInspector.hpp"
#include "plugins/PluginInterop.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeDescriptor.hpp"
#include "graph/NodeMetadataService.hpp"
#include "config/SchemaGenerator.hpp"
#include "metrics/IMetricsCallback.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <stdexcept>

namespace graph {

namespace fs = std::filesystem;

// ============================================================================
// SemanticVersion Implementation
// ============================================================================

/**
 * @brief Parse.
 * @param version_string Parameter for parse.
 */
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

/**
 * @brief Compare.
 * @param other Parameter for compare.
 */
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

/**
 * @brief To string.
 */
std::string SemanticVersion::ToString() const {
    return std::to_string(major) + "." +
           std::to_string(minor) + "." +
           std::to_string(patch);
}

// ============================================================================
// PluginCapabilities Implementation
// ============================================================================

/**
 * @brief Has i configurable.
 */
bool PluginCapabilities::HasIConfigurable() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IConfigurable" && cap.supported;
        });
}

/**
 * @brief Has i diagnosable.
 */
bool PluginCapabilities::HasIDiagnosable() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IDiagnosable" && cap.supported;
        });
}

/**
 * @brief Has i parameterized.
 */
bool PluginCapabilities::HasIParameterized() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IParameterized" && cap.supported;
        });
}

/**
 * @brief Has i metrics callback.
 */
bool PluginCapabilities::HasIMetricsCallback() const {
    // Phase 2: Modern C++20 ranges for semantic clarity and early termination
    return std::ranges::any_of(capabilities,
        [](const auto& cap) {
            return cap.name == "IMetricsCallbackProvider" && cap.supported;
        });
}

/**
 * @brief Is compliant.
 */
bool PluginCapabilities::IsCompliant() const {
    return HasIConfigurable();
}

/**
 * @brief To json.
 */
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

    if (!node_descriptor_schema.is_null() && !node_descriptor_schema.empty()) {
        j["node_descriptor_schema"] = node_descriptor_schema;
    }
    
    return j;
}

// ============================================================================
// PluginInspector Implementation
// ============================================================================

PluginInspector::PluginInspector(
    const std::string& plugin_dir,
    const INodeMetadataService* metadata_service)
    : plugin_dir_(plugin_dir),
      metadata_service_(metadata_service ? metadata_service : &GetDefaultNodeMetadataService()) {
}

PluginInspector::~PluginInspector() = default;

// ========================================================================
// Plugin Discovery
// ========================================================================

/**
 * @brief Discover plugins.
 */
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

/**
 * @brief Inspect plugin.
 * @param name Parameter for inspect plugin.
 */
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

/**
 * @brief Inspect all.
 */
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

/**
 * @brief Inspect loaded plugin.
 * @param info Parameter for inspect loaded plugin.
 */
PluginCapabilities PluginInspector::InspectLoadedPlugin(const PluginInfo& info) {
    PluginCapabilities result;
    result.info = info;
    result.node_descriptor_schema = nlohmann::json();

    void* plugin_handle = nullptr;
    void* node_handle = nullptr;
    const NodeFacade* inspected_facade = nullptr;

    try {
        plugin_handle = dlopen(info.path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!plugin_handle) {
            const char* error = dlerror();
            throw std::runtime_error(error ? error : "dlopen failed");
        }

        auto get_info = ResolveGetPluginInfoFunction(plugin_handle);
        auto get_facade = ResolveGetPluginFacadeFunction(plugin_handle);

        if (!get_info || !get_facade) {
            throw std::runtime_error("plugin_get_info/plugin_get_facade missing");
        }

        const char* info_string = (*get_info)();
        if (!info_string) {
            throw std::runtime_error("plugin_get_info returned null");
        }

        std::vector<std::string> info_parts;
        std::stringstream ss(info_string);
        std::string part;
        while (std::getline(ss, part, '|')) {
            info_parts.push_back(part);
        }

        if (info_parts.size() < 4) {
            throw std::runtime_error("plugin info format invalid");
        }

        const std::string& create_symbol = info_parts[3];
        auto create_node = ResolveCreateNodeFunction(plugin_handle, create_symbol);
        if (!create_node) {
            throw std::runtime_error("create function missing: " + create_symbol);
        }

        inspected_facade = (*get_facade)();
        if (!inspected_facade) {
            throw std::runtime_error("plugin_get_facade returned null");
        }

        auto created_node = CreateNodeFromPlugin(*create_node);
        if (!created_node) {
            throw std::runtime_error("create function returned null handle");
        }
        node_handle = *created_node;

        // Descriptor-driven metadata path: rely on NodeFacadeAdapter so all
        // surfaces share one descriptor extraction implementation.
        NodeFacadeAdapter adapter(
            node_handle,
            inspected_facade,
            metadata_service_);
        result.node_descriptor_schema =
            metadata_service_->DescriptorSchemaProvider().BuildSchema(adapter.GetDescriptor());

        InterfaceCapability config;
        config.name = "IConfigurable";
        config.supported =
            inspected_facade->GetAsIConfigurable &&
            (inspected_facade->GetAsIConfigurable(node_handle) != nullptr);
        config.description = "IConfigurable interface is exposed by the node";
        result.capabilities.push_back(config);

        InterfaceCapability diag;
        diag.name = "IDiagnosable";
        diag.supported =
            inspected_facade->GetAsIDiagnosable &&
            (inspected_facade->GetAsIDiagnosable(node_handle) != nullptr);
        diag.description = "IDiagnosable interface is exposed by the node";
        result.capabilities.push_back(diag);

        InterfaceCapability param;
        param.name = "IParameterized";
        param.supported =
            inspected_facade->GetAsIParameterized &&
            (inspected_facade->GetAsIParameterized(node_handle) != nullptr);
        param.description = "IParameterized interface is exposed by the node";
        result.capabilities.push_back(param);

        InterfaceCapability metrics;
        metrics.name = "IMetricsCallbackProvider";
        metrics.supported =
            inspected_facade->GetAsIMetricsCallbackProvider &&
            (inspected_facade->GetAsIMetricsCallbackProvider(node_handle) != nullptr);
        metrics.description = "Metrics callback provider interface is exposed by the node";
        result.capabilities.push_back(metrics);

        result.info.is_loaded = true;
    } catch (const std::exception& e) {
        result.info.is_loaded = false;
        result.info.load_error = e.what();
    }

    if (node_handle && inspected_facade && inspected_facade->Destroy) {
        inspected_facade->Destroy(node_handle);
        node_handle = nullptr;
    }

    if (node_handle) {
        // If we still have a node handle, we couldn't safely destroy it.
        // Mark inspection as failed to avoid leaking opaque plugin state.
        result.info.is_loaded = false;
        if (result.info.load_error.empty()) {
            result.info.load_error = "failed to destroy temporary inspection node";
        }
    }

    if (plugin_handle) {
        dlclose(plugin_handle);
    }

    return result;
}

// ========================================================================
// Compliance Analysis
// ========================================================================

/**
 * @brief Get compliance stats.
 */
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

/**
 * @brief Get non compliant plugins.
 */
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

/**
 * @brief Set plugin directory.
 * @param plugin_dir Parameter for set plugin directory.
 */
void PluginInspector::SetPluginDirectory(const std::string& plugin_dir) {
    plugin_dir_ = plugin_dir;
    ClearCache();  // Invalidate cache when directory changes
}

/**
 * @brief Get plugin directory.
 */
std::string PluginInspector::GetPluginDirectory() const {
    return plugin_dir_;
}

// ========================================================================
// Helper Methods
// ========================================================================

/**
 * @brief Escape csv field.
 * @param field Parameter for escape csv field.
 */
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

/**
 * @brief Extract plugin metadata.
 * @param info Parameter for extract plugin metadata.
 */
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

/**
 * @brief Extract version from elf.
 * @param plugin_path Parameter for extract version from elf.
 */
std::string PluginInspector::ExtractVersionFromELF(const std::string& plugin_path) {
    std::ifstream file(plugin_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    // Read a bounded chunk for quick metadata probing.
    std::string data;
    data.resize(64 * 1024);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(file.gcount()));

    if (data.size() < 4) {
        return "";
    }

    const auto b0 = static_cast<unsigned char>(data[0]);
    const auto b1 = static_cast<unsigned char>(data[1]);
    const auto b2 = static_cast<unsigned char>(data[2]);
    const auto b3 = static_cast<unsigned char>(data[3]);

    const bool is_elf = (b0 == 0x7f && b1 == 'E' && b2 == 'L' && b3 == 'F');

    // Mach-O variants (32/64-bit, little/big endian) plus fat binaries.
    const bool is_macho =
        (b0 == 0xFE && b1 == 0xED && b2 == 0xFA && (b3 == 0xCE || b3 == 0xCF)) ||
        ((b0 == 0xCE || b0 == 0xCF) && b1 == 0xFA && b2 == 0xED && b3 == 0xFE) ||
        (b0 == 0xCA && b1 == 0xFE && b2 == 0xBA && b3 == 0xBE) ||
        (b0 == 0xBE && b1 == 0xBA && b2 == 0xFE && b3 == 0xCA);

    if (!is_elf && !is_macho) {
        return "";
    }

    const auto is_ascii_alnum = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0;
    };

    auto parse_digits = [&](size_t pos, size_t* out_next) -> size_t {
        size_t p = pos;
        while (p < data.size() && std::isdigit(static_cast<unsigned char>(data[p])) != 0) {
            ++p;
        }
        *out_next = p;
        return p - pos;
    };

    std::string first_two_part;

    for (size_t i = 0; i < data.size(); ++i) {
        const bool has_prefix_v = (data[i] == 'v' || data[i] == 'V');
        if (has_prefix_v && (i + 1 >= data.size() || std::isdigit(static_cast<unsigned char>(data[i + 1])) == 0)) {
            continue;
        }

        if (!has_prefix_v && std::isdigit(static_cast<unsigned char>(data[i])) == 0) {
            continue;
        }

        if (i > 0 && (is_ascii_alnum(data[i - 1]) || data[i - 1] == '.')) {
            continue;
        }

        size_t pos = has_prefix_v ? i + 1 : i;
        size_t next = pos;
        if (parse_digits(pos, &next) == 0 || next >= data.size() || data[next] != '.') {
            continue;
        }

        pos = next + 1;
        if (parse_digits(pos, &next) == 0) {
            continue;
        }

        // Prefer major.minor.patch when available.
        if (next < data.size() && data[next] == '.') {
            size_t patch_start = next + 1;
            size_t patch_next = patch_start;
            if (parse_digits(patch_start, &patch_next) > 0) {
                if (patch_next == data.size() || !is_ascii_alnum(data[patch_next])) {
                    return data.substr(has_prefix_v ? i + 1 : i, patch_next - (has_prefix_v ? i + 1 : i));
                }
            }
        }

        // Remember first valid major.minor token as fallback.
        if ((next == data.size() || (!is_ascii_alnum(data[next]) && data[next] != '.')) && first_two_part.empty()) {
            const size_t start = has_prefix_v ? i + 1 : i;
            first_two_part = data.substr(start, next - start);
        }
    }

    if (!first_two_part.empty()) {
        return first_two_part;
    }

    return "";
}

/**
 * @brief Extract description from elf.
 * @param plugin_path Parameter for extract description from elf.
 */
std::string PluginInspector::ExtractDescriptionFromELF(const std::string& plugin_path) {
    // Similar to version extraction, would parse ELF custom sections
    // For now, return empty string
    
    (void)plugin_path;
    return "";  // Placeholder: return empty to indicate extraction failed
}

/**
 * @brief Parse version from filename.
 * @param filename Parameter for parse version from filename.
 */
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

/**
 * @brief Get cache info.
 */
CacheInfo PluginInspector::GetCacheInfo() const {
    return cache_info_;
}

/**
 * @brief Set caching enabled.
 * @param enabled Parameter for set caching enabled.
 */
void PluginInspector::SetCachingEnabled(bool enabled) {
    cache_info_.enabled = enabled;
    if (!enabled) {
        ClearCache();
    }
}

/**
 * @brief Set cache ttl.
 * @param ttl Parameter for set cache ttl.
 */
void PluginInspector::SetCacheTTL(std::chrono::milliseconds ttl) {
    cache_info_.ttl = ttl;
}

/**
 * @brief Clear cache.
 */
void PluginInspector::ClearCache() {
    cached_capabilities_.clear();
    cache_info_.cache_hits = 0;
    cache_info_.cache_misses = 0;
    cache_info_.last_cached = std::chrono::system_clock::time_point();
}

/**
 * @brief Get newest version.
 * @param plugin_name Parameter for get newest version.
 */
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
