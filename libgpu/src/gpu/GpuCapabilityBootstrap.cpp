// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/cuda/capabilities/DefaultCudaCapabilities.hpp"
#endif

#if GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#endif

#include <memory>

namespace graph::gpu {

namespace {

std::shared_ptr<graph::CapabilityBus> MakeSharedGpuCapabilityBus() {
    auto bus = std::make_shared<graph::CapabilityBus>();
    RegisterDefaultGpuCapabilities(*bus);
    return bus;
}

std::shared_ptr<graph::CapabilityBus>& SharedGpuCapabilityBusStorage() {
    static auto shared_bus = MakeSharedGpuCapabilityBus();
    return shared_bus;
}

} // namespace

void RegisterDefaultGpuCapabilities(graph::CapabilityBus& bus,
                                    const GpuCapabilityBootstrapOptions& options) {
#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    if (options.enable_cuda) {
        if (!bus.Has<cuda::capabilities::ICudaContextCapability>()) {
            bus.Register<cuda::capabilities::ICudaContextCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaContextCapability>());
        }
        if (!bus.Has<cuda::capabilities::ICudaMemoryPoolCapability>()) {
            bus.Register<cuda::capabilities::ICudaMemoryPoolCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaMemoryPoolCapability>());
        }
        if (!bus.Has<cuda::capabilities::ICudaTransferCapability>()) {
            bus.Register<cuda::capabilities::ICudaTransferCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaTransferCapability>());
        }
        if (!bus.Has<cuda::capabilities::ICudaKernelCapability>()) {
            bus.Register<cuda::capabilities::ICudaKernelCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaKernelCapability>());
        }
        if (!bus.Has<cuda::capabilities::ICudaTelemetryCapability>()) {
            bus.Register<cuda::capabilities::ICudaTelemetryCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaTelemetryCapability>());
        }
        if (!bus.Has<cuda::capabilities::ICudaCollectiveCapability>()) {
            bus.Register<cuda::capabilities::ICudaCollectiveCapability>(
                std::make_shared<cuda::capabilities::DefaultCudaCollectiveCapability>());
        }
    }
#else
    (void)options.enable_cuda;
#endif

#if GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    if (options.enable_sycl) {
        if (!bus.Has<sycl::capabilities::ISyclContextCapability>()) {
            bus.Register<sycl::capabilities::ISyclContextCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclContextCapability>());
        }
        if (!bus.Has<sycl::capabilities::ISyclMemoryPoolCapability>()) {
            bus.Register<sycl::capabilities::ISyclMemoryPoolCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclMemoryPoolCapability>());
        }
        if (!bus.Has<sycl::capabilities::ISyclTransferCapability>()) {
            bus.Register<sycl::capabilities::ISyclTransferCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclTransferCapability>());
        }
        if (!bus.Has<sycl::capabilities::ISyclKernelCapability>()) {
            bus.Register<sycl::capabilities::ISyclKernelCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclKernelCapability>());
        }
        if (!bus.Has<sycl::capabilities::ISyclTelemetryCapability>()) {
            bus.Register<sycl::capabilities::ISyclTelemetryCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclTelemetryCapability>());
        }
        if (!bus.Has<sycl::capabilities::ISyclCollectiveCapability>()) {
            bus.Register<sycl::capabilities::ISyclCollectiveCapability>(
                std::make_shared<sycl::capabilities::DefaultSyclCollectiveCapability>());
        }
    }
#else
    (void)options.enable_sycl;
#endif
}

graph::CapabilityBus& GetSharedGpuCapabilityBus() {
    return *SharedGpuCapabilityBusStorage();
}

std::shared_ptr<graph::CapabilityBus> OverrideSharedGpuCapabilityBusForTesting(
    std::shared_ptr<graph::CapabilityBus> replacement_bus) {
    auto& storage = SharedGpuCapabilityBusStorage();
    auto previous_bus = storage;
    storage = replacement_bus ? std::move(replacement_bus) : MakeSharedGpuCapabilityBus();
    return previous_bus;
}

} // namespace graph::gpu
