#pragma once

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

/**
 * @file NativeMetalRuntimeInternal.hpp
 * @brief Shared private state and helpers for native Metal capability units.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"

#include "gpu/accel/types/AccelValidation.hpp"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace graph::gpu::metal::capabilities {

/**
 * @class NativeMetalRuntimeContext
 * @brief Native metal runtime context implementation for GraphX.
 */
class NativeMetalRuntimeContext {
public:
    struct ContextState {
        std::mutex mutex{};
        std::uint32_t current_device_id{0};
        std::uint64_t next_queue_id{1};
        std::uint64_t next_event_id{1};
        MTL::Device* active_device{nullptr};
        std::unordered_map<std::uint64_t, MTL::CommandQueue*> queues{};
        std::unordered_map<std::uint64_t, MTL::SharedEvent*> events{};
        std::unordered_set<std::uint64_t> synthetic_events{};
    };

    struct MemoryPoolState {
        struct AllocationRecord {
            MTL::Buffer* buffer{nullptr};
            std::uint64_t bytes{0};
            std::byte* host_token{nullptr};
        };

        std::mutex mutex{};
        std::uint64_t next_allocation_id{1};
        std::unordered_map<std::uint64_t, AllocationRecord> device_allocations{};
        std::unordered_map<std::uint64_t, AllocationRecord> shared_allocations{};
        std::unordered_map<std::uint64_t, AllocationRecord> host_allocations{};
        std::uint64_t live_device_bytes{0};
        std::uint64_t live_shared_bytes{0};
        std::uint64_t live_host_bytes{0};
        std::uint64_t peak_device_bytes{0};
        std::uint64_t peak_shared_bytes{0};
        std::uint64_t peak_host_bytes{0};
        std::uint64_t allocation_count{0};
        std::uint64_t release_count{0};
    };

    struct TransferState {
        struct PendingD2HCopy {
            MTL::Buffer* staging{nullptr};
            void* dst_host_ptr{nullptr};
            std::uint64_t bytes{0};
            std::uint64_t queue_id{0};
        };

        std::mutex mutex{};
        std::uint64_t next_transfer_id{1};
        std::unordered_map<std::uint64_t, PendingD2HCopy> pending_d2h_copies{};
    };

    struct KernelState {
        struct KernelEntry {
            std::string name{};
            MTL::Library* library{nullptr};
            MTL::Function* function{nullptr};
            MTL::ComputePipelineState* pipeline{nullptr};
            std::vector<MetalKernelArgDescriptor> arg_layout{};
            MetalKernelDispatchDescriptor dispatch{};
        };

        std::mutex mutex{};
        std::unordered_map<std::uint64_t, KernelEntry> kernels{};
    };

    struct TelemetryState {
        std::uint64_t transfer_samples{0};
        std::uint64_t kernel_samples{0};
        std::uint64_t error_count{0};
        std::uint64_t transfer_total_duration_ns{0};
        std::uint64_t kernel_total_duration_ns{0};
        std::uint64_t last_transfer_duration_ns{0};
        std::uint64_t last_kernel_duration_ns{0};
        std::uint64_t h2d_transfer_samples{0};
        std::uint64_t d2h_transfer_samples{0};
        std::uint64_t d2d_transfer_samples{0};
        std::unordered_map<std::string, std::uint64_t> error_code_counts{};
        mutable std::mutex mutex{};
    };

