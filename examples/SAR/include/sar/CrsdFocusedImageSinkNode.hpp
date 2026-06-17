// SPDX-License-Identifier: MIT

/**
 * @file CrsdFocusedImageSinkNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/CrsdFocusedImageTransformNode.hpp"

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

struct CrsdFocusedImageSinkConfig {
    bool enabled{false};
    std::string output_dir{"sar_focused_image_output"};
    std::string artifact_stem{"focused_image"};
    std::string convenience_image_format{"pgm"};
};

/**
 * @class CrsdFocusedImageSinkNode
 * @brief CrsdFocusedImageSinkNode class.
 */
class CrsdFocusedImageSinkNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FocusedImageResult>,
          graph::TypeList<FocusedImageResult>,
          CrsdFocusedImageSinkNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    CrsdFocusedImageSinkNode() = default;

    std::optional<FocusedImageResult> Transfer(
        const FocusedImageResult& value,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Enable deterministic focused-image artifact persistence"
            },
            graph::JsonField{
                .name = "output_dir",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "sar_focused_image_output",
                .enum_values = std::nullopt,
                .description = "Directory where focused-image artifacts are written"
            },
            graph::JsonField{
                .name = "artifact_stem",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "focused_image",
                .enum_values = std::nullopt,
                .description = "Filename stem for artifact files"
            },
            graph::JsonField{
                .name = "convenience_image_format",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "pgm",
                .enum_values = std::nullopt,
                .description = "Convenience image format (pgm)"
            }
        }};
    }

    const CrsdFocusedImageSinkConfig& GetConfig() const noexcept {
        return config_;
    }

    std::size_t artifact_count() const noexcept {
        return artifact_count_;
    }

private:
    bool PersistArtifacts(const FocusedImageResult& value);
    bool WriteBinary(const FocusedImageResult& value, const std::string& path) const;
    bool WriteJson(const FocusedImageResult& value, const std::string& bin_path, const std::string& path) const;
    bool WritePgm(const FocusedImageResult& value, const std::string& path) const;

    static bool IsSupportedImageFormat(const std::string& format);
    static std::string Lowercase(std::string value);
    static std::uint64_t HashBytes(const std::vector<std::byte>& bytes);
    static std::vector<std::byte> ReadFileBytes(const std::string& path);

    CrsdFocusedImageSinkConfig config_{};
    std::size_t artifact_count_{0};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
