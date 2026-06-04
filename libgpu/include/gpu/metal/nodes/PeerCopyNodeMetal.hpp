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

class PeerCopyNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          PeerCopyNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    PeerCopyNodeMetal() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        return transfer_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!transfer_ || !accel::IsValidView(input)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        lease.device_view = input;
        if (!transfer_->EnqueueD2D(input, lease.device_view, queue_id_, last_ticket_)) {
            return std::nullopt;
        }

        return lease.device_view;
    }

    void SetQueue(std::uint64_t queue_id) {
        queue_id_ = queue_id;
    }

private:
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{1};
    accel::TransferTicket last_ticket_{};
};

} // namespace graph::gpu::metal::nodes