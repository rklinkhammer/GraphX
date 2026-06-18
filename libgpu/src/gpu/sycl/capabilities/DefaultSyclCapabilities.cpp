/**
 * @file DefaultSyclCapabilities.cpp
 * @brief Default SYCL Capabilities GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#if defined(__SYCL_COMPILER_VERSION) && __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#define GRAPHX_HAS_SYCL_RUNTIME 1
#else
#define GRAPHX_HAS_SYCL_RUNTIME 0
#endif

namespace graph::gpu::sycl::capabilities {

namespace {

#if GRAPHX_HAS_SYCL_RUNTIME
struct SyclRuntimeState {
    std::mutex mutex;
    std::uint64_t next_queue_id{1};
    std::uint64_t next_event_id{1};
    std::uint32_t current_device_id{0};
    std::unordered_map<std::uint64_t, std::shared_ptr<sycl::queue>> queues;
    std::unordered_map<std::uint64_t, std::shared_ptr<sycl::event>> events;
};

struct SyclAllocationState {
    std::mutex mutex;
    std::uint64_t next_allocation_id{1};

    struct AllocationRecord {
        void* pointer{nullptr};
        std::shared_ptr<sycl::queue> queue;
    };

    std::unordered_map<std::uint64_t, AllocationRecord> device_allocations;
    std::unordered_map<std::uint64_t, AllocationRecord> shared_allocations;
    std::unordered_map<std::uint64_t, AllocationRecord> host_allocations;
};

/**
 * @brief Runtime state.
 */
SyclRuntimeState& RuntimeState() {
    static SyclRuntimeState state;
    return state;
}

/**
 * @brief Allocation state.
 */
SyclAllocationState& AllocationState() {
    static SyclAllocationState state;
    return state;
}

/**
 * @brief Make runtime queue.
 * @param device_id Parameter for make runtime queue.
 */
std::shared_ptr<sycl::queue> MakeRuntimeQueue(std::uint32_t device_id) {
    try {
        std::vector<sycl::device> devices;
        for (const auto& platform : sycl::platform::get_platforms()) {
            const auto platform_devices = platform.get_devices();
            devices.insert(devices.end(), platform_devices.begin(), platform_devices.end());
        }

        if (!devices.empty()) {
            const auto index = std::min<std::size_t>(device_id, devices.size() - 1U);
            return std::make_shared<sycl::queue>(devices[index]);
        }
    } catch (...) {
    }

    return std::make_shared<sycl::queue>(sycl::default_selector_v);
}

/**
 * @brief Get or create queue.
 * @param queue_id Parameter for get or create queue.
 */
std::shared_ptr<sycl::queue> GetOrCreateQueue(std::uint64_t queue_id) {
    auto& state = RuntimeState();
    std::scoped_lock lock(state.mutex);

    if (queue_id == 0) {
        return nullptr;
    }

    auto existing = state.queues.find(queue_id);
    if (existing != state.queues.end()) {
        return existing->second;
    }

    auto queue = MakeRuntimeQueue(state.current_device_id);
    state.queues.emplace(queue_id, queue);
    if (queue_id >= state.next_queue_id) {
        state.next_queue_id = queue_id + 1;
    }
    return queue;
}

/**
 * @brief Register event.
 * @param event Parameter for register event.
 */
std::uint64_t RegisterEvent(std::shared_ptr<sycl::event> event) {
    auto& state = RuntimeState();
    std::scoped_lock lock(state.mutex);

    const auto event_id = state.next_event_id++;
    state.events.emplace(event_id, std::move(event));
    return event_id;
}

bool EnqueueMemcpyAndWait(const std::shared_ptr<sycl::queue>& queue,
                          void* destination,
                          const void* source,
                          std::uint64_t bytes,
                          std::uint64_t& out_event_id) {
    if (!queue || destination == nullptr || source == nullptr) {
        return false;
    }

    try {
        sycl::event event = queue->memcpy(destination, source, static_cast<std::size_t>(bytes));
        event.wait_and_throw();
        out_event_id = RegisterEvent(std::make_shared<sycl::event>(std::move(event)));
        return true;
    } catch (...) {
        return false;
    }
}

template <typename MapT>
/**
 * @brief Release allocation.
 * @param allocations Parameter for release allocation.
 * @param allocation_id Parameter for release allocation.
 */
