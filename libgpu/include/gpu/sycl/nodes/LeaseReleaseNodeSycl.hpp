/**
 * @file LeaseReleaseNodeSycl.hpp
 * @brief Lease Release Node SYCL GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace graph::gpu::sycl::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class LeaseReleaseNodeSycl
 * @brief Lease Release Node SYCL graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class LeaseReleaseNodeSycl
    : public graph::NamedSinkNode<LeaseReleaseNodeSycl, accel::BufferLease>,
      public graph::IGpuCapabilityBinding {
public:
    /**
     * @brief Executes the Lease Release Node Sycl operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    LeaseReleaseNodeSycl() = default;

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        memory_pool_ = capability_bus.Get<capabilities::ISyclMemoryPoolCapability>();
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
    std::shared_ptr<capabilities::ISyclMemoryPoolCapability> memory_pool_;
    accel::BufferLease last_released_lease_{};
    std::size_t release_count_{0};
};

} // namespace graph::gpu::sycl::nodes
