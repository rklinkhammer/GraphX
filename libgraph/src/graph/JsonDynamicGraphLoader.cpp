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
 * @file JsonDynamicGraphLoader.cpp
 * @brief Implementation of JSON graph loader for NodeFacadeAdapter-based graphs
 */

#include "graph/JsonDynamicGraphLoader.hpp"
#include "graph/GraphConfigParser.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NodeMetadataService.hpp"
#include "graph/NodeProvider.hpp"
#include "config/SchemaGenerator.hpp"
#include "config/JsonUtilities.hpp"
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <format>
#include <log4cxx/logger.h>

namespace graph::config {

static log4cxx::LoggerPtr logger_ = 
    log4cxx::Logger::getLogger("graph.config.JsonDynamicGraphLoader");

namespace {

std::expected<void, app::error::ConfigError> ValidateConfigSafe(
    const GraphConfig& config) noexcept {
    
    ValidationResult validation = GraphConfigParser::Validate(config);
    if (validation.valid) {
        return {};
    }

    std::string error_msg = "Configuration validation failed: ";
    for (const auto& error : validation.errors) {
        error_msg += error + "; ";
    }
    LOG4CXX_ERROR(logger_, error_msg);
    return std::unexpected(app::error::ConfigError::ValidationFailed);
}

std::expected<void, app::error::ConfigError> ValidatePortConfigAgainstDescriptorSchema(
    const NodeConfig& node_config,
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const INodeDescriptorSchemaProvider& descriptor_schema_provider) noexcept {

    if (node_config.port_config.empty()) {
        return {};
    }

    try {
        const auto descriptor_schema = descriptor_schema_provider.BuildSchema(adapter->GetDescriptor());
        std::unordered_set<std::string> valid_ports;

        if (descriptor_schema.contains("inputs") && descriptor_schema["inputs"].is_array()) {
            for (const auto& input_port : descriptor_schema["inputs"]) {
                if (input_port.contains("name") && input_port["name"].is_string()) {
                    valid_ports.insert(input_port["name"].get<std::string>());
                }
            }
        }

        if (descriptor_schema.contains("outputs") && descriptor_schema["outputs"].is_array()) {
            for (const auto& output_port : descriptor_schema["outputs"]) {
                if (output_port.contains("name") && output_port["name"].is_string()) {
                    valid_ports.insert(output_port["name"].get<std::string>());
                }
            }
        }

        for (const auto& [port_name, _] : node_config.port_config) {
            if (!valid_ports.contains(port_name)) {
                LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                              << "' has invalid port_config key '" << port_name
                              << "' for type '" << node_config.type << "'");
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }
        }

        return {};
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Descriptor schema validation failed for node '"
                      << node_config.id << "': " << e.what());
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Descriptor schema validation failed for node '"
                      << node_config.id << "': unknown error");
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    }
}

