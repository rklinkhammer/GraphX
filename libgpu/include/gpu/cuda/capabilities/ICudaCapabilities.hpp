/**
 * @file ICudaCapabilities.hpp
 * @brief GraphX source file.
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
 * @brief ICudaContextCapability class.
 */
class ICudaContextCapability {
public:
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
 * @brief ICudaMemoryPoolCapability class.
 */
class ICudaMemoryPoolCapability {
public:
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
 * @brief ICudaTransferCapability class.
 */
class ICudaTransferCapability {
public:
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
 * @brief ICudaKernelCapability class.
 */
class ICudaKernelCapability {
public:
    virtual ~ICudaKernelCapability() = default;

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

/**
 * @class ICudaTelemetryCapability
 * @brief ICudaTelemetryCapability class.
 */
class ICudaTelemetryCapability {
public:
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
 * @brief ICudaCollectiveCapability class.
 */
class ICudaCollectiveCapability {
public:
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
