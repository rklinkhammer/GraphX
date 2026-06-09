#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct SarVisualizationSinkConfig {
    bool enabled{false};
    std::string output_dir{"sar_viz_output"};
    std::string format{"pgm"};
    bool normalize{true};
    std::string file_prefix{"sar_tile"};
};

class SarVisualizationSinkNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarAccelControlToken>,
          graph::TypeList<SarAccelControlToken>,
          SarVisualizationSinkNode>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    SarVisualizationSinkNode() = default;

    std::optional<SarAccelControlToken> Transfer(
        const SarAccelControlToken& value,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Enable visualization artifact generation"
            },
            graph::JsonField{
                .name = "output_dir",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "sar_viz_output",
                .enum_values = std::nullopt,
                .description = "Directory where visualization artifacts are written"
            },
            graph::JsonField{
                .name = "format",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "pgm",
                .enum_values = std::nullopt,
                .description = "Output format: pgm or csv"
            },
            graph::JsonField{
                .name = "normalize",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "true",
                .enum_values = std::nullopt,
                .description = "Normalize pixel values before writing"
            },
            graph::JsonField{
                .name = "file_prefix",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "sar_tile",
                .enum_values = std::nullopt,
                .description = "Filename prefix for generated artifacts"
            }
        }};
    }

    const SarVisualizationSinkConfig& GetConfig() const noexcept {
        return config_;
    }

    std::size_t artifact_count() const noexcept {
        return artifact_count_;
    }

private:
    bool WriteArtifact(const SarAccelControlToken& value);
    bool WritePgm(std::size_t element_count, const std::string& path);
    bool WriteCsv(std::size_t element_count, const std::string& path);

    static bool IsSupportedFormat(const std::string& format);

    SarVisualizationSinkConfig config_{};
    std::size_t artifact_count_{0};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