std::expected<void, app::error::ConfigError> ValidateNodeConfigAgainstDescriptorSchema(
    const NodeConfig& node_config,
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const INodeDescriptorSchemaProvider& descriptor_schema_provider) noexcept {
    try {
        const auto descriptor_schema = descriptor_schema_provider.BuildSchema(adapter->GetDescriptor());
        const bool has_config_fields =
            descriptor_schema.contains("config_fields") &&
            descriptor_schema["config_fields"].is_array() &&
            !descriptor_schema["config_fields"].empty();
        const bool supports_configuration =
            descriptor_schema.contains("supports_configuration") &&
            descriptor_schema["supports_configuration"].is_boolean() &&
            descriptor_schema["supports_configuration"].get<bool>();

        if (node_config.node_config.is_null()) {
            if (!has_config_fields) {
                return {};
            }

            for (const auto& field : descriptor_schema["config_fields"]) {
                const bool required =
                    field.contains("required") && field["required"].is_boolean() &&
                    field["required"].get<bool>();
                if (!required) {
                    continue;
                }

                const std::string required_name =
                    field.contains("name") && field["name"].is_string()
                        ? field["name"].get<std::string>()
                        : "<unknown>";
                LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                              << "' missing required node_config field '"
                              << required_name << "'");
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }

            return {};
        }

        if (!supports_configuration && !has_config_fields) {
            LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                          << "' (type: " << node_config.type
                          << ") does not support node_config");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        if (!node_config.node_config.is_object()) {
            LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                          << "' has invalid node_config: expected JSON object");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        // Strict field-level validation is applied when config_fields is non-empty.
        // For configurable nodes that publish an empty config_fields list, only
        // an empty object is accepted; non-empty objects are rejected as unknown
        // node_config keys.
        if (supports_configuration && !has_config_fields) {
            if (!node_config.node_config.empty()) {
                LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                              << "' has unknown node_config fields: descriptor declares no config_fields");
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }
            return {};
        }

        if (descriptor_schema.contains("config_fields") &&
            descriptor_schema["config_fields"].is_array() &&
            !descriptor_schema["config_fields"].empty()) {

            std::unordered_map<std::string, std::pair<std::string, bool>> expected_fields;
            for (const auto& field : descriptor_schema["config_fields"]) {
                if (!field.contains("name") || !field["name"].is_string()) {
                    continue;
                }

                const std::string field_name = field["name"].get<std::string>();
                const std::string field_type =
                    field.contains("type") && field["type"].is_string()
                        ? field["type"].get<std::string>()
                        : "object";
                const bool required =
                    field.contains("required") && field["required"].is_boolean() &&
                    field["required"].get<bool>();

                expected_fields[field_name] = std::make_pair(field_type, required);
            }

            for (const auto& [field_name, field_info] : expected_fields) {
                if (field_info.second && !node_config.node_config.contains(field_name)) {
                    LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                                  << "' missing required node_config field '"
                                  << field_name << "'");
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }
            }

            const auto matches_type = [](const nlohmann::json& value,
                                         const std::string& expected_type) {
                if (expected_type == "string") {
                    return value.is_string();
                }
                if (expected_type == "number") {
                    return value.is_number();
                }
                if (expected_type == "integer") {
                    return value.is_number_integer() || value.is_number_unsigned();
                }
                if (expected_type == "boolean") {
                    return value.is_boolean();
                }
                if (expected_type == "array") {
                    return value.is_array();
                }
                if (expected_type == "object") {
                    return value.is_object();
                }
                return true;
            };

            for (const auto& [field_name, field_value] : node_config.node_config.items()) {
                auto it = expected_fields.find(field_name);
                if (it == expected_fields.end()) {
                    LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                                  << "' has unknown node_config field '"
                                  << field_name << "'");
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }

                if (!matches_type(field_value, it->second.first)) {
                    LOG4CXX_ERROR(logger_, "Node '" << node_config.id
                                  << "' has invalid type for node_config field '"
                                  << field_name << "' (expected " << it->second.first << ")");
                    return std::unexpected(app::error::ConfigError::ValidationFailed);
                }
            }
        }

        return {};
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Descriptor schema validation failed for node_config on node '"
                      << node_config.id << "': " << e.what());
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Descriptor schema validation failed for node_config on node '"
                      << node_config.id << "': unknown error");
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    }
}

