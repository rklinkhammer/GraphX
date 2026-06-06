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

class QueueSyncNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          QueueSyncNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    QueueSyncNodeMetal() = default;
    ~QueueSyncNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        shared_queue_ = capability_bus.Get<capabilities::IMetalSharedQueueCapability>();
        if (queue_id_ == 0 && context_ != nullptr) {
            if (shared_queue_ != nullptr) {
                queue_id_ = shared_queue_->GetOrCreateQueueId();
                owns_queue_ = false;
            }
            if (queue_id_ == 0) {
                queue_id_ = context_->CreateCommandQueue();
                owns_queue_ = queue_id_ != 0;
            }
        }
        return context_ != nullptr && queue_id_ != 0;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !accel::IsValidView(input) || queue_id_ == 0) {
            return std::nullopt;
        }

        auto output = input;
        output.ready_event = context_->CreateEvent();
        last_ready_event_ = output.ready_event;
        return output;
    }

    void SetQueue(std::uint64_t queue_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::uint64_t queue_id_{0};
    bool owns_queue_{false};
    std::uint64_t last_ready_event_{0};
};

} // namespace graph::gpu::metal::nodes