    ~NativeMetalRuntimeContext() {
        {
            std::scoped_lock lock(kernel_state.mutex);
            for (auto& [_, entry] : kernel_state.kernels) {
                if (entry.pipeline != nullptr) {
                    entry.pipeline->release();
                }
                if (entry.function != nullptr) {
                    entry.function->release();
                }
                if (entry.library != nullptr) {
                    entry.library->release();
                }
            }
            kernel_state.kernels.clear();
        }
        {
            std::scoped_lock lock(memory_pool_state.mutex);
            for (auto& [_, allocation] : memory_pool_state.device_allocations) {
                if (allocation.buffer != nullptr) {
                    allocation.buffer->release();
                }
                delete[] allocation.host_token;
            }
            for (auto& [_, allocation] : memory_pool_state.shared_allocations) {
                if (allocation.buffer != nullptr) {
                    allocation.buffer->release();
                }
                delete[] allocation.host_token;
            }
            for (auto& [_, allocation] : memory_pool_state.host_allocations) {
                if (allocation.buffer != nullptr) {
                    allocation.buffer->release();
                }
                delete[] allocation.host_token;
            }
            memory_pool_state.device_allocations.clear();
            memory_pool_state.shared_allocations.clear();
            memory_pool_state.host_allocations.clear();
        }
        {
            std::scoped_lock lock(transfer_state.mutex);
            for (auto& [_, pending_copy] : transfer_state.pending_d2h_copies) {
                if (pending_copy.staging != nullptr) {
                    pending_copy.staging->release();
                }
            }
            transfer_state.pending_d2h_copies.clear();
        }
        {
            std::scoped_lock lock(context_state.mutex);
            for (auto& [_, queue] : context_state.queues) {
                if (queue != nullptr) {
                    queue->release();
                }
            }
            for (auto& [_, event] : context_state.events) {
                if (event != nullptr) {
                    event->release();
                }
            }
            context_state.queues.clear();
            context_state.events.clear();
            context_state.synthetic_events.clear();
            if (context_state.active_device != nullptr) {
                context_state.active_device->release();
                context_state.active_device = nullptr;
            }
        }
    }

    ContextState context_state{};
    MemoryPoolState memory_pool_state{};
    TransferState transfer_state{};
    KernelState kernel_state{};
    TelemetryState telemetry_state{};
};

namespace {

using NativeMetalContextState = NativeMetalRuntimeContext::ContextState;
using NativeMetalMemoryPoolState = NativeMetalRuntimeContext::MemoryPoolState;
using NativeMetalTransferState = NativeMetalRuntimeContext::TransferState;
using NativeMetalKernelState = NativeMetalRuntimeContext::KernelState;
using NativeMetalTelemetryState = NativeMetalRuntimeContext::TelemetryState;

thread_local NativeMetalRuntimeContext* active_runtime_context = nullptr;

/**
 * @class ScopedNativeMetalRuntimeContext
 * @brief Scoped native metal runtime context implementation for GraphX.
 */
class ScopedNativeMetalRuntimeContext {
public:
    explicit ScopedNativeMetalRuntimeContext(NativeMetalRuntimeContext* runtime)
        : previous_(active_runtime_context) {
        active_runtime_context = runtime;
    }

