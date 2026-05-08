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
 * @file GraphConfigParser.hpp
 * @brief Parse JSON text/files into GraphConfig structures
 *
 * Provides JSON parsing and schema validation for graph configurations.
 * Phase 1: Core Infrastructure
 *
 * @author Copilot
 * @date 2026-01-04
 */

#pragma once

#include "GraphConfig.hpp"
#include <string>
#include <expected>
#include "graph/app/Errors.hpp"

namespace graph::config {

/**
 * @class GraphConfigParser
 * @brief Parse and validate JSON graph configurations
 *
 * Stateless parser that converts JSON text into GraphConfig structures
 * with comprehensive validation.
 */
class GraphConfigParser {
public:
    /**
     * Parse JSON text into GraphConfig structure
     *
     * @param json_text Raw JSON text containing graph configuration
     * @return Parsed GraphConfig
     *
     * @throws std::runtime_error on JSON parse error
     * @throws std::invalid_argument on missing required fields
     *
     * Steps:
     * 1. Parse JSON text into nlohmann::json object
     * 2. Extract graph section
     * 3. Parse nodes array
     * 4. Parse edges array
     * 5. Validate structure
     */
    static GraphConfig Parse(const std::string& json_text);
    
    /**
     * Parse JSON file into GraphConfig structure
     *
     * @param filepath Path to JSON file
     * @return Parsed GraphConfig
     *
     * @throws std::runtime_error on file I/O error
     * @throws std::runtime_error on JSON parse error
     */
    static GraphConfig ParseFile(const std::string& filepath);

    /**
     * Parse JSON text with type-safe error handling (Phase 5c)
     *
     * @param json_text Raw JSON text containing graph configuration
     * @return Expected<GraphConfig, ConfigError> - parsed config or error code
     *
     * Safe variant using expected<> instead of exceptions.
     *
     * @code
     *   auto result = ParseSafe(json_text);
     *   if (!result) {
     *       LOG_ERROR(error::ErrorMessage(result.error()));
     *       return;
     *   }
     *   GraphConfig cfg = result.value();
     * @endcode
     */
    [[nodiscard]] static std::expected<GraphConfig, app::error::ConfigError>
    ParseSafe(const std::string& json_text) noexcept;
    
    /**
     * Parse JSON file with type-safe error handling (Phase 5c)
     *
     * @param filepath Path to JSON file
     * @return Expected<GraphConfig, ConfigError> - parsed config or error
     *
     * Safe variant using expected<> instead of exceptions.
     */
    [[nodiscard]] static std::expected<GraphConfig, app::error::ConfigError>
    ParseFileSafe(const std::string& filepath) noexcept;
    
    /**
     * Validate a GraphConfig structure
     *
     * Performs multi-phase validation:
     * 1. Schema validation (required fields, types)
     * 2. Semantic validation (node types, port indices)
     * 3. Connectivity validation (edge endpoints exist)
     *
     * @param config Configuration to validate
     * @return ValidationResult with details
     */
    static ValidationResult Validate(const GraphConfig& config);

private:
    /**
     * Parse metadata object from JSON
     */
    static GraphConfig::Metadata ParseMetadata(const nlohmann::json& meta_json);
    
    /**
     * Parse a single node configuration
     */
    static NodeConfig ParseNode(const nlohmann::json& node_json);
    
    /**
     * Parse a single edge configuration
     */
    static EdgeConfig ParseEdge(const nlohmann::json& edge_json);
    
    /**
     * Validate node ID format
     */
    static bool IsValidNodeId(const std::string& id);
    
    /**
     * Validate edge source/target specification
     */
    static bool IsValidPortSpec(const std::string& spec);
};

}  // namespace graph::config

