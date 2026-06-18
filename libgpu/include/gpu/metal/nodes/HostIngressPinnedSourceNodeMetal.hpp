/**
 * @file HostIngressPinnedSourceNodeMetal.hpp
 * @brief Host Ingress Pinned Source Node Metal GPU acceleration support.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class HostIngressPinnedSourceNodeMetal
 * @brief Host Ingress Pinned Source Node Metal graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class HostIngressPinnedSourceNodeMetal
    : public graph::NamedSourceNode<HostIngressPinnedSourceNodeMetal, accel::HostPinnedBufferView>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    /**
     * @brief Executes the Host Ingress Pinned Source Node Metal operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    HostIngressPinnedSourceNodeMetal() = default;

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
        return memory_pool_ != nullptr;
    }

    std::optional<accel::HostPinnedBufferView> Produce(
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || pending_bytes_ == 0) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateHost(pending_bytes_, lease)) {
            return std::nullopt;
        }

        auto out_view = lease.host_view;
        if (out_view.layout.rank == 0) {
            out_view.layout.rank = 1;
            out_view.layout.shape[0] = pending_bytes_;
            out_view.layout.stride[0] = 1;
        }

        if (!accel::IsValidView(out_view)) {
            return std::nullopt;
        }

        last_lease_ = lease;
        pending_bytes_ = 0;
        return out_view;
    }

    bool ProduceForTest(std::uint64_t bytes,
                        accel::HostPinnedBufferView& out_view,
                        accel::BufferLease& out_lease) {
        pending_bytes_ = bytes;
        auto produced = Produce(std::integral_constant<std::size_t, 0>{});
        if (!produced) {
            return false;
        }

        out_view = *produced;
        out_lease = last_lease_;
        return true;
    }

    /**
     * @brief Executes the Stage Next Buffer Bytes operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param bytes Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void StageNextBufferBytes(std::uint64_t bytes) {
        pending_bytes_ = bytes;
    }

    /**
     * @brief Applies configuration to this object.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param cfg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Configure(const graph::JsonView& cfg) override {
        if (cfg.Contains("staged_bytes")) {
            auto staged_bytes = cfg.TryGetInt("staged_bytes");
            if (!staged_bytes) {
                throw staged_bytes.error();
            }
            if (staged_bytes.value() < 0) {
                throw std::invalid_argument("staged_bytes must be >= 0");
            }
            pending_bytes_ = static_cast<std::uint64_t>(staged_bytes.value());
        }
    }

    /**
     * @brief Returns the Parameters.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"staged_bytes", pending_bytes_},
        };
        return graph::JsonView(params);
    }

    /**
     * @brief Returns the Parameter Description.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param param_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override {
        static thread_local nlohmann::json desc;
        if (param_name == "staged_bytes") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Number of host bytes to stage for next Produce() call."},
            };
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    /**
     * @brief Returns the Parameter Names.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"staged_bytes"};
    }

private:
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::uint64_t pending_bytes_{0};
    accel::BufferLease last_lease_{};
};

} // namespace graph::gpu::metal::nodes
