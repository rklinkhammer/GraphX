// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

class H2DAsyncNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::HostPinnedBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          H2DAsyncNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    H2DAsyncNodeMetal() = default;
    ~H2DAsyncNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
        transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        if (queue_id_ == 0 && context_ != nullptr) {
            queue_id_ = context_->CreateCommandQueue();
            owns_queue_ = queue_id_ != 0;
        }
        return memory_pool_ != nullptr && transfer_ != nullptr && queue_id_ != 0;
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

    void SetQueueAndDevice(std::uint64_t queue_id, std::uint32_t device_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
        device_id_ = device_id;
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
    accel::BufferLease last_device_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::metal::nodes