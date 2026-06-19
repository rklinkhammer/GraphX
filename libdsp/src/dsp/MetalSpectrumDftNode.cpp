// SPDX-License-Identifier: MIT

#include "dsp/MetalSpectrumDftNode.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <algorithm>
#include <sstream>
#include <typeinfo>

namespace dsp {

namespace {

const char* BackendName(graph::gpu::accel::BackendKind backend) noexcept {
    switch (backend) {
        case graph::gpu::accel::BackendKind::CUDA: return "CUDA";
        case graph::gpu::accel::BackendKind::SYCL: return "SYCL";
        case graph::gpu::accel::BackendKind::Metal: return "Metal";
        case graph::gpu::accel::BackendKind::Unknown:
        default: return "Unknown";
    }
}

}  // namespace

template <std::size_t N>
MetalSpectrumDftNode<N>::MetalSpectrumDftNode() {
    this->SetName("__unnamed__");
    kernel_descriptor_ = BuildKernelDescriptor();
}

template <std::size_t N>
MetalSpectrumDftNode<N>::~MetalSpectrumDftNode() {
    if (owns_queue_ && context_ && queue_id_ != 0) {
        context_->DestroyCommandQueue(queue_id_);
    }
}

template <std::size_t N>
bool MetalSpectrumDftNode<N>::BindGpuCapabilities(graph::CapabilityBus& capability_bus) {
    context_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    shared_queue_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    memory_pool_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    kernel_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalKernelCapability>();
    kernel_descriptor_capability_ =
        std::dynamic_pointer_cast<graph::gpu::metal::capabilities::IMetalKernelDescriptorCapability>(kernel_);
    telemetry_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalTelemetryCapability>();

    if (context_ && !context_->SelectDevice(device_id_)) {
        return false;
    }

    (void)ResolveQueueId();
    if (!memory_pool_ || !kernel_ || !kernel_descriptor_capability_ || !telemetry_ ||
        queue_id_ == 0) {
        return false;
    }

    return RegisterDftKernel();
}

template <std::size_t N>
std::optional<typename MetalSpectrumDftNode<N>::OutputTokenType>
MetalSpectrumDftNode<N>::Transfer(
    const InputTokenType& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!memory_pool_ || !kernel_ || !telemetry_ || !kernel_registered_ ||
        ResolveQueueId() == 0 || !input.has_device_view ||
        !graph::gpu::accel::IsValidView(input.device_view) ||
        input.device_view.backend != graph::gpu::accel::BackendKind::Metal) {
        return std::nullopt;
    }

    const IqPacketType* input_packet = nullptr;
    try {
        input_packet = &input.sidecar.template get<IqPacketType>();
    } catch (const std::bad_cast&) {
        return std::nullopt;
    }
    if (input_packet == nullptr) {
        return std::nullopt;
    }

    if (input.has_transfer_ticket && context_ &&
        input.transfer_ticket.completion_event != 0 &&
        !context_->WaitEvent(input.transfer_ticket.completion_event, 5000)) {
        return std::nullopt;
    }

    graph::gpu::accel::BufferLease output_lease{};
    if (!memory_pool_->AllocateDevice(DspGpuBufferLayout<N>::kMagnitudeBytes,
                                      device_id_,
                                      output_lease)) {
        return std::nullopt;
    }

    auto output_view = output_lease.device_view;
    output_view.backend = graph::gpu::accel::BackendKind::Metal;
    output_view.bytes = DspGpuBufferLayout<N>::kMagnitudeBytes;
    output_view.dtype = DspGpuBufferLayout<N>::ScalarDataType();
    output_view.layout = DspGpuBufferLayout<N>::MagnitudeTensorLayout();
    output_view.device_id = device_id_;
    output_view.execution_queue_id = queue_id_;

    if (!graph::gpu::accel::IsValidView(output_view)) {
        return std::nullopt;
    }

    graph::gpu::accel::KernelTicket ticket{};
    ticket.backend = graph::gpu::accel::BackendKind::Metal;
    ticket.kernel_id = kernel_id_;
    ticket.execution_queue_id = queue_id_;
    ticket.arg_count = 2;
    ticket.completion_event = context_ ? context_->CreateEvent() : ++kernel_sequence_;
    if (ticket.completion_event == 0) {
        return std::nullopt;
    }
    if (!PopulateRegisteredKernelExecution(ticket)) {
        ticket.launch.grid_x = static_cast<std::uint32_t>(DspGpuBufferLayout<N>::kMagnitudeBinCount);
        ticket.launch.grid_y = 1;
        ticket.launch.grid_z = 1;
        ticket.launch.block_x = 1;
        ticket.launch.block_y = 1;
        ticket.launch.block_z = 1;
    }
    if (!graph::gpu::accel::IsValidKernelTicket(ticket)) {
        return std::nullopt;
    }

    auto input_view = input.device_view;
    graph::gpu::accel::DeviceBufferView* arg0 = &input_view;
    graph::gpu::accel::DeviceBufferView* arg1 = &output_view;
    void* const args[] = {arg0, arg1};
    if (!kernel_->Launch(ticket, args, 2)) {
        return std::nullopt;
    }

