// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace graph::gpu::cuda::capabilities {

class ICudaContextCapability {
public:
    virtual ~ICudaContextCapability() = default;

    virtual bool SetDevice(std::uint32_t device_id) = 0;
    virtual std::uint32_t CurrentDevice() const = 0;

    virtual std::uint64_t CreateStream() = 0;
    virtual void DestroyStream(std::uint64_t stream_id) = 0;

    virtual std::uint64_t CreateEvent() = 0;
    virtual void DestroyEvent(std::uint64_t event_id) = 0;
};

class ICudaMemoryPoolCapability {
public:
    virtual ~ICudaMemoryPoolCapability() = default;

    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocatePinnedHost(std::uint64_t bytes,
                                    accel::BufferLease& out_lease) = 0;
    virtual bool Release(const accel::BufferLease& lease) = 0;
};

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

class ICudaKernelCapability {
public:
    virtual ~ICudaKernelCapability() = default;

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

class ICudaTelemetryCapability {
public:
    virtual ~ICudaTelemetryCapability() = default;

    virtual void RecordTransfer(const accel::TransferTicket& ticket,
                                std::uint64_t duration_ns) = 0;
    virtual void RecordKernel(const accel::KernelTicket& ticket,
                              std::uint64_t duration_ns) = 0;
    virtual void IncrementErrorCounter(std::string_view error_code) = 0;
};

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
