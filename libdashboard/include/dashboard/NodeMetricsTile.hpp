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

#ifndef GDASHBOARD_NODE_METRICS_TILE_HPP
#define GDASHBOARD_NODE_METRICS_TILE_HPP

#include "ui/Metric.hpp"
#include "app/metrics/NodeMetricsSchema.hpp"
#include "core/VariantHelper.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>

/**
 * @brief Enhanced node metrics tile with schema and event tracking
 * 
 * Represents all metrics for a single node, with schema definitions and
 * event type tracking for real-time updates from MetricsEvent streams.
 */
struct NodeMetricsTile {
    std::string name;                               // Node name (e.g., "EstimationPipelineNode")
    std::string node_type;                          // Node type (e.g., "processor")
    std::vector<Metric> metrics;                    // All metrics for this node
    app::metrics::NodeMetricsSchema schema;                       // Schema definition from node
    std::map<std::string, int> event_type_counts;   // Counts per event type
    std::map<std::string, std::chrono::system_clock::time_point> 
        event_last_update;                          // Last update time per event type
    
    NodeMetricsTile() = default;
    
    explicit NodeMetricsTile(const std::string& n) : name(n) {}
    
    /**
     * @brief Record that an event of given type was processed
     */
    void RecordEvent(const std::string& event_type) {
        event_type_counts[event_type]++;
        event_last_update[event_type] = std::chrono::system_clock::now();
    }
};

/**
 * @brief Convert NodeMetricsSchema to NodeMetricsTile
 * 
 * Creates a NodeMetricsTile from a NodeMetricsSchema, initializing:
 * - Node name and type from schema
 * - Empty metrics array (populated as events arrive)
 * - Schema reference for display hints
 * - Empty event tracking (populated as events are recorded)
 * 
 * @param schema The node metrics schema to convert
 * @return NodeMetricsTile initialized from schema
 * 
 * @example
 * \code
 * app::metrics::NodeMetricsSchema schema;
 * schema.node_name = "EstimationPipelineNode";
 * schema.node_type = "processor";
 * 
 * NodeMetricsTile tile = NodeMetricsSchemaToTile(schema);
 * // Now ready to receive metrics updates
 * \endcode
 */
inline NodeMetricsTile NodeMetricsSchemaToTile(const app::metrics::NodeMetricsSchema& schema) {
    NodeMetricsTile tile;
    tile.name = schema.node_name;
    tile.node_type = schema.node_type;
    tile.schema = schema;
    // metrics array starts empty, will be populated as MetricsEvents arrive
    // event_type_counts and event_last_update start empty
    return tile;
}

/**
 * @brief Convert NodeMetricsSchema to NodeMetricsTile with pre-initialized metrics
 * 
 * Creates a NodeMetricsTile from a NodeMetricsSchema and initializes the metrics
 * vector based on the schema's field definitions. This is useful when you want to
 * display the expected metrics structure before any actual data arrives.
 * 
 * Extracts metric definitions from schema.metrics_schema JSON:
 * - Field "name" → Metric name
 * - Field "type" → MetricType (double/float → FLOAT, int → INT, bool → BOOL, string → STRING)
 * - Field "unit" → Unit string
 * 
 * Each metric is initialized with:
 * - type: parsed from schema
 * - value: default value based on type (0 for numeric, false for bool, "" for string)
 * - unit: from schema
 * - confidence: 1.0
 * - alert_level: 0 (OK)
 * - All other fields: defaults
 * 
 * @param schema The node metrics schema to convert
 * @return NodeMetricsTile with pre-initialized metrics from schema
 * 
 * @example
 * \code
 * app::metrics::NodeMetricsSchema schema;
 * schema.node_name = "AltitudeFusionNode";
 * schema.node_type = "processor";
 * schema.metrics_schema = {
 *   {"fields", json::array({
 *     {{"name", "altitude_m"}, {"type", "double"}, {"unit", "m"}},
 *     {{"name", "velocity_m_s"}, {"type", "double"}, {"unit", "m/s"}},
 *     {{"name", "valid"}, {"type", "bool"}, {"unit", ""}}
 *   })}
 * };
 * 
 * NodeMetricsTile tile = NodeMetricsSchemaToTileWithMetrics(schema);
 * // tile.metrics now contains 3 Metric objects, ready for display
 * \endcode
 */
inline NodeMetricsTile NodeMetricsSchemaToTileWithMetrics(const app::metrics::NodeMetricsSchema& schema) {
    NodeMetricsTile tile = NodeMetricsSchemaToTile(schema);
    
    // Extract metrics from schema JSON
    if (schema.metrics_schema.contains("fields") && schema.metrics_schema["fields"].is_array()) {
        for (const auto& field : schema.metrics_schema["fields"]) {
            if (!field.contains("name")) {
                continue;  // Skip fields without names
            }
            
            std::string metric_name = field["name"].get<std::string>();
            std::string type_str = field.contains("type") ? field["type"].get<std::string>() : "string";
            std::string unit_str = field.contains("unit") ? field["unit"].get<std::string>() : "";
            
            // Determine MetricType from type string
            MetricType metric_type = MetricType::STRING;
            std::variant<int, double, bool, std::string> default_value = "";
            
            if (type_str == "int" || type_str == "integer") {
                metric_type = MetricType::INT;
                default_value = 0;
            } else if (type_str == "double" || type_str == "float" || type_str == "number") {
                metric_type = MetricType::FLOAT;
                default_value = 0.0;
            } else if (type_str == "bool" || type_str == "boolean") {
                metric_type = MetricType::BOOL;
                default_value = false;
            } else {
                metric_type = MetricType::STRING;
                default_value = "";
            }
            
            // Create metric with default values
            Metric metric(
                metric_name,                           // name
                metric_type,                           // type
                default_value,                         // value (default)
                unit_str,                              // unit
                0,                                     // alert_level (OK)
                "",                                    // event_type (empty until first event)
                std::chrono::system_clock::now(),      // last_update
                1.0                                    // confidence
            );
            
            tile.metrics.push_back(metric);
        }
    }
    
    return tile;
}

#endif // GDASHBOARD_NODE_METRICS_TILE_HPP