    ~ScopedNativeMetalRuntimeContext() {
        active_runtime_context = previous_;
    }

private:
    NativeMetalRuntimeContext* previous_{nullptr};
};

/**
 * @brief Active runtime context.
 */
NativeMetalRuntimeContext& ActiveRuntimeContext() {
    return *active_runtime_context;
}

/**
 * @brief Context state.
 */
NativeMetalContextState& ContextState() {
    return ActiveRuntimeContext().context_state;
}

/**
 * @brief Memory pool state.
 */
NativeMetalMemoryPoolState& MemoryPoolState() {
    return ActiveRuntimeContext().memory_pool_state;
}

/**
 * @brief Transfer state.
 */
NativeMetalTransferState& TransferState() {
    return ActiveRuntimeContext().transfer_state;
}

/**
 * @brief Kernel state.
 */
NativeMetalKernelState& KernelState() {
    return ActiveRuntimeContext().kernel_state;
}

/**
 * @brief Telemetry state.
 */
NativeMetalTelemetryState& TelemetryState() {
    return ActiveRuntimeContext().telemetry_state;
}

/**
 * @brief Acquire device by id.
 * @param device_id Parameter for acquire device by id.
 */
MTL::Device* AcquireDeviceById(std::uint32_t device_id) {
    NS::Array* devices = MTL::CopyAllDevices();
    if (devices != nullptr) {
        if (device_id < devices->count()) {
            auto* device = static_cast<MTL::Device*>(devices->object(device_id));
            if (device != nullptr) {
                device->retain();
            }
            devices->release();
            return device;
        }
        devices->release();
    }

    if (device_id == 0) {
        return MTL::CreateSystemDefaultDevice();
    }

    return nullptr;
}

/**
 * @brief Ensure active device locked.
 * @param state Parameter for ensure active device locked.
 */
MTL::Device* EnsureActiveDeviceLocked(NativeMetalContextState& state) {
    if (state.active_device != nullptr) {
        return state.active_device;
    }

    state.active_device = AcquireDeviceById(state.current_device_id);
    return state.active_device;
}

/**
 * @brief Create buffer.
 * @param device Parameter for create buffer.
 * @param bytes Parameter for create buffer.
 * @param options Parameter for create buffer.
 */
MTL::Buffer* CreateBuffer(MTL::Device* device, std::uint64_t bytes, MTL::ResourceOptions options) {
    if (device == nullptr || bytes == 0) {
        return nullptr;
    }

    return device->newBuffer(bytes, options);
}

/**
 * @brief Acquire queue.
 * @param queue_id Parameter for acquire queue.
 */
MTL::CommandQueue* AcquireQueue(std::uint64_t queue_id) {
    auto& context_state = ContextState();
    std::scoped_lock lock(context_state.mutex);
    const auto it = context_state.queues.find(queue_id);
    if (it == context_state.queues.end() || it->second == nullptr) {
        return nullptr;
    }

    it->second->retain();
    return it->second;
}

struct BufferResolution {
    MTL::Buffer* buffer{nullptr};
    std::uint64_t offset{0};
};

/**
 * @brief Acquire device buffer from pointer.
 * @param pointer Parameter for acquire device buffer from pointer.
 */
BufferResolution AcquireDeviceBufferFromPointer(void* pointer) {
    if (pointer == nullptr) {
        return {};
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock lock(pool_state.mutex);

    const auto find_in = [pointer](const auto& allocations) -> BufferResolution {
        const auto* pointer_bytes = static_cast<const std::byte*>(pointer);
        for (const auto& [_, allocation] : allocations) {
            auto* buffer = allocation.buffer;
            if (buffer == nullptr) {
                continue;
            }

            const std::byte* base_bytes = nullptr;
            std::uint64_t buffer_length = allocation.bytes;
            if (allocation.host_token != nullptr) {
                base_bytes = allocation.host_token;
            } else {
                base_bytes = static_cast<const std::byte*>(buffer->contents());
                if (base_bytes == nullptr) {
                    continue;
                }
                if (buffer_length == 0) {
                    buffer_length = static_cast<std::uint64_t>(buffer->length());
                }
            }

            if (buffer_length == 0) {
                buffer_length = static_cast<std::uint64_t>(buffer->length());
            }
            const auto* end_bytes = base_bytes + buffer_length;
            if (pointer_bytes < base_bytes || pointer_bytes >= end_bytes) {
                continue;
            }

            const auto offset = static_cast<std::uint64_t>(pointer_bytes - base_bytes);
            if (offset >= buffer_length) {
                continue;
            }

            buffer->retain();
            return BufferResolution{buffer, offset};
        }
        return {};
    };

    if (auto resolved = find_in(pool_state.device_allocations); resolved.buffer != nullptr) {
        return resolved;
    }
    if (auto resolved = find_in(pool_state.shared_allocations); resolved.buffer != nullptr) {
        return resolved;
    }
    return {};
}

/**
 * @brief Register transfer completion event.
 */
std::uint64_t RegisterTransferCompletionEvent() {
    auto& context_state = ContextState();
    std::scoped_lock lock(context_state.mutex);

    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return 0;
    }

    const auto event_id = context_state.next_event_id++;
    auto* event = device->newSharedEvent();
    if (event != nullptr) {
        context_state.events.emplace(event_id, event);
    } else {
        context_state.synthetic_events.insert(event_id);
    }

    return event_id;
}

/**
 * @brief Next transfer id.
 */
std::uint64_t NextTransferId() {
    auto& transfer_state = TransferState();
    std::scoped_lock lock(transfer_state.mutex);
    return transfer_state.next_transfer_id++;
}

/**
 * @brief Resolve async 2 h mode enabled.
 */
bool ResolveAsyncD2HModeEnabled() {
    const char* value = std::getenv("GRAPHX_METAL_ASYNC_D2H");
    if (value == nullptr) {
        return false;
    }

    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized == "1" || normalized == "true" || normalized == "on" ||
           normalized == "yes";
}

void DropPendingD2HCopy(std::uint64_t completion_event);

void RegisterPendingD2HCopy(std::uint64_t completion_event,
                            MTL::Buffer* staging,
                            void* dst_host_ptr,
                            std::uint64_t bytes,
                            std::uint64_t queue_id) {
    if (completion_event == 0 || staging == nullptr || dst_host_ptr == nullptr || bytes == 0) {
        return;
    }

    auto& transfer_state = TransferState();
    std::scoped_lock lock(transfer_state.mutex);
    const auto existing = transfer_state.pending_d2h_copies.find(completion_event);
    if (existing != transfer_state.pending_d2h_copies.end() && existing->second.staging != nullptr) {
        existing->second.staging->release();
    }
    transfer_state.pending_d2h_copies[completion_event] =
        NativeMetalTransferState::PendingD2HCopy{staging, dst_host_ptr, bytes, queue_id};
}

/**
 * @brief Drop pending 2 h copies for queue.
 * @param queue_id Parameter for drop pending 2 h copies for queue.
 */
void DropPendingD2HCopiesForQueue(std::uint64_t queue_id) {
    if (queue_id == 0) {
        return;
    }

    auto& transfer_state = TransferState();
    std::vector<std::uint64_t> event_ids_to_drop{};
    {
        std::scoped_lock lock(transfer_state.mutex);
        for (const auto& [event_id, pending_copy] : transfer_state.pending_d2h_copies) {
            if (pending_copy.queue_id == queue_id) {
                event_ids_to_drop.push_back(event_id);
            }
        }
    }

    for (const auto event_id : event_ids_to_drop) {
        DropPendingD2HCopy(event_id);
    }
}

/**
 * @brief Drop pending 2 h copy.
 * @param completion_event Parameter for drop pending 2 h copy.
 */
void DropPendingD2HCopy(std::uint64_t completion_event) {
    if (completion_event == 0) {
        return;
    }

    auto& transfer_state = TransferState();
    std::scoped_lock lock(transfer_state.mutex);
    const auto it = transfer_state.pending_d2h_copies.find(completion_event);
    if (it == transfer_state.pending_d2h_copies.end()) {
        return;
    }

    if (it->second.staging != nullptr) {
        it->second.staging->release();
    }
    transfer_state.pending_d2h_copies.erase(it);
}

/**
 * @brief Finalize pending 2 h copy.
 * @param completion_event Parameter for finalize pending 2 h copy.
 */
void FinalizePendingD2HCopy(std::uint64_t completion_event) {
    if (completion_event == 0) {
        return;
    }

    NativeMetalTransferState::PendingD2HCopy pending_copy{};
    {
        auto& transfer_state = TransferState();
        std::scoped_lock lock(transfer_state.mutex);
        const auto it = transfer_state.pending_d2h_copies.find(completion_event);
        if (it == transfer_state.pending_d2h_copies.end()) {
            return;
        }
        pending_copy = it->second;
        transfer_state.pending_d2h_copies.erase(it);
    }

    if (pending_copy.staging == nullptr || pending_copy.dst_host_ptr == nullptr || pending_copy.bytes == 0) {
        if (pending_copy.staging != nullptr) {
            pending_copy.staging->release();
        }
        return;
    }

    std::memcpy(
        pending_copy.dst_host_ptr,
        pending_copy.staging->contents(),
        static_cast<std::size_t>(pending_copy.bytes));
    pending_copy.staging->release();
}

/**
 * @brief Acquire shared event.
 * @param event_id Parameter for acquire shared event.
 */
MTL::SharedEvent* AcquireSharedEvent(std::uint64_t event_id) {
    if (event_id == 0) {
        return nullptr;
    }

    auto& context_state = ContextState();
    std::scoped_lock lock(context_state.mutex);
    const auto it = context_state.events.find(event_id);
    if (it == context_state.events.end() || it->second == nullptr) {
        return nullptr;
    }

    it->second->retain();
    return it->second;
}

bool ExecuteBlitCopy(MTL::CommandQueue* queue,
                    MTL::Buffer* src,
                    std::uint64_t src_offset,
                    MTL::Buffer* dst,
                    std::uint64_t dst_offset,
                    std::uint64_t copy_bytes,
                    std::uint64_t completion_event,
                    bool wait_for_completion) {
    if (queue == nullptr || src == nullptr || dst == nullptr || copy_bytes == 0) {
        return false;
    }

    if (src_offset + copy_bytes > static_cast<std::uint64_t>(src->length()) ||
        dst_offset + copy_bytes > static_cast<std::uint64_t>(dst->length())) {
        return false;
    }

    auto* command_buffer = queue->commandBuffer();
    if (command_buffer == nullptr) {
        return false;
    }

    auto* blit = command_buffer->blitCommandEncoder();
    if (blit == nullptr) {
        return false;
    }

    blit->copyFromBuffer(src, src_offset, dst, dst_offset, copy_bytes);
    blit->endEncoding();

    auto* event = AcquireSharedEvent(completion_event);
    if (event != nullptr) {
        command_buffer->encodeSignalEvent(event, 1);
    }

    command_buffer->commit();

    if (wait_for_completion) {
        command_buffer->waitUntilCompleted();
    }

    if (event != nullptr) {
        event->release();
    }
    return true;
}

/**
 * @brief Is valid kernel name.
 * @param name Parameter for is valid kernel name.
 */
bool IsValidKernelName(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    for (const char c : name) {
        const bool valid_char = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        if (!valid_char) {
            return false;
        }
    }
    return true;
}

enum class BuiltinKernelKind : std::uint8_t {
    Noop,
    IdentityInplaceU8,
    XorInplaceU8,
    ReduceMetricsU8,
};

/**
 * @brief Starts with.
 * @param value Parameter for starts with.
 * @param prefix Parameter for starts with.
 */
bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

enum class DeviceStorageMode : std::uint8_t {
    Shared,
    Private,
};

/**
 * @brief Resolve device storage mode.
 */
DeviceStorageMode ResolveDeviceStorageMode() {
    const char* value = std::getenv("GRAPHX_METAL_DEVICE_STORAGE_MODE");
    if (value == nullptr) {
        return DeviceStorageMode::Shared;
    }

    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "private") {
        return DeviceStorageMode::Private;
    }
    return DeviceStorageMode::Shared;
}

