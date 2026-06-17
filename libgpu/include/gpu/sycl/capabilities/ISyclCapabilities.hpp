/**
 * @file ISyclCapabilities.hpp
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

namespace graph::gpu::sycl::capabilities {

/**
 * @class ISyclContextCapability
 * @brief ISyclContextCapability class.
 */
/**
 * @class ISyclContextCapability
 * @brief I sycl context capability implementation for GraphX.
 */
class ISyclContextCapability {
public:
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
 * @brief ISyclMemoryPoolCapability class.
 */
/**
 * @class ISyclMemoryPoolCapability
 * @brief I sycl memory pool capability implementation for GraphX.
 */
class ISyclMemoryPoolCapability {
public:
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
 * @brief ISyclTransferCapability class.
 */
/**
 * @class ISyclTransferCapability
 * @brief I sycl transfer capability implementation for GraphX.
 */
class ISyclTransferCapability {
public:
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
 * @brief ISyclKernelCapability class.
 */
/**
 * @class ISyclKernelCapability
 * @brief I sycl kernel capability implementation for GraphX.
 */
class ISyclKernelCapability {
public:
    virtual ~ISyclKernelCapability() = default;

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

/**
 * @class ISyclTelemetryCapability
 * @brief ISyclTelemetryCapability class.
 */
/**
 * @class ISyclTelemetryCapability
 * @brief I sycl telemetry capability implementation for GraphX.
 */
class ISyclTelemetryCapability {
public:
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
 * @brief ISyclCollectiveCapability class.
 */
/**
 * @class ISyclCollectiveCapability
 * @brief I sycl collective capability implementation for GraphX.
 */
class ISyclCollectiveCapability {
public:
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
