#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

bool NativeMetalMemoryPoolCapability::AllocateDevice(std::uint64_t bytes,
                                                     std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (bytes == 0) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);

    MTL::Device* device = context_state.active_device;
    if (device == nullptr || context_state.current_device_id != device_id) {
        auto* selected = AcquireDeviceById(device_id);
        if (selected == nullptr) {
            return false;
        }

        if (context_state.active_device != nullptr) {
            context_state.active_device->release();
        }
        context_state.active_device = selected;
        context_state.current_device_id = device_id;
        device = context_state.active_device;
    }

    auto* buffer = CreateBuffer(device, bytes, DeviceResourceOptionsForAllocation());
    if (buffer == nullptr) {
        return false;
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock pool_lock(pool_state.mutex);

    const auto allocation_id = pool_state.next_allocation_id++;
    NativeMetalMemoryPoolState::AllocationRecord record{};
    record.buffer = buffer;
    record.bytes = bytes;

    void* device_pointer = buffer->contents();
    if (device_pointer == nullptr) {
        record.host_token = AllocateHostToken(bytes);
        if (record.host_token == nullptr) {
            buffer->release();
            return false;
        }
        device_pointer = record.host_token;
    }

    pool_state.device_allocations.emplace(allocation_id, record);
    ++pool_state.allocation_count;
    pool_state.live_device_bytes += bytes;
    pool_state.peak_device_bytes = std::max(pool_state.peak_device_bytes, pool_state.live_device_bytes);

    out_lease.pool_id = 31;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::Metal;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = device_pointer;
    return true;
}

bool NativeMetalMemoryPoolCapability::AllocateShared(std::uint64_t bytes,
                                                     std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (bytes == 0) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);

    MTL::Device* device = context_state.active_device;
    if (device == nullptr || context_state.current_device_id != device_id) {
        auto* selected = AcquireDeviceById(device_id);
        if (selected == nullptr) {
            return false;
        }

        if (context_state.active_device != nullptr) {
            context_state.active_device->release();
        }
        context_state.active_device = selected;
        context_state.current_device_id = device_id;
        device = context_state.active_device;
    }

    auto* buffer = CreateBuffer(device, bytes, MTL::ResourceStorageModeShared);
    if (buffer == nullptr) {
        return false;
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock pool_lock(pool_state.mutex);

    const auto allocation_id = pool_state.next_allocation_id++;
    NativeMetalMemoryPoolState::AllocationRecord record{};
    record.buffer = buffer;
    record.bytes = bytes;

    void* device_pointer = buffer->contents();
    if (device_pointer == nullptr) {
        record.host_token = AllocateHostToken(bytes);
        if (record.host_token == nullptr) {
            buffer->release();
            return false;
        }
        device_pointer = record.host_token;
    }

    pool_state.shared_allocations.emplace(allocation_id, record);
    ++pool_state.allocation_count;
    pool_state.live_shared_bytes += bytes;
    pool_state.peak_shared_bytes = std::max(pool_state.peak_shared_bytes, pool_state.live_shared_bytes);

    out_lease.pool_id = 32;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.device_view.backend = accel::BackendKind::Metal;
    out_lease.device_view.device_id = device_id;
    out_lease.device_view.bytes = bytes;
    out_lease.device_view.dtype = accel::DataType::UInt8;
    out_lease.device_view.layout.rank = 1;
    out_lease.device_view.layout.shape[0] = bytes;
    out_lease.device_view.layout.stride[0] = 1;
    out_lease.device_view.device_ptr = device_pointer;
    return true;
}

bool NativeMetalMemoryPoolCapability::AllocateHost(std::uint64_t bytes,
                                                   accel::BufferLease& out_lease) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (bytes == 0) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);
    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return false;
    }

    auto* buffer = CreateBuffer(device, bytes, MTL::ResourceStorageModeShared);
    if (buffer == nullptr) {
        return false;
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock pool_lock(pool_state.mutex);
    const auto allocation_id = pool_state.next_allocation_id++;
    NativeMetalMemoryPoolState::AllocationRecord record{};
    record.buffer = buffer;
    record.bytes = bytes;
    pool_state.host_allocations.emplace(allocation_id, record);
    ++pool_state.allocation_count;
    pool_state.live_host_bytes += bytes;
    pool_state.peak_host_bytes = std::max(pool_state.peak_host_bytes, pool_state.live_host_bytes);

    out_lease.pool_id = 33;
    out_lease.allocation_id = allocation_id;
    out_lease.release_policy = accel::ReleasePolicy::Manual;
    out_lease.host_view.backend = accel::BackendKind::Metal;
    out_lease.host_view.bytes = bytes;
    out_lease.host_view.dtype = accel::DataType::UInt8;
    out_lease.host_view.layout.rank = 1;
    out_lease.host_view.layout.shape[0] = bytes;
    out_lease.host_view.layout.stride[0] = 1;
    out_lease.host_view.host_ptr = buffer->contents();
    out_lease.host_view.allocator_id = out_lease.pool_id;
    return true;
}

/**
 * @brief Release.
 * @param lease Parameter for release.
 */
bool NativeMetalMemoryPoolCapability::Release(const accel::BufferLease& lease) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (lease.allocation_id == 0) {
        return false;
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock lock(pool_state.mutex);

    const auto release_from = [&lease](auto& allocations,
                                       std::uint64_t& live_bytes,
                                       std::uint64_t& release_count) {
        const auto it = allocations.find(lease.allocation_id);
        if (it == allocations.end()) {
            return false;
        }

        if (live_bytes >= it->second.bytes) {
            live_bytes -= it->second.bytes;
        } else {
            live_bytes = 0;
        }

        if (it->second.buffer != nullptr) {
            it->second.buffer->release();
        }
        delete[] it->second.host_token;
        allocations.erase(it);
        ++release_count;
        return true;
    };

    const bool released_device = release_from(pool_state.device_allocations,
                                              pool_state.live_device_bytes,
                                              pool_state.release_count);
    const bool released_shared = release_from(pool_state.shared_allocations,
                                              pool_state.live_shared_bytes,
                                              pool_state.release_count);
    const bool released_host = release_from(pool_state.host_allocations,
                                            pool_state.live_host_bytes,
                                            pool_state.release_count);
    return released_device || released_shared || released_host;
}

/**
 * @brief Snapshot.
 */
IMetalMemoryPoolCapability::MemoryPoolSnapshot NativeMetalMemoryPoolCapability::Snapshot() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& pool_state = MemoryPoolState();
    std::scoped_lock lock(pool_state.mutex);

    MemoryPoolSnapshot out{};
    out.live_device_bytes = pool_state.live_device_bytes;
    out.live_shared_bytes = pool_state.live_shared_bytes;
    out.live_host_bytes = pool_state.live_host_bytes;
    out.peak_device_bytes = pool_state.peak_device_bytes;
    out.peak_shared_bytes = pool_state.peak_shared_bytes;
    out.peak_host_bytes = pool_state.peak_host_bytes;
    out.allocation_count = pool_state.allocation_count;
    out.release_count = pool_state.release_count;
    return out;
}

} // namespace graph::gpu::metal::capabilities
