/**
 * @file ResolvingNodeProvider.hpp
 * @brief Resolving Node Provider Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "graph/GraphConfig.hpp"
#include "graph/NodeResolutionRegistry.hpp"
#include "graph/NodeProvider.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace graph {

/**

 * @struct NodeResolutionDiagnostic

 * @brief Node Resolution Diagnostic data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct NodeResolutionDiagnostic {
    std::string intent_type;
    std::string concrete_type;
    ResolverBackend selected_backend{ResolverBackend::Unknown};
    ResolverFallbackReason fallback_reason{ResolverFallbackReason::None};
    std::string input_token_type;
    std::string output_token_type;
    bool fallback_used{false};
};

/**
 * @class ResolvingNodeProvider
 * @brief Resolving Node Provider graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class ResolvingNodeProvider final : public INodeProvider {
public:
    using AvailabilityFn = std::function<bool(const std::string&)>;

    ResolvingNodeProvider(std::shared_ptr<INodeProvider> inner,
                          GraphConfig::ResolverConfig resolver_config);

    ResolvingNodeProvider(std::shared_ptr<INodeProvider> inner,
                          GraphConfig::ResolverConfig resolver_config,
                          NodeResolutionRegistry resolution_registry);

    [[nodiscard]] std::expected<NodeFacadeAdapter, NodeCreationError>
    /**
     * @brief Creates or builds the object described by Create Node Expected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CreateNodeExpected(const std::string& node_type_name) noexcept override;

    /**
     * @brief Reports whether Is Node Type Available is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] bool IsNodeTypeAvailable(const std::string& node_type_name) const override;

    /**
     * @brief Returns the Available Node Types.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::vector<std::string> GetAvailableNodeTypes() const override;

    [[nodiscard]] std::optional<NodeResolutionDiagnostic>
    /**
     * @brief Updates or queries runtime registration through Resolve Node Type.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    ResolveNodeType(const std::string& node_type_name) const;

    /**
     * @brief Executes the Diagnostics operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] const std::vector<NodeResolutionDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    [[nodiscard]] std::optional<NodeResolutionDiagnostic>
    ResolveWithAvailability(const std::string& node_type_name,
                            const AvailabilityFn& available) const;

    std::shared_ptr<INodeProvider> inner_;
    GraphConfig::ResolverConfig resolver_config_{};
    NodeResolutionRegistry resolution_registry_;
    std::vector<NodeResolutionDiagnostic> diagnostics_{};
};

} // namespace graph
