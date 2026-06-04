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

class CollectiveReduceNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          CollectiveReduceNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    CollectiveReduceNodeMetal() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        collective_ = capability_bus.Get<capabilities::IMetalCollectiveCapability>();
        return collective_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!collective_ || !accel::IsValidView(input)) {
            return std::nullopt;
        }

        auto output = input;
        accel::CollectiveTicket ticket{};
        ticket.backend = accel::BackendKind::Metal;
        ticket.kind = accel::CollectiveKind::AllReduce;
        ticket.group_id = group_id_;
        ticket.rank = rank_;
        ticket.world_size = world_size_;
        ticket.execution_queue_id = input.execution_queue_id;

        if (!collective_->AllReduce(output, ticket)) {
            return std::nullopt;
        }

        last_ticket_ = ticket;
        return output;
    }

    void ConfigureCollective(std::uint64_t group_id,
                             std::uint32_t rank,
                             std::uint32_t world_size) {
        group_id_ = group_id;
        rank_ = rank;
        world_size_ = world_size;
    }

private:
    std::shared_ptr<capabilities::IMetalCollectiveCapability> collective_;
    std::uint64_t group_id_{1};
    std::uint32_t rank_{0};
    std::uint32_t world_size_{1};
    accel::CollectiveTicket last_ticket_{};
};

} // namespace graph::gpu::metal::nodes