// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

#include <algorithm>
#include <cstring>

namespace graph::gpu::metal::capabilities {

bool DefaultMetalContextCapability::SelectDevice(std::uint32_t device_id) {
    current_device_id_ = device_id;
    return true;
}

std::uint32_t DefaultMetalContextCapability::CurrentDevice() const {
    return current_device_id_;
}

std::uint64_t DefaultMetalContextCapability::CreateCommandQueue() {
    const auto id = next_queue_id_++;
    queues_.insert(id);
    return id;
}

void DefaultMetalContextCapability::DestroyCommandQueue(std::uint64_t queue_id) {
    queues_.erase(queue_id);
}

std::uint64_t DefaultMetalContextCapability::CreateEvent() {
    const auto id = next_event_id_++;
    events_.insert(id);
    completed_events_.erase(id);
    return id;
}

void DefaultMetalContextCapability::DestroyEvent(std::uint64_t event_id) {
    events_.erase(event_id);
    completed_events_.erase(event_id);
}

bool DefaultMetalContextCapability::IsEventComplete(std::uint64_t event_id) const {
    if (!events_.contains(event_id)) {
        return false;
    }
    return completed_events_.contains(event_id);
}

bool DefaultMetalContextCapability::WaitEvent(std::uint64_t event_id, std::uint64_t) {
    if (!events_.contains(event_id)) {
        return false;
    }
    completed_events_.insert(event_id);
    return true;
}

bool DefaultMetalMemoryPoolCapability::AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                                      accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    const auto allocation_id = next_allocation_id_++;
    auto [it, inserted] = device_allocations_.emplace(allocation_id,
                                                       std::vector<std::byte>(bytes));
    if (!inserted) {
        return false;
    }

    ++allocation_count_;
    live_device_bytes_ += bytes;
    peak_device_bytes_ = std::max(peak_device_bytes_, live_device_bytes_);

    out_lease.pool_id = 21;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::Metal;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = static_cast<void*>(it->second.data());
    return true;
}

bool DefaultMetalMemoryPoolCapability::AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                                      accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    const auto allocation_id = next_allocation_id_++;
    auto [it, inserted] = shared_allocations_.emplace(allocation_id,
                                                      std::vector<std::byte>(bytes));
    if (!inserted) {
        return false;
    }

    ++allocation_count_;
    live_shared_bytes_ += bytes;
    peak_shared_bytes_ = std::max(peak_shared_bytes_, live_shared_bytes_);

    out_lease.pool_id = 22;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::Metal;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = static_cast<void*>(it->second.data());
    return true;
}

bool DefaultMetalMemoryPoolCapability::AllocateHost(std::uint64_t bytes,
                                                    accel::BufferLease& out_lease) {
    if (bytes == 0) {
        return false;
    }

    const auto allocation_id = next_allocation_id_++;
    auto [it, inserted] = host_allocations_.emplace(allocation_id,
                                                    std::vector<std::byte>(bytes));
    if (!inserted) {
        return false;
    }

    ++allocation_count_;
    live_host_bytes_ += bytes;
    peak_host_bytes_ = std::max(peak_host_bytes_, live_host_bytes_);

    out_lease.pool_id = 23;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::Metal;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = static_cast<void*>(it->second.data());
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
}

bool DefaultMetalMemoryPoolCapability::Release(const accel::BufferLease& lease) {
    if (lease.allocation_id == 0) {
        return false;
    }

    const auto release_from = [this, &lease](auto& allocations, std::uint64_t& live_bytes) {
        const auto it = allocations.find(lease.allocation_id);
        if (it == allocations.end()) {
            return false;
        }

        if (live_bytes >= it->second.size()) {
            live_bytes -= static_cast<std::uint64_t>(it->second.size());
        } else {
            live_bytes = 0;
        }
        allocations.erase(it);
        ++release_count_;
        return true;
    };

    const bool released_device = release_from(device_allocations_, live_device_bytes_);
    const bool released_shared = release_from(shared_allocations_, live_shared_bytes_);
    const bool released_host = release_from(host_allocations_, live_host_bytes_);
    return released_device || released_shared || released_host;
}

IMetalMemoryPoolCapability::MemoryPoolSnapshot DefaultMetalMemoryPoolCapability::Snapshot() const {
    MemoryPoolSnapshot out{};
    out.live_device_bytes = live_device_bytes_;
    out.live_shared_bytes = live_shared_bytes_;
    out.live_host_bytes = live_host_bytes_;
    out.peak_device_bytes = peak_device_bytes_;
    out.peak_shared_bytes = peak_shared_bytes_;
    out.peak_host_bytes = peak_host_bytes_;
    out.allocation_count = allocation_count_;
    out.release_count = release_count_;
    return out;
}

bool DefaultMetalTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                                 accel::DeviceBufferView& dst,
                                                 std::uint64_t queue_id,
                                                 accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultMetalTransferCapability::EnqueueD2H(const accel::DeviceBufferView& src,
                                                 accel::HostPinnedBufferView& dst,
                                                 std::uint64_t queue_id,
                                                 accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
}

