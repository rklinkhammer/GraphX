/**
 * @file GraphExecutorBuilder.hpp
 * @brief Graph Executor Builder Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#pragma once

#include "graph/GraphExecutor.hpp"
#include "config/Errors.hpp"
#include <expected>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <utility>

namespace graph {

/**
 * @class GraphExecutorBuilder
 * @brief Builder for configuring and creating graph executors
 *
 * Single-use builder that creates a lightweight CONFIGURED executor shell:
 * 1. Validate builder inputs and record the immutable configuration snapshot
 * 2. Record plugin directories and runtime policy options
 * 3. Register stable control capabilities
 * 4. Return the executor without loading providers/plugins or building nodes
 *
 * Provider loading, plugin loading, node creation, and GraphManager
 * construction are deferred to GraphExecutor::Init().
 *
 * Thread Safety: Not thread-safe. Each thread should use its own builder instance.
 */
/**
 * @class GraphExecutorBuilder
 * @brief Graph Executor Builder builder.
 *
 * @details Collects configuration and constructs GraphX runtime objects in a predictable order. Builder methods are intended to be chained before final construction.
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
     * @brief Set the JSON source for the initial configuration snapshot
     *
     * @param path Path to JSON graph configuration file
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
/**
 * @brief With json config.
 * @param path Parameter for with json config.
 * @return Result of the operation.
 */
    GraphExecutorBuilder& WithJsonConfig(const std::string& path);

    /// Configure the executor shell from an already atomic coordinator value.
    GraphExecutorBuilder& WithGraphSnapshot(
        const GraphConfigurationSnapshot& snapshot);

    /**
     * @brief Set shared GraphManager instance (optional)
     *
     * Allows injecting a pre-constructed GraphManager for compatibility and
     * tests. If omitted, no manager is constructed by Build(); Init() creates
     * it lazily from the configured snapshot.
     *
     * @param graph_manager Shared pointer to GraphManager
     * @return Reference to this builder (fluent API)   
     */

/**
 * @brief With graph manager.
 * @param graph_manager Parameter for with graph manager.
 * @return Result of the operation.
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
/**
 * @brief With plugin directory.
 * @param directory Parameter for with plugin directory.
 * @return Result of the operation.
 */
    GraphExecutorBuilder& WithPluginDirectory(const std::string& directory);

    /**
     * @brief Add an additional plugin directory path (optional)
     *
     * Use this when a graph needs both common plugins and domain-local plugins,
     * for example libgpu Metal nodes plus example/SAR plugins.
     *
     * @param directory Path to additional directory containing plugin .so files
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
/**
 * @brief With additional plugin directory.
 * @param directory Parameter for with additional plugin directory.
 * @return Result of the operation.
 */
    GraphExecutorBuilder& WithAdditionalPluginDirectory(const std::string& directory);

    /**
     * @brief Add single CSV input for a node
     *
     * @param path Path to CSV file
     * @param node Target node name (optional, use empty for auto-discovery)
     * @return Reference to this builder (fluent API)
     * @throws std::invalid_argument if path is empty
     */
/**
 * @brief With csv input.
 * @param path Parameter for with csv input.
 * @param node Parameter for with csv input.
 * @return Result of the operation.
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
/**
 * @brief With csv inputs.
 * @param std::vector<std::pair<std::string Parameter for with csv inputs.
 * @param inputs Parameter for with csv inputs.
 * @return Result of the operation.
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
/**
 * @brief With csv injection rate.
 * @param rate_ms Parameter for with csv injection rate.
 * @return Result of the operation.
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
/**
 * @brief With executor timeout.
 * @param timeout Parameter for with executor timeout.
 * @return Result of the operation.
 */
    GraphExecutorBuilder& WithExecutorTimeout(const std::chrono::seconds& timeout);
    GraphExecutorBuilder& WithExecutorTimeout(
        const std::chrono::milliseconds& timeout);

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
/**
 * @brief With graph threads.
 * @param count Parameter for with graph threads.
 * @return Result of the operation.
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
/**
 * @brief With cli mode.
 * @param enabled Parameter for with cli mode.
 * @return Result of the operation.
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
/**
 * @brief With verbose logging.
 * @param enabled Parameter for with verbose logging.
 * @return Result of the operation.
 */
    GraphExecutorBuilder& WithVerboseLogging(bool enabled);

    /**
     * @brief Build and return configured executor
     *
     * Validates builder options, records the initial immutable snapshot and
     * lazy runtime inputs, installs stable control capabilities, and returns a
     * CONFIGURED GraphExecutor shell. Provider/plugin loading, GraphBuilder,
     * node creation, and GraphManager construction occur only during Init().
     *
     * @return Configured executor ready for Init()
     * @throws std::invalid_argument if builder options are invalid
     * @throws std::runtime_error if an explicitly supplied JSON snapshot
     *         cannot be read
     * @throws std::logic_error if Build() called more than once
     */
/**
 * @brief Build.
 * @return Result of the operation.
 */
    std::shared_ptr<GraphExecutor> Build();

    /**
     * @brief Build and return configured executor without throwing.
     *
     * Mirrors Build() semantics, including single-use behavior on success, but
     * converts validation and construction failures into GraphExecutionFailure.
     *
     * @return Configured executor, or typed failure with diagnostic message
     */
    [[nodiscard]] std::expected<std::shared_ptr<GraphExecutor>,
                                app::error::GraphExecutionFailure>
    /**
     * @brief Creates or builds the object described by Build Expected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    BuildExpected() noexcept;

private:
    // Configuration state
    std::string json_config_;
    std::optional<GraphConfigurationSnapshot> graph_snapshot_;
    std::shared_ptr<graph::GraphManager> graph_manager_;
    std::string plugin_directory_;
    std::vector<std::string> plugin_directories_;
    std::vector<std::pair<std::string, std::string>> csv_inputs_;
    uint32_t csv_injection_rate_ms_;
    std::chrono::milliseconds executor_timeout_;
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
/**
 * @brief Get default plugin directory.
 * @return Result of the operation.
 */
    static std::string GetDefaultPluginDirectory();

    /**
     * @brief Validate all configuration before building
     *
     * @throws std::invalid_argument if required fields not set
     * @throws std::runtime_error if files/directories don't exist
     */
/**
 * @brief Validate configuration.
 */
    void ValidateConfiguration();
};

}  // namespace graph
