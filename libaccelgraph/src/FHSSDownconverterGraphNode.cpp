// SPDX-License-Identifier: MIT

#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"

#include <cstddef>
#include <span>
#include <vector>

#include <utility>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "config/ConfigError.hpp"

namespace accelgraph::fhss {

namespace {

std::optional<dsp::fhss::FHSSGraphXDownconverterPhaseConvention> ParsePhaseConvention(
    const std::string& value) {
    if (value == "passthrough_no_phase_rotation") {
        return dsp::fhss::FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation;
    }
    if (value == "output_times_exp_negative_j_two_pi_translation_t") {
        return dsp::fhss::FHSSGraphXDownconverterPhaseConvention::OutputTimesExpNegativeJTwoPiTranslationT;
    }
    return std::nullopt;
}

std::string BuildBackendFallbackDiagnostic(const FHSSAccelConfig& config,
                                           const std::optional<std::string>& unavailable_diag) {
    if (unavailable_diag.has_value()) {
        return unavailable_diag.value();
    }
    if (config.backend == AcceleratorBackend::Metal) {
        return kFhssDownconverterMetalNativeNotImplementedDiagnostic;
    }
    if (config.backend == AcceleratorBackend::Cuda) {
        return kFhssDownconverterCudaNativeNotImplementedDiagnostic;
    }
    return {};
}

std::optional<std::string> RunCudaSessionProbe(const std::shared_ptr<accelgraph::IAcceleratorSession>& session,
                                               const char* node_name) {
    if (!session) {
        return std::string(node_name) + " CUDA session is null.";
    }

    constexpr std::size_t kProbeBytes = 64;
    std::vector<std::byte> probe_input(kProbeBytes, std::byte{0x5A});

    accelgraph::HostAllocationRequest host_in_req;
    host_in_req.byte_size = kProbeBytes;
    host_in_req.debug_label = std::string(node_name) + ".probe.host_in";
    auto host_in = session->AllocateHost(host_in_req);
    if (!host_in.has_value()) {
        return std::string(node_name) + " CUDA probe AllocateHost(host_in) failed: " +
               host_in.error().diagnostic;
    }

    accelgraph::HostWriteRequest write_req;
    write_req.source = std::span<const std::byte>(probe_input.data(), probe_input.size());
    write_req.destination_offset = 0;
    auto write_result = session->WriteHost(host_in->handle, write_req);
    if (!write_result.has_value()) {
        return std::string(node_name) + " CUDA probe WriteHost failed: " + write_result.error().diagnostic;
    }

    accelgraph::DeviceAllocationRequest device_req;
    device_req.byte_size = kProbeBytes;
    device_req.debug_label = std::string(node_name) + ".probe.device";
    auto device = session->AllocateDevice(device_req);
    if (!device.has_value()) {
        return std::string(node_name) + " CUDA probe AllocateDevice failed: " + device.error().diagnostic;
    }

    accelgraph::QueueRequest queue_req;
    queue_req.debug_label = std::string(node_name) + ".probe.queue";
    auto queue = session->AcquireQueue(queue_req);
    if (!queue.has_value()) {
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe AcquireQueue failed: " + queue.error().diagnostic;
    }

    accelgraph::TransferRequest transfer_req;
    transfer_req.byte_size = kProbeBytes;
    transfer_req.source_offset = 0;
    transfer_req.destination_offset = 0;
    transfer_req.debug_label = std::string(node_name) + ".probe.h2d";
    auto h2d = session->EnqueueHostToDevice(host_in->handle, device->handle, queue->handle, transfer_req);
    if (!h2d.has_value()) {
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe EnqueueHostToDevice failed: " + h2d.error().diagnostic;
    }

    auto wait_h2d = session->Wait(h2d->completion, accelgraph::WaitRequest{});
    if (!wait_h2d.has_value() || !wait_h2d->completed) {
        (void)session->Release(h2d->completion, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe Wait(h2d) failed.";
    }
    (void)session->Release(h2d->completion, accelgraph::ReleaseRequest{.allow_if_released = true});

    accelgraph::HostAllocationRequest host_out_req;
    host_out_req.byte_size = kProbeBytes;
    host_out_req.debug_label = std::string(node_name) + ".probe.host_out";
    auto host_out = session->AllocateHost(host_out_req);
    if (!host_out.has_value()) {
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe AllocateHost(host_out) failed: " +
               host_out.error().diagnostic;
    }

    transfer_req.debug_label = std::string(node_name) + ".probe.d2h";
    auto d2h = session->EnqueueDeviceToHost(device->handle, host_out->handle, queue->handle, transfer_req);
    if (!d2h.has_value()) {
        (void)session->Release(host_out->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe EnqueueDeviceToHost failed: " + d2h.error().diagnostic;
    }

    auto wait_d2h = session->Wait(d2h->completion, accelgraph::WaitRequest{});
    if (!wait_d2h.has_value() || !wait_d2h->completed) {
        (void)session->Release(d2h->completion, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(host_out->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe Wait(d2h) failed.";
    }
    (void)session->Release(d2h->completion, accelgraph::ReleaseRequest{.allow_if_released = true});

    accelgraph::HostReadRequest read_req;
    read_req.byte_size = kProbeBytes;
    read_req.source_offset = 0;
    auto read_result = session->ReadHost(host_out->handle, read_req);
    if (!read_result.has_value()) {
        (void)session->Release(host_out->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA probe ReadHost failed: " + read_result.error().diagnostic;
    }

    (void)session->Release(host_out->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(host_in->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});

    if (read_result->bytes != probe_input) {
        return std::string(node_name) + " CUDA probe round-trip data mismatch.";
    }

    return std::nullopt;
}

std::optional<std::string> StageEvidenceThroughCuda(const std::shared_ptr<accelgraph::IAcceleratorSession>& session,
                                                    const dsp::fhss::FHSSGraphXComplexEvidence& evidence,
                                                    const char* node_name) {
    if (!session) {
        return std::string(node_name) + " CUDA staging requested with null session.";
    }
    if (!dsp::fhss::FHSSGraphXEvidenceHasHostComplexIq(evidence)) {
        return std::nullopt;
    }

    const auto& samples = *evidence.host_complex64_samples;
    const auto total_bytes = samples.size() * sizeof(std::complex<double>);
    if (total_bytes == 0) {
        return std::nullopt;
    }

    const auto* raw = reinterpret_cast<const std::byte*>(samples.data());
    std::vector<std::byte> bytes(raw, raw + total_bytes);

    accelgraph::HostAllocationRequest host_in_req;
    host_in_req.byte_size = total_bytes;
    host_in_req.debug_label = std::string(node_name) + ".stage.host_in";
    auto host_in = session->AllocateHost(host_in_req);
    if (!host_in.has_value()) {
        return std::string(node_name) + " CUDA staging AllocateHost(host_in) failed: " +
               host_in.error().diagnostic;
    }

    accelgraph::HostWriteRequest write_req;
    write_req.source = std::span<const std::byte>(bytes.data(), bytes.size());
    auto write_result = session->WriteHost(host_in->handle, write_req);
    if (!write_result.has_value()) {
        return std::string(node_name) + " CUDA staging WriteHost failed: " + write_result.error().diagnostic;
    }

    accelgraph::DeviceAllocationRequest device_req;
    device_req.byte_size = total_bytes;
    device_req.debug_label = std::string(node_name) + ".stage.device";
    auto device = session->AllocateDevice(device_req);
    if (!device.has_value()) {
        return std::string(node_name) + " CUDA staging AllocateDevice failed: " + device.error().diagnostic;
    }

    accelgraph::QueueRequest queue_req;
    queue_req.debug_label = std::string(node_name) + ".stage.queue";
    auto queue = session->AcquireQueue(queue_req);
    if (!queue.has_value()) {
        (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
        return std::string(node_name) + " CUDA staging AcquireQueue failed: " + queue.error().diagnostic;
    }

    accelgraph::TransferRequest transfer_req;
    transfer_req.byte_size = total_bytes;
    transfer_req.debug_label = std::string(node_name) + ".stage.h2d";
    auto h2d = session->EnqueueHostToDevice(host_in->handle, device->handle, queue->handle, transfer_req);
    if (!h2d.has_value()) {
        return std::string(node_name) + " CUDA staging EnqueueHostToDevice failed: " + h2d.error().diagnostic;
    }
    auto wait_h2d = session->Wait(h2d->completion, accelgraph::WaitRequest{});
    if (!wait_h2d.has_value() || !wait_h2d->completed) {
        return std::string(node_name) + " CUDA staging Wait(h2d) failed.";
    }
    (void)session->Release(h2d->completion, accelgraph::ReleaseRequest{.allow_if_released = true});

    accelgraph::HostAllocationRequest host_out_req;
    host_out_req.byte_size = total_bytes;
    host_out_req.debug_label = std::string(node_name) + ".stage.host_out";
    auto host_out = session->AllocateHost(host_out_req);
    if (!host_out.has_value()) {
        return std::string(node_name) + " CUDA staging AllocateHost(host_out) failed: " +
               host_out.error().diagnostic;
    }

    transfer_req.debug_label = std::string(node_name) + ".stage.d2h";
    auto d2h = session->EnqueueDeviceToHost(device->handle, host_out->handle, queue->handle, transfer_req);
    if (!d2h.has_value()) {
        return std::string(node_name) + " CUDA staging EnqueueDeviceToHost failed: " + d2h.error().diagnostic;
    }
    auto wait_d2h = session->Wait(d2h->completion, accelgraph::WaitRequest{});
    if (!wait_d2h.has_value() || !wait_d2h->completed) {
        return std::string(node_name) + " CUDA staging Wait(d2h) failed.";
    }
    (void)session->Release(d2h->completion, accelgraph::ReleaseRequest{.allow_if_released = true});

    accelgraph::HostReadRequest read_req;
    read_req.byte_size = total_bytes;
    auto read_result = session->ReadHost(host_out->handle, read_req);
    if (!read_result.has_value()) {
        return std::string(node_name) + " CUDA staging ReadHost failed: " + read_result.error().diagnostic;
    }

    (void)session->Release(host_out->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(host_in->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(queue->handle, accelgraph::ReleaseRequest{.allow_if_released = true});
    (void)session->Release(device->handle, accelgraph::ReleaseRequest{.allow_if_released = true});

    return std::nullopt;
}

}  // namespace

void AccelFhssDownconverterNode::Configure(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        throw graph::ConfigError("AccelFhssDownconverterNode configuration must be a JSON object");
    }

    accel_config_ = ParseFHSSAccelConfig(cfg);
    downconverter_config_ = dsp::fhss::FHSSDownconverterConfigFromJson(cfg);

    if (json.contains("phase_convention")) {
        if (!json["phase_convention"].is_string()) {
            throw graph::ConfigError("AccelFhssDownconverterNode phase_convention must be a string");
        }
        auto convention = ParsePhaseConvention(json["phase_convention"].get<std::string>());
        if (!convention.has_value()) {
            throw graph::ConfigError(
                "AccelFhssDownconverterNode phase_convention must be one of: "
                "passthrough_no_phase_rotation, output_times_exp_negative_j_two_pi_translation_t");
        }
        downconverter_config_.phase_convention = convention.value();
    }

    if (auto validation = dsp::fhss::ValidateFHSSDownconverterConfig(downconverter_config_); !validation) {
        throw graph::ConfigError(validation.error().message);
    }

    requested_backend_ = accel_config_.backend;
    selected_backend_ = AcceleratorBackend::Cpu;
    used_fallback_ = false;
    fallback_diagnostic_.clear();
    metal_session_.reset();
    cuda_session_.reset();
    last_output_.reset();

    cpu_reference_.SetConfig(downconverter_config_);

    if (requested_backend_ == AcceleratorBackend::Cpu) {
        return;
    }

    std::optional<std::string> unavailable_diag;
    if (requested_backend_ == AcceleratorBackend::Metal) {
        MetalAcceleratorProvider provider;
        auto session = provider.CreateSession(AcceleratorSessionCreateRequest{});
        if (session.has_value()) {
            metal_session_ = session.value();
        } else {
            unavailable_diag = session.error().diagnostic;
        }
    } else {
        CudaAcceleratorProvider provider;
        AcceleratorSessionCreateRequest request;
        request.requested_device = AcceleratorDeviceId{"cuda:" + std::to_string(accel_config_.cuda_device_ordinal)};
        auto session = provider.CreateSession(request);
        if (session.has_value()) {
            auto probe_error = RunCudaSessionProbe(session.value(), "AccelFhssDownconverterNode");
            if (!probe_error.has_value()) {
                cuda_session_ = session.value();
                selected_backend_ = AcceleratorBackend::Cuda;
                return;
            }
            unavailable_diag = *probe_error;
        } else {
            unavailable_diag = session.error().diagnostic;
        }
    }

    fallback_diagnostic_ = BuildBackendFallbackDiagnostic(accel_config_, unavailable_diag);
    if (accel_config_.strict_fallback) {
        throw graph::ConfigError(fallback_diagnostic_);
    }

    selected_backend_ = AcceleratorBackend::Cpu;
    used_fallback_ = true;
}

std::optional<FHSSDownconvertedIqToken> AccelFhssDownconverterNode::Transfer(
    const FHSSSyntheticIqToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto output = cpu_reference_.Transfer(input, std::integral_constant<std::size_t, 0>{},
                                          std::integral_constant<std::size_t, 0>{});
    if (output.has_value() && selected_backend_ == AcceleratorBackend::Cuda && cuda_session_) {
        if (auto staging_error =
                StageEvidenceThroughCuda(cuda_session_, output->sidecar.iq, "AccelFhssDownconverterNode");
            staging_error.has_value()) {
            fallback_diagnostic_ = *staging_error;
            if (accel_config_.strict_fallback) {
                last_output_.reset();
                return std::nullopt;
            }
            selected_backend_ = AcceleratorBackend::Cpu;
            used_fallback_ = true;
        }
    }
    if (output.has_value()) {
        last_output_ = output;
    } else {
        last_output_.reset();
    }
    return output;
}

AcceleratorBackend AccelFhssDownconverterNode::RequestedBackend() const noexcept {
    return requested_backend_;
}

AcceleratorBackend AccelFhssDownconverterNode::SelectedBackend() const noexcept {
    return selected_backend_;
}

bool AccelFhssDownconverterNode::UsedFallback() const noexcept {
    return used_fallback_;
}

const std::string& AccelFhssDownconverterNode::FallbackDiagnostic() const noexcept {
    return fallback_diagnostic_;
}

std::optional<FHSSDownconvertedIqToken> AccelFhssDownconverterNode::LastOutput() const {
    return last_output_;
}

void AccelFhssDownconverterSinkNode::Configure(const graph::JsonView& cfg) {
    if (!cfg.Raw().is_object()) {
        throw graph::ConfigError("AccelFhssDownconverterSinkNode configuration must be a JSON object");
    }
    std::scoped_lock<std::mutex> lock(mutex_);
    last_packet_.reset();
}

bool AccelFhssDownconverterSinkNode::Consume(const FHSSDownconvertedIqToken& packet,
                                             std::integral_constant<std::size_t, 0>) {
    std::scoped_lock<std::mutex> lock(mutex_);
    last_packet_ = packet;
    return true;
}

std::optional<FHSSDownconvertedIqToken> AccelFhssDownconverterSinkNode::LastPacket() const {
    std::scoped_lock<std::mutex> lock(mutex_);
    return last_packet_;
}

}  // namespace accelgraph::fhss
