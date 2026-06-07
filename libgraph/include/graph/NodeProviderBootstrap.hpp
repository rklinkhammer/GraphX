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

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <expected>
#include <chrono>

// Forward declarations
namespace graph {
class INodeProvider;
class PluginLoader;
class PluginRegistry;
}

namespace app {

/**
 * @class NodeProviderBootstrap
 * @brief Manage node-provider lifecycle and plugin discovery
 * 
 * Purpose: Centralized management of plugin loading and provider creation.
 * 
 * Design:
 * - Static bootstrap entry point (no instances needed)
 * - Returns ProviderBootstrapResult (provider handle + diagnostics + lifetime)
 * - Graceful error handling (logs failures but doesn't crash on missing plugins)
 * - Query methods for discovering available node types
 * 
 * Thread Safety: Single-threaded use only (provider creation must happen before graph build)
 * 
 * PluginLoader does not dlclose loaded plugin handles during normal destruction,
 * so the returned loader primarily preserves plugin bookkeeping for callers that
 * need inspection or explicit unload operations.
 * 
 * Usage:
 * @code
 * auto bootstrap = NodeProviderBootstrap::CreateProviderExpected("./plugins");
 * if (!bootstrap) {
 *   std::cerr << "Failed to create provider\n";
 *   return 1;
 * }
 * 
 * auto provider = bootstrap->provider;
 * auto available = NodeProviderBootstrap::GetAvailableNodeTypesExpected(provider);
 * for (const auto& type : available) {
 *   std::cout << "  - " << type << "\n";
 * }
 * 
 * auto available = NodeProviderBootstrap::IsNodeTypeAvailableExpected(provider, "MyNode");
 * if (available && *available) {
 *   auto node = provider->CreateNodeExpected("MyNode");
 * }
 * 
 * // Keep bootstrap lifetime alive in AppContext
 * context.provider_bootstrap = bootstrap->lifetime;
 * context.provider = provider;
 * @endcode
 */
class NodeProviderBootstrap {
public:
  enum class ProviderBootstrapError {
    InvalidPluginDirectory = 1,
    RegistryCreationFailed = 2,
    LoaderCreationFailed = 3,
    ProviderCreationFailed = 4,
    NullProvider = 5,
    QueryFailed = 6,
    Unknown = 99,
  };

  struct ProviderBootstrapDiagnostics {
    std::string plugin_directory{};
    std::size_t discovered_count{0};
    std::size_t loaded_count{0};
    std::size_t failed_count{0};
    std::chrono::milliseconds scan_duration{0};
    std::chrono::milliseconds init_duration{0};
  };

  struct ProviderBootstrapHandle {
    std::shared_ptr<graph::PluginRegistry> plugin_registry;
    std::shared_ptr<graph::PluginLoader> loader;
  };

  struct ProviderBootstrapResult {
    std::shared_ptr<graph::INodeProvider> provider;
    ProviderBootstrapDiagnostics diagnostics{};
    std::shared_ptr<ProviderBootstrapHandle> lifetime;
  };

  /**
  * @brief Create node provider and PluginLoader from plugin directory
   * 
   * Process:
   * 1. Create PluginRegistry (for tracking loaded plugins)
   * 2. Create PluginLoader with registry and plugin directory
   * 3. Load plugins from directory (logs failures but continues)
  * 4. Create RegisteredNodeProvider with registry
  * 5. Return ProviderBootstrapResult
   * 
   * Error Handling:
   * - Missing plugin directory: Creates empty provider (logs warning)
   * - Plugin load failure: Logs error but continues (graceful degradation)
   * - Empty plugin directory: Creates provider with no plugins (not an error)
  * - Returns error on critical failure
   * 
   * @param plugin_directory Path to directory containing .so/.dll plugin files
  * @return ProviderBootstrapResult with provider-first handle, diagnostics, and lifetime bookkeeping
   *         - Both non-null if successful
   * 
   * Example:
   * @code
  * auto bootstrap = NodeProviderBootstrap::CreateProviderExpected("./plugins");
  * if (!bootstrap) {
   *   std::cerr << "Provider creation failed\n";
   *   return false;
   * }
   * 
  * app_context.provider_bootstrap = bootstrap->lifetime;
  * app_context.provider = bootstrap->provider;  // provider handle
   * @endcode
   */
  [[nodiscard]] static std::expected<ProviderBootstrapResult, ProviderBootstrapError>
  CreateProviderExpected(const std::string& plugin_directory) noexcept;
  
  /**
    * @brief Get list of available node types in provider
   * 
    * Queries the provider for all registered node types.
   * Types come from loaded plugins plus built-in node types.
   * 
    * @param provider INodeProvider instance to query (must be non-null)
   * @return Vector of node type names (may be empty if no plugins/types loaded)
   * 
   * Example:
   * @code
    * auto types = NodeProviderBootstrap::GetAvailableNodeTypesExpected(provider);
   * std::cout << "Available node types: " << types.size() << "\n";
   * for (const auto& type : types) {
   *   std::cout << "  - " << type << "\n";
   * }
   * @endcode
   */
  [[nodiscard]] static std::expected<std::vector<std::string>, ProviderBootstrapError>
  GetAvailableNodeTypesExpected(
      const std::shared_ptr<graph::INodeProvider>& provider) noexcept;
  
  /**
   * @brief Check if specific node type is available
   * 
    * Determines if the provider can create nodes of the given type.
   * More efficient than GetAvailableNodeTypes() when checking single type.
   * 
    * @param provider INodeProvider instance to query (must be non-null)
   * @param type_name Node type identifier to check
   * @return true if type is available, false otherwise
   * 
   * Example:
   * @code
    * auto available = NodeProviderBootstrap::IsNodeTypeAvailableExpected(provider, "AccelNode");
   * if (available && *available) {
    *   auto node = provider->CreateNodeExpected("AccelNode");
   *   graph_manager->AddNode(node);
   * } else {
   *   std::cerr << "AccelNode type not available\n";
   * }
   * @endcode
   */
  [[nodiscard]] static std::expected<bool, ProviderBootstrapError>
  IsNodeTypeAvailableExpected(
      const std::shared_ptr<graph::INodeProvider>& provider,
      const std::string& type_name) noexcept;
};

}  // namespace app
