/**
 * @file CapabilityContext.hpp
 * @brief Capability Context Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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
#include "graph/NodeMetadataService.hpp"
#include "graph/NodeDescriptor.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/IConfigurable.hpp"

namespace graph {

/**

 * @enum CapabilityContextError

 * @brief Capability Context Error values.

 *

 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

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
/**
 * @class CapabilityContext
 * @brief Capability Context capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class CapabilityContext {
public:
    explicit CapabilityContext(
        capabilities::GraphCapability& context,
                const INodeMetadataService* metadata_service = nullptr) noexcept
        : context_(context),
                    metadata_service_(
                            metadata_service ? metadata_service : &GetDefaultNodeMetadataService()) {}

    [[nodiscard]] std::expected<std::shared_ptr<GraphManager>, CapabilityContextError>
    /**
     * @brief Executes the Graph Manager Ptr operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    GraphManagerPtr() const noexcept {
        auto manager = context_.GetGraphManager();
        if (!manager) {
            return std::unexpected(CapabilityContextError::MissingGraphManager);
        }
        return manager;
    }

    [[nodiscard]] std::expected<std::span<const std::shared_ptr<INode>>, CapabilityContextError>
    /**
     * @brief Executes the Nodes operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    Nodes() const noexcept {
        auto manager = GraphManagerPtr();
        if (!manager) {
            return std::unexpected(manager.error());
        }
        return std::span<const std::shared_ptr<INode>>((*manager)->GetNodes());
    }

    template <typename CapabilityT>
    [[nodiscard]] std::expected<std::shared_ptr<CapabilityT>, CapabilityContextError>
    /**
     * @brief Executes the Node Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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
    /**
     * @brief Executes the Bus Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    BusCapability() const {
        auto capability = context_.GetCapabilityBus().Get<CapabilityT>();
        if (!capability) {
            return std::unexpected(CapabilityContextError::MissingBusCapability);
        }
        return capability;
    }

    /**
     * @brief Executes the Describe Node operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] NodeDescriptor DescribeNode(const std::shared_ptr<INode>& node) const {
        NodeDescriptor descriptor{};
        if (!node) {
            return descriptor;
        }

        auto input_ports = node->InputPorts();
        std::vector<PortMetadata> descriptor_input_ports;
        descriptor_input_ports.reserve(input_ports.size());
        for (const auto& input_port : input_ports) {
            descriptor_input_ports.push_back(ToPortMetadata(input_port));
        }

        auto output_ports = node->OutputPorts();
        std::vector<PortMetadata> descriptor_output_ports;
        descriptor_output_ports.reserve(output_ports.size());
        for (const auto& output_port : output_ports) {
            descriptor_output_ports.push_back(ToPortMetadata(output_port));
        }

        auto parameterized = DiscoverCapability<IParameterized>(node);
        auto configurable = DiscoverCapability<IConfigurable>(node);
        return metadata_service_->DescriptorProvider().BuildRuntimeDescriptor(
            RuntimeNodeDescriptorRequest{
            .seed = NodeDescriptorSeed{
                .name = GetNodeName(node),
                .type = GetTypeName(node),
                .description = "",
                .lifecycle_state = GetLifecycleState(node),
                .supports_configuration = static_cast<bool>(configurable),
            },
            .parameterized = parameterized ? parameterized.get() : nullptr,
            .input_ports = std::move(descriptor_input_ports),
            .output_ports = std::move(descriptor_output_ports),
        });
    }

    /**
     * @brief Executes the Graph Capability Ref operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] capabilities::GraphCapability& GraphCapabilityRef() const noexcept {
        return context_;
    }

private:
    capabilities::GraphCapability& context_;
    const INodeMetadataService* metadata_service_;
};

}  // namespace graph
