/**
 * @file NodeResolutionRegistry.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "graph/GraphConfig.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace graph {

struct NodeResolutionVariant {
    std::string backend;
    std::string concrete_type;
};

struct NodeResolutionContract {
    std::string intent_type;
    std::string input_token_type;
    std::string output_token_type;
    std::vector<NodeResolutionVariant> variants;
};

/**
 * @class NodeResolutionRegistry
 * @brief NodeResolutionRegistry class.
 */
class NodeResolutionRegistry {
public:
/**
 * @brief Create default.
 * @return Result of the operation.
 */
    static NodeResolutionRegistry CreateDefault();

/**
 * @brief Add contract.
 * @param contract Parameter for add contract.
 */
    void AddContract(NodeResolutionContract contract);
/**
 * @brief Add mappings.
 * @param mappings Parameter for add mappings.
 */
    void AddMappings(const std::vector<GraphConfig::ResolverMapping>& mappings);

    [[nodiscard]] const NodeResolutionContract* Find(const std::string& intent_type) const;
    [[nodiscard]] std::vector<std::string> IntentTypes() const;

private:
    std::map<std::string, NodeResolutionContract> contracts_;
};

} // namespace graph
