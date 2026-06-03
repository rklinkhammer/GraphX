// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/cuda/capabilities/DefaultCudaCapabilities.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

namespace graph::gpu::cuda::capabilities {

bool DefaultCudaContextCapability::SetDevice(std::uint32_t device_id) {
    current_device_id_ = device_id;
    return true;
}

std::uint32_t DefaultCudaContextCapability::CurrentDevice() const {
    return current_device_id_;
}

std::uint64_t DefaultCudaContextCapability::CreateStream() {
    const auto id = next_stream_id_++;
    streams_.insert(id);
    return id;
}

void DefaultCudaContextCapability::DestroyStream(std::uint64_t stream_id) {
    streams_.erase(stream_id);
}

std::uint64_t DefaultCudaContextCapability::CreateEvent() {
    const auto id = next_event_id_++;
    events_.insert(id);
    return id;
}

void DefaultCudaContextCapability::DestroyEvent(std::uint64_t event_id) {
    events_.erase(event_id);
}

bool DefaultCudaMemoryPoolCapability::AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    out_lease.pool_id = 1;
    out_lease.allocation_id = next_allocation_id_++;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::CUDA;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(out_lease.allocation_id));
    return true;
}

bool DefaultCudaMemoryPoolCapability::AllocatePinnedHost(std::uint64_t bytes,
                                                         accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    out_lease.pool_id = 2;
    out_lease.allocation_id = next_allocation_id_++;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::CUDA;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(out_lease.allocation_id));
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
}

bool DefaultCudaMemoryPoolCapability::Release(const accel::BufferLease& lease) {
    return lease.allocation_id != 0;
}

bool DefaultCudaTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t stream_id,
                                               accel::TransferTicket& out_ticket) {
    if (stream_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::CUDA;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = stream_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultCudaTransferCapability::EnqueueD2H(const accel::DeviceBufferView& src,
                                               accel::HostPinnedBufferView& dst,
                                               std::uint64_t stream_id,
                                               accel::TransferTicket& out_ticket) {
    if (stream_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::CUDA;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = stream_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
}

bool DefaultCudaTransferCapability::EnqueueD2D(const accel::DeviceBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t stream_id,
                                               accel::TransferTicket& out_ticket) {
    if (stream_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::CUDA;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = stream_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultCudaKernelCapability::RegisterKernel(std::uint64_t kernel_id,
                                                 std::string_view kernel_name) {
    if (kernel_id == 0 || kernel_name.empty()) {
        return false;
    }
    registered_kernels_.insert(kernel_id);
    return true;
}

bool DefaultCudaKernelCapability::Launch(const accel::KernelTicket& ticket,
                                         void* const* args,
                                         std::size_t arg_count) {
    if (!accel::IsValidKernelTicket(ticket) || args == nullptr) {
        return false;
    }
    if (arg_count != ticket.arg_count) {
        return false;
    }
    return registered_kernels_.contains(ticket.kernel_id);
}

void DefaultCudaTelemetryCapability::RecordTransfer(const accel::TransferTicket&,
                                                    std::uint64_t) {
    ++transfer_samples_;
}

void DefaultCudaTelemetryCapability::RecordKernel(const accel::KernelTicket&,
                                                  std::uint64_t) {
    ++kernel_samples_;
}

void DefaultCudaTelemetryCapability::IncrementErrorCounter(std::string_view) {
    ++error_count_;
}

bool DefaultCudaCollectiveCapability::AllReduce(accel::DeviceBufferView& in_out,
                                                const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(in_out) && accel::IsValidCollectiveTicket(ticket);
}

bool DefaultCudaCollectiveCapability::AllGather(const accel::DeviceBufferView& input,
                                                accel::DeviceBufferView& output,
                                                const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(input) && accel::IsValidView(output) &&
           accel::IsValidCollectiveTicket(ticket);
}

bool DefaultCudaCollectiveCapability::ReduceScatter(const accel::DeviceBufferView& input,
                                                    accel::DeviceBufferView& output,
                                                    const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(input) && accel::IsValidView(output) &&
           accel::IsValidCollectiveTicket(ticket);
}

} // namespace graph::gpu::cuda::capabilities
