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

class D2HAsyncNodeSycl
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::HostPinnedBufferView>,
          D2HAsyncNodeSycl>,
      public graph::IGpuCapabilityBinding {
public:
    D2HAsyncNodeSycl() = default;

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