std::expected<void, app::error::ConfigError> InternalApplyNodeConfiguration(
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const nlohmann::json& config_json) noexcept {

    if (config_json.empty()) {
        LOG4CXX_TRACE(logger_, "Configuration is empty, skipping");
        return {};
    }

    try {
        auto configurable_ptr = adapter ? adapter->GetConfigurablePtr() : nullptr;
        if (!configurable_ptr) {
            LOG4CXX_ERROR(logger_, "Node has node_config but does not expose IConfigurable");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        auto* configurable = static_cast<IConfigurable*>(configurable_ptr.get());
        if (!configurable) {
            LOG4CXX_ERROR(logger_, "Failed to access IConfigurable interface for node_config application");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        configurable->Configure(JsonView(config_json));
        return {};
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to apply node configuration: " << e.what());
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    }

    LOG4CXX_ERROR(logger_, "Failed to apply node configuration: unknown error");
    return std::unexpected(app::error::ConfigError::Unknown);
}

std::expected<void, app::error::ConfigError> InternalApplyPortConfiguration(
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const std::map<std::string, nlohmann::json>& port_config) noexcept {

    if (port_config.empty()) {
        return {};
    }

    const auto node_name = adapter ? adapter->GetName() : std::string("<unknown>");
    LOG4CXX_ERROR(logger_, "port_config is currently not executable at runtime for node '"
                  << node_name << "'; remove port_config or implement runtime port configuration support");
    return std::unexpected(app::error::ConfigError::ValidationFailed);
}

std::expected<std::vector<std::shared_ptr<NodeFacadeAdapter>>, app::error::ConfigError>
BuildNodeAdaptersFromConfig(
    const GraphConfig& config,
    std::shared_ptr<INodeProvider> node_provider,
    const INodeMetadataService* metadata_service) noexcept {

    if (!node_provider) {
        LOG4CXX_ERROR(logger_, "INodeProvider is null");
        return std::unexpected(app::error::ConfigError::ValidationFailed);
    }

    std::vector<std::shared_ptr<NodeFacadeAdapter>> nodes;
    nodes.reserve(config.nodes.size());

    const INodeMetadataService& active_metadata_service =
        metadata_service ? *metadata_service : GetDefaultNodeMetadataService();
    const INodeDescriptorSchemaProvider& active_descriptor_schema_provider =
        active_metadata_service.DescriptorSchemaProvider();

    for (const auto& node_config : config.nodes) {
        auto node = node_provider->CreateNodeExpected(node_config.type);
        if (!node) {
            LOG4CXX_ERROR(logger_, "Failed to create node '" << node_config.id
                          << "' (type: " << node_config.type << ")");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        try {
            auto created_adapter = std::move(node).value();
            auto adapter = std::make_shared<NodeFacadeAdapter>(std::move(created_adapter));
            adapter->SetMetadataService(&active_metadata_service);
            const std::string node_name = !node_config.name.empty()
                ? node_config.name
                : node_config.id;
            if (!node_name.empty()) {
                adapter->SetName(node_name);
            }

            auto descriptor_validation =
                ValidatePortConfigAgainstDescriptorSchema(
                    node_config,
                    adapter,
                    active_descriptor_schema_provider);
            if (!descriptor_validation) {
                return std::unexpected(descriptor_validation.error());
            }

            auto node_config_validation =
                ValidateNodeConfigAgainstDescriptorSchema(
                    node_config,
                    adapter,
                    active_descriptor_schema_provider);
            if (!node_config_validation) {
                return std::unexpected(node_config_validation.error());
            }

            auto apply_node_config = InternalApplyNodeConfiguration(
                adapter,
                node_config.node_config);
            if (!apply_node_config) {
                return std::unexpected(apply_node_config.error());
            }

            auto apply_port_config = InternalApplyPortConfiguration(
                adapter,
                node_config.port_config);
            if (!apply_port_config) {
                return std::unexpected(apply_port_config.error());
            }

            nodes.push_back(std::move(adapter));
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Failed to allocate node adapter for '"
                          << node_config.id << "' (type: " << node_config.type << ")");
            return std::unexpected(app::error::ConfigError::Unknown);
        }
    }

    return nodes;
}

}  // namespace

std::expected<std::vector<std::shared_ptr<NodeFacadeAdapter>>, app::error::ConfigError>
JsonDynamicGraphLoader::LoadNodesSafe(
    const std::string& filepath,
    std::shared_ptr<INodeProvider> node_provider,
    const INodeMetadataService* metadata_service) noexcept {
    
    LOG4CXX_TRACE(logger_, "Loading nodes (safe): " << filepath);

    try {
        auto config = ParseConfigFileSafe(filepath);
        if (!config) {
            return std::unexpected(config.error());
        }

        auto validation = ValidateConfigSafe(config.value());
        if (!validation) {
            return std::unexpected(validation.error());
        }

        auto nodes = BuildNodeAdaptersFromConfig(
            config.value(),
            std::move(node_provider),
            metadata_service);
        if (!nodes) {
            return std::unexpected(nodes.error());
        }

        LOG4CXX_TRACE(logger_, "Successfully loaded " << nodes->size() << " nodes");
        return nodes;
    } catch (const std::exception& e) {
        std::string error_msg = std::format("Failed to load nodes from '{}': {}",
                                           filepath, e.what());
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(app::error::ConfigError::Unknown);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to load nodes from '" << filepath << "': unknown error");
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<std::vector<EdgeConfig>, app::error::ConfigError>
JsonDynamicGraphLoader::LoadEdgesSafe(
    const std::string& filepath) noexcept {
    
    LOG4CXX_TRACE(logger_, "Loading edges (safe): " << filepath);
    
    try {
        auto config = ParseConfigFileSafe(filepath);
        if (!config) {
            return std::unexpected(config.error());
        }

        auto validation = ValidateConfigSafe(config.value());
        if (!validation) {
            return std::unexpected(validation.error());
        }

        LOG4CXX_TRACE(logger_, "Successfully loaded " << config->edges.size() << " edges");
        return config->edges;
    } catch (const std::exception& e) {
        std::string error_msg = std::format("Failed to load edges from '{}': {}",
                                           filepath, e.what());
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(app::error::ConfigError::Unknown);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to load edges from '" << filepath << "': unknown error");
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<std::pair<std::vector<std::shared_ptr<NodeFacadeAdapter>>, std::vector<EdgeConfig>>,
              app::error::ConfigError>
JsonDynamicGraphLoader::LoadGraphSafe(
    const std::string& filepath,
    std::shared_ptr<INodeProvider> node_provider,
    const INodeMetadataService* metadata_service) noexcept {
    
    LOG4CXX_TRACE(logger_, "Loading graph (safe): " << filepath);
    
    try {
        auto config = ParseConfigFileSafe(filepath);
        if (!config) {
            return std::unexpected(config.error());
        }

        auto validation = ValidateConfigSafe(config.value());
        if (!validation) {
            return std::unexpected(validation.error());
        }

        auto nodes = BuildNodeAdaptersFromConfig(
            config.value(),
            std::move(node_provider),
            metadata_service);
        if (!nodes) {
            return std::unexpected(nodes.error());
        }

        auto edges = config->edges;

        LOG4CXX_TRACE(logger_, "Successfully loaded graph: " << nodes->size()
                     << " nodes, " << edges.size() << " edges");
        return std::make_pair(std::move(nodes).value(), std::move(edges));
    } catch (const std::exception& e) {
        std::string error_msg = std::format("Failed to load graph from '{}': {}",
                                           filepath, e.what());
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(app::error::ConfigError::Unknown);
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to load graph from '" << filepath << "': unknown error");
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<GraphConfig, app::error::ConfigError>
JsonDynamicGraphLoader::ParseConfigFileSafe(
    const std::string& filepath) noexcept {
    
    LOG4CXX_TRACE(logger_, "Parsing JSON file (safe): " << filepath);
    
    try {
        return GraphConfigParser::ParseFileSafe(filepath);
    } catch (const std::exception& e) {
        std::string error_msg = std::format("Unexpected error parsing file '{}': {}",
                                           filepath, e.what());
        LOG4CXX_ERROR(logger_, error_msg);
        return std::unexpected(app::error::ConfigError::Unknown);
    }
}

std::expected<void, app::error::ConfigError> JsonDynamicGraphLoader::ApplyNodeConfiguration(
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const nlohmann::json& config_json) noexcept {
    return InternalApplyNodeConfiguration(adapter, config_json);
}

std::expected<void, app::error::ConfigError> JsonDynamicGraphLoader::ApplyPortConfiguration(
    const std::shared_ptr<NodeFacadeAdapter>& adapter,
    const std::map<std::string, nlohmann::json>& port_config) noexcept {
    return InternalApplyPortConfiguration(adapter, port_config);
}

}  // namespace graph::config
