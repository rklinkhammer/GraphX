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

class PeerCopyNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          PeerCopyNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    PeerCopyNodeMetal() = default;
    ~PeerCopyNodeMetal() {
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
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !transfer_ || !accel::IsValidView(input)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateDevice(input.bytes, input.device_id, lease)) {
            return std::nullopt;
        }

        auto output = lease.device_view;
        output.backend = input.backend;
        output.dtype = input.dtype;
        output.layout = input.layout;
        output.execution_queue_id = queue_id_;

        if (!transfer_->EnqueueD2D(input, output, queue_id_, last_ticket_)) {
            return std::nullopt;
        }

        output.ready_event = last_ticket_.completion_event;
        last_copy_lease_ = lease;

        return output;
    }

    void SetQueue(std::uint64_t queue_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{0};
    bool owns_queue_{false};
    accel::TransferTicket last_ticket_{};
    accel::BufferLease last_copy_lease_{};
};

} // namespace graph::gpu::metal::nodes