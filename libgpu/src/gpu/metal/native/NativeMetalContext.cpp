#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

std::shared_ptr<NativeMetalRuntimeContext> CreateNativeMetalRuntimeContext() {
    return std::make_shared<NativeMetalRuntimeContext>();
}

NativeMetalContextCapability::NativeMetalContextCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

NativeMetalMemoryPoolCapability::NativeMetalMemoryPoolCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

NativeMetalTransferCapability::NativeMetalTransferCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

NativeMetalKernelCapability::NativeMetalKernelCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

NativeMetalTelemetryCapability::NativeMetalTelemetryCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

NativeMetalCollectiveCapability::NativeMetalCollectiveCapability(
    std::shared_ptr<NativeMetalRuntimeContext> runtime_context)
    : runtime_context_(std::move(runtime_context)) {
    if (!runtime_context_) {
        runtime_context_ = CreateNativeMetalRuntimeContext();
    }
}

/**
 * @brief Native metal runtime available.
 */
bool NativeMetalRuntimeAvailable() {
    auto* device = MTL::CreateSystemDefaultDevice();
    if (device == nullptr) {
        return false;
    }

    device->release();
    return true;
}

/**
 * @brief Native metal runtime diagnostics.
 */
std::string NativeMetalRuntimeDiagnostics() {
    std::ostringstream diag;
    auto* devices = MTL::CopyAllDevices();
    if (devices == nullptr) {
        return "Metal device enumeration failed (CopyAllDevices returned null).";
    }

    const auto device_count = static_cast<std::uint64_t>(devices->count());
    diag << "enumerated_devices=" << device_count;

    auto* default_device = MTL::CreateSystemDefaultDevice();
    if (default_device == nullptr) {
        devices->release();
        diag << "; default_device=null";
        diag << "; likely running in an environment without active GPU access";
        return diag.str();
    }

    const auto* device_name_ns = default_device->name();
    if (device_name_ns != nullptr && device_name_ns->utf8String() != nullptr) {
        diag << "; default_device=" << device_name_ns->utf8String();
    } else {
        diag << "; default_device=<unnamed>";
    }

    default_device->release();
    devices->release();
    return diag.str();
}

/**
 * @brief Select device.
 * @param device_id Parameter for select device.
 */
bool NativeMetalContextCapability::SelectDevice(std::uint32_t device_id) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);

    auto* device = AcquireDeviceById(device_id);
    if (device == nullptr) {
        return false;
    }

    if (state.active_device != nullptr) {
        state.active_device->release();
    }
    state.active_device = device;
    state.current_device_id = device_id;
    return true;
}

/**
 * @brief Current device.
 */
std::uint32_t NativeMetalContextCapability::CurrentDevice() const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);
    return state.current_device_id;
}

/**
 * @brief Create command queue.
 */
std::uint64_t NativeMetalContextCapability::CreateCommandQueue() {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);

    auto* device = EnsureActiveDeviceLocked(state);
    if (device == nullptr) {
        return 0;
    }

    auto* queue = device->newCommandQueue();
    if (queue == nullptr) {
        return 0;
    }

    const auto queue_id = state.next_queue_id++;
    state.queues.emplace(queue_id, queue);
    return queue_id;
}

/**
 * @brief Destroy command queue.
 * @param queue_id Parameter for destroy command queue.
 */
void NativeMetalContextCapability::DestroyCommandQueue(std::uint64_t queue_id) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    DropPendingD2HCopiesForQueue(queue_id);

    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);

    const auto it = state.queues.find(queue_id);
    if (it == state.queues.end()) {
        return;
    }

    if (it->second != nullptr) {
        it->second->release();
    }
    state.queues.erase(it);
}

/**
 * @brief Create event.
 */
std::uint64_t NativeMetalContextCapability::CreateEvent() {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);

    auto* device = EnsureActiveDeviceLocked(state);
    if (device == nullptr) {
        return 0;
    }

    const auto event_id = state.next_event_id++;
    auto* event = device->newSharedEvent();
    if (event != nullptr) {
        state.events.emplace(event_id, event);
    } else {
        state.synthetic_events.insert(event_id);
    }

    return event_id;
}

/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
void NativeMetalContextCapability::DestroyEvent(std::uint64_t event_id) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    DropPendingD2HCopy(event_id);

    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);

    const auto event_it = state.events.find(event_id);
    if (event_it != state.events.end()) {
        if (event_it->second != nullptr) {
            event_it->second->release();
        }
        state.events.erase(event_it);
        return;
    }

    state.synthetic_events.erase(event_id);
}

/**
 * @brief Is event complete.
 * @param event_id Parameter for is event complete.
 */
bool NativeMetalContextCapability::IsEventComplete(std::uint64_t event_id) const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (event_id == 0) {
        return false;
    }

    MTL::SharedEvent* event = nullptr;
    {
        auto& state = ContextState();
        std::scoped_lock lock(state.mutex);

        if (state.synthetic_events.contains(event_id)) {
            FinalizePendingD2HCopy(event_id);
            return true;
        }

        const auto event_it = state.events.find(event_id);
        if (event_it == state.events.end() || event_it->second == nullptr) {
            return false;
        }

        event = event_it->second;
        event->retain();
    }

    const bool complete = event->signaledValue() >= 1;
    event->release();
    if (complete) {
        FinalizePendingD2HCopy(event_id);
    }
    return complete;
}

/**
 * @brief Wait event.
 * @param event_id Parameter for wait event.
 * @param timeout_ms Parameter for wait event.
 */
bool NativeMetalContextCapability::WaitEvent(std::uint64_t event_id, std::uint64_t timeout_ms) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (event_id == 0) {
        return false;
    }

    MTL::SharedEvent* event = nullptr;
    {
        auto& state = ContextState();
        std::scoped_lock lock(state.mutex);

        if (state.synthetic_events.contains(event_id)) {
            FinalizePendingD2HCopy(event_id);
            return true;
        }

        const auto event_it = state.events.find(event_id);
        if (event_it == state.events.end() || event_it->second == nullptr) {
            return false;
        }

        event = event_it->second;
        event->retain();
    }

    const bool signaled = event->waitUntilSignaledValue(1, timeout_ms);
    event->release();
    if (signaled) {
        FinalizePendingD2HCopy(event_id);
    }
    return signaled;
}

} // namespace graph::gpu::metal::capabilities