bool DefaultMetalTransferCapability::EnqueueD2D(const accel::DeviceBufferView& src,
                                                 accel::DeviceBufferView& dst,
                                                 std::uint64_t queue_id,
                                                 accel::TransferTicket& out_ticket) {
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.device_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
}

bool DefaultMetalKernelCapability::RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) {
    if (descriptor.kernel_id == 0 || descriptor.function_name.empty()) {
        return false;
    }

    RegisteredKernelExecution execution{};
    execution.arg_count = descriptor.arg_layout.empty()
        ? 0U
        : static_cast<std::uint32_t>(descriptor.arg_layout.size());
    execution.dispatch.grid_x = std::max(1U, descriptor.dispatch.default_grid_x);
    execution.dispatch.grid_y = std::max(1U, descriptor.dispatch.default_grid_y);
    execution.dispatch.grid_z = std::max(1U, descriptor.dispatch.default_grid_z);
    execution.dispatch.block_x = std::max(1U, descriptor.dispatch.default_block_x);
    execution.dispatch.block_y = std::max(1U, descriptor.dispatch.default_block_y);
    execution.dispatch.block_z = std::max(1U, descriptor.dispatch.default_block_z);

    registered_kernels_[descriptor.kernel_id] = execution;
    return true;
}

bool DefaultMetalKernelCapability::RegisterKernel(std::uint64_t kernel_id,
                                                  std::string_view kernel_name) {
    MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id;
    descriptor.function_name = std::string(kernel_name);
    return RegisterKernelDescriptor(descriptor);
}

bool DefaultMetalKernelCapability::TryGetRegisteredKernelExecution(
    std::uint64_t kernel_id,
    RegisteredKernelExecution& out_execution) const {
    const auto it = registered_kernels_.find(kernel_id);
    if (it == registered_kernels_.end()) {
        return false;
    }

    out_execution = it->second;
    return true;
}

bool DefaultMetalKernelCapability::Launch(const accel::KernelTicket& ticket,
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

void DefaultMetalTelemetryCapability::RecordTransfer(const accel::TransferTicket& ticket,
                                                     std::uint64_t duration_ns) {
    ++transfer_samples_;
    transfer_total_duration_ns_ += duration_ns;
    last_transfer_duration_ns_ = duration_ns;
    if (accel::IsValidView(ticket.src_host) && accel::IsValidView(ticket.dst_device)) {
        ++h2d_transfer_samples_;
    } else if (accel::IsValidView(ticket.src_device) && accel::IsValidView(ticket.dst_host)) {
        ++d2h_transfer_samples_;
    } else if (accel::IsValidView(ticket.src_device) && accel::IsValidView(ticket.dst_device)) {
        ++d2d_transfer_samples_;
    }
}

void DefaultMetalTelemetryCapability::RecordKernel(const accel::KernelTicket&,
                                                   std::uint64_t duration_ns) {
    ++kernel_samples_;
    kernel_total_duration_ns_ += duration_ns;
    last_kernel_duration_ns_ = duration_ns;
}

void DefaultMetalTelemetryCapability::IncrementErrorCounter(std::string_view) {
    ++error_count_;
}

IMetalTelemetryCapability::TelemetrySnapshot DefaultMetalTelemetryCapability::Snapshot() const {
    TelemetrySnapshot out{};
    out.transfer_samples = transfer_samples_;
    out.kernel_samples = kernel_samples_;
    out.error_count = error_count_;
    out.transfer_total_duration_ns = transfer_total_duration_ns_;
    out.kernel_total_duration_ns = kernel_total_duration_ns_;
    out.last_transfer_duration_ns = last_transfer_duration_ns_;
    out.last_kernel_duration_ns = last_kernel_duration_ns_;
    out.h2d_transfer_samples = h2d_transfer_samples_;
    out.d2h_transfer_samples = d2h_transfer_samples_;
    out.d2d_transfer_samples = d2d_transfer_samples_;
    return out;
}

bool DefaultMetalCollectiveCapability::AllReduce(accel::DeviceBufferView& in_out,
                                                 const accel::CollectiveTicket& ticket) {
    (void)in_out;
    (void)ticket;
    return false;
}

bool DefaultMetalCollectiveCapability::AllGather(const accel::DeviceBufferView& input,
                                                 accel::DeviceBufferView& output,
                                                 const accel::CollectiveTicket& ticket) {
    (void)input;
    (void)output;
    (void)ticket;
    return false;
}

bool DefaultMetalCollectiveCapability::ReduceScatter(const accel::DeviceBufferView& input,
                                                     accel::DeviceBufferView& output,
                                                     const accel::CollectiveTicket& ticket) {
    (void)input;
    (void)output;
    (void)ticket;
    return false;
}

} // namespace graph::gpu::metal::capabilities