/**
 * @file HostIngressPinnedSourceNode.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace graph::gpu::cuda::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class HostIngressPinnedSourceNode
 * @brief HostIngressPinnedSourceNode class.
 */
class HostIngressPinnedSourceNode
    : public graph::NamedSourceNode<HostIngressPinnedSourceNode, accel::HostPinnedBufferView>,
      public graph::IGpuCapabilityBinding {
public:
    HostIngressPinnedSourceNode() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::ICudaMemoryPoolCapability>();
        return memory_pool_ != nullptr;
    }

    std::optional<accel::HostPinnedBufferView> Produce(
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || pending_bytes_ == 0) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocatePinnedHost(pending_bytes_, lease)) {
            return std::nullopt;
        }

        auto out_view = lease.host_view;
        if (out_view.layout.rank == 0) {
            out_view.layout.rank = 1;
            out_view.layout.shape[0] = pending_bytes_;
            out_view.layout.stride[0] = 1;
        }

        if (!accel::IsValidView(out_view)) {
            return std::nullopt;
        }

        last_lease_ = lease;
        pending_bytes_ = 0;
        return out_view;
    }

    bool ProduceForTest(std::uint64_t bytes,
                        accel::HostPinnedBufferView& out_view,
                        accel::BufferLease& out_lease) {
        pending_bytes_ = bytes;
        auto produced = Produce(std::integral_constant<std::size_t, 0>{});
        if (!produced) {
            return false;
        }

        out_view = *produced;
        out_lease = last_lease_;
        return true;
    }

    void StageNextBufferBytes(std::uint64_t bytes) {
        pending_bytes_ = bytes;
    }

private:
    std::shared_ptr<capabilities::ICudaMemoryPoolCapability> memory_pool_;
    std::uint64_t pending_bytes_{0};
    accel::BufferLease last_lease_{};
};

} // namespace graph::gpu::cuda::nodes
