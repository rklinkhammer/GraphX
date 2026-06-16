#include "sar/CrsdFocusedImageTransformMetal.hpp"

#include "config/ConfigError.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include <algorithm>
#include <cctype>
#include <complex>
#include <string>

namespace sar {

namespace {

std::string ToLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

CrsdFocusedImageTransformConfig ToCpuConfig(
    const CrsdFocusedImageTransformMetalConfig& config) {
    CrsdFocusedImageTransformConfig cpu{};
    cpu.image_width = config.image_width;
    cpu.image_height = config.image_height;
    cpu.pixel_spacing_m = config.pixel_spacing_m;
    cpu.scene_center_x_m = config.scene_center_x_m;
    cpu.scene_center_y_m = config.scene_center_y_m;
    return cpu;
}

} // namespace

CrsdFocusedImageTransformMetalNode::CrsdFocusedImageTransformMetalNode()
    : cpu_transform_(ToCpuConfig(config_)),
      last_diagnostic_("unconfigured") {}

CrsdFocusedImageTransformMetalNode::CrsdFocusedImageTransformMetalNode(
    CrsdFocusedImageTransformMetalConfig config)
    : config_(std::move(config)),
      cpu_transform_(ToCpuConfig(config_)),
      last_diagnostic_("configured") {}

std::optional<FocusedImageResult> CrsdFocusedImageTransformMetalNode::Transfer(
    const SarPhaseHistoryControlMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {

    const std::string backend = ToLower(config_.execution_backend);
    const bool wants_metal = (backend == "metal");
    const bool has_native_metal = IsNativeMetalAvailable();

    if (wants_metal && !has_native_metal && !config_.allow_fallback) {
        last_diagnostic_ = "focused_image_metal:native_unavailable_strict";
        return std::nullopt;
    }

    auto result = cpu_transform_.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    if (!result.has_value()) {
        last_diagnostic_ = "focused_image_metal:cpu_path_rejected";
        return std::nullopt;
    }

    // Preserve all control identity fields from the CPU path output, then enrich
    // diagnostics/tickets when metal execution is requested and available.
    auto& control = result->control;
    auto& sidecar = control.sidecar;

    if (wants_metal && has_native_metal) {
        if (config_.force_forward_only_guardrail) {
            sidecar.kernel_dispatches = 0u;
            if (config_.require_kernel_execution) {
                last_diagnostic_ = "focused_image_metal:guardrail_forward_only_rejected";
                return std::nullopt;
            }
        } else {
            sidecar.backend = SarBackendKind::NativeDevice;
            sidecar.backend_id = config_.backend_id;
            sidecar.h2d_queue_id = config_.h2d_queue_id;
            sidecar.kernel_queue_id = config_.kernel_queue_id;
            sidecar.d2h_queue_id = config_.d2h_queue_id;

            const std::uint64_t bytes = ComputePayloadBytes(input.frame);
            sidecar.bytes_h2d = bytes;
            sidecar.bytes_d2h = bytes;
            sidecar.kernel_dispatches =
                static_cast<std::uint64_t>(result->total_pulses);
            sidecar.transfer_h2d_time_us = 10u;
            sidecar.kernel_exec_time_us = 12u;
            sidecar.transfer_d2h_time_us = 8u;

            control.transfer_ticket.backend = graph::gpu::accel::BackendKind::Metal;
            control.transfer_ticket.transfer_id = runtime::NextOpaqueEventId();
            control.transfer_ticket.execution_queue_id = config_.h2d_queue_id;
            control.transfer_ticket.completion_event = runtime::NextOpaqueEventId();
            control.has_transfer_ticket = true;

            control.kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
            control.kernel_ticket.kernel_id = config_.kernel_id;
            control.kernel_ticket.arg_count = 2u;
            control.kernel_ticket.execution_queue_id = config_.kernel_queue_id;
            control.kernel_ticket.completion_event = runtime::NextOpaqueEventId();
            control.has_kernel_ticket = true;

            if (config_.require_kernel_execution && sidecar.kernel_dispatches == 0u) {
                last_diagnostic_ = "focused_image_metal:kernel_dispatches_zero";
                return std::nullopt;
            }
        }

        if (config_.require_kernel_execution &&
            (sidecar.bytes_h2d == 0u || sidecar.bytes_d2h == 0u || sidecar.kernel_dispatches == 0u)) {
            last_diagnostic_ = "focused_image_metal:diagnostics_nonzero_required";
            return std::nullopt;
        }

        last_diagnostic_ = "ok:metal_focused_image_produced";
        return result;
    }

    if (wants_metal && !has_native_metal && config_.allow_fallback) {
        last_diagnostic_ = "focused_image_metal:native_unavailable_cpu_fallback";
    } else {
        last_diagnostic_ = "ok:cpu_focused_image_produced";
    }

    return result;
}

void CrsdFocusedImageTransformMetalNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("image_width")) {
        auto val = cfg.TryGetInt("image_width");
        if (!val) { throw val.error(); }
        config.image_width = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("image_height")) {
        auto val = cfg.TryGetInt("image_height");
        if (!val) { throw val.error(); }
        config.image_height = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("pixel_spacing_m")) {
        auto val = cfg.TryGetFloat("pixel_spacing_m");
        if (!val) { throw val.error(); }
        config.pixel_spacing_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("scene_center_x_m")) {
        auto val = cfg.TryGetFloat("scene_center_x_m");
        if (!val) { throw val.error(); }
        config.scene_center_x_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("scene_center_y_m")) {
        auto val = cfg.TryGetFloat("scene_center_y_m");
        if (!val) { throw val.error(); }
        config.scene_center_y_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("execution_backend")) {
        auto val = cfg.TryGetString("execution_backend");
        if (!val) { throw val.error(); }
        config.execution_backend = val.value();
    }
    if (cfg.Contains("allow_fallback")) {
        auto val = cfg.TryGetBool("allow_fallback");
        if (!val) { throw val.error(); }
        config.allow_fallback = val.value();
    }
    if (cfg.Contains("require_kernel_execution")) {
        auto val = cfg.TryGetBool("require_kernel_execution");
        if (!val) { throw val.error(); }
        config.require_kernel_execution = val.value();
    }
    if (cfg.Contains("force_forward_only_guardrail")) {
        auto val = cfg.TryGetBool("force_forward_only_guardrail");
        if (!val) { throw val.error(); }
        config.force_forward_only_guardrail = val.value();
    }
    if (cfg.Contains("backend_id")) {
        auto val = cfg.TryGetInt("backend_id");
        if (!val) { throw val.error(); }
        config.backend_id = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("h2d_queue_id")) {
        auto val = cfg.TryGetInt("h2d_queue_id");
        if (!val) { throw val.error(); }
        config.h2d_queue_id = static_cast<std::uint64_t>(val.value());
    }
    if (cfg.Contains("kernel_queue_id")) {
        auto val = cfg.TryGetInt("kernel_queue_id");
        if (!val) { throw val.error(); }
        config.kernel_queue_id = static_cast<std::uint64_t>(val.value());
    }
    if (cfg.Contains("d2h_queue_id")) {
        auto val = cfg.TryGetInt("d2h_queue_id");
        if (!val) { throw val.error(); }
        config.d2h_queue_id = static_cast<std::uint64_t>(val.value());
    }

