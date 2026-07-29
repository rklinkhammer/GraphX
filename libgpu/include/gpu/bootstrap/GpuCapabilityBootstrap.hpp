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
    bool enable_cpu{true};
#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    bool enable_cuda{true};
#else
    bool enable_cuda{false};
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

// Registers accelerator sessions and transitional backend capabilities in the
// graph-owned capability bus.
// Registration is compile-time gated by GRAPHX_ENABLE_* feature defines.
void RegisterDefaultGpuCapabilities(
    graph::CapabilityBus& bus,
    const GpuCapabilityBootstrapOptions& options = {});

} // namespace graph::gpu
