// SPDX-License-Identifier: MIT

/**
 * @file NodeResolutionRegistry.cpp
 * @brief Node Resolution Registry Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include "graph/NodeResolutionRegistry.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string ToLowerCopy(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

} // namespace

namespace graph {

const char* ToString(ResolverBackend backend) noexcept {
    switch (backend) {
        case ResolverBackend::Auto:
            return "auto";
        case ResolverBackend::Metal:
            return "metal";
        case ResolverBackend::Cuda:
            return "cuda";
        case ResolverBackend::Stub:
            return "stub";
        case ResolverBackend::Direct:
            return "direct";
        case ResolverBackend::Unknown:
            return "unknown";
        default:
            return "unknown";
    }
}

const char* ToString(ResolverFallbackPolicy policy) noexcept {
    switch (policy) {
        case ResolverFallbackPolicy::Strict:
            return "strict";
        case ResolverFallbackPolicy::AllowFallback:
            return "allow_fallback";
        default:
            return "strict";
    }
}

const char* ToString(ResolverFallbackReason reason) noexcept {
    switch (reason) {
        case ResolverFallbackReason::None:
            return "none";
        case ResolverFallbackReason::RequestedBackendUnavailable:
            return "requested-backend-unavailable";
        default:
            return "none";
    }
}

const char* ToString(ResolverCapability capability) noexcept {
    switch (capability) {
        case ResolverCapability::Supported: return "supported";
        case ResolverCapability::Unsupported: return "unsupported";
    }
    return "unsupported";
}

const char* ToString(ResolverResolutionState state) noexcept {
    switch (state) {
        case ResolverResolutionState::Selected: return "selected";
        case ResolverResolutionState::Fallback: return "fallback";
        case ResolverResolutionState::Unavailable: return "unavailable";
        case ResolverResolutionState::Unsupported: return "unsupported";
    }
    return "unsupported";
}

std::optional<ResolverBackend> ParseResolverBackend(
    const std::string& backend) noexcept {
    const auto normalized = ToLowerCopy(backend);
    if (normalized == "auto") {
        return ResolverBackend::Auto;
    }
    if (normalized == "metal") {
        return ResolverBackend::Metal;
    }
    if (normalized == "cuda") {
        return ResolverBackend::Cuda;
    }
    if (normalized == "stub") {
        return ResolverBackend::Stub;
    }
    if (normalized == "direct") {
        return ResolverBackend::Direct;
    }
    if (normalized == "unknown") {
        return ResolverBackend::Unknown;
    }
    return std::nullopt;
}

std::optional<ResolverFallbackPolicy> ParseResolverFallbackPolicy(
    const std::string& policy) noexcept {
    const auto normalized = ToLowerCopy(policy);
    if (normalized == "strict") {
        return ResolverFallbackPolicy::Strict;
    }
    if (normalized == "allow_fallback") {
        return ResolverFallbackPolicy::AllowFallback;
    }
    return std::nullopt;
}

std::optional<ResolverCapability> ParseResolverCapability(
    const std::string& capability) noexcept {
    const auto normalized = ToLowerCopy(capability);
    if (normalized == "supported") return ResolverCapability::Supported;
    if (normalized == "unsupported") return ResolverCapability::Unsupported;
    return std::nullopt;
}

/**
 * @brief Create default.
 */
NodeResolutionRegistry NodeResolutionRegistry::CreateDefault() {
    NodeResolutionRegistry registry;

    registry.AddContract(NodeResolutionContract{
        .intent_type = "H2DAsyncNode",
        .input_token_type = "HostPinnedBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {ResolverBackend::Metal, "H2DAsyncNodeMetal"},
            {ResolverBackend::Stub, "H2DAsyncNode"},
            {ResolverBackend::Cuda, "H2DAsyncNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "D2HAsyncNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "HostPinnedBufferView",
        .variants = {
            {ResolverBackend::Metal, "D2HAsyncNodeMetal"},
            {ResolverBackend::Stub, "D2HAsyncNode"},
            {ResolverBackend::Cuda, "D2HAsyncNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceTransformNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {ResolverBackend::Metal, "DeviceTransformNodeMetal"},
            {ResolverBackend::Stub, "DeviceTransformNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceKernelNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {ResolverBackend::Metal, "DeviceKernelNodeMetal"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceReduceNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {ResolverBackend::Metal, "DeviceReduceNodeMetal"},
            {ResolverBackend::Stub, "DeviceReduceNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "QueueSyncNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {ResolverBackend::Metal, "QueueSyncNodeMetal"},
            {ResolverBackend::Stub, "QueueSyncNode"},
        },
    });

    return registry;
}

/**
 * @brief Add contract.
 * @param contract Parameter for add contract.
 */
void NodeResolutionRegistry::AddContract(NodeResolutionContract contract) {
    if (contract.intent_type.empty()) {
        return;
    }
    contracts_[contract.intent_type] = std::move(contract);
}

void NodeResolutionRegistry::AddMappings(
    const std::vector<GraphConfig::ResolverMapping>& mappings) {
    for (const auto& mapping : mappings) {
        NodeResolutionContract contract{
            .intent_type = mapping.intent_type,
            .input_token_type = mapping.input_token_type,
            .output_token_type = mapping.output_token_type,
            .variants = {},
        };
        contract.variants.reserve(mapping.variants.size());
        for (const auto& variant : mapping.variants) {
            contract.variants.push_back(NodeResolutionVariant{
                .backend = variant.backend,
                .concrete_type = variant.concrete_type,
                .capability = variant.capability,
            });
        }
        AddContract(std::move(contract));
    }
}

const NodeResolutionContract* NodeResolutionRegistry::Find(
    const std::string& intent_type) const {
    const auto it = contracts_.find(intent_type);
    if (it == contracts_.end()) {
        return nullptr;
    }
    return &it->second;
}

/**
 * @brief Intent types.
 */
std::vector<std::string> NodeResolutionRegistry::IntentTypes() const {
    std::vector<std::string> result;
    result.reserve(contracts_.size());
    for (const auto& [intent_type, _] : contracts_) {
        result.push_back(intent_type);
    }
    return result;
}

} // namespace graph