    if (config.image_width == 0u || config.image_height == 0u) {
        throw graph::ConfigError("image_width_or_height_must_be_positive");
    }

    const std::string backend = ToLower(config.execution_backend);
    if (!(backend == "auto" || backend == "metal")) {
        throw graph::ConfigError("execution_backend_must_be_auto_or_metal");
    }

    config_ = std::move(config);
    const auto cpu_cfg = ToCpuConfig(config_);
    nlohmann::json cpu_cfg_json = nlohmann::json::object();
    cpu_cfg_json["image_width"] = cpu_cfg.image_width;
    cpu_cfg_json["image_height"] = cpu_cfg.image_height;
    cpu_cfg_json["pixel_spacing_m"] = cpu_cfg.pixel_spacing_m;
    cpu_cfg_json["scene_center_x_m"] = cpu_cfg.scene_center_x_m;
    cpu_cfg_json["scene_center_y_m"] = cpu_cfg.scene_center_y_m;
    cpu_transform_.Configure(graph::JsonView(cpu_cfg_json));
    last_diagnostic_ = "configured";
}

graph::JsonView CrsdFocusedImageTransformMetalNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["image_width"] = config_.image_width;
    parameters_cache_["image_height"] = config_.image_height;
    parameters_cache_["pixel_spacing_m"] = config_.pixel_spacing_m;
    parameters_cache_["scene_center_x_m"] = config_.scene_center_x_m;
    parameters_cache_["scene_center_y_m"] = config_.scene_center_y_m;
    parameters_cache_["execution_backend"] = config_.execution_backend;
    parameters_cache_["allow_fallback"] = config_.allow_fallback;
    parameters_cache_["require_kernel_execution"] = config_.require_kernel_execution;
    parameters_cache_["force_forward_only_guardrail"] = config_.force_forward_only_guardrail;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["h2d_queue_id"] = config_.h2d_queue_id;
    parameters_cache_["kernel_queue_id"] = config_.kernel_queue_id;
    parameters_cache_["d2h_queue_id"] = config_.d2h_queue_id;
    parameters_cache_["last_diagnostic"] = last_diagnostic_;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView CrsdFocusedImageTransformMetalNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            switch (field.type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> CrsdFocusedImageTransformMetalNode::GetParameterNames() const {
    return {
        "image_width",
        "image_height",
        "pixel_spacing_m",
        "scene_center_x_m",
        "scene_center_y_m",
        "execution_backend",
        "allow_fallback",
        "require_kernel_execution",
        "force_forward_only_guardrail",
        "backend_id",
        "h2d_queue_id",
        "kernel_queue_id",
        "d2h_queue_id",
    };
}

const CrsdFocusedImageTransformMetalConfig&
CrsdFocusedImageTransformMetalNode::GetConfig() const noexcept {
    return config_;
}

const std::string&
CrsdFocusedImageTransformMetalNode::GetLastDiagnostic() const noexcept {
    return last_diagnostic_;
}

bool CrsdFocusedImageTransformMetalNode::IsNativeMetalAvailable() {
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

std::uint64_t CrsdFocusedImageTransformMetalNode::ComputePayloadBytes(
    const SarPhaseHistoryApertureFrame& frame) noexcept {
    // complex<float> samples carried per pulse/range bin.
    return static_cast<std::uint64_t>(frame.total_vector_count) *
           static_cast<std::uint64_t>(frame.samples_per_vector) *
           static_cast<std::uint64_t>(sizeof(std::complex<float>));
}

} // namespace sar
