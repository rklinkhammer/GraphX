// SPDX-License-Identifier: MIT

#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"

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

    if (requested_backend_ == AcceleratorBackend::Cpu) {
        cpu_reference_.SetConfig(downconverter_config_);
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
    cpu_reference_.SetConfig(downconverter_config_);
}

std::optional<FHSSDownconvertedIqToken> AccelFhssDownconverterNode::Transfer(
    const FHSSSyntheticIqToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    auto output = cpu_reference_.Transfer(input, std::integral_constant<std::size_t, 0>{},
                                          std::integral_constant<std::size_t, 0>{});
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
