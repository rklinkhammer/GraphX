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

enum class RangeCompressionMode : std::uint8_t {
    FftMagnitude,
    MatchedFilter,
};

enum class RangeCompressionOutput : std::uint8_t {
    Magnitude,
    Complex,
};

struct RangeCompressionConfig {
    bool enabled{true};
    float gain{1.0f};
    double sample_rate_hz{48000.0};
    RangeCompressionMode mode{RangeCompressionMode::FftMagnitude};
    RangeCompressionOutput output{RangeCompressionOutput::Magnitude};
    double bandwidth_hz{4.0e6};
    double chirp_duration_s{1.0e-6};
    double range_origin_m{0.0};
    double range_spacing_m{0.25};
};

class RangeCompressionNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarAccelControlToken>,
          graph::TypeList<SarAccelControlToken>,
          RangeCompressionNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    RangeCompressionNode() = default;
    explicit RangeCompressionNode(RangeCompressionConfig config);

    std::optional<SarAccelControlToken> Transfer(
        const SarAccelControlToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 9> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "true",
                .enum_values = std::nullopt,
                .description = "Apply FFT-backed range compression to pulse IQ data"
            },
            graph::JsonField{
                .name = "gain",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "1.0",
                .enum_values = std::nullopt,
                .description = "Linear gain applied to compressed magnitude bins"
            },
            graph::JsonField{
                .name = "sample_rate_hz",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "48000.0",
                .enum_values = std::nullopt,
                .description = "Sample rate used for FFT frequency axis metadata"
            },
            graph::JsonField{
                .name = "mode",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "fft_magnitude",
                .enum_values = std::nullopt,
                .description = "Range compression implementation mode"
            },
            graph::JsonField{
                .name = "output",
                .type = graph::JsonType::String,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "magnitude",
                .enum_values = std::nullopt,
                .description = "Matched-filter output representation"
            },
            graph::JsonField{
                .name = "bandwidth_hz",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "4000000.0",
                .enum_values = std::nullopt,
                .description = "Linear-FM chirp bandwidth for matched-filter mode"
            },
            graph::JsonField{
                .name = "chirp_duration_s",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0.000001",
                .enum_values = std::nullopt,
                .description = "Linear-FM chirp duration for matched-filter mode"
            },
            graph::JsonField{
                .name = "range_origin_m",
                .type = graph::JsonType::Number,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "0.0",
                .enum_values = std::nullopt,
                .description = "Range origin metadata for matched-filter mode"
            },
            graph::JsonField{
                .name = "range_spacing_m",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0.25",
                .enum_values = std::nullopt,
                .description = "Range-bin spacing metadata for matched-filter mode"
            }
        }};
    }

    void SetConfig(const RangeCompressionConfig& config);
    const RangeCompressionConfig& GetConfig() const noexcept;

private:
    RangeCompressionConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
