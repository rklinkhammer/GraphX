#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

struct FHSSDownconverterConfig {
  double input_iq_center_frequency_hz = 0.0;
  double input_reference_frequency_hz = 0.0;
  double output_iq_center_frequency_hz = 0.0;
  double output_reference_frequency_hz = 0.0;
  double translation_frequency_hz = 0.0;
  bool passthrough = true;
  FHSSGraphXDownconverterPhaseConvention phase_convention =
      FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation;
  double sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
};

[[nodiscard]] inline FHSSVoidResult
ValidateFHSSDownconverterConfig(const FHSSDownconverterConfig &config) {
  if (config.sample_rate_hz <= 0.0) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidTiming,
                                     "downconverter sample rate must be positive"));
  }

  const double expected_translation =
      config.output_iq_center_frequency_hz - config.input_iq_center_frequency_hz;
  if (config.passthrough) {
    if (!NearlyEqual(config.translation_frequency_hz, 0.0) ||
        !NearlyEqual(config.input_iq_center_frequency_hz,
                     config.output_iq_center_frequency_hz) ||
        !NearlyEqual(config.input_reference_frequency_hz,
                     config.output_reference_frequency_hz) ||
        config.phase_convention !=
            FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidIqOffset,
          "downconverter passthrough requires matching reference frames"));
    }
    return {};
  }

  if (config.phase_convention !=
      FHSSGraphXDownconverterPhaseConvention::
          OutputTimesExpNegativeJTwoPiTranslationT) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidIqOffset,
        "translated downconverter must declare the negative exponential phase convention"));
  }
  if (!NearlyEqual(config.translation_frequency_hz, expected_translation)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidIqOffset,
        "downconverter translation must equal output center minus input center"));
  }
  return {};
}

[[nodiscard]] inline FHSSDownconverterConfig
FHSSDownconverterConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSDownconverterConfig config{};
  config.input_iq_center_frequency_hz =
      FHSSJsonDouble(json, "input_iq_center_frequency_hz", 0.0);
  config.input_reference_frequency_hz =
      FHSSJsonDouble(json, "input_reference_frequency_hz",
                     config.input_iq_center_frequency_hz);
  config.output_iq_center_frequency_hz =
      FHSSJsonDouble(json, "output_iq_center_frequency_hz",
                     config.input_iq_center_frequency_hz);
  config.output_reference_frequency_hz =
      FHSSJsonDouble(json, "output_reference_frequency_hz",
                     config.output_iq_center_frequency_hz);
  config.translation_frequency_hz =
      FHSSJsonDouble(json, "translation_frequency_hz",
                     config.output_iq_center_frequency_hz -
                         config.input_iq_center_frequency_hz);
  config.passthrough = FHSSJsonBool(json, "passthrough",
                                    NearlyEqual(config.translation_frequency_hz,
                                                0.0));
  config.sample_rate_hz =
      FHSSJsonDouble(json, "sample_rate_hz",
                     FHSSProtocolConstants::kSampleRateHz);
  if (!config.passthrough) {
    config.phase_convention =
        FHSSGraphXDownconverterPhaseConvention::
            OutputTimesExpNegativeJTwoPiTranslationT;
  }
  if (auto validation = ValidateFHSSDownconverterConfig(config); !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

class FHSSDownconverterNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSSyntheticIqToken>,
          graph::TypeList<FHSSDownconvertedIqToken>,
          FHSSDownconverterNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using InputTokenType = FHSSSyntheticIqToken;
  using OutputTokenType = FHSSDownconvertedIqToken;

  FHSSDownconverterNode() = default;
  explicit FHSSDownconverterNode(FHSSDownconverterConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSDownconverterConfig config) {
    config_ = std::move(config);
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSDownconverterConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params;
    params["input_iq_center_frequency_hz"] =
        config_.input_iq_center_frequency_hz;
    params["input_reference_frequency_hz"] =
        config_.input_reference_frequency_hz;
    params["output_iq_center_frequency_hz"] =
        config_.output_iq_center_frequency_hz;
    params["output_reference_frequency_hz"] =
        config_.output_reference_frequency_hz;
    params["translation_frequency_hz"] = config_.translation_frequency_hz;
    params["passthrough"] = config_.passthrough;
    params["sample_rate_hz"] = config_.sample_rate_hz;
    return graph::JsonView(params);
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return graph::JsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"input_iq_center_frequency_hz",
            "input_reference_frequency_hz",
            "output_iq_center_frequency_hz",
            "output_reference_frequency_hz",
            "translation_frequency_hz",
            "passthrough",
            "sample_rate_hz"};
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (auto validation = ValidateFHSSDownconverterConfig(config_);
        !validation) {
      return std::nullopt;
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.downconverter = Metadata(input.sidecar.iq);
    output.sidecar.truth_metadata_required_for_decision = false;

    if (config_.passthrough) {
      output.sidecar.iq = input.sidecar.iq;
      output.sidecar.iq.sample_time_map.input_sample_rate_hz =
          config_.sample_rate_hz;
      output.sidecar.iq.sample_time_map.output_sample_rate_hz =
          config_.sample_rate_hz;
      return output;
    }

    const auto &source = *input.sidecar.iq.host_complex64_samples;
    const auto offset = input.sidecar.iq.sample_offset;
    const auto count = input.sidecar.iq.sample_count;
    if (offset > source.size() || count > source.size() - offset) {
      return std::nullopt;
    }

    auto translated =
        std::make_shared<std::vector<std::complex<double>>>();
    translated->reserve(static_cast<std::size_t>(count));
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double radians_per_sample =
        -kTwoPi * config_.translation_frequency_hz / config_.sample_rate_hz;
    const auto global_start =
        input.sidecar.iq.sample_time_map.input_packet_global_start_sample;
    for (std::uint64_t i = 0; i < count; ++i) {
      const double phase =
          radians_per_sample * static_cast<double>(global_start + i);
      translated->push_back(source[offset + i] *
                            std::complex<double>(std::cos(phase),
                                                 std::sin(phase)));
    }

    output.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
        translated, translated->size(), input.sidecar.iq.sample_time_map);
    output.sidecar.iq.sample_time_map.input_sample_rate_hz =
        config_.sample_rate_hz;
    output.sidecar.iq.sample_time_map.output_sample_rate_hz =
        config_.sample_rate_hz;
    return output;
  }

private:
  [[nodiscard]] FHSSGraphXDownconverterMetadata
  Metadata(const FHSSGraphXComplexEvidence &input) const {
    FHSSGraphXDownconverterMetadata metadata{};
    metadata.input_iq_center_frequency_hz =
        config_.input_iq_center_frequency_hz;
    metadata.input_reference_frequency_hz =
        config_.input_reference_frequency_hz;
    metadata.output_iq_center_frequency_hz =
        config_.output_iq_center_frequency_hz;
    metadata.output_reference_frequency_hz =
        config_.output_reference_frequency_hz;
    metadata.translation_frequency_hz = config_.translation_frequency_hz;
    metadata.passthrough = config_.passthrough;
    metadata.phase_convention = config_.phase_convention;
    metadata.sample_rate_hz = config_.sample_rate_hz;
    metadata.input_global_start_sample =
        input.sample_time_map.input_packet_global_start_sample;
    metadata.output_global_start_sample =
        input.sample_time_map.input_packet_global_start_sample;
    metadata.sample_time_map = input.sample_time_map;
    return metadata;
  }

  FHSSDownconverterConfig config_{};
};

} // namespace dsp::fhss
