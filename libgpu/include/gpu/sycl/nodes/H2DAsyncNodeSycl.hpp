/**
 * @file H2DAsyncNodeSycl.hpp
 * @brief H2 Dasync Node SYCL GPU acceleration support.
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
 * @class H2DAsyncNodeSycl
 * @brief H2 Dasync Node SYCL graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class H2DAsyncNodeSycl
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::HostPinnedBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          H2DAsyncNodeSycl>,
      public graph::IGpuCapabilityBinding {
public:
    /**
     * @brief Executes the H2 Dasync Node Sycl operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    H2DAsyncNodeSycl() = default;

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

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::HostPinnedBufferView& host_view,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !transfer_ || queue_id_ == 0 ||
            !accel::IsValidView(host_view)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateDevice(host_view.bytes, device_id_, lease)) {
            return std::nullopt;
        }

        auto out_device_view = lease.device_view;
        out_device_view.dtype = host_view.dtype;
        out_device_view.layout = host_view.layout;

        if (!accel::IsValidView(out_device_view)) {
            return std::nullopt;
        }

        accel::TransferTicket ticket{};
        if (!transfer_->EnqueueH2D(host_view, out_device_view, queue_id_, ticket)) {
            return std::nullopt;
        }

        if (!accel::IsValidLease(lease) || !accel::IsValidTransferTicket(ticket)) {
            return std::nullopt;
        }

        last_device_lease_ = lease;
        last_transfer_ticket_ = ticket;
        return out_device_view;
    }

private:
    std::shared_ptr<capabilities::ISyclMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::ISyclTransferCapability> transfer_;
    std::uint64_t queue_id_{1};
    std::uint32_t device_id_{0};
    accel::BufferLease last_device_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::sycl::nodes
