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

// Forward declarations
namespace graph {
class NodeFactory;
class PluginLoader;
class PluginRegistry;
}

namespace app {

/**
 * @class FactoryManager
 * @brief Manage NodeFactory lifecycle and plugin discovery
 * 
 * Purpose: Centralized management of plugin loading and factory creation with proper
 * lifetime management. Ensures PluginLoader outlives all node creation operations.
 * 
 * Design:
 * - Static factory creation (no instances needed)
 * - Returns pair of (factory, loader) to force caller to manage loader lifetime
 * - Graceful error handling (logs failures but doesn't crash on missing plugins)
 * - Query methods for discovering available node types
 * 
 * Thread Safety: Single-threaded use only (factory creation must happen before graph build)
 * 
 * Critical Lifetime Management:
 * The PluginLoader MUST outlive all plugin function calls. This is because dlopen() keeps
 * function pointers that are invalid after dlclose(). If PluginLoader is destroyed before
 * the factory finishes using plugin functions, segfaults occur (root cause of previous
 * graphsim crashes).
 * 
 * Solution: Return (factory, loader) pair from CreateFactory(). Caller MUST:
 * 1. Store loader in AppContext
 * 2. Keep loader alive until application shutdown
 * 3. Destroy factory BEFORE destroying loader
 * 
 * Usage:
 * @code
 * auto [factory, loader] = FactoryManager::CreateFactory("./plugins");
 * if (!factory) {
 *   std::cerr << "Failed to create factory\n";
 *   return 1;
 * }
 * 
 * auto available = FactoryManager::GetAvailableNodeTypes(factory);
 * for (const auto& type : available) {
 *   std::cout << "  - " << type << "\n";
 * }
 * 
 * if (FactoryManager::IsNodeTypeAvailable(factory, "MyNode")) {
 *   auto node = factory->CreateNode("MyNode");
 * }
 * 
 * // Keep loader alive in AppContext
 * context.plugin_loader = loader;
 * context.factory = factory;
 * @endcode
 */
class FactoryManager {
public:
  /**
   * @brief Create NodeFactory and PluginLoader from plugin directory
   * 
   * Process:
   * 1. Create PluginRegistry (for tracking loaded plugins)
   * 2. Create PluginLoader with registry and plugin directory
   * 3. Load plugins from directory (logs failures but continues)
   * 4. Create NodeFactory with registry
   * 5. Return (factory, loader) pair
   * 
   * Error Handling:
   * - Missing plugin directory: Creates empty factory (logs warning)
   * - Plugin load failure: Logs error but continues (graceful degradation)
   * - Empty plugin directory: Creates factory with no plugins (not an error)
   * - Returns (nullptr, nullptr) only on critical failure
   * 
   * @param plugin_directory Path to directory containing .so/.dll plugin files
   * @return std::pair of (NodeFactory, PluginLoader)
   *         - Both non-null if successful
   *         - (nullptr, nullptr) if critical error (rare)
   *         - Caller MUST keep loader alive longer than factory
   * 
   * @throws std::runtime_error if plugin_directory path is invalid or inaccessible
   * 
   * Example:
   * @code
   * auto [factory, loader] = FactoryManager::CreateFactory("./plugins");
   * if (!factory || !loader) {
   *   std::cerr << "Factory creation failed\n";
   *   return false;
   * }
   * 
   * // Keep loader alive
   * app_context.plugin_loader = loader;  // CRITICAL
   * app_context.factory = factory;
   * @endcode
   */
  static std::pair<std::shared_ptr<graph::NodeFactory>, 
                   std::shared_ptr<graph::PluginLoader>>
  CreateFactory(const std::string& plugin_directory);
  
  /**
   * @brief Get list of available node types in factory
   * 
   * Queries the factory for all registered node types.
   * Types come from loaded plugins plus built-in node types.
   * 
   * @param factory NodeFactory instance to query (must be non-null)
   * @return Vector of node type names (may be empty if no plugins/types loaded)
   * 
   * @throws std::runtime_error if factory is null
   * 
   * Example:
   * @code
   * auto types = FactoryManager::GetAvailableNodeTypes(factory);
   * std::cout << "Available node types: " << types.size() << "\n";
   * for (const auto& type : types) {
   *   std::cout << "  - " << type << "\n";
   * }
   * @endcode
   */
  static std::vector<std::string> GetAvailableNodeTypes(
      const std::shared_ptr<graph::NodeFactory>& factory);
  
  /**
   * @brief Check if specific node type is available
   * 
   * Determines if the factory can create nodes of the given type.
   * More efficient than GetAvailableNodeTypes() when checking single type.
   * 
   * @param factory NodeFactory instance to query (must be non-null)
   * @param type_name Node type identifier to check
   * @return true if type is available, false otherwise
   * 
   * @throws std::runtime_error if factory is null
   * 
   * Example:
   * @code
   * if (FactoryManager::IsNodeTypeAvailable(factory, "AccelNode")) {
   *   auto node = factory->CreateNode("AccelNode");
   *   graph_manager->AddNode(node);
   * } else {
   *   std::cerr << "AccelNode type not available\n";
   * }
   * @endcode
   */
  static bool IsNodeTypeAvailable(
      const std::shared_ptr<graph::NodeFactory>& factory,
      const std::string& type_name);
};

}  // namespace app

