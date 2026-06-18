/**
 * @file ISyclCapabilities.hpp
 * @brief Isycl Capabilities GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace graph::gpu::sycl::capabilities {

/**
 * @class ISyclContextCapability
 * @brief Isycl Context Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclContextCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Context Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclContextCapability() = default;

/**
 * @brief Select device.
 * @param device_id Parameter for select device.
 * @return Result of the operation.
 */
    virtual bool SelectDevice(std::uint32_t device_id) = 0;
/**
 * @brief Current device.
 * @return Result of the operation.
 */
    virtual std::uint32_t CurrentDevice() const = 0;

/**
 * @brief Create queue.
 * @return Result of the operation.
 */
    virtual std::uint64_t CreateQueue() = 0;
/**
 * @brief Destroy queue.
 * @param queue_id Parameter for destroy queue.
 */
    virtual void DestroyQueue(std::uint64_t queue_id) = 0;

/**
 * @brief Create event.
 * @return Result of the operation.
 */
    virtual std::uint64_t CreateEvent() = 0;
/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
    virtual void DestroyEvent(std::uint64_t event_id) = 0;
};

/**
 * @class ISyclMemoryPoolCapability
 * @brief Isycl Memory Pool Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclMemoryPoolCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Memory Pool Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclMemoryPoolCapability() = default;

    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateHost(std::uint64_t bytes,
                              accel::BufferLease& out_lease) = 0;
/**
 * @brief Release.
 * @param lease Parameter for release.
 * @return Result of the operation.
 */
    virtual bool Release(const accel::BufferLease& lease) = 0;
};

/**
 * @class ISyclTransferCapability
 * @brief Isycl Transfer Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclTransferCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Transfer Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclTransferCapability() = default;

    virtual bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2H(const accel::DeviceBufferView& src,
                            accel::HostPinnedBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2D(const accel::DeviceBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;
};

/**
 * @class ISyclKernelCapability
 * @brief Isycl Kernel Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclKernelCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Kernel Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclKernelCapability() = default;

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

/**
 * @class ISyclTelemetryCapability
 * @brief Isycl Telemetry Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclTelemetryCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Telemetry Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclTelemetryCapability() = default;

    virtual void RecordTransfer(const accel::TransferTicket& ticket,
                                std::uint64_t duration_ns) = 0;
    virtual void RecordKernel(const accel::KernelTicket& ticket,
                              std::uint64_t duration_ns) = 0;
/**
 * @brief Increment error counter.
 * @param error_code Parameter for increment error counter.
 */
    virtual void IncrementErrorCounter(std::string_view error_code) = 0;
};

/**
 * @class ISyclCollectiveCapability
 * @brief Isycl Collective Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ISyclCollectiveCapability {
public:
    /**
     * @brief Releases resources owned by Isycl Collective Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ISyclCollectiveCapability() = default;

    virtual bool AllReduce(accel::DeviceBufferView& in_out,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool AllGather(const accel::DeviceBufferView& input,
                           accel::DeviceBufferView& output,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool ReduceScatter(const accel::DeviceBufferView& input,
                               accel::DeviceBufferView& output,
                               const accel::CollectiveTicket& ticket) = 0;
};

} // namespace graph::gpu::sycl::capabilities