    telemetry_->RecordKernel(ticket, 0);
    output_view.ready_event = ticket.completion_event;
    output_lease.device_view = output_view;

    MagnitudePacketType sidecar{};
    sidecar.packet_number = input_packet->packet_number;
    sidecar.sample_rate_hz = input_packet->sample_rate_hz;
    sidecar.num_accumulated_packets = 1;
    sidecar.valid = true;

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = sidecar;
    output.lease = output_lease;
    output.device_view = output_view;
    output.host_view = input.host_view;
    output.transfer_ticket = input.transfer_ticket;
    output.kernel_ticket = ticket;
    output.has_lease = true;
    output.has_device_view = true;
    output.has_host_view = input.has_host_view;
    output.has_transfer_ticket = input.has_transfer_ticket;
    output.has_kernel_ticket = true;

    last_output_lease_ = output_lease;
    last_input_view_ = input.device_view;
    last_output_view_ = output_view;
    last_kernel_ticket_ = ticket;
    return output;
}

template <std::size_t N>
void MetalSpectrumDftNode<N>::Configure(const graph::JsonView& cfg) {
    if (cfg.Contains("queue_id")) {
        auto parsed = cfg.TryGetInt("queue_id");
        if (!parsed) {
            throw parsed.error();
        }
        if (parsed.value() < 0) {
            throw graph::ConfigError("queue_id must be >= 0");
        }
        queue_id_ = static_cast<std::uint64_t>(parsed.value());
        owns_queue_ = false;
    }

    if (cfg.Contains("device_id")) {
        auto parsed = cfg.TryGetInt("device_id");
        if (!parsed) {
            throw parsed.error();
        }
        if (parsed.value() < 0) {
            throw graph::ConfigError("device_id must be >= 0");
        }
        device_id_ = static_cast<std::uint32_t>(parsed.value());
    } else if (cfg.Contains("backend_id")) {
        auto parsed = cfg.TryGetInt("backend_id");
        if (!parsed) {
            throw parsed.error();
        }
        if (parsed.value() < 0) {
            throw graph::ConfigError("backend_id must be >= 0");
        }
        device_id_ = static_cast<std::uint32_t>(parsed.value());
    }

    if (cfg.Contains("kernel_id")) {
        auto parsed = cfg.TryGetInt("kernel_id");
        if (!parsed) {
            throw parsed.error();
        }
        if (parsed.value() <= 0) {
            throw graph::ConfigError("kernel_id must be > 0");
        }
        kernel_id_ = static_cast<std::uint64_t>(parsed.value());
    }

    kernel_descriptor_ = BuildKernelDescriptor();
    kernel_registered_ = false;
    if (kernel_descriptor_capability_) {
        kernel_registered_ = RegisterDftKernel();
    }
}

template <std::size_t N>
graph::JsonView MetalSpectrumDftNode<N>::GetParameters() const {
    parameters_cache_ = {
        {"queue_id", queue_id_},
        {"device_id", device_id_},
        {"kernel_id", kernel_id_},
        {"input_iq_bytes", DspGpuBufferLayout<N>::kIqBytes},
        {"output_magnitude_bytes", DspGpuBufferLayout<N>::kMagnitudeBytes},
        {"algorithm", "direct_dft"},
    };
    return graph::JsonView(parameters_cache_);
}

template <std::size_t N>
graph::JsonView MetalSpectrumDftNode<N>::GetParameterDescription(const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    if (param_name == "queue_id") {
        parameter_description_cache_ = {
            {"type", "integer"},
            {"required", false},
            {"description", "Optional Metal command queue id. 0 means bind from shared queue or context."},
        };
    } else if (param_name == "device_id" || param_name == "backend_id") {
        parameter_description_cache_ = {
            {"type", "integer"},
            {"required", false},
            {"description", "Metal device id used for output magnitude allocation."},
        };
    } else if (param_name == "kernel_id") {
        parameter_description_cache_ = {
            {"type", "integer"},
            {"required", false},
            {"description", "Non-zero Metal kernel registration id for the inline DFT kernel."},
        };
    }
    return graph::JsonView(parameter_description_cache_);
}

template <std::size_t N>
std::vector<std::string> MetalSpectrumDftNode<N>::GetParameterNames() const {
    return {"queue_id", "device_id", "backend_id", "kernel_id"};
}

template <std::size_t N>
graph::JsonView MetalSpectrumDftNode<N>::GetDiagnostics() const {
    diagnostics_cache_ = {
        {"backend", BackendName(last_output_view_.backend)},
        {"kernel_registered", kernel_registered_},
        {"kernel_id", kernel_id_},
        {"has_input_device_view", graph::gpu::accel::IsValidView(last_input_view_)},
        {"has_output_device_view", graph::gpu::accel::IsValidView(last_output_view_)},
        {"has_kernel_ticket", graph::gpu::accel::IsValidKernelTicket(last_kernel_ticket_)},
        {"input_iq_bytes", last_input_view_.bytes},
        {"output_magnitude_bytes", last_output_view_.bytes},
        {"algorithm", "direct_dft"},
    };
    return graph::JsonView(diagnostics_cache_);
}

