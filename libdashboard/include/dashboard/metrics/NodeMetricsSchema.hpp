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

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>


/**
 * @file MetricsSchemaDiscovery.hpp
 * @brief Dynamic discovery of node metrics capabilities via JSON schema
 *
 * MetricsSchemaDiscovery enables the dashboard to automatically discover
 * all nodes in the execution pipeline and their available metrics, without
 * requiring any hardcoded knowledge of node types.
 *
 * When a new node is added to the pipeline, it automatically appears in
 * the dashboard with appropriate panel rendering - no code changes needed.
 *
 * @section usage Usage
 *
 * \code
 * MetricsSchemaDiscovery discovery;
 * auto schemas = discovery.DiscoverAllNodes(executor);
 *
 * for (const auto& schema : schemas) {
 *     std::cout << "Node: " << schema.node_name << "\n";
 *     std::cout << "Type: " << schema.node_type << "\n";
 *     std::cout << "Metrics: " << schema.metrics_schema.dump() << "\n";
 * }
 * \endcode
 */
namespace app::metrics {

using json = nlohmann::json;

/**
 * @brief Schema description of a single node's metrics capabilities
 *
 * Returned by discovery process to describe what metrics a node publishes
 * and how they should be rendered.
 */
struct NodeMetricsSchema {
    /**
     * @brief Name of the node instance (e.g., "EstimationPipelineNode")
     */
    std::string node_name;

    /**
     * @brief Type classification (e.g., "processor", "sensor", "analyzer")
     */
    std::string node_type;

    /**
     * @brief JSON schema describing available metrics
     *
     * Expected structure:
     * \code
     * {
     *   "fields": [
     *     {
     *       "name": "metric_name",
     *       "type": "double|int|bool|string",
     *       "unit": "m|m/s|percent|°C|..."
     *     },
     *     ...
     *   ]
     * }
     * \endcode
     */
    json metrics_schema;

    /**
     * @brief List of async event types this node can emit
     *
     * Examples: ["estimation_update", "altitude_fusion_quality"]
     */
    std::vector<std::string> event_types;

    /**
     * @brief Optional: node-specific display hints from config
     */
    json display_hints;

    /**
     * @brief C++20 Designated Initializer Support (Phase 1 C++26 Modernization)
     *
     * Structured bindings and designated initializers improve code clarity:
     *
     * **Before (traditional):**
     * \code
     * NodeMetricsSchema schema;
     * schema.node_name = "EstimationNode";
     * schema.node_type = "processor";
     * schema.metrics_schema = json::object();
     * schema.event_types = {"estimation_update"};
     * schema.display_hints = json::object();
     * \endcode
     *
     * **After (designated initializer - C++20+):**
     * \code
     * auto schema = NodeMetricsSchema{
     *     .node_name = "EstimationNode",
     *     .node_type = "processor",
     *     .metrics_schema = json::object(),
     *     .event_types = {"estimation_update"},
     *     .display_hints = json::object()
     * };
     * \endcode
     *
     * **Benefits:**
     * - More concise and readable
     * - Self-documenting (field names visible at use site)
     * - Uninitialized fields get default values automatically
     * - Compiler enforces all required fields in order
     */
};

void PrintNodeMetricsSchema(const app::metrics::NodeMetricsSchema& schema, bool verbose = false);

}  // namespace app::metrics