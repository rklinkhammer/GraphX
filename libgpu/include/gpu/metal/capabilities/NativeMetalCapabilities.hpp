/**
 * @file NativeMetalCapabilities.hpp
 * @brief Native Metal Capabilities GPU acceleration support.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

/**

 * @class NativeMetalRuntimeContext

 * @brief Native Metal Runtime Context type.

 *

 * @details Part of the GraphX public API for libgpu. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.

 */

class NativeMetalRuntimeContext;

/**
 * @brief Create native metal runtime context.
 * @return Result of the operation.
 */
std::shared_ptr<NativeMetalRuntimeContext> CreateNativeMetalRuntimeContext();

// Native Metal capabilities currently delegate dataflow behavior to the
// existing contract-safe defaults while native runtime plumbing is brought up.
/**
 * @class NativeMetalContextCapability
 * @brief Native Metal Context Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class NativeMetalContextCapability final : public IMetalContextCapability {
public:
    explicit NativeMetalContextCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

/**
 * @brief Select device.
 * @param device_id Parameter for select device.
 * @return Result of the operation.
 */
    bool SelectDevice(std::uint32_t device_id) override;
/**
 * @brief Current device.
 * @return Result of the operation.
 */
    std::uint32_t CurrentDevice() const override;
/**
 * @brief Create command queue.
 * @return Result of the operation.
 */
    std::uint64_t CreateCommandQueue() override;
/**
 * @brief Destroy command queue.
 * @param queue_id Parameter for destroy command queue.
 */
    void DestroyCommandQueue(std::uint64_t queue_id) override;
/**
 * @brief Create event.
 * @return Result of the operation.
 */
    std::uint64_t CreateEvent() override;
/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
    void DestroyEvent(std::uint64_t event_id) override;
/**
 * @brief Is event complete.
 * @param event_id Parameter for is event complete.
 * @return Result of the operation.
 */
    bool IsEventComplete(std::uint64_t event_id) const override;
/**
 * @brief Wait event.
 * @param event_id Parameter for wait event.
 * @param timeout_ms Parameter for wait event.
 * @return Result of the operation.
 */
    bool WaitEvent(std::uint64_t event_id, std::uint64_t timeout_ms) override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

/**
 * @class NativeMetalMemoryPoolCapability
 * @brief Native Metal Memory Pool Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
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
/**
 * @brief Release.
 * @param lease Parameter for release.
 * @return Result of the operation.
 */
    bool Release(const accel::BufferLease& lease) override;
    /**
     * @brief Executes the Snapshot operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] MemoryPoolSnapshot Snapshot() const override;

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

/**
 * @class NativeMetalTransferCapability
 * @brief Native Metal Transfer Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
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

/**
 * @class NativeMetalKernelCapability
 * @brief Native Metal Kernel Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class NativeMetalKernelCapability final : public IMetalKernelCapability,
                                          public IMetalKernelDescriptorCapability {
public:
    explicit NativeMetalKernelCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

/**
 * @brief Register kernel descriptor.
 * @param descriptor Parameter for register kernel descriptor.
 * @return Result of the operation.
 */
    bool RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) override;

/**
 * @brief Register kernel.
 * @param descriptor Parameter for register kernel.
 * @return Result of the operation.
 */
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

/**
 * @class NativeMetalTelemetryCapability
 * @brief Native Metal Telemetry Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class NativeMetalTelemetryCapability final : public IMetalTelemetryCapability {
public:
    explicit NativeMetalTelemetryCapability(
        std::shared_ptr<NativeMetalRuntimeContext> runtime_context =
            CreateNativeMetalRuntimeContext());

    void RecordTransfer(const accel::TransferTicket& ticket,
                        std::uint64_t duration_ns) override;
    void RecordKernel(const accel::KernelTicket& ticket,
                      std::uint64_t duration_ns) override;
/**
 * @brief Increment error counter.
 * @param error_code Parameter for increment error counter.
 */
    void IncrementErrorCounter(std::string_view error_code) override;
    /**
     * @brief Executes the Snapshot operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] TelemetrySnapshot Snapshot() const override;

    /**
     * @brief Processes data through the Transfer Samples operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::uint64_t TransferSamples() const;
    /**
     * @brief Executes the Kernel Samples operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::uint64_t KernelSamples() const;
    /**
     * @brief Executes the Error Count operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::uint64_t ErrorCount() const;

#if GRAPHX_ENABLE_GPU_TEST_HOOKS
/**
 * @brief Reset for testing.
 */
    void ResetForTesting();
#endif

private:
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context_;
};

/**
 * @class NativeMetalCollectiveCapability
 * @brief Native Metal Collective Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
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

/**
 * @brief Native metal runtime available.
 * @return Result of the operation.
 */
bool NativeMetalRuntimeAvailable();
/**
 * @brief Native metal runtime diagnostics.
 * @return Result of the operation.
 */
std::string NativeMetalRuntimeDiagnostics();

} // namespace graph::gpu::metal::capabilities
