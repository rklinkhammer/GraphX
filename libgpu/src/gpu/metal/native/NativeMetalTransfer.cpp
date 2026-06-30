#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

bool NativeMetalTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    if (copy_bytes == 0) {
        return false;
    }

    auto* queue = AcquireQueue(queue_id);
    if (queue == nullptr) {
        return false;
    }

    const auto dst_resolution = AcquireDeviceBufferFromPointer(dst.device_ptr);
    auto* dst_buffer = dst_resolution.buffer;
    if (dst_buffer == nullptr) {
        queue->release();
        return false;
    }

    auto* staging = CreateBuffer(dst_buffer->device(), copy_bytes, MTL::ResourceStorageModeShared);
    if (staging == nullptr) {
        dst_buffer->release();
        queue->release();
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
        staging->release();
        dst_buffer->release();
        queue->release();
        return false;
    }

    std::memcpy(staging->contents(), src.host_ptr, static_cast<std::size_t>(copy_bytes));
    const bool copied = ExecuteBlitCopy(
        queue,
        staging,
        0,
        dst_buffer,
        dst_resolution.offset,
        copy_bytes,
        completion_event,
        false);
    staging->release();
    dst_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = NextTransferId();
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = completion_event;
    out_ticket.src_host = src;
    out_ticket.dst_device = dst;
    dst.ready_event = completion_event;
    return true;
}

bool NativeMetalTransferCapability::EnqueueD2H(const accel::DeviceBufferView& src,
                                               accel::HostPinnedBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    if (copy_bytes == 0) {
        return false;
    }

    auto* queue = AcquireQueue(queue_id);
    if (queue == nullptr) {
        return false;
    }

    const auto src_resolution = AcquireDeviceBufferFromPointer(src.device_ptr);
    auto* src_buffer = src_resolution.buffer;
    if (src_buffer == nullptr) {
        queue->release();
        return false;
    }

    auto* staging = CreateBuffer(src_buffer->device(), copy_bytes, MTL::ResourceStorageModeShared);
    if (staging == nullptr) {
        src_buffer->release();
        queue->release();
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
        staging->release();
        src_buffer->release();
        queue->release();
        return false;
    }

    const bool async_d2h_enabled = ResolveAsyncD2HModeEnabled();

    const bool copied = ExecuteBlitCopy(
        queue,
        src_buffer,
        src_resolution.offset,
        staging,
        0,
        copy_bytes,
        completion_event,
        !async_d2h_enabled);

    if (copied && async_d2h_enabled) {
        staging->retain();
        RegisterPendingD2HCopy(completion_event, staging, dst.host_ptr, copy_bytes, queue_id);
    }

    if (copied && !async_d2h_enabled) {
        std::memcpy(dst.host_ptr, staging->contents(), static_cast<std::size_t>(copy_bytes));
    }

    staging->release();
    src_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = NextTransferId();
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = completion_event;
    out_ticket.src_device = src;
    out_ticket.dst_host = dst;
    return true;
}

bool NativeMetalTransferCapability::EnqueueD2D(const accel::DeviceBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (queue_id == 0 || !accel::IsValidView(src) || !accel::IsValidView(dst)) {
        return false;
    }

    const auto copy_bytes = std::min(src.bytes, dst.bytes);
    if (copy_bytes == 0) {
        return false;
    }

    auto* queue = AcquireQueue(queue_id);
    if (queue == nullptr) {
        return false;
    }

    const auto src_resolution = AcquireDeviceBufferFromPointer(src.device_ptr);
    const auto dst_resolution = AcquireDeviceBufferFromPointer(dst.device_ptr);
    auto* src_buffer = src_resolution.buffer;
    auto* dst_buffer = dst_resolution.buffer;
    if (src_buffer == nullptr || dst_buffer == nullptr) {
        if (src_buffer != nullptr) {
            src_buffer->release();
        }
        if (dst_buffer != nullptr) {
            dst_buffer->release();
        }
        queue->release();
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
        src_buffer->release();
        dst_buffer->release();
        queue->release();
        return false;
    }

    const bool copied = ExecuteBlitCopy(
        queue,
        src_buffer,
        src_resolution.offset,
        dst_buffer,
        dst_resolution.offset,
        copy_bytes,
        completion_event,
        false);
    src_buffer->release();
    dst_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    out_ticket.backend = accel::BackendKind::Metal;
    out_ticket.transfer_id = NextTransferId();
    out_ticket.execution_queue_id = queue_id;
    out_ticket.completion_event = completion_event;
    out_ticket.src_device = src;
    out_ticket.dst_device = dst;
    dst.ready_event = completion_event;
    return true;
}

} // namespace graph::gpu::metal::capabilities
