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
 * @file GraphExecutorBuilder.hpp
 * @brief Builder for creating pre-configured graph executors
 *
 * Handles all setup concerns:
 * - JSON configuration loading via ConfigManager
 * - Plugin discovery and loading via FactoryManager
 * - Graph building via GraphBuilder
 * - CSV input configuration (optional)
 * - Execution parameters (timeout, threads, logging)
 *
 * Returns appropriate executor type:
 * - GraphExecutor (no CSV inputs)
 * - CSVGraphExecutor (with CSV inputs)
 *
 * Usage (fluent API):
 * @code
 * auto executor = graph::GraphExecutorBuilder()
 *     .WithJsonConfig("config.json")
 *     .WithPluginDirectory("./plugins")
 *     .WithCSVInputs({{"data.csv", "SensorNode"}})
 *     .WithCSVInjectionRate(10)
 *     .WithExecutorTimeout(std::chrono::seconds(30))
 *     .WithGraphThreads(4)
 *     .WithVerboseLogging(true)
 *     .Build();  // Returns shared_ptr<GraphExecutor>
 * @endcode
 *
 * @author Copilot
 * @date 2026-01-07
 */

#pragma once

#include "graph/GraphExecutor.hpp"
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <utility>

namespace graph {

/**
 * @class GraphExecutorBuilder
 * @brief Builder for configuring and creating graph executors
 *
 * Single-use builder that orchestrates graph construction:
 * 1. Validate required configuration (JSON path)
 * 2. Load JSON via ConfigManager
 * 3. Create and initialize FactoryManager
 * 4. Load plugins from specified directory
 * 5. Build graph via GraphBuilder
 * 6. Return appropriate executor type (with/without CSV support)
 *
 * Thread Safety: Not thread-safe. Each thread should use its own builder instance.
 */
class GraphExecutorBuilder {
public:
    /**
     * @brief Construct a new GraphExecutorBuilder
     *
     * Initializes with default values:
     * - executor_timeout: 30 seconds
     * - graph_threads: 4
     * - csv_injection_rate_ms: 10 (100Hz)
     * - verbose_logging: false
     */
    GraphExecutorBuilder();

    /**
     * @brief Destructor
     */
    ~GraphExecutorBuilder();

    /**
     * @brief Set JSON configuration file path (REQUIRED)
     *
     * @param path Path to JSON graph configuration file
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
    GraphExecutorBuilder& WithJsonConfig(const std::string& path);

    /**
     * @brief Set shared GraphManager instance (optional)
     *
     * Allows injecting a pre-constructed GraphManager.
     * If not set, a new GraphManager will be created internally.
     *
     * @param graph_manager Shared pointer to GraphManager
     * @return Reference to this builder (fluent API)   
     */

     GraphExecutorBuilder& WithGraphManager(std::shared_ptr<graph::GraphManager> graph_manager);

    /**
     * @brief Set plugin directory path (optional)
     *
     * If not set, defaults to "./plugins"
     *
     * @param directory Path to directory containing plugin .so files
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
    GraphExecutorBuilder& WithPluginDirectory(const std::string& directory);

    /**
     * @brief Add single CSV input for a node
     *
     * @param path Path to CSV file
     * @param node Target node name (optional, use empty for auto-discovery)
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
    GraphExecutorBuilder& WithCSVInput(const std::string& path, const std::string& node = "");

    /**
     * @brief Set multiple CSV inputs
     *
     * Replaces any previously set CSV inputs.
     *
     * @param inputs Vector of (csv_path, node_name) pairs
     * @return Reference to this builder (fluent API)
     */
    GraphExecutorBuilder& WithCSVInputs(const std::vector<std::pair<std::string, std::string>>& inputs);

    /**
     * @brief Set CSV injection rate in milliseconds
     *
     * Controls how often rows are injected from CSV files.
     * Default: 10ms (100Hz)
     *
     * @param rate_ms Milliseconds between row injections
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if rate_ms is 0
     */
    GraphExecutorBuilder& WithCSVInjectionRate(uint32_t rate_ms);

    /**
     * @brief Set executor timeout
     *
     * Maximum time to wait for graph execution to complete.
     * Default: 30 seconds
     *
     * @param timeout Timeout duration
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if timeout is <= 0 seconds
     */
    GraphExecutorBuilder& WithExecutorTimeout(const std::chrono::seconds& timeout);

    /**
     * @brief Set number of graph execution threads
     *
     * Recommended: 4 (default) for typical systems.
     * Adjust based on CPU cores and workload.
     *
     * @param count Number of threads (must be > 0)
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if count is 0
     */
    GraphExecutorBuilder& WithGraphThreads(size_t count);

    /**
     * @brief Enable or disable CLI mode
     * 
     * Default: false (normal operation with dashboard)
     * When enabled, executor will run in command-line mode.
     *
     * @param enabled If true, enable CLI mode; if false, disable CLI mode
     * @return Reference to this builder (fluent API)
     */
    GraphExecutorBuilder& WithCliMode(bool enabled);

    /**
     * @brief Enable/disable verbose logging
     *
     * Default: false (normal operation)
     *
     * @param enabled If true, log detailed execution information
     * @return Reference to this builder (fluent API)
     */
    GraphExecutorBuilder& WithVerboseLogging(bool enabled);

    /**
     * @brief Build and return configured executor
     *
     * Performs complete graph construction:
     * 1. Validates required configuration (WithJsonConfig must be called)
     * 2. Loads JSON configuration
     * 3. Creates and initializes factory manager
     * 4. Loads plugins from plugin directory
     * 5. Builds graph using GraphBuilder
     * 6. Creates appropriate executor type
     *
     * Return type is determined by CSV configuration:
     * - If csv_inputs is empty: returns GraphExecutor
     * - If csv_inputs has entries: returns CSVGraphExecutor
     *
     * Both returned as shared_ptr<GraphExecutor> (polymorphic).
     *
     * @return Configured executor ready for execution
     * @throws std::invalid_argument if required configuration missing
     * @throws std::runtime_error if JSON loading fails, plugins not found, graph building fails
     * @throws std::logic_error if Build() called more than once
     */
    std::shared_ptr<GraphExecutor> Build();

private:
    // Configuration state
    std::string json_config_;
    std::shared_ptr<graph::GraphManager> graph_manager_;
    std::string plugin_directory_;
    std::vector<std::pair<std::string, std::string>> csv_inputs_;
    uint32_t csv_injection_rate_ms_;
    std::chrono::seconds executor_timeout_;
    size_t graph_threads_;
    bool verbose_logging_;
    bool already_built_;
    bool cli_mode_;
    
    /**
     * @brief Get default plugin directory
     *
     * Searches in order:
     * 1. Relative to executable directory
     * 2. Relative to current working directory
     * 3. Falls back to "./plugins"
     *
     * @return Default plugin directory path
     */
    static std::string GetDefaultPluginDirectory();

    /**
     * @brief Validate all configuration before building
     *
     * @throws std::invalid_argument if required fields not set
     * @throws std::runtime_error if files/directories don't exist
     */
    void ValidateConfiguration();
};

}  // namespace graph

