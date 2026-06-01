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
#include "graph/NodeFactory.hpp"
#include "config/JsonUtilities.hpp"
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

}  // namespace

std::expected<std::vector<std::shared_ptr<NodeFacadeAdapter>>, app::error::ConfigError>
JsonDynamicGraphLoader::LoadNodesSafe(
    const std::string& filepath,
    std::shared_ptr<NodeFactory> factory) noexcept {
    
    LOG4CXX_TRACE(logger_, "Loading nodes (safe): " << filepath);

    try {
        if (!factory) {
            LOG4CXX_ERROR(logger_, "NodeFactory is null");
            return std::unexpected(app::error::ConfigError::ValidationFailed);
        }

        auto config = ParseConfigFileSafe(filepath);
        if (!config) {
            return std::unexpected(config.error());
        }

        auto validation = ValidateConfigSafe(config.value());
        if (!validation) {
            return std::unexpected(validation.error());
        }

        std::vector<std::shared_ptr<NodeFacadeAdapter>> nodes;
        nodes.reserve(config->nodes.size());

        for (const auto& node_config : config->nodes) {
            auto node = factory->CreateDynamicNodeExpected(node_config.type);
            if (!node) {
                LOG4CXX_ERROR(logger_, "Failed to create node '" << node_config.id
                              << "' (type: " << node_config.type << ")");
                return std::unexpected(app::error::ConfigError::ValidationFailed);
            }

            try {
                auto adapter = std::make_shared<NodeFacadeAdapter>(std::move(node).value());
                if (!node_config.node_config.empty()) {
                    ApplyNodeConfiguration(adapter, node_config.node_config);
                }
                nodes.push_back(std::move(adapter));
            } catch (...) {
                LOG4CXX_ERROR(logger_, "Failed to allocate node adapter for '"
                              << node_config.id << "' (type: " << node_config.type << ")");
                return std::unexpected(app::error::ConfigError::Unknown);
            }
        }

        LOG4CXX_TRACE(logger_, "Successfully loaded " << nodes.size() << " nodes");
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
    std::shared_ptr<NodeFactory> factory) noexcept {
    
    LOG4CXX_TRACE(logger_, "Loading graph (safe): " << filepath);
    
    try {
        auto nodes = LoadNodesSafe(filepath, std::move(factory));
        if (!nodes) {
            return std::unexpected(nodes.error());
        }

        auto edges = LoadEdgesSafe(filepath);
        if (!edges) {
            return std::unexpected(edges.error());
        }

        LOG4CXX_TRACE(logger_, "Successfully loaded graph: " << nodes->size() 
                     << " nodes, " << edges->size() << " edges");
        return std::make_pair(std::move(nodes).value(), std::move(edges).value());
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

void JsonDynamicGraphLoader::ApplyNodeConfiguration(
    [[maybe_unused]] std::shared_ptr<NodeFacadeAdapter> adapter,
    const nlohmann::json& config_json) {
    
    if (config_json.empty()) {
        LOG4CXX_TRACE(logger_, "Configuration is empty, skipping");
        return;
    }
    
    try {
        // Note: Configuration via SetConfigurationJSON has been removed
        // Nodes should be initialized with their configuration at creation time
    } catch (const std::exception& e) {
        LOG4CXX_WARN(logger_, "Failed to apply node configuration: " << e.what());
    }
}

}  // namespace graph::config
