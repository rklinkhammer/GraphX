/**
 * @file NodeResolutionRegistry.hpp
 * @brief Node Resolution Registry Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

/**

 * @struct NodeResolutionVariant

 * @brief Node Resolution Variant data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct NodeResolutionVariant {
    ResolverBackend backend{ResolverBackend::Unknown};
    std::string concrete_type;
};

/**

 * @struct NodeResolutionContract

 * @brief Node Resolution Contract data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct NodeResolutionContract {
    std::string intent_type;
    std::string input_token_type;
    std::string output_token_type;
    std::vector<NodeResolutionVariant> variants;
};

/**
 * @class NodeResolutionRegistry
 * @brief Node Resolution Registry graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
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

    /**
     * @brief Executes the Find operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param intent_type Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] const NodeResolutionContract* Find(const std::string& intent_type) const;
    /**
     * @brief Executes the Intent Types operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::vector<std::string> IntentTypes() const;

private:
    std::map<std::string, NodeResolutionContract> contracts_;
};

} // namespace graph
