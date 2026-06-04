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

class DeviceShardNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceShardNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    DeviceShardNodeMetal() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
        return context_ != nullptr && memory_pool_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !memory_pool_ || !accel::IsValidView(input) || shard_count_ == 0) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateShared(input.bytes / shard_count_, input.device_id, lease)) {
            return std::nullopt;
        }

        auto output = lease.device_view;
        output.backend = input.backend;
        output.dtype = input.dtype;
        output.layout = input.layout;
        output.device_id = shard_index_;
        output.execution_queue_id = input.execution_queue_id;
        output.ready_event = input.ready_event;
        last_shard_lease_ = lease;
        return output;
    }

    void ConfigureShard(std::uint32_t shard_index, std::uint32_t shard_count) {
        shard_index_ = shard_index;
        shard_count_ = shard_count;
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::uint32_t shard_index_{0};
    std::uint32_t shard_count_{1};
    accel::BufferLease last_shard_lease_{};
};

} // namespace graph::gpu::metal::nodes