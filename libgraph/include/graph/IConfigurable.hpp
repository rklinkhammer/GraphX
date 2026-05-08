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

#include <cstddef>
#include <vector>
#include <string>
#include "config/JsonView.hpp"

namespace graph {

/**
 * Interface for nodes that support JSON configuration.
 * 
 * Nodes that implement this interface can be configured
 * via JSON at runtime, supporting both compile-time and
 * dynamically-loaded node types.
 * 
 * Usage:
 * @code
 * // Node declaration
 * class MyNode : public INode, public IConfigurable {
 * public:
 *     void Configure(const JsonView& cfg) override;
 * };
 * 
 * // Node implementation
 * void MyNode::Configure(const JsonView& cfg) {
 *     auto config = ConfigParser<MyNodeConfig>::Parse(cfg);
 *     // Apply config to node state
 * }
 * 
 * // Usage
 * auto node = factory.CreateNode("MyNode");
 * if (auto* configurable = dynamic_cast<IConfigurable*>(node.get())) {
 *     configurable->Configure(json_view);
 * }
 * @endcode
 */
struct IConfigurable {
    virtual ~IConfigurable() = default;
    
    /**
     * Configure this node from JSON.
     * 
     * The implementation should:
     * 1. Parse the JSON into a config struct
     * 2. Validate the config (usually done by parser)
     * 3. Apply the config to the node
     * 4. Throw ConfigError if configuration fails
     * 
     * @param cfg JSON view containing configuration
     * @throws ConfigError if configuration invalid
     */
    virtual void Configure(const JsonView& cfg) = 0;
};

/**
 * Interface for nodes that expose runtime diagnostics and metrics.
 * 
 * Nodes implementing this interface can expose performance metrics,
 * internal state information, and diagnostic data for monitoring,
 * debugging, and analysis. This is particularly important for
 * plugin-based deployments where direct code introspection is not possible.
 * 
 * Usage:
 * @code
 * auto node = CreateNode("AltitudeFusionNode");
 * if (auto* diagnosable = dynamic_cast<IDiagnosable*>(node.get())) {
 *     auto metrics = diagnosable->GetDiagnostics();
 *     // Process metrics...
 * }
 * @endcode
 */
struct IDiagnosable {
    virtual ~IDiagnosable() = default;
    
    /**
     * @brief Get node diagnostics and runtime metrics
     * 
     * Returns a JSON object containing:
     * - Performance metrics (latencies, throughputs, queue sizes)
     * - Thread state information
     * - Custom node-specific diagnostics
     * - Timestamp of diagnostic collection
     * 
     * Should be callable at any time during node execution and must
     * not block the worker threads.
     * 
     * @return config::JsonView containing diagnostic information
     */
    virtual JsonView GetDiagnostics() const = 0;
};

/**
 * Interface for nodes that expose parameter queries and runtime introspection.
 * 
 * Nodes implementing this interface can expose their internal parameters,
 * configuration state, and capability information. This enables dynamic
 * discovery and validation of node configuration.
 * 
 * Usage:
 * @code
 * auto node = CreateNode("ExtendedKalmanFilterNode");
 * if (auto* parameterized = dynamic_cast<IParameterized*>(node.get())) {
 *     auto params = parameterized->GetParameters();
 *     auto desc = parameterized->GetParameterDescription("process_noise");
 * }
 * @endcode
 */
struct IParameterized {
    virtual ~IParameterized() = default;
    
    /**
     * @brief Get all configurable parameters and their current values
     * 
     * Returns a JSON object mapping parameter names to their current values.
     * This enables querying the current configuration state without access
     * to internal implementation details.
     * 
     * Example return format:
     * @code
     * {
     *     "process_noise": 0.001,
     *     "measurement_noise": 0.01,
     *     "enabled": true
     * }
     * @endcode
     * 
     * @return JsonView with parameter names and values
     */
    virtual JsonView GetParameters() const = 0;
    
    /**
     * @brief Get parameter metadata for a specific parameter
     * 
     * Returns description, type, valid range, and other metadata
     * for introspection and validation.
     * 
     * Should return empty JsonView if parameter not found.
     * 
     * @param param_name Name of parameter to describe
     * @return JsonView with parameter metadata
     */
    virtual JsonView GetParameterDescription(const std::string& param_name) const = 0;
    
    /**
     * @brief Get list of all available parameter names
     * 
     * @return Vector of parameter names that can be queried or configured
     */
    virtual std::vector<std::string> GetParameterNames() const = 0;
};

}  // namespace graph
