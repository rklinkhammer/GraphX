// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace graph::gpu::cuda::nodes {

class LeaseReleaseNode
    : public graph::NamedSinkNode<LeaseReleaseNode, accel::BufferLease>,
      public graph::IGpuCapabilityBinding {
public:
    LeaseReleaseNode() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::ICudaMemoryPoolCapability>();
        return memory_pool_ != nullptr;
    }

    bool Consume(const accel::BufferLease& lease,
                 std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !accel::IsValidLease(lease)) {
            return false;
        }

        if (!memory_pool_->Release(lease)) {
            return false;
        }

        last_released_lease_ = lease;
        ++release_count_;
        return true;
    }

    bool ConsumeForTest(const accel::BufferLease& lease) {
        return Consume(lease, std::integral_constant<std::size_t, 0>{});
    }

    [[nodiscard]] std::size_t ReleaseCount() const noexcept {
        return release_count_;
    }

    [[nodiscard]] const accel::BufferLease& LastReleasedLease() const noexcept {
        return last_released_lease_;
    }

private:
    std::shared_ptr<capabilities::ICudaMemoryPoolCapability> memory_pool_;
    accel::BufferLease last_released_lease_{};
    std::size_t release_count_{0};
};

} // namespace graph::gpu::cuda::nodes