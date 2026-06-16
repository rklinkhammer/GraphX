#pragma once

#include "sar/CrsdFocusedImageTransformNode.hpp"
#include "sar/SarMessages.hpp"

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

struct CrsdFocusedImageTransformMetalConfig {
    std::uint32_t image_width{32};
    std::uint32_t image_height{32};
    double pixel_spacing_m{0.0};
    double scene_center_x_m{0.0};
    double scene_center_y_m{0.0};

    std::string execution_backend{"auto"};
    bool allow_fallback{true};
    bool require_kernel_execution{true};
    bool force_forward_only_guardrail{false};
    std::uint32_t backend_id{0};
    std::uint64_t h2d_queue_id{11};
    std::uint64_t kernel_queue_id{21};
    std::uint64_t d2h_queue_id{31};
    std::uint64_t kernel_id{99001};
};

class CrsdFocusedImageTransformMetalNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarPhaseHistoryControlMessage>,
          graph::TypeList<FocusedImageResult>,
          CrsdFocusedImageTransformMetalNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    CrsdFocusedImageTransformMetalNode();
    explicit CrsdFocusedImageTransformMetalNode(CrsdFocusedImageTransformMetalConfig config);

    std::optional<FocusedImageResult> Transfer(
        const SarPhaseHistoryControlMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 12> Fields() {
        return {{
            graph::JsonField{.name = "image_width", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "32", .enum_values = std::nullopt, .description = "Focused image output width in pixels"},
            graph::JsonField{.name = "image_height", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "32", .enum_values = std::nullopt, .description = "Focused image output height in pixels"},
            graph::JsonField{.name = "pixel_spacing_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Image pixel spacing in metres (0.0 = auto from range spacing)"},
            graph::JsonField{.name = "scene_center_x_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Scene centre x coordinate in metres"},
            graph::JsonField{.name = "scene_center_y_m", .type = graph::JsonType::Number, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "0.0", .enum_values = std::nullopt, .description = "Scene centre y coordinate in metres"},
            graph::JsonField{.name = "execution_backend", .type = graph::JsonType::String, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "auto", .enum_values = std::nullopt, .description = "Execution backend: auto or metal"},
            graph::JsonField{.name = "allow_fallback", .type = graph::JsonType::Boolean, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "true", .enum_values = std::nullopt, .description = "Allow CPU fallback when native Metal is unavailable"},
            graph::JsonField{.name = "require_kernel_execution", .type = graph::JsonType::Boolean, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "true", .enum_values = std::nullopt, .description = "Require nonzero kernel dispatches for metal lane"},
            graph::JsonField{.name = "force_forward_only_guardrail", .type = graph::JsonType::Boolean, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "false", .enum_values = std::nullopt, .description = "Test-only switch to emulate token forwarding without kernel execution"},
            graph::JsonField{.name = "backend_id", .type = graph::JsonType::Integer, .required = false, .min = 0.0, .max = std::nullopt, .default_value = "0", .enum_values = std::nullopt, .description = "Backend device identifier for diagnostics"},
            graph::JsonField{.name = "h2d_queue_id", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "11", .enum_values = std::nullopt, .description = "Synthetic/Native H2D queue identifier"},
            graph::JsonField{.name = "kernel_queue_id", .type = graph::JsonType::Integer, .required = false, .min = 1.0, .max = std::nullopt, .default_value = "21", .enum_values = std::nullopt, .description = "Synthetic/Native kernel queue identifier"},
        }};
    }

    const CrsdFocusedImageTransformMetalConfig& GetConfig() const noexcept;
    const std::string& GetLastDiagnostic() const noexcept;

private:
    static bool IsNativeMetalAvailable();

    static std::uint64_t ComputePayloadBytes(
        const SarPhaseHistoryApertureFrame& frame) noexcept;

    CrsdFocusedImageTransformMetalConfig config_{};
    CrsdFocusedImageTransformNode cpu_transform_{};
    std::string last_diagnostic_{"unconfigured"};

    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
