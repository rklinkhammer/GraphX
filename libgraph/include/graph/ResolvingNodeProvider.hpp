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

struct NodeResolutionDiagnostic {
    std::string intent_type;
    std::string concrete_type;
    std::string selected_backend;
    std::string fallback_reason;
    std::string input_token_type;
    std::string output_token_type;
    bool fallback_used{false};
};

class ResolvingNodeProvider final : public INodeProvider {
public:
    using AvailabilityFn = std::function<bool(const std::string&)>;

    ResolvingNodeProvider(std::shared_ptr<INodeProvider> inner,
                          GraphConfig::ResolverConfig resolver_config);

    ResolvingNodeProvider(std::shared_ptr<INodeProvider> inner,
                          GraphConfig::ResolverConfig resolver_config,
                          NodeResolutionRegistry resolution_registry);

    [[nodiscard]] std::expected<NodeFacadeAdapter, NodeCreationError>
    CreateNodeExpected(const std::string& node_type_name) noexcept override;

    [[nodiscard]] bool IsNodeTypeAvailable(const std::string& node_type_name) const override;

    [[nodiscard]] std::vector<std::string> GetAvailableNodeTypes() const override;

    [[nodiscard]] std::optional<NodeResolutionDiagnostic>
    ResolveNodeType(const std::string& node_type_name) const;

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
