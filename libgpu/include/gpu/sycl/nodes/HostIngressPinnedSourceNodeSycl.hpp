/**
 * @file HostIngressPinnedSourceNodeSycl.hpp
 * @brief Host Ingress Pinned Source Node SYCL GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace graph::gpu::sycl::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class HostIngressPinnedSourceNodeSycl
 * @brief Host Ingress Pinned Source Node SYCL graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class HostIngressPinnedSourceNodeSycl
    : public graph::NamedSourceNode<HostIngressPinnedSourceNodeSycl, accel::HostPinnedBufferView>,
      public graph::IGpuCapabilityBinding {
public:
    /**
     * @brief Executes the Host Ingress Pinned Source Node Sycl operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    HostIngressPinnedSourceNodeSycl() = default;

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::ISyclMemoryPoolCapability>();
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

private:
    std::shared_ptr<capabilities::ISyclMemoryPoolCapability> memory_pool_;
    std::uint64_t pending_bytes_{0};
    accel::BufferLease last_lease_{};
};

} // namespace graph::gpu::sycl::nodes