bool ReleaseAllocation(MapT& allocations, std::uint64_t allocation_id) {
    auto it = allocations.find(allocation_id);
    if (it == allocations.end()) {
        return false;
    }

    try {
        if (it->second.pointer != nullptr && it->second.queue) {
            sycl::free(it->second.pointer, *it->second.queue);
        }
    } catch (...) {
        return false;
    }

    allocations.erase(it);
    return true;
}
#endif

} // namespace

DefaultSyclContextCapability::DefaultSyclContextCapability() = default;

DefaultSyclMemoryPoolCapability::DefaultSyclMemoryPoolCapability() = default;

DefaultSyclTransferCapability::DefaultSyclTransferCapability() = default;

/**
 * @brief Select device.
 * @param device_id Parameter for select device.
 */
bool DefaultSyclContextCapability::SelectDevice(std::uint32_t device_id) {
#if GRAPHX_HAS_SYCL_RUNTIME
    RuntimeState().current_device_id = device_id;
#else
    current_device_id_ = device_id;
#endif
    return true;
}

/**
 * @brief Current device.
 */
std::uint32_t DefaultSyclContextCapability::CurrentDevice() const {
#if GRAPHX_HAS_SYCL_RUNTIME
    return RuntimeState().current_device_id;
#else
    return current_device_id_;
#endif
}

/**
 * @brief Create queue.
 */
std::uint64_t DefaultSyclContextCapability::CreateQueue() {
#if GRAPHX_HAS_SYCL_RUNTIME
    auto& state = RuntimeState();
    std::scoped_lock lock(state.mutex);

    const auto id = state.next_queue_id++;
    state.queues.emplace(id, MakeRuntimeQueue(state.current_device_id));
    return id;
#else
    const auto id = next_queue_id_++;
    queues_.insert(id);
    return id;
#endif
}

/**
 * @brief Destroy queue.
 * @param queue_id Parameter for destroy queue.
 */
void DefaultSyclContextCapability::DestroyQueue(std::uint64_t queue_id) {
#if GRAPHX_HAS_SYCL_RUNTIME
    auto& state = RuntimeState();
    std::scoped_lock lock(state.mutex);
    state.queues.erase(queue_id);
#else
    queues_.erase(queue_id);
#endif
}

/**
 * @brief Create event.
 */
std::uint64_t DefaultSyclContextCapability::CreateEvent() {
#if GRAPHX_HAS_SYCL_RUNTIME
    const auto queue = GetOrCreateQueue(1);
    if (!queue) {
        return 0;
    }

    try {
        auto event = std::make_shared<sycl::event>(queue->submit([](sycl::handler& cgh) {
            cgh.host_task([] {});
        }));
        return RegisterEvent(std::move(event));
    } catch (...) {
        return 0;
    }
#else
    const auto id = next_event_id_++;
    events_.insert(id);
    return id;
#endif
}

/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
void DefaultSyclContextCapability::DestroyEvent(std::uint64_t event_id) {
#if GRAPHX_HAS_SYCL_RUNTIME
    auto& state = RuntimeState();
    std::scoped_lock lock(state.mutex);
    state.events.erase(event_id);
#else
    events_.erase(event_id);
#endif
}

bool DefaultSyclMemoryPoolCapability::AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (bytes == 0) {
        return false;
    }

    auto queue = MakeRuntimeQueue(device_id);
    void* pointer = nullptr;
    try {
        pointer = sycl::malloc_device(static_cast<std::size_t>(bytes), *queue);
    } catch (...) {
        return false;
    }

    if (pointer == nullptr) {
        return false;
    }

    std::memset(pointer, 0, static_cast<std::size_t>(bytes));

    auto& state = AllocationState();
    std::scoped_lock lock(state.mutex);
    const auto allocation_id = state.next_allocation_id++;
    state.device_allocations.emplace(allocation_id, SyclAllocationState::AllocationRecord{pointer, queue});

    out_lease.pool_id = 11;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::SYCL;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = pointer;
    return true;
#else
    if (bytes == 0) {
        return false;
    }

    const auto allocation_id = next_allocation_id_++;
    auto [it, inserted] = device_allocations_.emplace(allocation_id,
                                                       std::vector<std::byte>(bytes));
    if (!inserted) {
        return false;
    }

    out_lease.pool_id = 11;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::SYCL;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = static_cast<void*>(it->second.data());
    return true;
#endif
}

