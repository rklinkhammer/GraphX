#include "gpu/metal/native/NativeMetalRuntimeInternal.hpp"

namespace graph::gpu::metal::capabilities {

bool NativeMetalKernelCapability::RegisterKernel(std::uint64_t kernel_id,
                                                 std::string_view kernel_name) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    const auto descriptor = ParseKernelRegistration(kernel_id, kernel_name);
    if (!descriptor.has_value()) {
        return false;
    }

    return RegisterKernel(*descriptor);
}

/**
 * @brief Register kernel descriptor.
 * @param descriptor Parameter for register kernel descriptor.
 */
bool NativeMetalKernelCapability::RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) {
    return RegisterKernel(descriptor);
}

/**
 * @brief Register kernel.
 * @param descriptor Parameter for register kernel.
 */
bool NativeMetalKernelCapability::RegisterKernel(const MetalKernelDescriptor& descriptor) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    if (descriptor.kernel_id == 0 || !IsValidKernelName(descriptor.function_name)) {
        return false;
    }

    auto canonical_descriptor = descriptor;
    PopulateBuiltinKernelDefaults(canonical_descriptor);

    for (const auto& arg : canonical_descriptor.arg_layout) {
        if (arg.kind != MetalKernelArgKind::DeviceBuffer) {
            return false;
        }
    }

    auto& context_state = ContextState();
    std::scoped_lock context_lock(context_state.mutex);
    auto* device = EnsureActiveDeviceLocked(context_state);
    if (device == nullptr) {
        return false;
    }

    MTL::Library* library = nullptr;
    std::string entry_label{};

    switch (canonical_descriptor.source_kind) {
    case MetalKernelSourceKind::Builtin: {
        const auto kernel_kind = ResolveBuiltinKernelKind(canonical_descriptor.function_name);
        const auto source_text = MakeKernelSource(canonical_descriptor.function_name, kernel_kind);
        library = BuildLibraryFromSource(device, source_text);
        entry_label = canonical_descriptor.function_name;
        break;
    }
    case MetalKernelSourceKind::InlineSource:
        if (canonical_descriptor.source_payload.empty()) {
            return false;
        }
        library = BuildLibraryFromSource(device, canonical_descriptor.source_payload);
        entry_label = "source:" + canonical_descriptor.function_name;
        break;
    case MetalKernelSourceKind::MetallibPath:
        if (canonical_descriptor.source_payload.empty()) {
            return false;
        }
        library = LoadLibraryFromMetallibPath(device, canonical_descriptor.source_payload);
        entry_label = "metallib:" + canonical_descriptor.function_name + "::" + canonical_descriptor.source_payload;
        break;
    default:
        return false;
    }

    if (library == nullptr) {
        return false;
    }

    return CreateAndStoreKernelPipeline(
        canonical_descriptor.kernel_id,
        entry_label,
        canonical_descriptor.function_name,
        canonical_descriptor,
        device,
        library);
}

bool NativeMetalKernelCapability::RegisterKernelBuiltin(std::uint64_t kernel_id,
                                                        std::string_view function_name) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id;
    descriptor.function_name = std::string(function_name);
    descriptor.source_kind = MetalKernelSourceKind::Builtin;
    PopulateBuiltinKernelDefaults(descriptor);
    return RegisterKernel(descriptor);
}

bool NativeMetalKernelCapability::RegisterKernelFromSource(std::uint64_t kernel_id,
                                                           std::string_view function_name,
                                                           std::string_view msl_source) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id;
    descriptor.function_name = std::string(function_name);
    descriptor.source_kind = MetalKernelSourceKind::InlineSource;
    descriptor.source_payload = std::string(msl_source);
    return RegisterKernel(descriptor);
}

bool NativeMetalKernelCapability::RegisterKernelFromMetallib(std::uint64_t kernel_id,
                                                             std::string_view function_name,
                                                             std::string_view metallib_path) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id;
    descriptor.function_name = std::string(function_name);
    descriptor.source_kind = MetalKernelSourceKind::MetallibPath;
    descriptor.source_payload = std::string(metallib_path);
    return RegisterKernel(descriptor);
}

bool NativeMetalKernelCapability::TryGetRegisteredKernelExecution(
    std::uint64_t kernel_id,
    RegisteredKernelExecution& out_execution) const {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
    auto& kernel_state = KernelState();
    std::scoped_lock kernel_lock(kernel_state.mutex);
    const auto it = kernel_state.kernels.find(kernel_id);
    if (it == kernel_state.kernels.end()) {
        return false;
    }

    out_execution.arg_count = static_cast<std::uint32_t>(it->second.arg_layout.size());
    out_execution.dispatch.grid_x = std::max(1U, it->second.dispatch.default_grid_x);
    out_execution.dispatch.grid_y = std::max(1U, it->second.dispatch.default_grid_y);
    out_execution.dispatch.grid_z = std::max(1U, it->second.dispatch.default_grid_z);
    out_execution.dispatch.block_x = std::max(1U, it->second.dispatch.default_block_x);
    out_execution.dispatch.block_y = std::max(1U, it->second.dispatch.default_block_y);
    out_execution.dispatch.block_z = std::max(1U, it->second.dispatch.default_block_z);
    return true;
}

bool NativeMetalKernelCapability::Launch(const accel::KernelTicket& ticket,
                                         void* const* args,
                                         std::size_t arg_count) {
    ScopedNativeMetalRuntimeContext runtime_guard(runtime_context_.get());
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
    std::size_t expected_args = 0;
    {
        auto& kernel_state = KernelState();
        std::scoped_lock kernel_lock(kernel_state.mutex);
        const auto it = kernel_state.kernels.find(ticket.kernel_id);
        if (it == kernel_state.kernels.end() || it->second.pipeline == nullptr) {
            return false;
        }
        if (!it->second.arg_layout.empty()) {
            expected_args = it->second.arg_layout.size();
            if (ticket.arg_count != expected_args) {
                return false;
            }
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

    auto* event = AcquireSharedEvent(ticket.completion_event);
    if (event != nullptr) {
        command_buffer->encodeSignalEvent(event, 1);
    }

    command_buffer->commit();

    for (auto* b : bound_buffers) {
        b->release();
    }

    if (event != nullptr) {
        event->release();
    }

    queue->release();
    pipeline->release();
    return true;
}

} // namespace graph::gpu::metal::capabilities
