// SPDX-License-Identifier: MIT

#include "dsp/DspMagnitudeD2HNode.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <algorithm>

namespace dsp {

namespace {

const char* BackendName(graph::gpu::accel::BackendKind backend) noexcept {
    switch (backend) {
        case graph::gpu::accel::BackendKind::CPU: return "CPU";
        case graph::gpu::accel::BackendKind::CUDA: return "CUDA";
        case graph::gpu::accel::BackendKind::SYCL: return "SYCL";
        case graph::gpu::accel::BackendKind::Metal: return "Metal";
        case graph::gpu::accel::BackendKind::Unknown:
        default: return "Unknown";
    }
}

}  // namespace

template <std::size_t N>
DspMagnitudeD2HNode<N>::DspMagnitudeD2HNode() {
    this->SetName("__unnamed__");
}

template <std::size_t N>
DspMagnitudeD2HNode<N>::~DspMagnitudeD2HNode() {
    if (owns_queue_ && context_ && queue_id_ != 0) {
        context_->DestroyCommandQueue(queue_id_);
    }
}

template <std::size_t N>
bool DspMagnitudeD2HNode<N>::BindGpuCapabilities(graph::CapabilityBus& capability_bus) {
    context_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    shared_queue_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    transfer_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();

    if (context_ && !context_->SelectDevice(0U)) {
        return false;
    }

    (void)ResolveQueueId();
    return transfer_ != nullptr && queue_id_ != 0;
}

template <std::size_t N>
std::optional<typename DspMagnitudeD2HNode<N>::TokenType>
DspMagnitudeD2HNode<N>::Transfer(
    const TokenType& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!transfer_ || ResolveQueueId() == 0 || !input.has_device_view ||
        !graph::gpu::accel::IsValidView(input.device_view) ||
        input.device_view.backend != graph::gpu::accel::BackendKind::Metal ||
        input.device_view.bytes < DspGpuBufferLayout<N>::kMagnitudeBytes) {
        return std::nullopt;
    }

    auto host_view = BuildHostView();
    if (!graph::gpu::accel::IsValidView(host_view)) {
        return std::nullopt;
    }

    if (input.has_kernel_ticket && context_ &&
        input.kernel_ticket.completion_event != 0 &&
        !context_->WaitEvent(input.kernel_ticket.completion_event, 5000)) {
        return std::nullopt;
    }

    auto device_view = input.device_view;
    graph::gpu::accel::TransferTicket ticket{};
    if (!transfer_->EnqueueD2H(device_view, host_view, queue_id_, ticket)) {
        return std::nullopt;
    }
    if (!graph::gpu::accel::IsValidTransferTicket(ticket)) {
        return std::nullopt;
    }

    auto output = input;
    output.host_view = host_view;
    output.has_host_view = true;
    output.transfer_ticket = ticket;
    output.has_transfer_ticket = true;
    output.sidecar = ReconstructPacket(input.sidecar);

    last_host_view_ = host_view;
    last_device_view_ = device_view;
    last_transfer_ticket_ = ticket;
    last_packet_ = output.sidecar;
    return output;
}

template <std::size_t N>
void DspMagnitudeD2HNode<N>::Configure(const graph::JsonView& cfg) {
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
}

template <std::size_t N>
graph::JsonView DspMagnitudeD2HNode<N>::GetParameters() const {
    parameters_cache_ = {
        {"queue_id", queue_id_},
        {"magnitude_bins", DspGpuBufferLayout<N>::kMagnitudeBinCount},
        {"magnitude_bytes", DspGpuBufferLayout<N>::kMagnitudeBytes},
    };
    return graph::JsonView(parameters_cache_);
}

template <std::size_t N>
graph::JsonView DspMagnitudeD2HNode<N>::GetParameterDescription(const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    if (param_name == "queue_id") {
        parameter_description_cache_ = {
            {"type", "integer"},
            {"required", false},
            {"description", "Optional Metal command queue id. 0 means bind from shared queue or context."},
        };
    }
    return graph::JsonView(parameter_description_cache_);
}

template <std::size_t N>
std::vector<std::string> DspMagnitudeD2HNode<N>::GetParameterNames() const {
    return {"queue_id"};
}

template <std::size_t N>
graph::JsonView DspMagnitudeD2HNode<N>::GetDiagnostics() const {
    diagnostics_cache_ = {
        {"has_host_view", graph::gpu::accel::IsValidView(last_host_view_)},
        {"has_device_view", graph::gpu::accel::IsValidView(last_device_view_)},
        {"has_transfer_ticket", graph::gpu::accel::IsValidTransferTicket(last_transfer_ticket_)},
        {"queue_id", queue_id_},
        {"magnitude_bytes", DspGpuBufferLayout<N>::kMagnitudeBytes},
        {"peak_bin", last_packet_.peak_bin},
        {"peak_frequency_hz", last_packet_.peak_frequency_hz},
        {"peak_magnitude", last_packet_.peak_magnitude},
        {"backend", BackendName(last_device_view_.backend)},
    };
    return graph::JsonView(diagnostics_cache_);
}

template <std::size_t N>
const graph::gpu::accel::HostPinnedBufferView& DspMagnitudeD2HNode<N>::last_host_view() const noexcept {
    return last_host_view_;
}

template <std::size_t N>
const graph::gpu::accel::DeviceBufferView& DspMagnitudeD2HNode<N>::last_device_view() const noexcept {
    return last_device_view_;
}

template <std::size_t N>
const graph::gpu::accel::TransferTicket& DspMagnitudeD2HNode<N>::last_transfer_ticket() const noexcept {
    return last_transfer_ticket_;
}

template <std::size_t N>
const typename DspMagnitudeD2HNode<N>::MagnitudePacketType&
DspMagnitudeD2HNode<N>::last_packet() const noexcept {
    return last_packet_;
}

template <std::size_t N>
graph::gpu::accel::HostPinnedBufferView DspMagnitudeD2HNode<N>::BuildHostView() {
    graph::gpu::accel::HostPinnedBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::Metal;
    view.host_ptr = host_magnitude_buffer_.data();
    view.bytes = DspGpuBufferLayout<N>::kMagnitudeBytes;
    view.dtype = DspGpuBufferLayout<N>::ScalarDataType();
    view.layout = DspGpuBufferLayout<N>::MagnitudeTensorLayout();
    view.allocator_id = 1;
    return view;
}

template <std::size_t N>
std::uint64_t DspMagnitudeD2HNode<N>::ResolveQueueId() {
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
typename DspMagnitudeD2HNode<N>::MagnitudePacketType
DspMagnitudeD2HNode<N>::ReconstructPacket(const MagnitudePacketType& input_sidecar) const {
    auto packet = input_sidecar;
    std::copy(host_magnitude_buffer_.begin(),
              host_magnitude_buffer_.end(),
              packet.magnitudes.begin());
    packet.valid = true;

    auto peak_it = std::max_element(packet.magnitudes.begin(), packet.magnitudes.end());
    if (peak_it != packet.magnitudes.end()) {
        packet.peak_bin = static_cast<std::size_t>(std::distance(packet.magnitudes.begin(), peak_it));
        packet.peak_magnitude = *peak_it;
        packet.peak_frequency_hz = packet.BinToFrequency(packet.peak_bin);
    }
    return packet;
}

template class DspMagnitudeD2HNode<256>;

}  // namespace dsp
