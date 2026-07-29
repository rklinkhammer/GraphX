// SPDX-License-Identifier: MIT

#include "gpu/session/AcceleratorSessionProviders.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace graph::gpu {
namespace {

class DirectSession final : public IAcceleratorSession,
                            public IMemoryCapability,
                            public ITransferCapability,
                            public IEventCapability,
                            public IExecutionCapability,
                            public ITelemetryCapability {
public:
    explicit DirectSession(BackendDescriptor descriptor) : descriptor_(std::move(descriptor)) {}

    const BackendDescriptor& Describe() const override { return descriptor_; }
    IMemoryCapability& Memory() override { return *this; }
    ITransferCapability& Transfer() override { return *this; }
    IEventCapability& Events() override { return *this; }
    IExecutionCapability& Execution() override { return *this; }
    ITelemetryCapability& Telemetry() override { return *this; }

    bool CapabilitiesMatchDescriptor() const override {
        if (descriptor_.execution_mode == ExecutionMode::Stub) {
            return !descriptor_.features.device_memory &&
                   !descriptor_.features.host_visible_memory &&
                   !descriptor_.features.transfers && !descriptor_.features.events &&
                   !descriptor_.features.execution && !descriptor_.features.telemetry;
        }
        return descriptor_.features.device_memory &&
               descriptor_.features.host_visible_memory && descriptor_.features.transfers &&
               descriptor_.features.events && descriptor_.features.execution &&
               descriptor_.features.telemetry;
    }

    bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                        accel::BufferLease& out_lease) override {
        if (!descriptor_.features.device_memory || bytes == 0 ||
            device_id != descriptor_.device_id) {
            return false;
        }
        auto storage = std::make_shared<std::vector<std::byte>>(bytes);
        std::scoped_lock lock(mutex_);
        const auto id = next_allocation_id_++;
        allocations_.emplace(id, storage);
        out_lease = {};
        out_lease.session_id = descriptor_.session_id;
        out_lease.pool_id = descriptor_.session_id;
        out_lease.allocation_id = id;
        out_lease.device_view.backend = descriptor_.backend;
        out_lease.device_view.session_id = descriptor_.session_id;
        out_lease.device_view.device_ptr = storage->data();
        out_lease.device_view.bytes = bytes;
        out_lease.device_view.device_id = device_id;
        return true;
    }

    bool AllocateHostVisible(std::uint64_t bytes, accel::BufferLease& out_lease) override {
        if (!descriptor_.features.host_visible_memory || bytes == 0) {
            return false;
        }
        auto storage = std::make_shared<std::vector<std::byte>>(bytes);
        std::scoped_lock lock(mutex_);
        const auto id = next_allocation_id_++;
        allocations_.emplace(id, storage);
        out_lease = {};
        out_lease.session_id = descriptor_.session_id;
        out_lease.pool_id = descriptor_.session_id;
        out_lease.allocation_id = id;
        out_lease.host_view.backend = descriptor_.backend;
        out_lease.host_view.session_id = descriptor_.session_id;
        out_lease.host_view.host_ptr = storage->data();
        out_lease.host_view.bytes = bytes;
        out_lease.host_view.allocator_id = descriptor_.session_id;
        return true;
    }

    bool Release(const accel::BufferLease& lease) override {
        if (lease.session_id != descriptor_.session_id || lease.pool_id != descriptor_.session_id ||
            lease.allocation_id == 0) {
            return false;
        }
        std::scoped_lock lock(mutex_);
        return allocations_.erase(lease.allocation_id) == 1;
    }

    bool EnqueueH2D(const accel::HostPinnedBufferView& src, accel::DeviceBufferView& dst,
                    std::uint64_t queue_id, accel::TransferTicket& out_ticket) override {
        if (!ValidateTransfer(src.session_id, dst.session_id, src.host_ptr, dst.device_ptr,
                              src.bytes, dst.bytes, queue_id)) {
            return false;
        }
        std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(src.bytes));
        StampTransfer(out_ticket, queue_id);
        out_ticket.src_host = src;
        out_ticket.dst_device = dst;
        return true;
    }

    bool EnqueueD2H(const accel::DeviceBufferView& src, accel::HostPinnedBufferView& dst,
                    std::uint64_t queue_id, accel::TransferTicket& out_ticket) override {
        if (!ValidateTransfer(src.session_id, dst.session_id, src.device_ptr, dst.host_ptr,
                              src.bytes, dst.bytes, queue_id)) {
            return false;
        }
        std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(src.bytes));
        StampTransfer(out_ticket, queue_id);
        out_ticket.src_device = src;
        out_ticket.dst_host = dst;
        return true;
    }

    bool EnqueueD2D(const accel::DeviceBufferView& src, accel::DeviceBufferView& dst,
                    std::uint64_t queue_id, accel::TransferTicket& out_ticket) override {
        if (!ValidateTransfer(src.session_id, dst.session_id, src.device_ptr, dst.device_ptr,
                              src.bytes, dst.bytes, queue_id)) {
            return false;
        }
        std::memcpy(dst.device_ptr, src.device_ptr, static_cast<std::size_t>(src.bytes));
        StampTransfer(out_ticket, queue_id);
        out_ticket.src_device = src;
        out_ticket.dst_device = dst;
        return true;
    }

    std::uint64_t Create() override {
        if (!descriptor_.features.events) return 0;
        std::scoped_lock lock(mutex_);
        const auto id = next_event_id_++;
        events_.insert(id);
        return id;
    }

    bool IsComplete(std::uint64_t event_id) const override {
        std::scoped_lock lock(mutex_);
        return events_.contains(event_id);
    }
    bool Wait(std::uint64_t event_id, std::uint64_t) override { return IsComplete(event_id); }
    void Destroy(std::uint64_t event_id) override {
        std::scoped_lock lock(mutex_);
        events_.erase(event_id);
    }

    std::uint64_t AcquireQueue() override {
        if (!descriptor_.features.execution) return 0;
        std::scoped_lock lock(mutex_);
        const auto id = next_queue_id_++;
        queues_.insert(id);
        return id;
    }
    void ReleaseQueue(std::uint64_t queue_id) override {
        std::scoped_lock lock(mutex_);
        queues_.erase(queue_id);
    }
    bool Submit(const accel::KernelTicket& ticket, void* const*, std::size_t) override {
        if (!descriptor_.features.execution || ticket.backend != descriptor_.backend ||
            ticket.session_id != descriptor_.session_id) {
            return false;
        }
        std::scoped_lock lock(mutex_);
        return queues_.contains(ticket.execution_queue_id);
    }

    void RecordTransfer(const accel::TransferTicket& ticket, std::uint64_t) override {
        if (ticket.session_id == descriptor_.session_id) ++transfer_samples_;
    }
    void RecordExecution(const accel::KernelTicket& ticket, std::uint64_t) override {
        if (ticket.session_id == descriptor_.session_id) ++execution_samples_;
    }
    void IncrementErrorCounter(std::string_view) override { ++error_count_; }

