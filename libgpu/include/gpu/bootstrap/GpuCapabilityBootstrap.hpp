/**
 * @file GpuCapabilityBootstrap.hpp
 * @brief GPU Capability Bootstrap GPU acceleration support.
 *
 * @details Provides GPU capability bootstrap and backend integration support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include <memory>

#include "graph/CapabilityBus.hpp"

namespace graph::gpu {

/**

 * @struct GpuCapabilityBootstrapOptions

 * @brief GPU Capability Bootstrap Options data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct GpuCapabilityBootstrapOptions {
#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    bool enable_cuda{true};
#else
    bool enable_cuda{false};
#endif

#if GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    bool enable_sycl{true};
#else
    bool enable_sycl{false};
#endif

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    bool enable_metal{true};
#else
    bool enable_metal{false};
#endif

#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    bool require_native_metal_runtime{true};
#else
    bool require_native_metal_runtime{false};
#endif
};

// Registers default GPU capabilities in the shared capability bus.
// Registration is compile-time gated by GRAPHX_ENABLE_* feature defines.
void RegisterDefaultGpuCapabilities(
    graph::CapabilityBus& bus,
    const GpuCapabilityBootstrapOptions& options = {});

/**
 * @brief Get shared gpu capability bus.
 * @return Result of the operation.
 */
graph::CapabilityBus& GetSharedGpuCapabilityBus();

std::shared_ptr<graph::CapabilityBus> OverrideSharedGpuCapabilityBusForTesting(
    std::shared_ptr<graph::CapabilityBus> replacement_bus);

/**
 * @class ScopedGpuCapabilityBusOverride
 * @brief Scoped GPU Capability Bus Override capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ScopedGpuCapabilityBusOverride {
public:
    explicit ScopedGpuCapabilityBusOverride(
        std::shared_ptr<graph::CapabilityBus> replacement_bus)
        : previous_bus_(OverrideSharedGpuCapabilityBusForTesting(
              std::move(replacement_bus))) {}

    /**
     * @brief Executes the Scoped GPU Capability Bus Override operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ScopedGpuCapabilityBusOverride Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    ScopedGpuCapabilityBusOverride(const ScopedGpuCapabilityBusOverride&) = delete;
    /**
     * @brief Executes the Operator overload operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ScopedGpuCapabilityBusOverride Input or configuration value consumed by the method.
     */
    ScopedGpuCapabilityBusOverride& operator=(const ScopedGpuCapabilityBusOverride&) = delete;

    /**
     * @brief Releases resources owned by Scoped GPU Capability Bus Override.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~ScopedGpuCapabilityBusOverride() {
        /**
         * @brief Executes the Override Shared GPU Capability Bus For Testing operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        OverrideSharedGpuCapabilityBusForTesting(std::move(previous_bus_));
    }

private:
    std::shared_ptr<graph::CapabilityBus> previous_bus_;
};

} // namespace graph::gpu
