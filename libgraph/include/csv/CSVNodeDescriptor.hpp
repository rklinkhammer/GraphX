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
#include <memory>
#include <functional>
#include <optional>
#include <vector>

#include "core/ActiveQueue.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "graph/IDataInjectionSource.hpp"

namespace graph {
class INode;
}

namespace app {

/**
 * @brief Descriptor for a CSV-capable node discovered in the graph
 *
 * CSVNodeDescriptor encapsulates metadata and type-erased access closures
 * for a node that can consume CSV data. The closures allow CSVDataStreamManager
 * to inject data without needing friend class declarations or private member
 * access.
 *
 * Design Pattern:
 * - Metadata: type_identifier, node_name, node pointer, data_injection_config
 * - Closures: get_queue, parse_row, validate_row
 * - Closures capture node references at discovery time
 * - No modifications to CSV node classes needed
 *
 * Example Usage:
 * ```cpp
 * // Discovery creates descriptor with closures
 * auto descriptor = CSVNodeDescriptor{
 *     .type_identifier = "accel",
 *     .node_name = "IMU_Accel_1",
 *     .node = shared_ptr_to_node,
 *     .data_injection_config = config,
 *     .get_queue = [node_ptr]() { return node_ptr->GetCSVQueue(); },
 *     .parse_row = [](const auto& row, const auto& cfg) {
 *         return csv::ParseAccelerometerRow(row, cfg);
 *     },
 *     .validate_row = [](const auto& row) { return ValidateCSVRow(row); }
 * };
 *
 * // Injection uses closures to access queue and parse data
 * auto& queue = descriptor.get_queue();  // Type-erased access
 * auto payload = descriptor.parse_row(csv_row, *descriptor.data_injection_config);
 * if (payload) {
 *     queue.Enqueue(*payload);
 * }
 * ```
 */
struct CSVNodeDescriptor {
    // ========== Metadata ==========

    /// Sensor type identifier: "accel", "gyro", "mag", "baro", "gps"
    std::string type_identifier;

    /// Human-readable node name for debugging/logging
    std::string node_name;

    /// Shared pointer to the actual node in the graph
    /// Stored for reference and to keep node alive during injection
    std::shared_ptr<graph::nodes::INode> node;

    /// CSV configuration (column indices, format, etc.)
    /// Shared pointer for shared ownership of config
    std::shared_ptr<const graph::datasources::IDataInjectionSource> data_injection_config;

    // ========== Type-Erased Closures ==========

    /**
     * @brief Closure to get reference to node's CSV queue
     *
     * Called during injection to get queue reference without friend class.
     * Captures node pointer at discovery time.
     *
     * Type-erased signature allows dispatch through map of descriptors.
     * Actual return type is ActiveQueue<SensorPayload>&.
     *
     * @return Reference to the node's CSV input queue
     * @throws std::exception if queue access fails
     */
    std::function<core::ActiveQueue<sensors::SensorPayload>&()> get_queue;

    /**
     * @brief Closure to parse CSV row to sensor payload
     *
     * Dispatches to sensor-specific parser based on type_identifier.
     * Captures sensor type information at discovery time.
     *
     * Signatures:
     * - Input: vector of CSV column values, CSV configuration
     * - Output: Optional<SensorPayload> (nullopt if parse failed)
     *
     * @param row_values CSV column values from this row
     * @param config CSV configuration (columns, indices, etc.)
     * @return Parsed SensorPayload, or nullopt if parse failed
     */
    std::function<std::optional<sensors::SensorPayload>(
        const std::vector<std::string>& row_values,
        const graph::IDataInjectionSource& config)> parse_row;

    /**
     * @brief Optional closure to validate CSV row
     *
     * Checks if row has required non-empty columns.
     * Called before parse_row to fail fast on malformed rows.
     *
     * @param row_values CSV column values from this row
     * @return true if row is valid, false otherwise
     */
    std::function<bool(const std::vector<std::string>& row_values)> validate_row;
};

}  // namespace app