template <std::size_t N>
const graph::gpu::metal::capabilities::MetalKernelDescriptor&
MetalSpectrumDftNode<N>::kernel_descriptor() const noexcept {
    return kernel_descriptor_;
}

template <std::size_t N>
const graph::gpu::accel::KernelTicket& MetalSpectrumDftNode<N>::last_kernel_ticket() const noexcept {
    return last_kernel_ticket_;
}

template <std::size_t N>
const graph::gpu::accel::DeviceBufferView& MetalSpectrumDftNode<N>::last_input_view() const noexcept {
    return last_input_view_;
}

template <std::size_t N>
const graph::gpu::accel::DeviceBufferView& MetalSpectrumDftNode<N>::last_output_view() const noexcept {
    return last_output_view_;
}

template <std::size_t N>
graph::gpu::metal::capabilities::MetalKernelDescriptor
MetalSpectrumDftNode<N>::BuildKernelDescriptor() const {
    graph::gpu::metal::capabilities::MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = kernel_id_;
    descriptor.function_name = "graphx_dsp_metal_spectrum_dft_256";
    descriptor.source_kind = graph::gpu::metal::capabilities::MetalKernelSourceKind::InlineSource;
    descriptor.source_payload = BuildKernelSource();
    descriptor.arg_layout = {
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadOnly},
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::WriteOnly},
    };
    descriptor.dispatch.default_grid_x =
        static_cast<std::uint32_t>(DspGpuBufferLayout<N>::kMagnitudeBinCount);
    descriptor.dispatch.default_grid_y = 1;
    descriptor.dispatch.default_grid_z = 1;
    descriptor.dispatch.default_block_x = 1;
    descriptor.dispatch.default_block_y = 1;
    descriptor.dispatch.default_block_z = 1;
    return descriptor;
}

template <std::size_t N>
std::string MetalSpectrumDftNode<N>::BuildKernelSource() const {
    std::ostringstream source;
    source << "#include <metal_stdlib>\n"
           << "using namespace metal;\n"
           << "kernel void graphx_dsp_metal_spectrum_dft_256(\n"
           << "    const device float2* iq [[buffer(0)]],\n"
           << "    device float* magnitudes [[buffer(1)]],\n"
           << "    uint gid [[thread_position_in_grid]]) {\n"
           << "    constexpr uint kSampleCount = " << N << "u;\n"
           << "    constexpr uint kBinCount = " << DspGpuBufferLayout<N>::kMagnitudeBinCount << "u;\n"
           << "    constexpr float kTwoPi = 6.28318530717958647692f;\n"
           << "    if (gid >= kBinCount) { return; }\n"
           << "    float2 accum = float2(0.0f, 0.0f);\n"
           << "    for (uint n = 0; n < kSampleCount; ++n) {\n"
           << "        const float angle = -kTwoPi * float(gid) * float(n) / float(kSampleCount);\n"
           << "        const float c = cos(angle);\n"
           << "        const float s = sin(angle);\n"
           << "        const float2 sample = iq[n];\n"
           << "        accum.x += sample.x * c - sample.y * s;\n"
           << "        accum.y += sample.x * s + sample.y * c;\n"
           << "    }\n"
           << "    magnitudes[gid] = sqrt(accum.x * accum.x + accum.y * accum.y);\n"
           << "}\n";
    return source.str();
}

template <std::size_t N>
std::uint64_t MetalSpectrumDftNode<N>::ResolveQueueId() {
    if (queue_id_ != 0) {
        return queue_id_;
    }
    if (shared_queue_) {
        queue_id_ = shared_queue_->GetOrCreateQueueId();
        owns_queue_ = false;
    }
    if (queue_id_ == 0 && context_) {
        queue_id_ = context_->CreateCommandQueue();
        owns_queue_ = queue_id_ != 0;
    }
    return queue_id_;
}

template <std::size_t N>
bool MetalSpectrumDftNode<N>::RegisterDftKernel() {
    if (!kernel_descriptor_capability_ || kernel_id_ == 0) {
        kernel_registered_ = false;
        return false;
    }
    kernel_descriptor_ = BuildKernelDescriptor();
    kernel_registered_ = kernel_descriptor_capability_->RegisterKernelDescriptor(kernel_descriptor_);
    return kernel_registered_;
}

template <std::size_t N>
bool MetalSpectrumDftNode<N>::PopulateRegisteredKernelExecution(
    graph::gpu::accel::KernelTicket& ticket) const {
    if (!kernel_) {
        return false;
    }

    graph::gpu::metal::capabilities::IMetalKernelCapability::RegisteredKernelExecution execution{};
    if (!kernel_->TryGetRegisteredKernelExecution(kernel_id_, execution)) {
        return false;
    }

    if (execution.arg_count != 0) {
        ticket.arg_count = execution.arg_count;
    }
    ticket.launch = execution.dispatch;
    return true;
}

template class MetalSpectrumDftNode<256>;

}  // namespace dsp
