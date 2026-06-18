/**
 * @file D2HAsyncNodeSycl.hpp
 * @brief D2 Hasync Node SYCL GPU acceleration support.
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
 * @class D2HAsyncNodeSycl
 * @brief D2 Hasync Node SYCL graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class D2HAsyncNodeSycl
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::HostPinnedBufferView>,
          D2HAsyncNodeSycl>,
      public graph::IGpuCapabilityBinding {
public:
    /**
     * @brief Executes the D2 Hasync Node Sycl operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    D2HAsyncNodeSycl() = default;

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::ISyclMemoryPoolCapability>();
        transfer_ = capability_bus.Get<capabilities::ISyclTransferCapability>();
        return memory_pool_ != nullptr && transfer_ != nullptr;
    }

    std::optional<accel::HostPinnedBufferView> Transfer(
        const accel::DeviceBufferView& device_view,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !transfer_ || queue_id_ == 0 ||
            !accel::IsValidView(device_view)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateHost(device_view.bytes, lease)) {
            return std::nullopt;
        }

        auto out_host_view = lease.host_view;
        out_host_view.dtype = device_view.dtype;
        out_host_view.layout = device_view.layout;

        if (!accel::IsValidView(out_host_view)) {
            return std::nullopt;
        }

        accel::TransferTicket ticket{};
        if (!transfer_->EnqueueD2H(device_view, out_host_view, queue_id_, ticket)) {
            return std::nullopt;
        }

        if (!accel::IsValidLease(lease) || !accel::IsValidTransferTicket(ticket)) {
            return std::nullopt;
        }

        last_host_lease_ = lease;
        last_transfer_ticket_ = ticket;
        return out_host_view;
    }

private:
    std::shared_ptr<capabilities::ISyclMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::ISyclTransferCapability> transfer_;
    std::uint64_t queue_id_{1};
    accel::BufferLease last_host_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::sycl::nodes
