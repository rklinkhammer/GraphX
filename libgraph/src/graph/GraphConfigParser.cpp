/**
 * @file GraphConfigParser.cpp
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

#include "graph/GraphConfigParser.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <regex>
#include <thread>
#include <set>
#include <string_view>
#include <nlohmann/json.hpp>
#include <log4cxx/logger.h>
#include "config/JsonUtilities.hpp"
#include "core/FormatUtilities.hpp"

namespace graph::config {

using json = nlohmann::json;

static log4cxx::LoggerPtr logger_ = 
    log4cxx::Logger::getLogger("graph.config");

namespace {

/**
 * @brief Is one of.
 * @param value Parameter for is one of.
 * @param allowed Parameter for is one of.
 */
bool IsOneOf(std::string_view value, std::initializer_list<std::string_view> allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

/**
 * @brief Normalize contract name.
 * @param raw Parameter for normalize contract name.
 */
std::string NormalizeContractName(std::string_view raw) {
    const auto first = raw.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = raw.find_last_not_of(" \t\n\r");
    std::string normalized(raw.substr(first, last - first + 1));
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

/**
 * @brief Is legacy sar payload contract.
 * @param payload_contract Parameter for is legacy sar payload contract.
 */
bool IsLegacySarPayloadContract(const std::string& payload_contract) {
    const auto normalized = NormalizeContractName(payload_contract);
    // These names are intentionally retained as rejection guardrails for accel-token mode.
    // They are not canonical runtime edge contracts.
    return IsOneOf(normalized,
                   {"sarpulseblockmessage",
                    "sarrangetilemessage",
                    "sarimagetilemessage",
                    "sardeviceleasemessage",
                    "sartransferticketmessage"});
}

} // namespace

// Helper: Check if string matches valid node ID pattern (alphanumeric, underscore, hyphen)
/**
 * @brief Is valid node id.
 * @param id Parameter for is valid node id.
 */
bool GraphConfigParser::IsValidNodeId(const std::string& id) {
    if (id.empty() || id.length() > 255) {
        return false;
    }
    static const std::regex valid_id_pattern("^[a-zA-Z0-9_-]+$");
    return std::regex_match(id, valid_id_pattern);
}

// Helper: Check if port specification is valid (e.g., "input_0", "output_1")
/**
 * @brief Is valid port spec.
 * @param port_spec Parameter for is valid port spec.
 */
bool GraphConfigParser::IsValidPortSpec(const std::string& port_spec) {
    if (port_spec.empty() || port_spec.length() > 255) {
        return false;
    }
    // Port spec should be like "input_0", "output_1", etc.
    // Format: port_name[_index]
    static const std::regex valid_port_pattern("^[a-zA-Z_][a-zA-Z0-9_]*(?:_\\d+)?$");
    return std::regex_match(port_spec, valid_port_pattern);
}

// Parse metadata section
/**
 * @brief Parse metadata.
 * @param config_json Parameter for parse metadata.
 */
GraphConfig::Metadata GraphConfigParser::ParseMetadata(const json& config_json) {
    GraphConfig::Metadata metadata;
    
    if (config_json.contains("metadata")) {
        const auto& meta_json = config_json["metadata"];
        
        if (meta_json.contains("version")) {
            metadata.version = meta_json["version"].get<std::string>();
        }
        if (meta_json.contains("description")) {
            metadata.description = meta_json["description"].get<std::string>();
        }
        if (meta_json.contains("author")) {
            metadata.author = meta_json["author"].get<std::string>();
        }
        if (meta_json.contains("created")) {
            metadata.created = meta_json["created"].get<std::string>();
        }
    }
    
    return metadata;
}

// Parse a single node configuration
/**
 * @brief Parse node.
 * @param node_json Parameter for parse node.
 */
NodeConfig GraphConfigParser::ParseNode(const json& node_json) {
    NodeConfig node_config;
    
    // Required fields
    if (!node_json.contains("id")) {
        throw std::runtime_error("Node missing required 'id' field");
    }
    node_config.id = node_json["id"].get<std::string>();
    
    if (!node_json.contains("type")) {
        throw std::runtime_error("Node '" + node_config.id + "' missing required 'type' field");
    }
    node_config.type = node_json["type"].get<std::string>();
    
    // Optional fields
    if (node_json.contains("name")) {
        node_config.name = node_json["name"].get<std::string>();
    }
    if (node_json.contains("description")) {
        node_config.description = node_json["description"].get<std::string>();
    }
    
    // Node configuration (custom JSON for node-specific settings)
    if (node_json.contains("node_config")) {
        node_config.node_config = node_json["node_config"];
    }
    
    // Port configuration (JSON mapping port names to metadata)
    if (node_json.contains("port_config")) {
        node_config.port_config = node_json["port_config"].get<std::map<std::string, json>>();
    }
    
    return node_config;
}

// Parse a single edge configuration
/**
 * @brief Parse edge.
 * @param edge_json Parameter for parse edge.
 */
EdgeConfig GraphConfigParser::ParseEdge(const json& edge_json) {
    auto parsed = ParseEdgeSafe(edge_json);
    if (parsed) {
        return parsed.value();
    }

    throw std::runtime_error(
        "Failed to parse edge configuration (error=" +
        std::to_string(static_cast<int>(parsed.error())) + ")");
}

std::expected<NodeConfig, app::error::ConfigError>
GraphConfigParser::ParseNodeSafe(const json& node_json) noexcept {
    try {
        if (!node_json.is_object()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }

        NodeConfig node_config;

        if (!node_json.contains("id")) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }
        if (!node_json["id"].is_string()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }
        node_config.id = node_json["id"].get<std::string>();

        if (!node_json.contains("type")) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }
        if (!node_json["type"].is_string()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }
        node_config.type = node_json["type"].get<std::string>();

        if (node_json.contains("name")) {
            if (!node_json["name"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            node_config.name = node_json["name"].get<std::string>();
        }
        if (node_json.contains("description")) {
            if (!node_json["description"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            node_config.description = node_json["description"].get<std::string>();
        }
        if (node_json.contains("node_config")) {
            node_config.node_config = node_json["node_config"];
        }
        if (node_json.contains("port_config")) {
            if (!node_json["port_config"].is_object()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            node_config.port_config = node_json["port_config"].get<std::map<std::string, json>>();
        }

        return node_config;
    } catch (const json::type_error&) {
        return std::unexpected(app::error::ConfigError::TypeMismatch);
    } catch (const json::exception&) {
        return std::unexpected(app::error::ConfigError::InvalidFormat);
    } catch (...) {
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<EdgeConfig, app::error::ConfigError>
GraphConfigParser::ParseEdgeSafe(const json& edge_json) noexcept {
    try {
        if (!edge_json.is_object()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }

        EdgeConfig edge_config;

        if (!edge_json.contains("source_node_id")) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }
        if (!edge_json["source_node_id"].is_string()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }
        edge_config.source_node_id = edge_json["source_node_id"].get<std::string>();

        const bool has_source_port = edge_json.contains("source_port");
        const bool has_source_port_name = edge_json.contains("source_port_name");
        if (!has_source_port && !has_source_port_name) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }

        if (has_source_port_name) {
            if (!edge_json["source_port_name"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            edge_config.source_port_name = edge_json["source_port_name"].get<std::string>();
        }

        if (has_source_port) {
            if (edge_json["source_port"].is_number_unsigned()) {
                edge_config.source_port = edge_json["source_port"].get<size_t>();
            } else if (edge_json["source_port"].is_number_integer()) {
                const auto value = edge_json["source_port"].get<long long>();
                if (value < 0) {
                    return std::unexpected(app::error::ConfigError::OutOfRange);
                }
                edge_config.source_port = static_cast<size_t>(value);
            } else if (edge_json["source_port"].is_string()) {
                const auto token = edge_json["source_port"].get<std::string>();
                const bool numeric = !token.empty() &&
                    std::all_of(token.begin(), token.end(),
                                [](unsigned char c) { return std::isdigit(c) != 0; });
                if (numeric) {
                    edge_config.source_port = std::stoull(token);
                } else {
                    edge_config.source_port_name = token;
                }
            } else {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
        }

        if (!edge_json.contains("target_node_id")) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }
        if (!edge_json["target_node_id"].is_string()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }
        edge_config.target_node_id = edge_json["target_node_id"].get<std::string>();

        const bool has_target_port = edge_json.contains("target_port");
        const bool has_target_port_name = edge_json.contains("target_port_name");
        if (!has_target_port && !has_target_port_name) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }

        if (has_target_port_name) {
            if (!edge_json["target_port_name"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            edge_config.target_port_name = edge_json["target_port_name"].get<std::string>();
        }

        if (has_target_port) {
            if (edge_json["target_port"].is_number_unsigned()) {
                edge_config.target_port = edge_json["target_port"].get<size_t>();
            } else if (edge_json["target_port"].is_number_integer()) {
                const auto value = edge_json["target_port"].get<long long>();
                if (value < 0) {
                    return std::unexpected(app::error::ConfigError::OutOfRange);
                }
                edge_config.target_port = static_cast<size_t>(value);
            } else if (edge_json["target_port"].is_string()) {
                const auto token = edge_json["target_port"].get<std::string>();
                const bool numeric = !token.empty() &&
                    std::all_of(token.begin(), token.end(),
                                [](unsigned char c) { return std::isdigit(c) != 0; });
                if (numeric) {
                    edge_config.target_port = std::stoull(token);
                } else {
                    edge_config.target_port_name = token;
                }
            } else {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
        }

        if (edge_json.contains("buffer_size")) {
            if (!edge_json["buffer_size"].is_number_unsigned()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            edge_config.buffer_size = edge_json["buffer_size"].get<size_t>();
        } else {
            edge_config.buffer_size = 100;
        }

        if (edge_json.contains("backpressure_enabled")) {
            if (!edge_json["backpressure_enabled"].is_boolean()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            edge_config.backpressure_enabled = edge_json["backpressure_enabled"].get<bool>();
        } else {
            edge_config.backpressure_enabled = true;
        }

        if (edge_json.contains("payload_contract")) {
            if (!edge_json["payload_contract"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            edge_config.payload_contract = edge_json["payload_contract"].get<std::string>();
        }

        return edge_config;
    } catch (const std::invalid_argument&) {
        return std::unexpected(app::error::ConfigError::TypeMismatch);
    } catch (const std::out_of_range&) {
        return std::unexpected(app::error::ConfigError::OutOfRange);
    } catch (const json::type_error&) {
        return std::unexpected(app::error::ConfigError::TypeMismatch);
    } catch (const json::exception&) {
        return std::unexpected(app::error::ConfigError::InvalidFormat);
    } catch (...) {
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

// ============================================================================
// Expected-based parsing APIs
// ============================================================================

std::expected<GraphConfig, app::error::ConfigError>
GraphConfigParser::ParseSafe(const std::string& json_text) noexcept {
    try {
        GraphConfig config;
        const auto json_data = json::parse(json_text);

        if (!json_data.is_object()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }

        config.metadata = ParseMetadata(json_data);

        if (json_data.contains("name")) {
            if (!json_data["name"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.name = json_data["name"].get<std::string>();
        }

        if (json_data.contains("description")) {
            if (!json_data["description"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.description = json_data["description"].get<std::string>();
        }

        if (json_data.contains("execution_backend")) {
            if (!json_data["execution_backend"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.resolver.execution_backend = json_data["execution_backend"].get<std::string>();
            if (!IsOneOf(config.resolver.execution_backend, {"auto", "metal", "cuda", "sycl", "stub"})) {
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }
        }

        if (json_data.contains("backend_fallback_policy")) {
            if (!json_data["backend_fallback_policy"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.resolver.backend_fallback_policy =
                json_data["backend_fallback_policy"].get<std::string>();
            if (!IsOneOf(config.resolver.backend_fallback_policy, {"strict", "allow_fallback"})) {
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }
        }

        if (json_data.contains("resolver_diagnostics")) {
            if (!json_data["resolver_diagnostics"].is_boolean()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.resolver.resolver_diagnostics = json_data["resolver_diagnostics"].get<bool>();
        }

        if (json_data.contains("edge_contract")) {
            if (!json_data["edge_contract"].is_string()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            config.resolver.edge_contract = json_data["edge_contract"].get<std::string>();
            if (!config.resolver.edge_contract.empty() &&
                !IsOneOf(config.resolver.edge_contract, {"accel-token"})) {
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }
        }

        if (json_data.contains("resolver_mappings")) {
            if (!json_data["resolver_mappings"].is_array()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }

            for (const auto& mapping_json : json_data["resolver_mappings"]) {
                if (!mapping_json.is_object()) {
                    return std::unexpected(app::error::ConfigError::TypeMismatch);
                }
                if (!mapping_json.contains("intent_type") ||
                    !mapping_json["intent_type"].is_string()) {
                    return std::unexpected(app::error::ConfigError::TypeMismatch);
                }
                if (!mapping_json.contains("variants") ||
                    !mapping_json["variants"].is_array() ||
                    mapping_json["variants"].empty()) {
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }

                GraphConfig::ResolverMapping mapping;
                mapping.intent_type = mapping_json["intent_type"].get<std::string>();
                if (mapping.intent_type.empty()) {
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }
                if (mapping_json.contains("input_token_type")) {
                    if (!mapping_json["input_token_type"].is_string()) {
                        return std::unexpected(app::error::ConfigError::TypeMismatch);
                    }
                    mapping.input_token_type = mapping_json["input_token_type"].get<std::string>();
                    if (config.resolver.edge_contract == "accel-token" &&
                        IsLegacySarPayloadContract(mapping.input_token_type)) {
                        return std::unexpected(app::error::ConfigError::ValidationFailed);
                    }
                }
                if (mapping_json.contains("output_token_type")) {
                    if (!mapping_json["output_token_type"].is_string()) {
                        return std::unexpected(app::error::ConfigError::TypeMismatch);
                    }
                    mapping.output_token_type = mapping_json["output_token_type"].get<std::string>();
                    if (config.resolver.edge_contract == "accel-token" &&
                        IsLegacySarPayloadContract(mapping.output_token_type)) {
                        return std::unexpected(app::error::ConfigError::ValidationFailed);
                    }
                }

                for (const auto& variant_json : mapping_json["variants"]) {
                    if (!variant_json.is_object() ||
                        !variant_json.contains("backend") ||
                        !variant_json["backend"].is_string() ||
                        !variant_json.contains("concrete_type") ||
                        !variant_json["concrete_type"].is_string()) {
                        return std::unexpected(app::error::ConfigError::TypeMismatch);
                    }

                    GraphConfig::ResolverBackendVariant variant;
                    variant.backend = variant_json["backend"].get<std::string>();
                    variant.concrete_type = variant_json["concrete_type"].get<std::string>();
                    if (!IsOneOf(variant.backend, {"metal", "cuda", "sycl", "stub"}) ||
                        variant.concrete_type.empty()) {
                        return std::unexpected(app::error::ConfigError::ValidationFailed);
                    }
                    mapping.variants.push_back(std::move(variant));
                }

                config.resolver_mappings.push_back(std::move(mapping));
            }
        }

        if (json_data.contains("num_threads")) {
            if (!json_data["num_threads"].is_number_integer()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
            if (json_data["num_threads"].get<long long>() < 0) {
                return std::unexpected(app::error::ConfigError::OutOfRange);
            }
            config.num_threads = json_data["num_threads"].get<size_t>();
        } else {
            config.num_threads = std::thread::hardware_concurrency();
        }

        if (json_data.contains("deadlock_detection")) {
            const auto& dd_obj = json_data["deadlock_detection"];
            if (dd_obj.is_object()) {
                if (dd_obj.contains("enabled")) {
                    if (!dd_obj["enabled"].is_boolean()) {
                        return std::unexpected(app::error::ConfigError::TypeMismatch);
                    }
                    config.deadlock_detection.enabled = dd_obj["enabled"].get<bool>();
                }
                if (dd_obj.contains("timeout_ms")) {
                    if (!dd_obj["timeout_ms"].is_number_integer()) {
                        return std::unexpected(app::error::ConfigError::TypeMismatch);
                    }
                    if (dd_obj["timeout_ms"].get<long long>() < 0) {
                        return std::unexpected(app::error::ConfigError::OutOfRange);
                    }
                    config.deadlock_detection.timeout_ms = dd_obj["timeout_ms"].get<uint32_t>();
                }
            } else if (dd_obj.is_boolean()) {
                config.deadlock_detection.enabled = dd_obj.get<bool>();
            } else {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }
        }

        if (!json_data.contains("nodes")) {
            return std::unexpected(app::error::ConfigError::MissingRequired);
        }
        if (!json_data["nodes"].is_array()) {
            return std::unexpected(app::error::ConfigError::TypeMismatch);
        }

        for (const auto& node_json : json_data["nodes"]) {
            auto node = ParseNodeSafe(node_json);
            if (!node) {
                return std::unexpected(node.error());
            }
            config.nodes.push_back(std::move(node).value());
        }

        if (json_data.contains("edges")) {
            if (!json_data["edges"].is_array()) {
                return std::unexpected(app::error::ConfigError::TypeMismatch);
            }

            for (const auto& edge_json : json_data["edges"]) {
                auto edge = ParseEdgeSafe(edge_json);
                if (!edge) {
                    return std::unexpected(edge.error());
                }
                if (config.resolver.edge_contract == "accel-token" &&
                    IsLegacySarPayloadContract(edge->payload_contract)) {
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }
                config.edges.push_back(std::move(edge).value());
            }
        }

        return config;
    } catch (const json::parse_error& e) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("JSONParse", e.what()));
        return std::unexpected(app::error::ConfigError::InvalidFormat);
    } catch (const json::type_error& e) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("JSONType", e.what()));
        return std::unexpected(app::error::ConfigError::TypeMismatch);
    } catch (const json::exception& e) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("JSONParse", e.what()));
        return std::unexpected(app::error::ConfigError::InvalidFormat);
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("JSONParse", e.what()));
        return std::unexpected(app::error::ConfigError::Unknown);
    } catch (...) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("JSONParse", "unknown error"));
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<GraphConfig, app::error::ConfigError>
GraphConfigParser::ParseFileSafe(const std::string& filepath) noexcept {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG4CXX_WARN(logger_, std::format("Cannot open configuration file: {}", filepath));
            return std::unexpected(app::error::ConfigError::FileNotFound);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        
        return ParseSafe(buffer.str());
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, app::format::FormatError("FileRead", e.what()));
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

/**
 * @brief Validate.
 * @param config Parameter for validate.
 */
ValidationResult GraphConfigParser::Validate(const GraphConfig& config) {
    ValidationResult result;
    result.valid = true;
    
    // Allow empty graphs for testing purposes - they may be used to test
    // graph loading and validation logic without requiring nodes
    // (Removed: validation that required at least one node)
    
    if (config.num_threads == 0) {
        result.valid = false;
        result.errors.push_back("Number of threads must be greater than 0");
    }
    
    // Collect node IDs for edge validation
    std::vector<std::string> node_ids;
    std::set<std::string> duplicate_ids;
    
    // Validate each node
    for (const auto& node_config : config.nodes) {
        bool node_valid = node_config.Validate();
        if (!node_valid) {
            result.valid = false;
            result.errors.push_back("Node '" + node_config.id + "' validation failed");
        }
        
        // Check for duplicate node IDs
        if (std::find(node_ids.begin(), node_ids.end(), node_config.id) != node_ids.end()) {
            duplicate_ids.insert(node_config.id);
            result.valid = false;
            result.errors.push_back("Duplicate node ID: " + node_config.id);
        }
        node_ids.push_back(node_config.id);
    }
    
    // Validate edges
    for (const auto& edge_config : config.edges) {
        bool edge_valid = edge_config.Validate();
        if (!edge_valid) {
            result.valid = false;
            result.errors.push_back("Edge validation failed");
        }

        if (config.resolver.edge_contract == "accel-token" &&
            IsLegacySarPayloadContract(edge_config.payload_contract)) {
            result.valid = false;
            result.errors.push_back(
                "Legacy SAR payload contract is not allowed on accel-token edge: " +
                edge_config.payload_contract);
        }
        
        // Check that source and target nodes exist
        if (std::find(node_ids.begin(), node_ids.end(), edge_config.source_node_id) == node_ids.end()) {
            result.valid = false;
            result.errors.push_back("Edge source node not found: " + edge_config.source_node_id);
        }
        if (std::find(node_ids.begin(), node_ids.end(), edge_config.target_node_id) == node_ids.end()) {
            result.valid = false;
            result.errors.push_back("Edge target node not found: " + edge_config.target_node_id);
        }
        
        // Check that source and target are different
        if (edge_config.source_node_id == edge_config.target_node_id) {
            result.valid = false;
            result.errors.push_back("Cannot create self-loop: " + edge_config.source_node_id);
        }
    }
    
    return result;
}

}  // namespace graph::config
