// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

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
        transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        return context_ != nullptr && memory_pool_ != nullptr && transfer_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !memory_pool_ || !transfer_ || !accel::IsValidView(input) ||
            shard_count_ == 0 || shard_index_ >= shard_count_) {
            return std::nullopt;
        }

        const auto shard_base_bytes = input.bytes / shard_count_;
        const auto shard_remainder_bytes = input.bytes % shard_count_;
        const auto shard_bytes = shard_base_bytes +
                                 ((shard_index_ == (shard_count_ - 1)) ? shard_remainder_bytes : 0U);
        if (shard_bytes == 0) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateShared(shard_bytes, input.device_id, lease)) {
            return std::nullopt;
        }

        accel::DeviceBufferView src_slice = input;
        src_slice.bytes = shard_bytes;
        const auto offset_bytes = shard_base_bytes * shard_index_;
        src_slice.device_ptr = static_cast<void*>(
            static_cast<std::byte*>(input.device_ptr) + static_cast<std::ptrdiff_t>(offset_bytes));

        auto output = lease.device_view;
        output.backend = input.backend;
        output.dtype = input.dtype;
        output.layout = input.layout;
        if (output.layout.rank > 0) {
            const auto base_dim0 = input.layout.shape[0] / shard_count_;
            const auto remainder_dim0 = input.layout.shape[0] % shard_count_;
            output.layout.shape[0] = base_dim0 +
                                     ((shard_index_ == (shard_count_ - 1)) ? remainder_dim0 : 0U);
            if (output.layout.shape[0] == 0) {
                output.layout.shape[0] = 1;
            }
        }
        output.device_id = input.device_id;
        output.execution_queue_id = input.execution_queue_id;

        const auto queue_id = input.execution_queue_id == 0 ? 1 : input.execution_queue_id;
        if (!transfer_->EnqueueD2D(src_slice, output, queue_id, last_transfer_ticket_)) {
            return std::nullopt;
        }

        output.ready_event = last_transfer_ticket_.completion_event;
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
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint32_t shard_index_{0};
    std::uint32_t shard_count_{1};
    accel::BufferLease last_shard_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::metal::nodes