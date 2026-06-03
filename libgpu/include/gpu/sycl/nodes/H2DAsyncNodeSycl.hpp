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

class H2DAsyncNodeSycl
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::HostPinnedBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          H2DAsyncNodeSycl>,
      public graph::IGpuCapabilityBinding {
public:
    H2DAsyncNodeSycl() = default;

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

        last_device_lease_ = lease;
        last_transfer_ticket_ = ticket;
        return out_device_view;
    }

    bool TransferForTest(const accel::HostPinnedBufferView& host_view,
                         std::uint64_t queue_id,
                         std::uint32_t device_id,
                         accel::DeviceBufferView& out_device_view,
                         accel::BufferLease& out_device_lease,
                         accel::TransferTicket& out_ticket) {
        queue_id_ = queue_id;
        device_id_ = device_id;
        auto out = Transfer(host_view,
                            std::integral_constant<std::size_t, 0>{},
                            std::integral_constant<std::size_t, 0>{});
        if (!out) {
            return false;
        }
        out_device_view = *out;
        out_device_lease = last_device_lease_;
        out_ticket = last_transfer_ticket_;
        return true;
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
