/**
 * @file DefaultMetalCapabilities.hpp
 * @brief Default Metal Capabilities GPU acceleration support.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/metal/capabilities/IMetalCapabilities.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace graph::gpu::metal::capabilities {

/**
 * @class DefaultMetalContextCapability
 * @brief Default Metal Context Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalContextCapability final : public IMetalContextCapability {
public:
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
    std::uint32_t current_device_id_{0};
    std::uint64_t next_queue_id_{1};
    std::uint64_t next_event_id_{1};
    std::unordered_set<std::uint64_t> queues_{};
    std::unordered_set<std::uint64_t> events_{};
    std::unordered_set<std::uint64_t> completed_events_{};
};

/**
 * @class DefaultMetalMemoryPoolCapability
 * @brief Default Metal Memory Pool Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalMemoryPoolCapability final : public IMetalMemoryPoolCapability {
public:
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
    std::uint64_t next_allocation_id_{1};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> shared_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_{};
    std::uint64_t live_device_bytes_{0};
    std::uint64_t live_shared_bytes_{0};
    std::uint64_t live_host_bytes_{0};
    std::uint64_t peak_device_bytes_{0};
    std::uint64_t peak_shared_bytes_{0};
    std::uint64_t peak_host_bytes_{0};
    std::uint64_t allocation_count_{0};
    std::uint64_t release_count_{0};
};

/**
 * @class DefaultMetalTransferCapability
 * @brief Default Metal Transfer Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalTransferCapability final : public IMetalTransferCapability {
public:
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
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{1};
};

/**
 * @class DefaultMetalKernelCapability
 * @brief Default Metal Kernel Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalKernelCapability final : public IMetalKernelCapability,
                                           public IMetalKernelDescriptorCapability {
public:
/**
 * @brief Register kernel descriptor.
 * @param descriptor Parameter for register kernel descriptor.
 * @return Result of the operation.
 */
    bool RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) override;

    bool RegisterKernel(std::uint64_t kernel_id,
                        std::string_view kernel_name) override;

    bool TryGetRegisteredKernelExecution(
        std::uint64_t kernel_id,
        RegisteredKernelExecution& out_execution) const override;

    bool Launch(const accel::KernelTicket& ticket,
                void* const* args,
                std::size_t arg_count) override;

private:
    std::unordered_map<std::uint64_t, RegisteredKernelExecution> registered_kernels_{};
};

/**
 * @class DefaultMetalTelemetryCapability
 * @brief Default Metal Telemetry Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalTelemetryCapability final : public IMetalTelemetryCapability {
public:
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

    [[nodiscard]] std::uint64_t TransferSamples() const { return transfer_samples_; }
    [[nodiscard]] std::uint64_t KernelSamples() const { return kernel_samples_; }
    [[nodiscard]] std::uint64_t ErrorCount() const { return error_count_; }

private:
    std::uint64_t transfer_samples_{0};
    std::uint64_t kernel_samples_{0};
    std::uint64_t error_count_{0};
    std::uint64_t transfer_total_duration_ns_{0};
    std::uint64_t kernel_total_duration_ns_{0};
    std::uint64_t last_transfer_duration_ns_{0};
    std::uint64_t last_kernel_duration_ns_{0};
    std::uint64_t h2d_transfer_samples_{0};
    std::uint64_t d2h_transfer_samples_{0};
    std::uint64_t d2d_transfer_samples_{0};
};

/**
 * @class DefaultMetalCollectiveCapability
 * @brief Default Metal Collective Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultMetalCollectiveCapability final : public IMetalCollectiveCapability {
public:
    bool AllReduce(accel::DeviceBufferView& in_out,
                   const accel::CollectiveTicket& ticket) override;
    bool AllGather(const accel::DeviceBufferView& input,
                   accel::DeviceBufferView& output,
                   const accel::CollectiveTicket& ticket) override;
    bool ReduceScatter(const accel::DeviceBufferView& input,
                       accel::DeviceBufferView& output,
                       const accel::CollectiveTicket& ticket) override;
};

} // namespace graph::gpu::metal::capabilities
