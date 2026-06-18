/**
 * @file ICudaCapabilities.hpp
 * @brief Icuda Capabilities GPU acceleration support.
 *
 * @details Provides CUDA acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace graph::gpu::cuda::capabilities {

/**
 * @class ICudaContextCapability
 * @brief Icuda Context Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaContextCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Context Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaContextCapability() = default;

/**
 * @brief Set device.
 * @param device_id Parameter for set device.
 * @return Result of the operation.
 */
    virtual bool SetDevice(std::uint32_t device_id) = 0;
/**
 * @brief Current device.
 * @return Result of the operation.
 */
    virtual std::uint32_t CurrentDevice() const = 0;

/**
 * @brief Create stream.
 * @return Result of the operation.
 */
    virtual std::uint64_t CreateStream() = 0;
/**
 * @brief Destroy stream.
 * @param stream_id Parameter for destroy stream.
 */
    virtual void DestroyStream(std::uint64_t stream_id) = 0;

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
 * @class ICudaMemoryPoolCapability
 * @brief Icuda Memory Pool Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaMemoryPoolCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Memory Pool Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaMemoryPoolCapability() = default;

    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocatePinnedHost(std::uint64_t bytes,
                                    accel::BufferLease& out_lease) = 0;
/**
 * @brief Release.
 * @param lease Parameter for release.
 * @return Result of the operation.
 */
    virtual bool Release(const accel::BufferLease& lease) = 0;
};

/**
 * @class ICudaTransferCapability
 * @brief Icuda Transfer Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaTransferCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Transfer Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaTransferCapability() = default;

    virtual bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t stream_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2H(const accel::DeviceBufferView& src,
                            accel::HostPinnedBufferView& dst,
                            std::uint64_t stream_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2D(const accel::DeviceBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t stream_id,
                            accel::TransferTicket& out_ticket) = 0;
};

/**
 * @class ICudaKernelCapability
 * @brief Icuda Kernel Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaKernelCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Kernel Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaKernelCapability() = default;

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

/**
 * @class ICudaTelemetryCapability
 * @brief Icuda Telemetry Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaTelemetryCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Telemetry Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaTelemetryCapability() = default;

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
 * @class ICudaCollectiveCapability
 * @brief Icuda Collective Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class ICudaCollectiveCapability {
public:
    /**
     * @brief Releases resources owned by Icuda Collective Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~ICudaCollectiveCapability() = default;

    virtual bool AllReduce(accel::DeviceBufferView& in_out,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool AllGather(const accel::DeviceBufferView& input,
                           accel::DeviceBufferView& output,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool ReduceScatter(const accel::DeviceBufferView& input,
                               accel::DeviceBufferView& output,
                               const accel::CollectiveTicket& ticket) = 0;
};

} // namespace graph::gpu::cuda::capabilities