/**
 * @brief Device resource options for allocation.
 */
MTL::ResourceOptions DeviceResourceOptionsForAllocation() {
    return ResolveDeviceStorageMode() == DeviceStorageMode::Private
               ? MTL::ResourceStorageModePrivate
               : MTL::ResourceStorageModeShared;
}

/**
 * @brief Allocate host token.
 * @param bytes Parameter for allocate host token.
 */
std::byte* AllocateHostToken(std::uint64_t bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    return new (std::nothrow) std::byte[bytes];
}

/**
 * @brief To ns string.
 * @param value Parameter for to ns string.
 */
NS::String* ToNSString(std::string_view value) {
    std::string owned(value);
    return NS::String::string(owned.c_str(), NS::UTF8StringEncoding);
}

std::optional<MetalKernelDescriptor> ParseKernelRegistration(
    std::uint64_t kernel_id,
    std::string_view registration) {
    constexpr std::string_view kBuiltinPrefix = "builtin:";
    constexpr std::string_view kSourcePrefix = "source:";
    constexpr std::string_view kMetallibPrefix = "metallib:";
    constexpr std::string_view kDivider = "::";

    MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id;

    if (StartsWith(registration, kBuiltinPrefix)) {
        const auto function_name = registration.substr(kBuiltinPrefix.size());
        if (!IsValidKernelName(function_name)) {
            return std::nullopt;
        }
        descriptor.source_kind = MetalKernelSourceKind::Builtin;
        descriptor.function_name = std::string(function_name);
        return descriptor;
    }

    if (StartsWith(registration, kSourcePrefix)) {
        const auto body = registration.substr(kSourcePrefix.size());
        const auto divider_pos = body.find(kDivider);
        if (divider_pos == std::string_view::npos) {
            return std::nullopt;
        }

        const auto function_name = body.substr(0, divider_pos);
        const auto source = body.substr(divider_pos + kDivider.size());
        if (!IsValidKernelName(function_name) || source.empty()) {
            return std::nullopt;
        }

        descriptor.source_kind = MetalKernelSourceKind::InlineSource;
        descriptor.function_name = std::string(function_name);
        descriptor.source_payload = std::string(source);
        return descriptor;
    }

    if (StartsWith(registration, kMetallibPrefix)) {
        const auto body = registration.substr(kMetallibPrefix.size());
        const auto divider_pos = body.find(kDivider);
        if (divider_pos == std::string_view::npos) {
            return std::nullopt;
        }

        const auto function_name = body.substr(0, divider_pos);
        const auto path = body.substr(divider_pos + kDivider.size());
        if (!IsValidKernelName(function_name) || path.empty()) {
            return std::nullopt;
        }

        descriptor.source_kind = MetalKernelSourceKind::MetallibPath;
        descriptor.function_name = std::string(function_name);
        descriptor.source_payload = std::string(path);
        return descriptor;
    }

    if (!IsValidKernelName(registration)) {
        return std::nullopt;
    }

    descriptor.source_kind = MetalKernelSourceKind::Builtin;
    descriptor.function_name = std::string(registration);
    return descriptor;
}

