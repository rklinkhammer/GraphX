// SPDX-License-Identifier: MIT

/**
 * @file ResolvingNodeProvider.cpp
 * @brief Resolving Node Provider Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include "graph/ResolvingNodeProvider.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace graph {

nlohmann::json
NodeResolutionDiagnosticToJson(const NodeResolutionDiagnostic& diagnostic) {
    return nlohmann::json{
        {"intent_type", diagnostic.intent_type},
        {"concrete_type", diagnostic.concrete_type},
        {"state", ToString(diagnostic.state)},
        {"selected_backend", ToString(diagnostic.selected_backend)},
        {"fallback_reason", ToString(diagnostic.fallback_reason)},
        {"fallback_used", diagnostic.fallback_used},
        {"input_token_type", diagnostic.input_token_type},
        {"output_token_type", diagnostic.output_token_type},
    };
}

nlohmann::json NodeResolutionFailureToJson(
    const NodeResolutionFailure& failure) {
    return nlohmann::json{
        {"intent_type", failure.intent_type},
        {"requested_backend", ToString(failure.requested_backend)},
        {"state", ToString(failure.state)},
        {"detail", failure.detail},
    };
}

namespace {

std::vector<ResolverBackend> BackendPreference(ResolverBackend requested_backend,
                                               ResolverFallbackPolicy fallback_policy) {
    const std::array<ResolverBackend, 3> auto_order{
        ResolverBackend::Metal,
        ResolverBackend::Stub,
        ResolverBackend::Cuda,
    };
    if (requested_backend == ResolverBackend::Auto) {
        return {auto_order.begin(), auto_order.end()};
    }

    std::vector<ResolverBackend> order{requested_backend};
    if (fallback_policy != ResolverFallbackPolicy::AllowFallback) {
        return order;
    }

    for (const auto& backend : auto_order) {
        if (std::find(order.begin(), order.end(), backend) == order.end()) {
            order.push_back(backend);
        }
    }
    return order;
}

const NodeResolutionVariant* VariantForBackend(
    const NodeResolutionContract& contract, ResolverBackend backend) {
    for (const auto& variant : contract.variants) {
        if (variant.backend == backend) {
            return &variant;
        }
    }
    return nullptr;
}

} // namespace

ResolvingNodeProvider::ResolvingNodeProvider(
    std::shared_ptr<INodeProvider> inner,
    GraphConfig::ResolverConfig resolver_config)
    : ResolvingNodeProvider(
          std::move(inner),
          std::move(resolver_config),
          NodeResolutionRegistry::CreateDefault()) {}

ResolvingNodeProvider::ResolvingNodeProvider(
    std::shared_ptr<INodeProvider> inner,
    GraphConfig::ResolverConfig resolver_config,
    NodeResolutionRegistry resolution_registry)
    : inner_(std::move(inner)),
      resolver_config_(std::move(resolver_config)),
      resolution_registry_(std::move(resolution_registry)) {}

std::expected<NodeFacadeAdapter, NodeCreationError>
ResolvingNodeProvider::CreateNodeExpected(const std::string& node_type_name) noexcept {
    if (!inner_) {
        return std::unexpected(NodeCreationError::NotInitialized);
    }

    auto resolved = ResolveNodeType(node_type_name);
    if (!resolved) {
        if (resolver_config_.resolver_diagnostics) {
            resolution_failures_.push_back(resolved.error());
        }
        return std::unexpected(
            resolved.error().state == ResolverResolutionState::Unsupported
                ? NodeCreationError::Unsupported
                : NodeCreationError::BackendUnavailable);
    }

    if (resolver_config_.resolver_diagnostics) {
        diagnostics_.push_back(*resolved);
    }

    return inner_->CreateNodeExpected(resolved->concrete_type);
}

/**
 * @brief Is node type available.
 * @param node_type_name Parameter for is node type available.
 */
bool ResolvingNodeProvider::IsNodeTypeAvailable(const std::string& node_type_name) const {
    return ResolveNodeType(node_type_name).has_value();
}

/**
 * @brief Get available node types.
 */
