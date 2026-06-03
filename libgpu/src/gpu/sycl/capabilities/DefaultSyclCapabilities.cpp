// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

namespace graph::gpu::sycl::capabilities {

bool DefaultSyclContextCapability::SelectDevice(std::uint32_t device_id) {
    current_device_id_ = device_id;
    return true;
}

std::uint32_t DefaultSyclContextCapability::CurrentDevice() const {
    return current_device_id_;
}

std::uint64_t DefaultSyclContextCapability::CreateQueue() {
    const auto id = next_queue_id_++;
    queues_.insert(id);
    return id;
}

void DefaultSyclContextCapability::DestroyQueue(std::uint64_t queue_id) {
    queues_.erase(queue_id);
}

std::uint64_t DefaultSyclContextCapability::CreateEvent() {
    const auto id = next_event_id_++;
    events_.insert(id);
    return id;
}

void DefaultSyclContextCapability::DestroyEvent(std::uint64_t event_id) {
    events_.erase(event_id);
}

bool DefaultSyclMemoryPoolCapability::AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    out_lease.pool_id = 11;
    out_lease.allocation_id = next_allocation_id_++;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::SYCL;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(out_lease.allocation_id));
    return true;
}

bool DefaultSyclMemoryPoolCapability::AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
    return AllocateDevice(bytes, device_id, out_lease);
}

bool DefaultSyclMemoryPoolCapability::AllocateHost(std::uint64_t bytes,
                                                   accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    out_lease.pool_id = 12;
    out_lease.allocation_id = next_allocation_id_++;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::SYCL;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(out_lease.allocation_id));
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
}

bool DefaultSyclMemoryPoolCapability::Release(const accel::BufferLease& lease) {
    return lease.allocation_id != 0;
}

bool DefaultSyclTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultSyclTransferCapability::EnqueueD2H(const accel::DeviceBufferView& src,
                                               accel::HostPinnedBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
}

bool DefaultSyclTransferCapability::EnqueueD2D(const accel::DeviceBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultSyclKernelCapability::RegisterKernel(std::uint64_t kernel_id,
                                                 std::string_view kernel_name) {
    if (kernel_id == 0 || kernel_name.empty()) {
        return false;
    }
    registered_kernels_.insert(kernel_id);
    return true;
}

bool DefaultSyclKernelCapability::Launch(const accel::KernelTicket& ticket,
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

void DefaultSyclTelemetryCapability::RecordTransfer(const accel::TransferTicket&,
                                                    std::uint64_t) {
    ++transfer_samples_;
}

void DefaultSyclTelemetryCapability::RecordKernel(const accel::KernelTicket&,
                                                  std::uint64_t) {
    ++kernel_samples_;
}

void DefaultSyclTelemetryCapability::IncrementErrorCounter(std::string_view) {
    ++error_count_;
}

bool DefaultSyclCollectiveCapability::AllReduce(accel::DeviceBufferView& in_out,
                                                const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(in_out) && accel::IsValidCollectiveTicket(ticket);
}

bool DefaultSyclCollectiveCapability::AllGather(const accel::DeviceBufferView& input,
                                                accel::DeviceBufferView& output,
                                                const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(input) && accel::IsValidView(output) &&
           accel::IsValidCollectiveTicket(ticket);
}

bool DefaultSyclCollectiveCapability::ReduceScatter(const accel::DeviceBufferView& input,
                                                    accel::DeviceBufferView& output,
                                                    const accel::CollectiveTicket& ticket) {
    return accel::IsValidView(input) && accel::IsValidView(output) &&
           accel::IsValidCollectiveTicket(ticket);
}

} // namespace graph::gpu::sycl::capabilities
