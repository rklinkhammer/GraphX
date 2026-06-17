/**
 * @file JsonDynamicGraphLoader.hpp
 * @brief GraphX source file.
 */

// MIT License
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

/**
 * @file JsonDynamicGraphLoader.hpp
 * @brief Load JSON graph configurations as NodeFacadeAdapter collections
 *
 * JsonDynamicGraphLoader replaces the old JsonGraphLoader with a cleaner,
 * simpler design that works with NodeFacadeAdapter instances.
 *
 * Key features:
 * - Uses INodeProvider for node creation and type discovery
 * - No integration with INode or compile-time type system
 * - Returns raw NodeFacadeAdapter instances for manual graph assembly
 * - Proven interface used by rocket_telemetry.cpp
 *
 * Usage:
 * @code
 * auto provider = std::make_shared<RegisteredNodeProvider>(registry);
 * auto nodes = JsonDynamicGraphLoader::LoadNodesSafe("config.json", provider);
 * auto edges = JsonDynamicGraphLoader::LoadEdgesSafe("config.json");
 * 
 * // Initialize and start nodes...
 * for (auto& node : nodes) {
 *     node.Init();
 *     node.Start();
 * }
 * @endcode
 *
 * @see graph::NodeFacadeAdapter
 * @see graph::INodeProvider
 * @see graph::config::GraphConfig
 *
 * @author GitHub Copilot
 * @date 2026-01-04
 */

#pragma once

#include "GraphConfig.hpp"
#include "graph/NodeFacade.hpp"
#include "config/Errors.hpp"
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <expected>

namespace graph {
    // Forward declaration
    class INodeProvider;
    class INodeMetadataService;
}

namespace graph::config {

/**
 * @class JsonDynamicGraphLoader
 * @brief Loads JSON graph configurations as collections of NodeFacadeAdapter instances
 *
 * Replaces the old JsonGraphLoader. This loader works exclusively with the
 * provider-backed creation path and returns NodeFacadeAdapter instances. All
 * nodes in the configuration must be available via the active provider.
 *
 * Key differences from JsonGraphLoader:
 * - No INode integration (completely removed)
 * - No compile-time typed node support
 * - Simpler API (just load nodes and edges)
 * - Manual edge connection (not automatic)
 * - Proven pattern from rocket_telemetry.cpp
 */
/**
 * @class JsonDynamicGraphLoader
 * @brief Json dynamic graph loader implementation for GraphX.
 */
class JsonDynamicGraphLoader {
public:
    /**
     * Load all nodes from JSON configuration (safe version with expected<>)
     *
     * @param filepath Path to JSON configuration file
     * @param node_provider Node provider for creating configured nodes
     * @return expected<vector<NodeFacadeAdapter>, ConfigError>
     * @noexcept No exceptions in normal operation
     *
     * @note Marked [[nodiscard]] - error code cannot be ignored
     *
     * Example:
     * @code
     * auto result = JsonDynamicGraphLoader::LoadNodesSafe("config.json", provider);
     * if (result) {
     *     auto& nodes = result.value();
     *     // Use nodes
     * } else {
     *     std::cerr << "Failed to load nodes: " 
     *               << app::error::ErrorMessage(result.error()) << std::endl;
     * }
     * @endcode
     */
    [[nodiscard]] static std::expected<
        std::vector<std::shared_ptr<NodeFacadeAdapter>>,
        app::error::ConfigError>
    LoadNodesSafe(
        const std::string& filepath,
        std::shared_ptr<INodeProvider> node_provider,
        const INodeMetadataService* metadata_service = nullptr) noexcept;

    /**
     * Load edge specifications from JSON configuration
     *
     * Returns edge metadata without creating actual connections.
     *
     * @param filepath Path to JSON configuration file
     * @return expected<vector<EdgeConfig>, ConfigError>
     * @noexcept No exceptions in normal operation
     *
     * @note Marked [[nodiscard]] - error code cannot be ignored
     *
     * Example:
     * @code
     * auto result = JsonDynamicGraphLoader::LoadEdgesSafe("config.json");
     * if (result) {
     *     auto& edges = result.value();
     *     for (const auto& edge : edges) {
     *         // Connect nodes manually
     *     }
     * } else {
     *     LOG_ERROR("Failed to load edges: " << result.error());
     * }
     * @endcode
     */
    [[nodiscard]] static std::expected<
        std::vector<EdgeConfig>,
        app::error::ConfigError>
    LoadEdgesSafe(
        const std::string& filepath) noexcept;

    /**
     * Load complete graph configuration
     *
     * Loads both nodes and edges in one call.
     *
     * @param filepath Path to JSON configuration file
    * @param node_provider Node provider for creating configured nodes
     * @return expected<pair<nodes, edges>, ConfigError>
     * @noexcept No exceptions in normal operation
     *
     * @note Marked [[nodiscard]] - error code cannot be ignored
     *
     * Example:
     * @code
     * auto result = JsonDynamicGraphLoader::LoadGraphSafe("config.json", provider);
     * if (result) {
     *     auto [nodes, edges] = result.value();
     *     // Use nodes and edges
     * } else {
     *     std::string error = app::error::ErrorMessage(result.error());
     *     return InitializationError{error};
     * }
     * @endcode
     */
    [[nodiscard]] static std::expected<
        std::pair<std::vector<std::shared_ptr<NodeFacadeAdapter>>, std::vector<EdgeConfig>>,
        app::error::ConfigError>
    LoadGraphSafe(
        const std::string& filepath,
        std::shared_ptr<INodeProvider> node_provider,
        const INodeMetadataService* metadata_service = nullptr) noexcept;

private:
    /**
     * Parse JSON file safely with expected<> error handling (Phase 5c)
     *
     * @param filepath Path to JSON file
     * @return expected<GraphConfig, ConfigError> with parsed config or error
     * @noexcept No exceptions in normal operation
     */
    [[nodiscard]] static std::expected<GraphConfig, app::error::ConfigError>
    ParseConfigFileSafe(const std::string& filepath) noexcept;

    /**
     * Apply node-specific configuration from JSON
     *
     * @param adapter NodeFacadeAdapter to configure
     * @param config_json JSON object with node configuration
     */
    [[nodiscard]] static std::expected<void, app::error::ConfigError> ApplyNodeConfiguration(
        const std::shared_ptr<NodeFacadeAdapter>& adapter,
        const nlohmann::json& config_json) noexcept;

    [[nodiscard]] static std::expected<void, app::error::ConfigError> ApplyPortConfiguration(
        const std::shared_ptr<NodeFacadeAdapter>& adapter,
        const std::map<std::string, nlohmann::json>& port_config) noexcept;
};

}  // namespace graph::config
