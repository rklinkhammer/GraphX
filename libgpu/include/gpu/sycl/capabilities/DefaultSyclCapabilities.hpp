/**
 * @file DefaultSyclCapabilities.hpp
 * @brief Default SYCL Capabilities GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__has_include)
#  if __has_include(<sycl/sycl.hpp>)
#    include <sycl/sycl.hpp>
#    define GRAPHX_HAS_SYCL_RUNTIME 1
#  else
#    define GRAPHX_HAS_SYCL_RUNTIME 0
#  endif
#else
#  define GRAPHX_HAS_SYCL_RUNTIME 0
#endif

namespace graph::gpu::sycl::capabilities {

/**

 * @struct SyclRuntimeState

 * @brief SYCL Runtime State data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct SyclRuntimeState;

/**
 * @class DefaultSyclContextCapability
 * @brief Default SYCL Context Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclContextCapability final : public ISyclContextCapability {
public:
    /**
     * @brief Executes the Default Sycl Context Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    DefaultSyclContextCapability();

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
 * @brief Create queue.
 * @return Result of the operation.
 */
    std::uint64_t CreateQueue() override;
/**
 * @brief Destroy queue.
 * @param queue_id Parameter for destroy queue.
 */
    void DestroyQueue(std::uint64_t queue_id) override;

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

private:
#if GRAPHX_HAS_SYCL_RUNTIME
    std::shared_ptr<SyclRuntimeState> runtime_;
#endif
    std::uint32_t current_device_id_{0};
    std::uint64_t next_queue_id_{1};
    std::uint64_t next_event_id_{1};
    std::unordered_set<std::uint64_t> queues_{};
    std::unordered_set<std::uint64_t> events_{};
};

/**
 * @class DefaultSyclMemoryPoolCapability
 * @brief Default SYCL Memory Pool Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclMemoryPoolCapability final : public ISyclMemoryPoolCapability {
public:
    /**
     * @brief Executes the Default Sycl Memory Pool Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    DefaultSyclMemoryPoolCapability();

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

private:
#if GRAPHX_HAS_SYCL_RUNTIME
    std::shared_ptr<SyclRuntimeState> runtime_;
#endif
    std::uint64_t next_allocation_id_{1};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_{};
};

/**
 * @class DefaultSyclTransferCapability
 * @brief Default SYCL Transfer Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclTransferCapability final : public ISyclTransferCapability {
public:
    /**
     * @brief Processes data through the Default Sycl Transfer Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    DefaultSyclTransferCapability();

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
#if GRAPHX_HAS_SYCL_RUNTIME
    std::shared_ptr<SyclRuntimeState> runtime_;
#endif
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{1};
};

/**
 * @class DefaultSyclKernelCapability
 * @brief Default SYCL Kernel Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclKernelCapability final : public ISyclKernelCapability {
public:
    bool RegisterKernel(std::uint64_t kernel_id,
                        std::string_view kernel_name) override;

    bool Launch(const accel::KernelTicket& ticket,
                void* const* args,
                std::size_t arg_count) override;

private:
    std::unordered_set<std::uint64_t> registered_kernels_{};
};

/**
 * @class DefaultSyclTelemetryCapability
 * @brief Default SYCL Telemetry Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclTelemetryCapability final : public ISyclTelemetryCapability {
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

    [[nodiscard]] std::uint64_t TransferSamples() const { return transfer_samples_; }
    [[nodiscard]] std::uint64_t KernelSamples() const { return kernel_samples_; }
    [[nodiscard]] std::uint64_t ErrorCount() const { return error_count_; }

private:
    std::uint64_t transfer_samples_{0};
    std::uint64_t kernel_samples_{0};
    std::uint64_t error_count_{0};
};

/**
 * @class DefaultSyclCollectiveCapability
 * @brief Default SYCL Collective Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class DefaultSyclCollectiveCapability final : public ISyclCollectiveCapability {
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

} // namespace graph::gpu::sycl::capabilities
