#include "graph/ResolvingNodeProvider.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace graph {

namespace {

std::vector<std::string> BackendPreference(const std::string& requested_backend,
                                           const std::string& fallback_policy) {
    const std::array<std::string, 4> auto_order{"metal", "sycl", "stub", "cuda"};
    if (requested_backend == "auto") {
        return {auto_order.begin(), auto_order.end()};
    }

    std::vector<std::string> order{requested_backend};
    if (fallback_policy != "allow_fallback") {
        return order;
    }

    for (const auto& backend : auto_order) {
        if (std::find(order.begin(), order.end(), backend) == order.end()) {
            order.push_back(backend);
        }
    }
    return order;
}

std::optional<std::string> ConcreteForBackend(const NodeResolutionContract& contract,
                                              const std::string& backend) {
    for (const auto& variant : contract.variants) {
        if (variant.backend == backend) {
            return variant.concrete_type;
        }
    }
    return std::nullopt;
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
        return std::unexpected(NodeCreationError::TypeNotFound);
    }

    if (resolver_config_.resolver_diagnostics) {
        diagnostics_.push_back(*resolved);
    }

    return inner_->CreateNodeExpected(resolved->concrete_type);
}

bool ResolvingNodeProvider::IsNodeTypeAvailable(const std::string& node_type_name) const {
    return ResolveNodeType(node_type_name).has_value();
}

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

std::optional<NodeResolutionDiagnostic>
ResolvingNodeProvider::ResolveNodeType(const std::string& node_type_name) const {
    if (!inner_) {
        return std::nullopt;
    }
    return ResolveWithAvailability(
        node_type_name,
        [provider = inner_](const std::string& type) {
            return provider->IsNodeTypeAvailable(type);
        });
}

std::optional<NodeResolutionDiagnostic>
ResolvingNodeProvider::ResolveWithAvailability(
    const std::string& node_type_name,
    const AvailabilityFn& available) const {
    if (!available) {
        return std::nullopt;
    }

    const auto* contract = resolution_registry_.Find(node_type_name);
    if (contract == nullptr) {
        if (!available(node_type_name)) {
            return std::nullopt;
        }
        return NodeResolutionDiagnostic{
            .intent_type = node_type_name,
            .concrete_type = node_type_name,
            .selected_backend = "direct",
            .fallback_reason = "",
            .input_token_type = "",
            .output_token_type = "",
            .fallback_used = false,
        };
    }

    const auto requested_backend = resolver_config_.execution_backend.empty()
        ? std::string{"auto"}
        : resolver_config_.execution_backend;
    const auto fallback_policy = resolver_config_.backend_fallback_policy.empty()
        ? std::string{"strict"}
        : resolver_config_.backend_fallback_policy;

    for (const auto& backend : BackendPreference(requested_backend, fallback_policy)) {
        auto concrete = ConcreteForBackend(*contract, backend);
        if (!concrete || !available(*concrete)) {
            continue;
        }

        const bool fallback_used =
            requested_backend != "auto" && backend != requested_backend;
        return NodeResolutionDiagnostic{
            .intent_type = node_type_name,
            .concrete_type = *concrete,
            .selected_backend = backend,
            .fallback_reason = fallback_used ? "requested-backend-unavailable" : "",
            .input_token_type = contract->input_token_type,
            .output_token_type = contract->output_token_type,
            .fallback_used = fallback_used,
        };
    }

    return std::nullopt;
}

} // namespace graph
