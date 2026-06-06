// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include <memory>

#include "graph/CapabilityBus.hpp"

namespace graph::gpu {

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

graph::CapabilityBus& GetSharedGpuCapabilityBus();

std::shared_ptr<graph::CapabilityBus> OverrideSharedGpuCapabilityBusForTesting(
    std::shared_ptr<graph::CapabilityBus> replacement_bus);

class ScopedGpuCapabilityBusOverride {
public:
    explicit ScopedGpuCapabilityBusOverride(
        std::shared_ptr<graph::CapabilityBus> replacement_bus)
        : previous_bus_(OverrideSharedGpuCapabilityBusForTesting(
              std::move(replacement_bus))) {}

    ScopedGpuCapabilityBusOverride(const ScopedGpuCapabilityBusOverride&) = delete;
    ScopedGpuCapabilityBusOverride& operator=(const ScopedGpuCapabilityBusOverride&) = delete;

    ~ScopedGpuCapabilityBusOverride() {
        OverrideSharedGpuCapabilityBusForTesting(std::move(previous_bus_));
    }

private:
    std::shared_ptr<graph::CapabilityBus> previous_bus_;
};

} // namespace graph::gpu