bool DefaultSyclMemoryPoolCapability::AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (bytes == 0) {
        return false;
    }

    auto queue = MakeRuntimeQueue(device_id);
    void* pointer = nullptr;
    try {
        pointer = sycl::malloc_shared(static_cast<std::size_t>(bytes), *queue);
    } catch (...) {
        return false;
    }

    if (pointer == nullptr) {
        return false;
    }

    std::memset(pointer, 0, static_cast<std::size_t>(bytes));

    auto& state = AllocationState();
    std::scoped_lock lock(state.mutex);
    const auto allocation_id = state.next_allocation_id++;
    state.shared_allocations.emplace(allocation_id, SyclAllocationState::AllocationRecord{pointer, queue});

    out_lease.pool_id = 13;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::SYCL;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = pointer;
    return true;
#else
    return AllocateDevice(bytes, device_id, out_lease);
#endif
}

bool DefaultSyclMemoryPoolCapability::AllocateHost(std::uint64_t bytes,
                                                   accel::BufferLease& out_lease) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (bytes == 0) {
        return false;
    }

    auto queue = MakeRuntimeQueue(0);
    void* pointer = nullptr;
    try {
        pointer = sycl::malloc_host(static_cast<std::size_t>(bytes), *queue);
    } catch (...) {
        return false;
    }

    if (pointer == nullptr) {
        return false;
    }

    std::memset(pointer, 0, static_cast<std::size_t>(bytes));

    auto& state = AllocationState();
    std::scoped_lock lock(state.mutex);
    const auto allocation_id = state.next_allocation_id++;
    state.host_allocations.emplace(allocation_id, SyclAllocationState::AllocationRecord{pointer, queue});

    out_lease.pool_id = 12;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::SYCL;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = pointer;
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
#else
    if (bytes == 0) {
        return false;
    }

    const auto allocation_id = next_allocation_id_++;
    auto [it, inserted] = host_allocations_.emplace(allocation_id,
                                                     std::vector<std::byte>(bytes));
    if (!inserted) {
        return false;
    }

    out_lease.pool_id = 12;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::SYCL;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = static_cast<void*>(it->second.data());
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
#endif
}

/**
 * @brief Release.
 * @param lease Parameter for release.
 */
bool DefaultSyclMemoryPoolCapability::Release(const accel::BufferLease& lease) {
#if GRAPHX_HAS_SYCL_RUNTIME
    auto& state = AllocationState();
    std::scoped_lock lock(state.mutex);

    if (lease.allocation_id == 0) {
        return false;
    }

    if (ReleaseAllocation(state.device_allocations, lease.allocation_id)) {
        return true;
    }
    if (ReleaseAllocation(state.shared_allocations, lease.allocation_id)) {
        return true;
    }
    if (ReleaseAllocation(state.host_allocations, lease.allocation_id)) {
        return true;
    }
    return false;
#else
    if (lease.allocation_id == 0) {
        return false;
    }

    const auto released_device = device_allocations_.erase(lease.allocation_id);
    const auto released_host = host_allocations_.erase(lease.allocation_id);
    return released_device != 0 || released_host != 0;
#endif
}

bool DefaultSyclTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto queue = GetOrCreateQueue(queue_id);
    if (!queue) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::uint64_t event_id{0};
    if (!EnqueueMemcpyAndWait(queue, dst.device_ptr, src.host_ptr, copy_bytes, event_id)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = event_id;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
#else
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
#endif
}

bool DefaultSyclTransferCapability::EnqueueD2H(const accel::DeviceBufferView& src,
                                               accel::HostPinnedBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto queue = GetOrCreateQueue(queue_id);
    if (!queue) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::uint64_t event_id{0};
    if (!EnqueueMemcpyAndWait(queue, dst.host_ptr, src.device_ptr, copy_bytes, event_id)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = event_id;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
#else
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
#endif
}

bool DefaultSyclTransferCapability::EnqueueD2D(const accel::DeviceBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
#if GRAPHX_HAS_SYCL_RUNTIME
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto queue = GetOrCreateQueue(queue_id);
    if (!queue) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::uint64_t event_id{0};
    if (!EnqueueMemcpyAndWait(queue, dst.device_ptr, src.device_ptr, copy_bytes, event_id)) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = event_id;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
#else
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    std::memcpy(dst.device_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

    out_ticket.backend = accel::BackendKind::SYCL;
    out_ticket.transfer_id = next_transfer_id_++;
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = next_event_id_++;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = out_ticket.completion_event;
    return true;
#endif
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

/**
 * @brief Increment error counter.
 * @param std::string_view Parameter for increment error counter.
 */
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