/**
 * @brief Resolve builtin kernel kind.
 * @param kernel_name Parameter for resolve builtin kernel kind.
 */
BuiltinKernelKind ResolveBuiltinKernelKind(std::string_view kernel_name) {
    if (kernel_name.find("xor") != std::string_view::npos ||
        kernel_name.find("transform") != std::string_view::npos) {
        return BuiltinKernelKind::XorInplaceU8;
    }

    if (kernel_name.find("identity") != std::string_view::npos ||
        kernel_name.find("stress") != std::string_view::npos ||
        kernel_name.find("lifecycle") != std::string_view::npos ||
        kernel_name.find("failure") != std::string_view::npos) {
        return BuiltinKernelKind::IdentityInplaceU8;
    }

    if (kernel_name.find("reduce") != std::string_view::npos ||
        kernel_name.find("metrics") != std::string_view::npos ||
        kernel_name.find("health") != std::string_view::npos) {
        return BuiltinKernelKind::ReduceMetricsU8;
    }

    return BuiltinKernelKind::Noop;
}

/**
 * @brief Populate builtin kernel defaults.
 * @param descriptor Parameter for populate builtin kernel defaults.
 */
void PopulateBuiltinKernelDefaults(MetalKernelDescriptor& descriptor) {
    if (descriptor.source_kind != MetalKernelSourceKind::Builtin ||
        !descriptor.arg_layout.empty()) {
        return;
    }

    switch (ResolveBuiltinKernelKind(descriptor.function_name)) {
    case BuiltinKernelKind::XorInplaceU8:
    case BuiltinKernelKind::IdentityInplaceU8:
        descriptor.arg_layout.push_back(MetalKernelArgDescriptor{
            MetalKernelArgKind::DeviceBuffer,
            MetalKernelArgAccess::ReadWrite});
        break;
    case BuiltinKernelKind::ReduceMetricsU8:
        descriptor.arg_layout.push_back(MetalKernelArgDescriptor{
            MetalKernelArgKind::DeviceBuffer,
            MetalKernelArgAccess::ReadOnly});
        descriptor.arg_layout.push_back(MetalKernelArgDescriptor{
            MetalKernelArgKind::DeviceBuffer,
            MetalKernelArgAccess::WriteOnly});
        break;
    case BuiltinKernelKind::Noop:
    default:
        break;
    }
}

