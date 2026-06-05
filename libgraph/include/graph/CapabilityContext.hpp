// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "capabilities/GraphCapability.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeDescriptor.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"

namespace graph {

enum class CapabilityContextError {
    NullNode,
    MissingGraphManager,
    MissingNodeCapability,
    MissingBusCapability
};

/**
 * Typed access layer for graph services, node capabilities, and node metadata.
 *
 * CapabilityContext keeps dynamic/native/plugin probing behind one small API.
 * Callers get explicit C++26-style std::expected failures instead of mixing
 * nullptr checks, direct casts, and wrapper checks at every policy site.
 */
class CapabilityContext {
public:
    explicit CapabilityContext(capabilities::GraphCapability& context) noexcept
        : context_(context) {}

    [[nodiscard]] std::expected<std::shared_ptr<GraphManager>, CapabilityContextError>
    GraphManagerPtr() const noexcept {
        auto manager = context_.GetGraphManager();
        if (!manager) {
            return std::unexpected(CapabilityContextError::MissingGraphManager);
        }
        return manager;
    }

    [[nodiscard]] std::expected<std::span<const std::shared_ptr<INode>>, CapabilityContextError>
    Nodes() const noexcept {
        auto manager = GraphManagerPtr();
        if (!manager) {
            return std::unexpected(manager.error());
        }
        return std::span<const std::shared_ptr<INode>>((*manager)->GetNodes());
    }

    template <typename CapabilityT>
    [[nodiscard]] std::expected<std::shared_ptr<CapabilityT>, CapabilityContextError>
    NodeCapability(const std::shared_ptr<INode>& node) const {
        if (!node) {
            return std::unexpected(CapabilityContextError::NullNode);
        }

        auto capability = DiscoverCapability<CapabilityT>(node);
        if (!capability) {
            return std::unexpected(CapabilityContextError::MissingNodeCapability);
        }
        return capability;
    }

    template <typename CapabilityT>
    [[nodiscard]] std::expected<std::shared_ptr<CapabilityT>, CapabilityContextError>
    BusCapability() const {
        auto capability = context_.GetCapabilityBus().Get<CapabilityT>();
        if (!capability) {
            return std::unexpected(CapabilityContextError::MissingBusCapability);
        }
        return capability;
    }

    [[nodiscard]] NodeDescriptor DescribeNode(const std::shared_ptr<INode>& node) const {
        NodeDescriptor descriptor{};
        if (!node) {
            return descriptor;
        }

        descriptor.name = GetNodeName(node);
        descriptor.type = GetTypeName(node);
        descriptor.lifecycle_state = GetLifecycleState(node);

        auto input_ports = node->InputPorts();
        descriptor.input_ports.reserve(input_ports.size());
        for (const auto& input_port : input_ports) {
            descriptor.input_ports.push_back(ToPortMetadata(input_port));
        }

        auto output_ports = node->OutputPorts();
        descriptor.output_ports.reserve(output_ports.size());
        for (const auto& output_port : output_ports) {
            descriptor.output_ports.push_back(ToPortMetadata(output_port));
        }

        return descriptor;
    }

    [[nodiscard]] capabilities::GraphCapability& GraphCapabilityRef() const noexcept {
        return context_;
    }

private:
    capabilities::GraphCapability& context_;
};

}  // namespace graph
