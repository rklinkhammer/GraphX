/**
 * @file LeaseReleaseNode.hpp
 * @brief Lease Release Node GPU acceleration support.
 *
 * @details Provides CUDA acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class LeaseReleaseNode
 * @brief Lease Release Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class LeaseReleaseNode
    : public graph::NamedSinkNode<LeaseReleaseNode, accel::BufferLease>,
      public graph::IGpuCapabilityBinding {
public:
    /**
     * @brief Executes the Lease Release Node operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    LeaseReleaseNode() = default;

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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

    /**
     * @brief Processes data through the Consume For Test operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param lease Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool ConsumeForTest(const accel::BufferLease& lease) {
        return Consume(lease, std::integral_constant<std::size_t, 0>{});
    }

    /**
     * @brief Executes the Release Count operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::size_t ReleaseCount() const noexcept {
        return release_count_;
    }

    /**
     * @brief Executes the Last Released Lease operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] const accel::BufferLease& LastReleasedLease() const noexcept {
        return last_released_lease_;
    }

private:
    std::shared_ptr<capabilities::ICudaMemoryPoolCapability> memory_pool_;
    accel::BufferLease last_released_lease_{};
    std::size_t release_count_{0};
};

} // namespace graph::gpu::cuda::nodes
