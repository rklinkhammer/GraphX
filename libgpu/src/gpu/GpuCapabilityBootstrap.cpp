// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/cuda/capabilities/DefaultCudaCapabilities.hpp"
#endif

#if GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#endif

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#endif
#endif

#include <memory>
#include <stdexcept>
#include <string>

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

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    if (options.enable_metal) {
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
        const bool use_native_metal = metal::capabilities::NativeMetalRuntimeAvailable();
        auto native_runtime_context = use_native_metal
            ? metal::capabilities::CreateNativeMetalRuntimeContext()
            : std::shared_ptr<metal::capabilities::NativeMetalRuntimeContext>{};
    if (options.require_native_metal_runtime && !use_native_metal) {
        throw std::runtime_error(
        std::string("Native Metal runtime required but no usable Metal device is available. ") +
        "Diagnostics: " + metal::capabilities::NativeMetalRuntimeDiagnostics());
    }
#else
    if (options.require_native_metal_runtime) {
        throw std::runtime_error(
        "Native Metal runtime required but this build does not include native Metal runtime support.");
    }
#endif

        if (!bus.Has<metal::capabilities::IMetalContextCapability>()) {
            bus.Register<metal::capabilities::IMetalContextCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalContextCapability>(
                          std::make_shared<metal::capabilities::NativeMetalContextCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalContextCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalContextCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalContextCapability>());
#endif
        }
        if (!bus.Has<metal::capabilities::IMetalSharedQueueCapability>()) {
            auto context = bus.Get<metal::capabilities::IMetalContextCapability>();
            if (context != nullptr) {
                bus.Register<metal::capabilities::IMetalSharedQueueCapability>(
                    std::make_shared<metal::capabilities::MetalSharedQueueCapability>(
                        std::move(context)));
            }
        }
        if (!bus.Has<metal::capabilities::IMetalMemoryPoolCapability>()) {
            bus.Register<metal::capabilities::IMetalMemoryPoolCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalMemoryPoolCapability>(
                          std::make_shared<metal::capabilities::NativeMetalMemoryPoolCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalMemoryPoolCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalMemoryPoolCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalMemoryPoolCapability>());
#endif
        }
        if (!bus.Has<metal::capabilities::IMetalTransferCapability>()) {
            bus.Register<metal::capabilities::IMetalTransferCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalTransferCapability>(
                          std::make_shared<metal::capabilities::NativeMetalTransferCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalTransferCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalTransferCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalTransferCapability>());
#endif
        }
        if (!bus.Has<metal::capabilities::IMetalKernelCapability>()) {
            bus.Register<metal::capabilities::IMetalKernelCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalKernelCapability>(
                          std::make_shared<metal::capabilities::NativeMetalKernelCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalKernelCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalKernelCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalKernelCapability>());
#endif
        }
        if (!bus.Has<metal::capabilities::IMetalTelemetryCapability>()) {
            bus.Register<metal::capabilities::IMetalTelemetryCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalTelemetryCapability>(
                          std::make_shared<metal::capabilities::NativeMetalTelemetryCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalTelemetryCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalTelemetryCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalTelemetryCapability>());
#endif
        }
        if (!bus.Has<metal::capabilities::IMetalCollectiveCapability>()) {
            bus.Register<metal::capabilities::IMetalCollectiveCapability>(
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
                use_native_metal
                    ? std::static_pointer_cast<metal::capabilities::IMetalCollectiveCapability>(
                          std::make_shared<metal::capabilities::NativeMetalCollectiveCapability>(
                            native_runtime_context))
                    : std::static_pointer_cast<metal::capabilities::IMetalCollectiveCapability>(
                          std::make_shared<metal::capabilities::DefaultMetalCollectiveCapability>()));
#else
                std::make_shared<metal::capabilities::DefaultMetalCollectiveCapability>());
#endif
        }
    }
#else
    (void)options.enable_metal;
    (void)options.require_native_metal_runtime;
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
