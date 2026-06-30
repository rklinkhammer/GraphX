// SPDX-License-Identifier: MIT

/**
 * @file CrsdFocusedImageTransformNode.cpp
 * @brief GraphX source file.
 */

#include "sar/CrsdFocusedImageTransformNode.hpp"

#include "config/ConfigError.hpp"

#include <cmath>
#include <numeric>
#include <string>

namespace sar {

namespace {

constexpr double kSpeedOfLight = 299792458.0;
constexpr double kDefaultWavelengthM = 0.03;

// FNV-1a 64-bit hash for image pixels (quantised to nearest integer at scale 1e6).
std::uint64_t HashImagePixels(const std::vector<float>& pixels) {
    std::uint64_t hash = 14695981039346656037ull;
    for (float pixel : pixels) {
        const auto quantized = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                std::llround(static_cast<double>(pixel) * 1.0e6)));
        hash ^= quantized;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

CrsdFocusedImageTransformNode::CrsdFocusedImageTransformNode()
    : last_diagnostic_{"unconfigured"} {}

CrsdFocusedImageTransformNode::CrsdFocusedImageTransformNode(
    CrsdFocusedImageTransformConfig config)
    : config_(std::move(config)),
      last_diagnostic_{"configured"} {}

// static
reference::Geometry CrsdFocusedImageTransformNode::DeriveGeometry(
    const SarPhaseHistoryApertureFrame& frame,
    const CrsdFocusedImageTransformConfig& config) {

    // Collect all vectors in order.
    std::vector<const SarPhaseHistoryVector*> all_vectors;
    all_vectors.reserve(static_cast<std::size_t>(frame.total_vector_count));
    for (const auto& seg : frame.segments) {
        for (const auto& vec : seg.vectors) {
            all_vectors.push_back(&vec);
        }
    }

    if (all_vectors.empty()) {
        throw std::invalid_argument("focused_image_transform:no_vectors");
    }

    const double x_start = all_vectors.front()->platform_position_m[0];
    const double x_end   = all_vectors.back()->platform_position_m[0];

    // Mean platform Y across all pulses.
    double platform_y_sum = 0.0;
    for (const auto* vec : all_vectors) {
        platform_y_sum += vec->platform_position_m[1];
    }
    const double platform_y = platform_y_sum / static_cast<double>(all_vectors.size());

    const double wavelength_m =
        (frame.carrier_hz > 0.0)
            ? (kSpeedOfLight / frame.carrier_hz)
            : kDefaultWavelengthM;

    const double range_spacing_m =
        (frame.sample_rate_hz > 0.0)
            ? (kSpeedOfLight / (2.0 * frame.sample_rate_hz))
            : (kSpeedOfLight / (2.0 * 10.0e6));  // 10 MHz default

    const double pixel_spacing_m =
        (config.pixel_spacing_m > 0.0) ? config.pixel_spacing_m : range_spacing_m;

    reference::Geometry geometry{};
    geometry.pulse_count = static_cast<std::uint32_t>(all_vectors.size());
    geometry.range_bin_count = frame.samples_per_vector;
    geometry.image_width = config.image_width;
    geometry.image_height = config.image_height;
    geometry.platform_x_start_m = x_start;
    geometry.platform_x_end_m = x_end;
    geometry.platform_y_m = platform_y;
    geometry.range_origin_m = 0.0;
    geometry.range_spacing_m = range_spacing_m;
    geometry.scene_center_x_m = config.scene_center_x_m;
    geometry.scene_center_y_m = config.scene_center_y_m;
    geometry.pixel_spacing_m = pixel_spacing_m;
    geometry.wavelength_m = wavelength_m;

    return geometry;
}

std::optional<FocusedImageResult> CrsdFocusedImageTransformNode::Transfer(
    const SarPhaseHistoryControlMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {

    const auto& frame = input.frame;

    // Watermarks pass through as an EOS to downstream (no image payload).
    if (frame.control_marker == SarPhaseHistoryControlMarker::Watermark) {
        last_diagnostic_ = "watermark_forwarded";
        FocusedImageResult result{};
        result.control = input.control;
        return result;
    }

    // Non-EOS data frames are not expected in the current contract.
    if (frame.control_marker != SarPhaseHistoryControlMarker::EndOfStream) {
        last_diagnostic_ = "unsupported_control_marker";
        return std::nullopt;
    }

    // Safety check: reject empty or zero-vector frames — this would be
    // a diagnostics-only or payload-ignored path, which is forbidden.
    if (frame.segments.empty() || frame.total_vector_count == 0u ||
        frame.samples_per_vector == 0u) {
        last_diagnostic_ = "focused_image_transform:empty_payload_rejected";
        return std::nullopt;
    }

    // Derive geometry from the full-aperture phase history.
    reference::Geometry geometry;
    try {
        geometry = DeriveGeometry(frame, config_);
    } catch (const std::invalid_argument& ex) {
        last_diagnostic_ = std::string("focused_image_transform:geometry_error:") + ex.what();
        return std::nullopt;
    }

    // Assemble the full-aperture phase-history buffer in vector order.
    const auto pulse_count = static_cast<std::size_t>(geometry.pulse_count);
    const auto samples_per_pulse = static_cast<std::size_t>(geometry.range_bin_count);
    std::vector<std::complex<double>> phase_history(
        pulse_count * samples_per_pulse, std::complex<double>{0.0, 0.0});

    std::size_t global_pulse_idx = 0u;
    for (const auto& seg : frame.segments) {
        for (const auto& vec : seg.vectors) {
            if (global_pulse_idx >= pulse_count) {
                break;
            }
            const std::size_t base = global_pulse_idx * samples_per_pulse;
            for (std::size_t s = 0u; s < vec.samples.size() && s < samples_per_pulse; ++s) {
                phase_history[base + s] = std::complex<double>{
                    static_cast<double>(vec.samples[s].real()),
                    static_cast<double>(vec.samples[s].imag())};
            }
            ++global_pulse_idx;
        }
    }

    // Run deterministic near-field backprojection (CPU, reuses SarCpuReference).
    const reference::Image image =
        reference::BackprojectNearestRange(geometry, phase_history);

    // Build grid metadata.
    FocusedImageGrid grid{};
    grid.width = image.width;
    grid.height = image.height;
    grid.pixel_spacing_m = geometry.pixel_spacing_m;
    grid.scene_center_x_m = geometry.scene_center_x_m;
    grid.scene_center_y_m = geometry.scene_center_y_m;
    grid.range_origin_m = geometry.range_origin_m;
    grid.range_spacing_m = geometry.range_spacing_m;
    grid.wavelength_m = geometry.wavelength_m;
    grid.platform_x_start_m = geometry.platform_x_start_m;
    grid.platform_x_end_m = geometry.platform_x_end_m;
    grid.platform_y_m = geometry.platform_y_m;

    FocusedImageResult result{};
    result.control = input.control;
    result.grid = grid;
    result.pixels = image.pixels;
    result.output_hash = HashImagePixels(image.pixels);
    result.input_ordered_set_hash = frame.ordered_set_payload_hash;
    result.total_pulses = geometry.pulse_count;
    result.samples_per_pulse = geometry.range_bin_count;
    result.ordered_crsd_segment_indices.reserve(frame.segments.size());
    result.per_segment_input_hashes.reserve(frame.segments.size());
    for (const auto& seg : frame.segments) {
        result.ordered_crsd_segment_indices.push_back(seg.segment_index);
        result.per_segment_input_hashes.push_back(seg.payload_hash);
    }
    result.lineage_complete_aperture = true;

    last_diagnostic_ = "ok:focused_image_produced";
    return result;
}

void CrsdFocusedImageTransformNode::Configure(const graph::JsonView& cfg) {
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

    if (config.image_width == 0u || config.image_height == 0u) {
        throw graph::ConfigError("image_width_or_height_must_be_positive");
    }

    config_ = std::move(config);
    last_diagnostic_ = "configured";
}

graph::JsonView CrsdFocusedImageTransformNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["image_width"] = config_.image_width;
    parameters_cache_["image_height"] = config_.image_height;
    parameters_cache_["pixel_spacing_m"] = config_.pixel_spacing_m;
    parameters_cache_["scene_center_x_m"] = config_.scene_center_x_m;
    parameters_cache_["scene_center_y_m"] = config_.scene_center_y_m;
    parameters_cache_["last_diagnostic"] = last_diagnostic_;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView CrsdFocusedImageTransformNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            switch (field.type) {
                case graph::JsonType::String:  type_name = "string";  break;
                case graph::JsonType::Number:  type_name = "number";  break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object:  type_name = "object";  break;
                case graph::JsonType::Array:   type_name = "array";   break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> CrsdFocusedImageTransformNode::GetParameterNames() const {
    return {
        "image_width",
        "image_height",
        "pixel_spacing_m",
        "scene_center_x_m",
        "scene_center_y_m",
    };
}

const CrsdFocusedImageTransformConfig&
CrsdFocusedImageTransformNode::GetConfig() const noexcept {
    return config_;
}

const std::string&
CrsdFocusedImageTransformNode::GetLastDiagnostic() const noexcept {
    return last_diagnostic_;
}

} // namespace sar
