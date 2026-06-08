#include "graph/ResolvingNodeProvider.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace graph {

namespace {

struct BackendVariant {
    std::string backend;
    std::string concrete_type;
};

struct IntentContract {
    std::string input_token_type;
    std::string output_token_type;
    std::vector<BackendVariant> variants;
};

const std::map<std::string, IntentContract>& IntentContracts() {
    static const std::map<std::string, IntentContract> contracts{
        {
            "H2DAsyncNode",
            IntentContract{
                .input_token_type = "HostPinnedBufferView",
                .output_token_type = "DeviceBufferView",
                .variants = {
                    {"metal", "H2DAsyncNodeMetal"},
                    {"sycl", "H2DAsyncNodeSycl"},
                    {"stub", "H2DAsyncNode"},
                    {"cuda", "H2DAsyncNode"},
                },
            },
        },
        {
            "D2HAsyncNode",
            IntentContract{
                .input_token_type = "DeviceBufferView",
                .output_token_type = "HostPinnedBufferView",
                .variants = {
                    {"metal", "D2HAsyncNodeMetal"},
                    {"sycl", "D2HAsyncNodeSycl"},
                    {"stub", "D2HAsyncNode"},
                    {"cuda", "D2HAsyncNode"},
                },
            },
        },
        {
            "DeviceTransformNode",
            IntentContract{
                .input_token_type = "DeviceBufferView",
                .output_token_type = "DeviceBufferView",
                .variants = {
                    {"metal", "DeviceTransformNodeMetal"},
                    {"stub", "DeviceTransformNode"},
                },
            },
        },
        {
            "DeviceKernelNode",
            IntentContract{
                .input_token_type = "DeviceBufferView",
                .output_token_type = "DeviceBufferView",
                .variants = {
                    {"metal", "DeviceKernelNodeMetal"},
                },
            },
        },
        {
            "DeviceReduceNode",
            IntentContract{
                .input_token_type = "DeviceBufferView",
                .output_token_type = "DeviceBufferView",
                .variants = {
                    {"metal", "DeviceReduceNodeMetal"},
                    {"stub", "DeviceReduceNode"},
                },
            },
        },
        {
            "QueueSyncNode",
            IntentContract{
                .input_token_type = "DeviceBufferView",
                .output_token_type = "DeviceBufferView",
                .variants = {
                    {"metal", "QueueSyncNodeMetal"},
                    {"stub", "QueueSyncNode"},
                },
            },
        },
    };
    return contracts;
}

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

std::optional<std::string> ConcreteForBackend(const IntentContract& contract,
                                              const std::string& backend) {
    for (const auto& variant : contract.variants) {
        if (variant.backend == backend) {
            return variant.concrete_type;
        }
    }
    return std::nullopt;
}

bool IsKnownIntent(const std::string& node_type_name) {
    return IntentContracts().contains(node_type_name);
}

} // namespace

ResolvingNodeProvider::ResolvingNodeProvider(
    std::shared_ptr<INodeProvider> inner,
    GraphConfig::ResolverConfig resolver_config)
    : inner_(std::move(inner)), resolver_config_(std::move(resolver_config)) {}

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
    for (const auto& [intent, _] : IntentContracts()) {
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

    const auto contract_it = IntentContracts().find(node_type_name);
    if (contract_it == IntentContracts().end()) {
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

    const auto& contract = contract_it->second;
    const auto requested_backend = resolver_config_.execution_backend.empty()
        ? std::string{"auto"}
        : resolver_config_.execution_backend;
    const auto fallback_policy = resolver_config_.backend_fallback_policy.empty()
        ? std::string{"strict"}
        : resolver_config_.backend_fallback_policy;

    for (const auto& backend : BackendPreference(requested_backend, fallback_policy)) {
        auto concrete = ConcreteForBackend(contract, backend);
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
            .input_token_type = contract.input_token_type,
            .output_token_type = contract.output_token_type,
            .fallback_used = fallback_used,
        };
    }

    if (!IsKnownIntent(node_type_name)) {
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace graph
