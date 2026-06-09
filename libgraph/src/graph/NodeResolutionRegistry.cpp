#include "graph/NodeResolutionRegistry.hpp"

namespace graph {

NodeResolutionRegistry NodeResolutionRegistry::CreateDefault() {
    NodeResolutionRegistry registry;

    registry.AddContract(NodeResolutionContract{
        .intent_type = "H2DAsyncNode",
        .input_token_type = "HostPinnedBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {"metal", "H2DAsyncNodeMetal"},
            {"sycl", "H2DAsyncNodeSycl"},
            {"stub", "H2DAsyncNode"},
            {"cuda", "H2DAsyncNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "D2HAsyncNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "HostPinnedBufferView",
        .variants = {
            {"metal", "D2HAsyncNodeMetal"},
            {"sycl", "D2HAsyncNodeSycl"},
            {"stub", "D2HAsyncNode"},
            {"cuda", "D2HAsyncNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceTransformNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {"metal", "DeviceTransformNodeMetal"},
            {"stub", "DeviceTransformNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceKernelNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {"metal", "DeviceKernelNodeMetal"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "DeviceReduceNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {"metal", "DeviceReduceNodeMetal"},
            {"stub", "DeviceReduceNode"},
        },
    });

    registry.AddContract(NodeResolutionContract{
        .intent_type = "QueueSyncNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {"metal", "QueueSyncNodeMetal"},
            {"stub", "QueueSyncNode"},
        },
    });

    return registry;
}

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

std::vector<std::string> NodeResolutionRegistry::IntentTypes() const {
    std::vector<std::string> result;
    result.reserve(contracts_.size());
    for (const auto& [intent_type, _] : contracts_) {
        result.push_back(intent_type);
    }
    return result;
}

} // namespace graph
