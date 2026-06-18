/**
 * @file GraphConfigParser.hpp
 * @brief Graph Config Parser Graph runtime support.
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

#include "GraphConfig.hpp"
#include <string>
#include <expected>
#include "config/Errors.hpp"

namespace graph::config {

/**
 * @class GraphConfigParser
 * @brief Parse and validate JSON graph configurations
 *
 * Stateless parser that converts JSON text into GraphConfig structures
 * with comprehensive validation.
 */
/**
 * @class GraphConfigParser
 * @brief Graph Config Parser parser.
 *
 * @details Translates external configuration or files into typed GraphX runtime data. Diagnostics should identify invalid inputs without changing runtime state.
 */
class GraphConfigParser {
public:
    /**
     * Parse JSON text into GraphConfig structure
     *
     * @param json_text Raw JSON text containing graph configuration
     * @return Parsed GraphConfig
     *
     * Steps:
     * 1. Parse JSON text into nlohmann::json object
     * 2. Extract graph section
     * 3. Parse nodes array
     * 4. Parse edges array
     * 5. Validate structure
     */
    /**
     * Parse JSON text with type-safe error handling
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
    /**
     * @brief Executes the Parse Safe operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param json_text Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    ParseSafe(const std::string& json_text) noexcept;
    
    /**
     * Parse JSON file with type-safe error handling
     *
     * @param filepath Path to JSON file
     * @return Expected<GraphConfig, ConfigError> - parsed config or error
     *
     * Safe variant using expected<> instead of exceptions.
     */
    [[nodiscard]] static std::expected<GraphConfig, app::error::ConfigError>
    /**
     * @brief Executes the Parse File Safe operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param filepath Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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
/**
 * @brief Validate.
 * @param config Parameter for validate.
 * @return Result of the operation.
 */
    static ValidationResult Validate(const GraphConfig& config);

private:
    /**
     * Parse metadata object from JSON
     */
/**
 * @brief Parse metadata.
 * @param meta_json Parameter for parse metadata.
 * @return Result of the operation.
 */
    static GraphConfig::Metadata ParseMetadata(const nlohmann::json& meta_json);
    
    /**
     * Parse a single node configuration
     */
/**
 * @brief Parse node.
 * @param node_json Parameter for parse node.
 * @return Result of the operation.
 */
    static NodeConfig ParseNode(const nlohmann::json& node_json);

    /**
     * Parse a single node configuration with typed errors
     */
    [[nodiscard]] static std::expected<NodeConfig, app::error::ConfigError>
    /**
     * @brief Executes the Parse Node Safe operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_json Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    ParseNodeSafe(const nlohmann::json& node_json) noexcept;
    
    /**
     * Parse a single edge configuration
     */
/**
 * @brief Parse edge.
 * @param edge_json Parameter for parse edge.
 * @return Result of the operation.
 */
    static EdgeConfig ParseEdge(const nlohmann::json& edge_json);

    /**
     * Parse a single edge configuration with typed errors
     */
    [[nodiscard]] static std::expected<EdgeConfig, app::error::ConfigError>
    /**
     * @brief Executes the Parse Edge Safe operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param edge_json Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    ParseEdgeSafe(const nlohmann::json& edge_json) noexcept;
    
    /**
     * Validate node ID format
     */
/**
 * @brief Is valid node id.
 * @param id Parameter for is valid node id.
 * @return Result of the operation.
 */
    static bool IsValidNodeId(const std::string& id);
    
    /**
     * Validate edge source/target specification
     */
/**
 * @brief Is valid port spec.
 * @param spec Parameter for is valid port spec.
 * @return Result of the operation.
 */
    static bool IsValidPortSpec(const std::string& spec);
};

}  // namespace graph::config