/**
 * @brief Make kernel source.
 * @param kernel_name Parameter for make kernel source.
 * @param kind Parameter for make kernel source.
 */
std::string MakeKernelSource(std::string_view kernel_name, BuiltinKernelKind kind) {
    std::string source;
    source.reserve(512 + kernel_name.size());
    source += "#include <metal_stdlib>\n";
    source += "using namespace metal;\n";
    source += "kernel void ";
    source += kernel_name;

    switch (kind) {
    case BuiltinKernelKind::XorInplaceU8:
        source += "(device uchar* data [[buffer(0)]], uint gid [[thread_position_in_grid]]) {\n";
        source += "    data[gid] = uchar(data[gid] ^ uchar(0xA5));\n";
        source += "}\n";
        break;
    case BuiltinKernelKind::IdentityInplaceU8:
        source += "(device uchar* data [[buffer(0)]], uint gid [[thread_position_in_grid]]) {\n";
        source += "    data[gid] = data[gid];\n";
        source += "}\n";
        break;
    case BuiltinKernelKind::ReduceMetricsU8:
        source += "(const device uchar* data [[buffer(0)]], device uint* metrics [[buffer(1)]], "
                  "uint3 gid [[thread_position_in_grid]], uint3 threads [[threads_per_grid]]) {\n";
        source += "    if (gid.x != 0 || gid.y != 0 || gid.z != 0) { return; }\n";
        source += "    const uint count = threads.x;\n";
        source += "    if (count == 0) { metrics[0] = 0; metrics[1] = 0; return; }\n";
        source += "    ulong sumsq = 0;\n";
        source += "    uint peak = 0;\n";
        source += "    for (uint i = 0; i < count; ++i) {\n";
        source += "        const uint v = uint(data[i]);\n";
        source += "        sumsq += ulong(v * v);\n";
        source += "        if (v > peak) { peak = v; }\n";
        source += "    }\n";
        source += "    const float mean_sq = float(sumsq) / float(count);\n";
        source += "    metrics[0] = uint(sqrt(mean_sq));\n";
        source += "    metrics[1] = peak;\n";
        source += "}\n";
        break;
    case BuiltinKernelKind::Noop:
    default:
        source += "(uint3 gid [[thread_position_in_grid]]) { (void)gid; }\n";
        break;
    }

    return source;
}