std::vector<std::string> ResolvingNodeProvider::GetAvailableNodeTypes() const {
    if (!inner_) {
        return {};
    }

    std::set<std::string> types;
    for (const auto& type : inner_->GetAvailableNodeTypes()) {
        types.insert(type);
    }
    for (const auto& intent : resolution_registry_.IntentTypes()) {
        if (IsNodeTypeAvailable(intent)) {
            types.insert(intent);
        }
    }
    return {types.begin(), types.end()};
}

NodeResolutionResult
ResolvingNodeProvider::ResolveNodeType(const std::string& node_type_name) const {
    if (!inner_) {
        return std::unexpected(NodeResolutionFailure{
            .intent_type = node_type_name,
            .requested_backend = resolver_config_.execution_backend,
            .state = ResolverResolutionState::Unavailable,
            .detail = "node provider is not initialized",
        });
    }
    return ResolveWithAvailability(
        node_type_name,
        [provider = inner_](const std::string& type) {
            return provider->IsNodeTypeAvailable(type);
        });
}

NodeResolutionResult
ResolvingNodeProvider::ResolveWithAvailability(
    const std::string& node_type_name,
    const AvailabilityFn& available) const {
    if (!available) {
        return std::unexpected(NodeResolutionFailure{
            .intent_type = node_type_name,
            .requested_backend = resolver_config_.execution_backend,
            .state = ResolverResolutionState::Unavailable,
            .detail = "node availability query is not configured",
        });
    }

    const auto* contract = resolution_registry_.Find(node_type_name);
    if (contract == nullptr) {
        if (!available(node_type_name)) {
            return std::unexpected(NodeResolutionFailure{
                .intent_type = node_type_name,
                .requested_backend = ResolverBackend::Direct,
                .state = ResolverResolutionState::Unavailable,
                .detail = "direct node type is unavailable",
            });
        }
        return NodeResolutionDiagnostic{
            .intent_type = node_type_name,
            .concrete_type = node_type_name,
            .selected_backend = ResolverBackend::Direct,
            .fallback_reason = ResolverFallbackReason::None,
            .input_token_type = "",
            .output_token_type = "",
            .fallback_used = false,
            .state = ResolverResolutionState::Selected,
        };
    }

    const auto requested_backend = resolver_config_.execution_backend;
    const auto fallback_policy = resolver_config_.backend_fallback_policy;

    bool supported_variant_seen = false;
    for (const auto& backend : BackendPreference(requested_backend, fallback_policy)) {
        const auto* variant = VariantForBackend(*contract, backend);
        if (variant == nullptr ||
            variant->capability == ResolverCapability::Unsupported) {
            continue;
        }
        supported_variant_seen = true;
        if (!available(variant->concrete_type)) continue;

        const bool fallback_used =
            requested_backend != ResolverBackend::Auto && backend != requested_backend;
        return NodeResolutionDiagnostic{
            .intent_type = node_type_name,
            .concrete_type = variant->concrete_type,
            .selected_backend = backend,
            .fallback_reason = fallback_used
                ? ResolverFallbackReason::RequestedBackendUnavailable
                : ResolverFallbackReason::None,
            .input_token_type = contract->input_token_type,
            .output_token_type = contract->output_token_type,
            .fallback_used = fallback_used,
            .state = fallback_used ? ResolverResolutionState::Fallback
                                   : ResolverResolutionState::Selected,
        };
    }

    const bool requested_has_variant = requested_backend == ResolverBackend::Auto ||
        VariantForBackend(*contract, requested_backend) != nullptr;
    const auto failure_state =
        (!requested_has_variant || !supported_variant_seen)
            ? ResolverResolutionState::Unsupported
            : ResolverResolutionState::Unavailable;
    return std::unexpected(NodeResolutionFailure{
        .intent_type = node_type_name,
        .requested_backend = requested_backend,
        .state = failure_state,
        .detail = failure_state == ResolverResolutionState::Unsupported
            ? "requested backend does not support this node intent"
            : "supported backend node is unavailable",
    });
}

} // namespace graph
