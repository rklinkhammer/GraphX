// SPDX-License-Identifier: MIT

/**
 * @file CrsdFocusedImageTransformNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"
#include "sar/SarPhaseHistoryModel.hpp"
#include "sar/SarCpuReference.hpp"

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

// -----------------------------------------------------------------------
// Geometry assumptions (explicit, documented)
//
// CrsdFocusedImageTransformNode uses a 2D near-field backprojection model:
//
//   - Single channel/polarization (first channel_id from assembled frame).
//   - Image grid is centered at (scene_center_x_m, scene_center_y_m) = (0, 0).
//   - Platform trajectory is approximated from per-vector platform_position_m:
//       x_start = first vector platform_position_m[0]
//       x_end   = last vector platform_position_m[0]
//       y       = mean of all vector platform_position_m[1]
//   - Range bin spacing is derived from sample_rate_hz and speed of light:
//       range_spacing_m = c / (2 * sample_rate_hz)
//       range_origin_m  = 0.0 (near edge of range window)
//   - Wavelength is derived from carrier_hz:
//       wavelength_m = c / carrier_hz  (if carrier_hz > 0, else 0.03 m default)
//   - Image pixel spacing is configurable (default: range_spacing_m).
//   - Output pixel values are float32 magnitude (abs of backprojected complex sum
//     divided by pulse count), row-major, width x height layout.
//   - The transform produces one focused image from the full aperture,
//     NOT one image per CRSD segment.
//
// -----------------------------------------------------------------------

struct FocusedImageGrid {
    std::uint32_t width{0};
    std::uint32_t height{0};
    double pixel_spacing_m{0.0};
    double scene_center_x_m{0.0};
    double scene_center_y_m{0.0};
    double range_origin_m{0.0};
    double range_spacing_m{0.0};
    double wavelength_m{0.0};
    double platform_x_start_m{0.0};
    double platform_x_end_m{0.0};
    double platform_y_m{0.0};
};

struct FocusedImageResult {
    SarControlToken control{};
    FocusedImageGrid grid{};
    std::vector<float> pixels{};
    std::uint64_t output_hash{0};
    std::uint64_t input_ordered_set_hash{0};
    std::uint32_t total_pulses{0};
    std::uint32_t samples_per_pulse{0};
    std::vector<std::uint64_t> ordered_crsd_segment_indices{};
    std::vector<std::uint64_t> per_segment_input_hashes{};
    bool lineage_complete_aperture{false};
};

struct CrsdFocusedImageTransformConfig {
    std::uint32_t image_width{32};
    std::uint32_t image_height{32};
    double pixel_spacing_m{0.0};  // 0.0 = auto-derive from range_spacing
    double scene_center_x_m{0.0};
    double scene_center_y_m{0.0};
};

/**
 * @class CrsdFocusedImageTransformNode
 * @brief CrsdFocusedImageTransformNode class.
 */
class CrsdFocusedImageTransformNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarPhaseHistoryControlMessage>,
          graph::TypeList<FocusedImageResult>,
          CrsdFocusedImageTransformNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    CrsdFocusedImageTransformNode();
    explicit CrsdFocusedImageTransformNode(CrsdFocusedImageTransformConfig config);

    std::optional<FocusedImageResult> Transfer(
        const SarPhaseHistoryControlMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{.name = "image_width", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "32", .enum_values = std::nullopt, .description = "Focused image output width in pixels"},
            graph::JsonField{.name = "image_height", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "32", .enum_values = std::nullopt, .description = "Focused image output height in pixels"},
            graph::JsonField{.name = "pixel_spacing_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Image pixel spacing in metres (0.0 = auto from range_spacing)"},
            graph::JsonField{.name = "scene_center_x_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Scene centre x coordinate in metres"},
            graph::JsonField{.name = "scene_center_y_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Scene centre y coordinate in metres"},
        }};
    }

    const CrsdFocusedImageTransformConfig& GetConfig() const noexcept;
    const std::string& GetLastDiagnostic() const noexcept;

private:
    static reference::Geometry DeriveGeometry(
        const SarPhaseHistoryApertureFrame& frame,
        const CrsdFocusedImageTransformConfig& config);

    CrsdFocusedImageTransformConfig config_{};
    std::string last_diagnostic_{"unconfigured"};

    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
