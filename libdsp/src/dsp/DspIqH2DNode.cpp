// SPDX-License-Identifier: MIT

#include "dsp/DspIqH2DNode.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <algorithm>
#include <stdexcept>
#include <typeinfo>

namespace dsp {

namespace {

/**
 * @brief Converts accelerator backend enum to a stable diagnostic string.
 */
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
DspIqH2DNode<N>::DspIqH2DNode() {
    this->SetName("__unnamed__");
}

template <std::size_t N>
DspIqH2DNode<N>::~DspIqH2DNode() {
    if (owns_queue_ && context_ && queue_id_ != 0) {
        context_->DestroyCommandQueue(queue_id_);
    }
}

template <std::size_t N>
bool DspIqH2DNode<N>::BindGpuCapabilities(graph::CapabilityBus& capability_bus) {
    context_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    shared_queue_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    memory_pool_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    transfer_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();

    if (context_ && !context_->SelectDevice(device_id_)) {
        return false;
    }

    if (queue_id_ == 0 && shared_queue_) {
        queue_id_ = shared_queue_->GetOrCreateQueueId();
        owns_queue_ = false;
    }
    if (queue_id_ == 0 && context_) {
        queue_id_ = context_->CreateCommandQueue();
        owns_queue_ = queue_id_ != 0;
    }

    return memory_pool_ != nullptr && transfer_ != nullptr && queue_id_ != 0;
}

template <std::size_t N>
std::optional<typename DspIqH2DNode<N>::TokenType> DspIqH2DNode<N>::Transfer(
    const TokenType& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!memory_pool_ || !transfer_ || ResolveQueueId() == 0) {
        return std::nullopt;
    }

    const IqPacketType* packet = nullptr;
    try {
        packet = &input.sidecar.template get<IqPacketType>();
    } catch (const std::bad_cast&) {
        return std::nullopt;
    }
    if (packet == nullptr || !packet->IsValid()) {
        return std::nullopt;
    }

    PackIqPacket(*packet);
    auto host_view = BuildHostView();
    if (!graph::gpu::accel::IsValidView(host_view)) {
        return std::nullopt;
    }

    graph::gpu::accel::BufferLease lease{};
    if (!memory_pool_->AllocateDevice(host_view.bytes, device_id_, lease)) {
        return std::nullopt;
    }

    auto device_view = lease.device_view;
    device_view.backend = graph::gpu::accel::BackendKind::Metal;
    device_view.dtype = host_view.dtype;
    device_view.layout = host_view.layout;
    device_view.device_id = device_id_;
    device_view.execution_queue_id = queue_id_;

    if (!graph::gpu::accel::IsValidView(device_view)) {
        return std::nullopt;
    }

    graph::gpu::accel::TransferTicket ticket{};
    if (!transfer_->EnqueueH2D(host_view, device_view, queue_id_, ticket)) {
        return std::nullopt;
    }

    if (!graph::gpu::accel::IsValidTransferTicket(ticket)) {
        return std::nullopt;
    }

    last_host_view_ = host_view;
    last_device_view_ = device_view;
    last_lease_ = lease;
    last_lease_.device_view = device_view;
    last_transfer_ticket_ = ticket;

    auto output = input;
    output.host_view = host_view;
    output.has_host_view = true;
    output.device_view = device_view;
    output.has_device_view = true;
    output.lease = last_lease_;
    output.has_lease = true;
    output.transfer_ticket = ticket;
    output.has_transfer_ticket = true;
    return output;
}

template <std::size_t N>
void DspIqH2DNode<N>::Configure(const graph::JsonView& cfg) {
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
}

template <std::size_t N>
graph::JsonView DspIqH2DNode<N>::GetParameters() const {
    parameters_cache_ = {
        {"queue_id", queue_id_},
        {"device_id", device_id_},
        {"input_packet_samples", N},
        {"iq_bytes", DspGpuBufferLayout<N>::kIqBytes},
    };
    return graph::JsonView(parameters_cache_);
}

template <std::size_t N>
graph::JsonView DspIqH2DNode<N>::GetParameterDescription(const std::string& param_name) const {
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
            {"description", "Metal device id used for IQ device allocation."},
        };
    }
    return graph::JsonView(parameter_description_cache_);
}

template <std::size_t N>
std::vector<std::string> DspIqH2DNode<N>::GetParameterNames() const {
    return {"queue_id", "device_id", "backend_id"};
}

template <std::size_t N>
graph::JsonView DspIqH2DNode<N>::GetDiagnostics() const {
    diagnostics_cache_ = {
        {"has_host_view", graph::gpu::accel::IsValidView(last_host_view_)},
        {"has_device_view", graph::gpu::accel::IsValidView(last_device_view_)},
        {"has_lease", graph::gpu::accel::IsValidLease(last_lease_)},
        {"has_transfer_ticket", graph::gpu::accel::IsValidTransferTicket(last_transfer_ticket_)},
        {"queue_id", queue_id_},
        {"device_id", device_id_},
        {"iq_bytes", DspGpuBufferLayout<N>::kIqBytes},
        {"backend", BackendName(last_device_view_.backend)},
    };
    return graph::JsonView(diagnostics_cache_);
}

template <std::size_t N>
const graph::gpu::accel::HostPinnedBufferView& DspIqH2DNode<N>::last_host_view() const noexcept {
    return last_host_view_;
}

template <std::size_t N>
const graph::gpu::accel::DeviceBufferView& DspIqH2DNode<N>::last_device_view() const noexcept {
    return last_device_view_;
}

template <std::size_t N>
const graph::gpu::accel::BufferLease& DspIqH2DNode<N>::last_lease() const noexcept {
    return last_lease_;
}

template <std::size_t N>
const graph::gpu::accel::TransferTicket& DspIqH2DNode<N>::last_transfer_ticket() const noexcept {
    return last_transfer_ticket_;
}

template <std::size_t N>
void DspIqH2DNode<N>::PackIqPacket(const IqPacketType& packet) {
    for (std::size_t sample = 0; sample < N; ++sample) {
        host_iq_buffer_[sample * 2] = packet.samples[sample].real();
        host_iq_buffer_[sample * 2 + 1] = packet.samples[sample].imag();
    }
}

template <std::size_t N>
graph::gpu::accel::HostPinnedBufferView DspIqH2DNode<N>::BuildHostView() const {
    graph::gpu::accel::HostPinnedBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::Metal;
    view.host_ptr = const_cast<float*>(host_iq_buffer_.data());
    view.bytes = DspGpuBufferLayout<N>::kIqBytes;
    view.dtype = DspGpuBufferLayout<N>::ScalarDataType();
    view.layout = DspGpuBufferLayout<N>::IqTensorLayout();
    view.allocator_id = 1;
    return view;
}

template <std::size_t N>
std::uint64_t DspIqH2DNode<N>::ResolveQueueId() {
    if (queue_id_ != 0) {
        return queue_id_;
    }
    if (shared_queue_) {
        queue_id_ = shared_queue_->GetOrCreateQueueId();
    }
    if (queue_id_ == 0 && context_) {
        queue_id_ = context_->CreateCommandQueue();
        owns_queue_ = queue_id_ != 0;
    }
    return queue_id_;
}

template class DspIqH2DNode<256>;

}  // namespace dsp
