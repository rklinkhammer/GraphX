// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace graph::gpu::metal::capabilities {

class NativeMetalRuntimeContext;

std::shared_ptr<NativeMetalRuntimeContext> CreateNativeMetalRuntimeContext();

// Native Metal capabilities currently delegate dataflow behavior to the
// existing contract-safe defaults while native runtime plumbing is brought up.
class NativeMetalContextCapability final : public IMetalContextCapability {
public:
    explicit NativeMetalContextCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    bool SelectDevice(std::uint32_t device_id) override;
    std::uint32_t CurrentDevice() const override;
    std::uint64_t CreateCommandQueue() override;
    void DestroyCommandQueue(std::uint64_t queue_id) override;
    std::uint64_t CreateEvent() override;
    void DestroyEvent(std::uint64_t event_id) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

class NativeMetalMemoryPoolCapability final : public IMetalMemoryPoolCapability {
public:
    explicit NativeMetalMemoryPoolCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                        accel::BufferLease& out_lease) override;
    bool AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                        accel::BufferLease& out_lease) override;
    bool AllocateHost(std::uint64_t bytes,
                      accel::BufferLease& out_lease) override;
    bool Release(const accel::BufferLease& lease) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

class NativeMetalTransferCapability final : public IMetalTransferCapability {
public:
    explicit NativeMetalTransferCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                    accel::DeviceBufferView& dst,
                    std::uint64_t queue_id,
                    accel::TransferTicket& out_ticket) override;

    bool EnqueueD2H(const accel::DeviceBufferView& src,
                    accel::HostPinnedBufferView& dst,
                    std::uint64_t queue_id,
                    accel::TransferTicket& out_ticket) override;

    bool EnqueueD2D(const accel::DeviceBufferView& src,
                    accel::DeviceBufferView& dst,
                    std::uint64_t queue_id,
                    accel::TransferTicket& out_ticket) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

class NativeMetalKernelCapability final : public IMetalKernelCapability,
                                          public IMetalKernelDescriptorCapability {
public:
    explicit NativeMetalKernelCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    bool RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) override;

    bool RegisterKernel(const MetalKernelDescriptor& descriptor);

    // Registers one of GraphX built-in kernels by function name.
    bool RegisterKernelBuiltin(std::uint64_t kernel_id,
                               std::string_view function_name);

    // Registers an inline MSL kernel source and creates a pipeline for function_name.
    bool RegisterKernelFromSource(std::uint64_t kernel_id,
                                  std::string_view function_name,
                                  std::string_view msl_source);

    // Loads a precompiled .metallib from disk and creates a pipeline for function_name.
    bool RegisterKernelFromMetallib(std::uint64_t kernel_id,
                                    std::string_view function_name,
                                    std::string_view metallib_path);

    bool RegisterKernel(std::uint64_t kernel_id,
                        std::string_view kernel_name) override;

    bool TryGetRegisteredKernelExecution(
        std::uint64_t kernel_id,
        RegisteredKernelExecution& out_execution) const override;

    bool Launch(const accel::KernelTicket& ticket,
                void* const* args,
                std::size_t arg_count) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

class NativeMetalTelemetryCapability final : public IMetalTelemetryCapability {
public:
    explicit NativeMetalTelemetryCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    void RecordTransfer(const accel::TransferTicket& ticket,
                        std::uint64_t duration_ns) override;
    void RecordKernel(const accel::KernelTicket& ticket,
                      std::uint64_t duration_ns) override;
    void IncrementErrorCounter(std::string_view error_code) override;

    [[nodiscard]] std::uint64_t TransferSamples() const;
    [[nodiscard]] std::uint64_t KernelSamples() const;
    [[nodiscard]] std::uint64_t ErrorCount() const;

#if GRAPHX_ENABLE_GPU_TEST_HOOKS
    void ResetForTesting();
#endif

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

class NativeMetalCollectiveCapability final : public IMetalCollectiveCapability {
public:
    explicit NativeMetalCollectiveCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    bool AllReduce(accel::DeviceBufferView& in_out,
                   const accel::CollectiveTicket& ticket) override;
    bool AllGather(const accel::DeviceBufferView& input,
                   accel::DeviceBufferView& output,
                   const accel::CollectiveTicket& ticket) override;
    bool ReduceScatter(const accel::DeviceBufferView& input,
                       accel::DeviceBufferView& output,
                       const accel::CollectiveTicket& ticket) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

bool NativeMetalRuntimeAvailable();
std::string NativeMetalRuntimeDiagnostics();

} // namespace graph::gpu::metal::capabilities
