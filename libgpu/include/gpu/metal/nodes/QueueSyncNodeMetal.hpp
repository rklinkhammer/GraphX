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

class QueueSyncNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          QueueSyncNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    QueueSyncNodeMetal() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        return context_ != nullptr;
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
        queue_id_ = queue_id;
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::uint64_t queue_id_{1};
    std::uint64_t last_ready_event_{0};
};

} // namespace graph::gpu::metal::nodes