private:
    bool ValidateTransfer(std::uint64_t src_session, std::uint64_t dst_session,
                          const void* src, const void* dst, std::uint64_t src_bytes,
                          std::uint64_t dst_bytes, std::uint64_t queue_id) const {
        if (!descriptor_.features.transfers || src_session != descriptor_.session_id ||
            dst_session != descriptor_.session_id || src == nullptr || dst == nullptr ||
            src_bytes == 0 || src_bytes > dst_bytes) {
            return false;
        }
        std::scoped_lock lock(mutex_);
        return queues_.contains(queue_id);
    }

    void StampTransfer(accel::TransferTicket& ticket, std::uint64_t queue_id) {
        ticket = {};
        ticket.backend = descriptor_.backend;
        ticket.session_id = descriptor_.session_id;
        ticket.transfer_id = next_transfer_id_++;
        ticket.execution_queue_id = queue_id;
        ticket.completion_event = Create();
    }

    BackendDescriptor descriptor_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<std::vector<std::byte>>> allocations_;
    std::unordered_set<std::uint64_t> queues_;
    std::unordered_set<std::uint64_t> events_;
    std::uint64_t next_allocation_id_{1};
    std::uint64_t next_queue_id_{1};
    std::uint64_t next_event_id_{1};
    std::atomic<std::uint64_t> next_transfer_id_{1};
    std::atomic<std::uint64_t> transfer_samples_{0};
    std::atomic<std::uint64_t> execution_samples_{0};
    std::atomic<std::uint64_t> error_count_{0};
};

BackendDescriptor CpuDescriptor() {
    BackendDescriptor descriptor{};
    descriptor.backend = accel::BackendKind::CPU;
    descriptor.execution_mode = ExecutionMode::CpuFallback;
    descriptor.provider_name = "graphx-cpu";
    descriptor.runtime_version = "host";
    descriptor.device_name = "Host CPU";
    descriptor.architecture = "host";
    descriptor.session_id = 0x4350550000000001ULL;
    descriptor.features.device_memory = true;
    descriptor.features.host_visible_memory = true;
    descriptor.features.transfers = true;
    descriptor.features.events = true;
    descriptor.features.execution = true;
    descriptor.features.telemetry = true;
    return descriptor;
}

BackendDescriptor StubDescriptor(accel::BackendKind backend, std::string name,
                                 std::uint64_t session_id) {
    BackendDescriptor descriptor{};
    descriptor.backend = backend;
    descriptor.execution_mode = ExecutionMode::Stub;
    descriptor.provider_name = std::move(name);
    descriptor.runtime_version = "unavailable";
    descriptor.device_name = "No native device";
    descriptor.architecture = "stub";
    descriptor.session_id = session_id;
    return descriptor;
}

} // namespace

std::shared_ptr<IAcceleratorSession> CreateCpuAcceleratorSession() {
    return std::make_shared<DirectSession>(CpuDescriptor());
}
std::shared_ptr<IAcceleratorSession> CreateCudaStubAcceleratorSession() {
    return std::make_shared<DirectSession>(
        StubDescriptor(accel::BackendKind::CUDA, "graphx-cuda-stub", 0x4355444100000001ULL));
}
std::shared_ptr<IAcceleratorSession> CreateMetalStubAcceleratorSession() {
    return std::make_shared<DirectSession>(
        StubDescriptor(accel::BackendKind::Metal, "graphx-metal-stub", 0x4d45544100000001ULL));
}

std::shared_ptr<AcceleratorSessionRegistry>
CreateDefaultAcceleratorSessionRegistry(const AcceleratorProviderOptions& options) {
    auto registry = std::make_shared<AcceleratorSessionRegistry>();
    auto add = [&](std::shared_ptr<IAcceleratorSession> session) {
        const auto result = registry->Register(std::move(session));
        if (!result) throw std::runtime_error(result.error().detail);
    };
    if (options.enable_cpu) add(CreateCpuAcceleratorSession());
    if (options.enable_cuda) add(CreateCudaStubAcceleratorSession());
    if (options.enable_metal) add(CreateMetalStubAcceleratorSession());
    return registry;
}

} // namespace graph::gpu