/**
 * @brief Build library from source.
 * @param device Parameter for build library from source.
 * @param source Parameter for build library from source.
 */
MTL::Library* BuildLibraryFromSource(MTL::Device* device, std::string_view source) {
    if (device == nullptr || source.empty()) {
        return nullptr;
    }

    auto* source_string = ToNSString(source);
    if (source_string == nullptr) {
        return nullptr;
    }

    NS::Error* error = nullptr;
    auto* library = device->newLibrary(source_string, nullptr, &error);
    if (library == nullptr || error != nullptr) {
        if (library != nullptr) {
            library->release();
        }
        return nullptr;
    }

    return library;
}

/**
 * @brief Load library from metallib path.
 * @param device Parameter for load library from metallib path.
 * @param metallib_path Parameter for load library from metallib path.
 */
MTL::Library* LoadLibraryFromMetallibPath(MTL::Device* device, std::string_view metallib_path) {
    if (device == nullptr || metallib_path.empty()) {
        return nullptr;
    }

    auto* filepath = ToNSString(metallib_path);
    if (filepath == nullptr) {
        return nullptr;
    }

    NS::Error* error = nullptr;
    auto* library = device->newLibrary(filepath, &error);
    if (library == nullptr || error != nullptr) {
        if (library != nullptr) {
            library->release();
        }
        return nullptr;
    }

    return library;
}

bool CreateAndStoreKernelPipeline(std::uint64_t kernel_id,
                                  std::string_view entry_label,
                                  std::string_view function_name,
                                  const MetalKernelDescriptor& descriptor,
                                  MTL::Device* device,
                                  MTL::Library* library) {
    if (kernel_id == 0 || device == nullptr || library == nullptr || !IsValidKernelName(function_name)) {
        return false;
    }

    auto* function_name_ns = ToNSString(function_name);
    if (function_name_ns == nullptr) {
        library->release();
        return false;
    }

    auto* function = library->newFunction(function_name_ns);
    if (function == nullptr) {
        library->release();
        return false;
    }

    NS::Error* error = nullptr;
    auto* pipeline = device->newComputePipelineState(function, &error);
    if (pipeline == nullptr || error != nullptr) {
        function->release();
        library->release();
        return false;
    }

    auto& kernel_state = KernelState();
    std::scoped_lock kernel_lock(kernel_state.mutex);
    auto it = kernel_state.kernels.find(kernel_id);
    if (it != kernel_state.kernels.end()) {
        if (it->second.pipeline != nullptr) {
            it->second.pipeline->release();
        }
        if (it->second.function != nullptr) {
            it->second.function->release();
        }
        if (it->second.library != nullptr) {
            it->second.library->release();
        }
    }

    kernel_state.kernels[kernel_id] = NativeMetalKernelState::KernelEntry{
        std::string(entry_label),
        library,
        function,
        pipeline,
        descriptor.arg_layout,
        descriptor.dispatch};
    return true;
}

} // namespace

} // namespace graph::gpu::metal::capabilities

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
