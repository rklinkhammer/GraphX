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
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace graph::gpu::metal::capabilities {

namespace {

struct NativeMetalContextState {
    std::mutex mutex{};
    std::uint32_t current_device_id{0};
    std::uint64_t next_queue_id{1};
    std::uint64_t next_event_id{1};
    MTL::Device* active_device{nullptr};
    std::unordered_map<std::uint64_t, MTL::CommandQueue*> queues{};
    std::unordered_map<std::uint64_t, MTL::SharedEvent*> events{};
    std::unordered_set<std::uint64_t> synthetic_events{};
};

struct NativeMetalMemoryPoolState {
    std::mutex mutex{};
    std::uint64_t next_allocation_id{1};
    std::unordered_map<std::uint64_t, MTL::Buffer*> device_allocations{};
    std::unordered_map<std::uint64_t, MTL::Buffer*> shared_allocations{};
    std::unordered_map<std::uint64_t, MTL::Buffer*> host_allocations{};
};

struct NativeMetalTransferState {
    std::mutex mutex{};
    std::uint64_t next_transfer_id{1};
};

struct NativeMetalKernelState {
    struct KernelEntry {
        std::string name{};
        MTL::Library* library{nullptr};
        MTL::Function* function{nullptr};
        MTL::ComputePipelineState* pipeline{nullptr};
    };

    std::mutex mutex{};
    std::unordered_map<std::uint64_t, KernelEntry> kernels{};
};

struct NativeMetalTelemetryState {
    std::uint64_t transfer_samples{0};
    std::uint64_t kernel_samples{0};
    std::uint64_t error_count{0};
    std::uint64_t last_transfer_duration_ns{0};
    std::uint64_t last_kernel_duration_ns{0};
    std::unordered_map<std::string, std::uint64_t> error_code_counts{};
    mutable std::mutex mutex{};
};

NativeMetalContextState& ContextState() {
    static NativeMetalContextState state{};
    return state;
}

NativeMetalMemoryPoolState& MemoryPoolState() {
    static NativeMetalMemoryPoolState state{};
    return state;
}

NativeMetalTransferState& TransferState() {
    static NativeMetalTransferState state{};
    return state;
}

NativeMetalKernelState& KernelState() {
    static NativeMetalKernelState state{};
    return state;
}

NativeMetalTelemetryState& TelemetryState() {
    static NativeMetalTelemetryState state{};
    return state;
}

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

MTL::Device* EnsureActiveDeviceLocked(NativeMetalContextState& state) {
    if (state.active_device != nullptr) {
        return state.active_device;
    }

    state.active_device = AcquireDeviceById(state.current_device_id);
    return state.active_device;
}

MTL::Buffer* CreateBuffer(MTL::Device* device, std::uint64_t bytes, MTL::ResourceOptions options) {
    if (device == nullptr || bytes == 0) {
        return nullptr;
    }

    return device->newBuffer(bytes, options);
}

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

BufferResolution AcquireDeviceBufferFromPointer(void* pointer) {
    if (pointer == nullptr) {
        return {};
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock lock(pool_state.mutex);

    const auto find_in = [pointer](const auto& allocations) -> BufferResolution {
        const auto* pointer_bytes = static_cast<const std::byte*>(pointer);
        for (const auto& [_, buffer] : allocations) {
            if (buffer == nullptr || buffer->contents() == nullptr) {
                continue;
            }

            const auto* base_bytes = static_cast<const std::byte*>(buffer->contents());
            const auto buffer_length = static_cast<std::uint64_t>(buffer->length());
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

std::uint64_t NextTransferId() {
    auto& transfer_state = TransferState();
    std::scoped_lock lock(transfer_state.mutex);
    return transfer_state.next_transfer_id++;
}

bool ExecuteBlitCopy(MTL::CommandQueue* queue,
                    MTL::Buffer* src,
                    std::uint64_t src_offset,
                    MTL::Buffer* dst,
                    std::uint64_t dst_offset,
                    std::uint64_t copy_bytes) {
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
        command_buffer->release();
        return false;
    }

    blit->copyFromBuffer(src, src_offset, dst, dst_offset, copy_bytes);
    blit->endEncoding();
    command_buffer->commit();
    command_buffer->waitUntilCompleted();
    command_buffer->release();
    return true;
}

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

enum class KernelRegistrationMode : std::uint8_t {
    Builtin,
    Source,
    Metallib,
};

struct KernelRegistrationSpec {
    KernelRegistrationMode mode{KernelRegistrationMode::Builtin};
    std::string function_name{};
    std::string payload{};
};

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

NS::String* ToNSString(std::string_view value) {
    std::string owned(value);
    return NS::String::string(owned.c_str(), NS::UTF8StringEncoding);
}

std::optional<KernelRegistrationSpec> ParseKernelRegistration(std::string_view registration) {
    constexpr std::string_view kBuiltinPrefix = "builtin:";
    constexpr std::string_view kSourcePrefix = "source:";
    constexpr std::string_view kMetallibPrefix = "metallib:";
    constexpr std::string_view kDivider = "::";

    if (StartsWith(registration, kBuiltinPrefix)) {
        const auto function_name = registration.substr(kBuiltinPrefix.size());
        if (!IsValidKernelName(function_name)) {
            return std::nullopt;
        }
        return KernelRegistrationSpec{KernelRegistrationMode::Builtin, std::string(function_name), {}};
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

        return KernelRegistrationSpec{KernelRegistrationMode::Source,
                                      std::string(function_name),
                                      std::string(source)};
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

        return KernelRegistrationSpec{KernelRegistrationMode::Metallib,
                                      std::string(function_name),
                                      std::string(path)};
    }

    if (!IsValidKernelName(registration)) {
        return std::nullopt;
    }

    return KernelRegistrationSpec{KernelRegistrationMode::Builtin, std::string(registration), {}};
}

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
        std::string(entry_label), library, function, pipeline};
    return true;
}

} // namespace

bool NativeMetalRuntimeAvailable() {
    auto* device = MTL::CreateSystemDefaultDevice();
    if (device == nullptr) {
        return false;
    }

    device->release();
    return true;
}

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

bool NativeMetalContextCapability::SelectDevice(std::uint32_t device_id) {
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

std::uint32_t NativeMetalContextCapability::CurrentDevice() const {
    auto& state = ContextState();
    std::scoped_lock lock(state.mutex);
    return state.current_device_id;
}

std::uint64_t NativeMetalContextCapability::CreateCommandQueue() {
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

void NativeMetalContextCapability::DestroyCommandQueue(std::uint64_t queue_id) {
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

std::uint64_t NativeMetalContextCapability::CreateEvent() {
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

void NativeMetalContextCapability::DestroyEvent(std::uint64_t event_id) {
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

bool NativeMetalMemoryPoolCapability::AllocateDevice(std::uint64_t bytes,
                                                     std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
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
    pool_state.device_allocations.emplace(allocation_id, buffer);

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
    out_lease.device_view.device_ptr = buffer->contents();
    return true;
}

bool NativeMetalMemoryPoolCapability::AllocateShared(std::uint64_t bytes,
                                                     std::uint32_t device_id,
                                                     accel::BufferLease& out_lease) {
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
    pool_state.shared_allocations.emplace(allocation_id, buffer);

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
    out_lease.device_view.device_ptr = buffer->contents();
    return true;
}

bool NativeMetalMemoryPoolCapability::AllocateHost(std::uint64_t bytes,
                                                   accel::BufferLease& out_lease) {
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
    pool_state.host_allocations.emplace(allocation_id, buffer);

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

bool NativeMetalMemoryPoolCapability::Release(const accel::BufferLease& lease) {
    if (lease.allocation_id == 0) {
        return false;
    }

    auto& pool_state = MemoryPoolState();
    std::scoped_lock lock(pool_state.mutex);

    const auto release_from = [&lease](auto& allocations) {
        const auto it = allocations.find(lease.allocation_id);
        if (it == allocations.end()) {
            return false;
        }

        if (it->second != nullptr) {
            it->second->release();
        }
        allocations.erase(it);
        return true;
    };

    const bool released_device = release_from(pool_state.device_allocations);
    const bool released_shared = release_from(pool_state.shared_allocations);
    const bool released_host = release_from(pool_state.host_allocations);
    return released_device || released_shared || released_host;
}

bool NativeMetalTransferCapability::EnqueueH2D(const accel::HostPinnedBufferView& src,
                                               accel::DeviceBufferView& dst,
                                               std::uint64_t queue_id,
                                               accel::TransferTicket& out_ticket) {
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

    std::memcpy(staging->contents(), src.host_ptr, static_cast<std::size_t>(copy_bytes));
    const bool copied = ExecuteBlitCopy(queue, staging, 0, dst_buffer, dst_resolution.offset, copy_bytes);
    staging->release();
    dst_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
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

    const bool copied = ExecuteBlitCopy(queue, src_buffer, src_resolution.offset, staging, 0, copy_bytes);
    if (copied) {
        std::memcpy(dst.host_ptr, staging->contents(), static_cast<std::size_t>(copy_bytes));
    }

    staging->release();
    src_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
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

    const bool copied = ExecuteBlitCopy(
        queue,
        src_buffer,
        src_resolution.offset,
        dst_buffer,
        dst_resolution.offset,
        copy_bytes);
    src_buffer->release();
    dst_buffer->release();
    queue->release();
    if (!copied) {
        return false;
    }

    const auto completion_event = RegisterTransferCompletionEvent();
    if (completion_event == 0) {
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

bool NativeMetalKernelCapability::RegisterKernel(std::uint64_t kernel_id,
                                                 std::string_view kernel_name) {
    const auto spec = ParseKernelRegistration(kernel_name);
    if (!spec.has_value()) {
        return false;
    }

    switch (spec->mode) {
    case KernelRegistrationMode::Builtin:
        return RegisterKernelBuiltin(kernel_id, spec->function_name);
    case KernelRegistrationMode::Source:
        return RegisterKernelFromSource(kernel_id, spec->function_name, spec->payload);
    case KernelRegistrationMode::Metallib:
        return RegisterKernelFromMetallib(kernel_id, spec->function_name, spec->payload);
    default:
        return false;
    }
}

bool NativeMetalKernelCapability::RegisterKernelBuiltin(std::uint64_t kernel_id,
                                                        std::string_view function_name) {
    if (kernel_id == 0 || !IsValidKernelName(function_name)) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);
    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return false;
    }

    const auto kernel_kind = ResolveBuiltinKernelKind(function_name);
    const auto source_text = MakeKernelSource(function_name, kernel_kind);
    auto* library = BuildLibraryFromSource(device, source_text);
    if (library == nullptr) {
        return false;
    }

    return CreateAndStoreKernelPipeline(kernel_id, function_name, function_name, device, library);
}

bool NativeMetalKernelCapability::RegisterKernelFromSource(std::uint64_t kernel_id,
                                                           std::string_view function_name,
                                                           std::string_view msl_source) {
    if (kernel_id == 0 || !IsValidKernelName(function_name) || msl_source.empty()) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);
    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return false;
    }

    auto* library = BuildLibraryFromSource(device, msl_source);
    if (library == nullptr) {
        return false;
    }

    std::string entry_label = "source:";
    entry_label += function_name;
    return CreateAndStoreKernelPipeline(kernel_id, entry_label, function_name, device, library);
}

bool NativeMetalKernelCapability::RegisterKernelFromMetallib(std::uint64_t kernel_id,
                                                             std::string_view function_name,
                                                             std::string_view metallib_path) {
    if (kernel_id == 0 || !IsValidKernelName(function_name) || metallib_path.empty()) {
        return false;
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);
    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return false;
    }

    auto* library = LoadLibraryFromMetallibPath(device, metallib_path);
    if (library == nullptr) {
        return false;
    }

    std::string entry_label = "metallib:";
    entry_label += function_name;
    entry_label += "::";
    entry_label += metallib_path;
    return CreateAndStoreKernelPipeline(kernel_id, entry_label, function_name, device, library);
}

bool NativeMetalKernelCapability::Launch(const accel::KernelTicket& ticket,
                                         void* const* args,
                                         std::size_t arg_count) {
    if (!accel::IsValidKernelTicket(ticket)) {
        return false;
    }
    if (arg_count != ticket.arg_count) {
        return false;
    }
    if (arg_count > 0 && args == nullptr) {
        return false;
    }

    MTL::ComputePipelineState* pipeline = nullptr;
    {
        auto& kernel_state = KernelState();
        std::scoped_lock kernel_lock(kernel_state.mutex);
        const auto it = kernel_state.kernels.find(ticket.kernel_id);
        if (it == kernel_state.kernels.end() || it->second.pipeline == nullptr) {
            return false;
        }
        pipeline = it->second.pipeline;
        pipeline->retain();
    }

    auto* queue = AcquireQueue(ticket.execution_queue_id);
    if (queue == nullptr) {
        pipeline->release();
        return false;
    }

    auto* command_buffer = queue->commandBuffer();
    if (command_buffer == nullptr) {
        queue->release();
        pipeline->release();
        return false;
    }

    auto* encoder = command_buffer->computeCommandEncoder();
    if (encoder == nullptr) {
        command_buffer->release();
        queue->release();
        pipeline->release();
        return false;
    }

    encoder->setComputePipelineState(pipeline);

    std::vector<MTL::Buffer*> bound_buffers;
    bound_buffers.reserve(arg_count);
    for (std::size_t i = 0; i < arg_count; ++i) {
        if (args[i] == nullptr) {
            for (auto* b : bound_buffers) {
                b->release();
            }
            encoder->endEncoding();
            command_buffer->release();
            queue->release();
            pipeline->release();
            return false;
        }

        auto* view = static_cast<accel::DeviceBufferView*>(args[i]);
        if (!accel::IsValidView(*view) || view->backend != accel::BackendKind::Metal) {
            for (auto* b : bound_buffers) {
                b->release();
            }
            encoder->endEncoding();
            command_buffer->release();
            queue->release();
            pipeline->release();
            return false;
        }

        const auto resolution = AcquireDeviceBufferFromPointer(view->device_ptr);
        auto* buffer = resolution.buffer;
        if (buffer == nullptr) {
            for (auto* b : bound_buffers) {
                b->release();
            }
            encoder->endEncoding();
            command_buffer->release();
            queue->release();
            pipeline->release();
            return false;
        }

        encoder->setBuffer(buffer, resolution.offset, static_cast<NS::UInteger>(i));
        bound_buffers.push_back(buffer);
    }

    const auto threadgroups = MTL::Size::Make(ticket.launch.grid_x,
                                              ticket.launch.grid_y,
                                              ticket.launch.grid_z);
    const auto threads_per_group = MTL::Size::Make(ticket.launch.block_x,
                                                   ticket.launch.block_y,
                                                   ticket.launch.block_z);
    encoder->dispatchThreadgroups(threadgroups, threads_per_group);
    encoder->endEncoding();
    command_buffer->commit();
    command_buffer->waitUntilCompleted();

    for (auto* b : bound_buffers) {
        b->release();
    }

    command_buffer->release();
    queue->release();
    pipeline->release();
    return true;
}

void NativeMetalTelemetryCapability::RecordTransfer(const accel::TransferTicket& ticket,
                                                    std::uint64_t duration_ns) {
    if (!accel::IsValidTransferTicket(ticket)) {
        IncrementErrorCounter("invalid-transfer-ticket");
        return;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.transfer_samples;
    telemetry.last_transfer_duration_ns = duration_ns;
}

void NativeMetalTelemetryCapability::RecordKernel(const accel::KernelTicket& ticket,
                                                  std::uint64_t duration_ns) {
    if (!accel::IsValidKernelTicket(ticket)) {
        IncrementErrorCounter("invalid-kernel-ticket");
        return;
    }

    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.kernel_samples;
    telemetry.last_kernel_duration_ns = duration_ns;
}

void NativeMetalTelemetryCapability::IncrementErrorCounter(std::string_view error_code) {
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    ++telemetry.error_count;
    telemetry.error_code_counts[std::string(error_code)]++;
}

std::uint64_t NativeMetalTelemetryCapability::TransferSamples() const {
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.transfer_samples;
}

std::uint64_t NativeMetalTelemetryCapability::KernelSamples() const {
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.kernel_samples;
}

std::uint64_t NativeMetalTelemetryCapability::ErrorCount() const {
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    return telemetry.error_count;
}

#if GRAPHX_ENABLE_GPU_TEST_HOOKS
void NativeMetalTelemetryCapability::ResetForTesting() {
    auto& telemetry = TelemetryState();
    std::scoped_lock lock(telemetry.mutex);
    telemetry.transfer_samples = 0;
    telemetry.kernel_samples = 0;
    telemetry.error_count = 0;
    telemetry.last_transfer_duration_ns = 0;
    telemetry.last_kernel_duration_ns = 0;
    telemetry.error_code_counts.clear();
}
#endif

bool NativeMetalCollectiveCapability::AllReduce(accel::DeviceBufferView& in_out,
                                                const accel::CollectiveTicket& ticket) {
    if (!accel::IsValidView(in_out) || !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    // Single-process baseline: treat as in-place no-op reduction and surface completion.
    in_out.ready_event = ticket.completion_event;
    return true;
}

bool NativeMetalCollectiveCapability::AllGather(const accel::DeviceBufferView& input,
                                                accel::DeviceBufferView& output,
                                                const accel::CollectiveTicket& ticket) {
    if (!accel::IsValidView(input) || !accel::IsValidView(output) ||
        !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    const auto copy_bytes = std::min(input.bytes, output.bytes);
    if (copy_bytes == 0) {
        return false;
    }

    std::memcpy(output.device_ptr, input.device_ptr, static_cast<std::size_t>(copy_bytes));
    output.ready_event = ticket.completion_event;
    return true;
}

bool NativeMetalCollectiveCapability::ReduceScatter(const accel::DeviceBufferView& input,
                                                    accel::DeviceBufferView& output,
                                                    const accel::CollectiveTicket& ticket) {
    if (!accel::IsValidView(input) || !accel::IsValidView(output) ||
        !accel::IsValidCollectiveTicket(ticket)) {
        return false;
    }

    const auto copy_bytes = std::min(input.bytes, output.bytes);
    if (copy_bytes == 0) {
        return false;
    }

    std::memcpy(output.device_ptr, input.device_ptr, static_cast<std::size_t>(copy_bytes));
    output.ready_event = ticket.completion_event;
    return true;
}

} // namespace graph::gpu::metal::capabilities
