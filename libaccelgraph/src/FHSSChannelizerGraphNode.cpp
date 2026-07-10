// SPDX-License-Identifier: MIT

#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"

#include <optional>
#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "config/ConfigError.hpp"

namespace accelgraph::fhss {

namespace {

std::string BuildBackendFallbackDiagnostic(const FHSSAccelConfig& config,
                                           const std::optional<std::string>& unavailable_diag) {
    if (unavailable_diag.has_value()) {
        return unavailable_diag.value();
    }
    if (config.backend == AcceleratorBackend::Metal) {
        return kFhssChannelizerMetalNativeNotImplementedDiagnostic;
    }
    if (config.backend == AcceleratorBackend::Cuda) {
        return kFhssChannelizerCudaNativeNotImplementedDiagnostic;
    }
    return {};
}

}  // namespace

void AccelFhssChannelizerNode::Configure(const graph::JsonView& cfg) {
    const auto& json = cfg.Raw();
    if (!json.is_object()) {
        throw graph::ConfigError("AccelFhssChannelizerNode configuration must be a JSON object");
    }

    accel_config_ = ParseFHSSAccelConfig(cfg);
    channelizer_config_ = dsp::fhss::FHSSChannelizerConfigFromJson(cfg);

    if (auto validation = dsp::fhss::ValidateFHSSChannelizerConfig(channelizer_config_); !validation) {
        throw graph::ConfigError(validation.error().message);
    }

    requested_backend_ = accel_config_.backend;
    selected_backend_ = AcceleratorBackend::Cpu;
    used_fallback_ = false;
    fallback_diagnostic_.clear();
    metal_session_.reset();
    cuda_session_.reset();

    cpu_reference_.SetConfig(channelizer_config_);

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
            cuda_session_ = session.value();
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

AcceleratorBackend AccelFhssChannelizerNode::RequestedBackend() const noexcept {
    return requested_backend_;
}

AcceleratorBackend AccelFhssChannelizerNode::SelectedBackend() const noexcept {
    return selected_backend_;
}

bool AccelFhssChannelizerNode::UsedFallback() const noexcept {
    return used_fallback_;
}

const std::string& AccelFhssChannelizerNode::FallbackDiagnostic() const noexcept {
    return fallback_diagnostic_;
}

void AccelFhssChannelizerSinkNode::Configure(const graph::JsonView& cfg) {
    if (!cfg.Raw().is_object()) {
        throw graph::ConfigError("AccelFhssChannelizerSinkNode configuration must be a JSON object");
    }
    std::scoped_lock<std::mutex> lock(mutex_);
    last_packet_.reset();
}

bool AccelFhssChannelizerSinkNode::Consume(const FHSSChannelizedIqToken& packet,
                                           std::integral_constant<std::size_t, 0>) {
    std::scoped_lock<std::mutex> lock(mutex_);
    last_packet_ = packet;
    return true;
}

std::optional<FHSSChannelizedIqToken> AccelFhssChannelizerSinkNode::LastPacket() const {
    std::scoped_lock<std::mutex> lock(mutex_);
    return last_packet_;
}

}  // namespace accelgraph::fhss
