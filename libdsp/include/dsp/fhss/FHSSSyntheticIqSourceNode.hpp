#pragma once

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSSyntheticIqSourceNode
    : public graph::NamedSourceNode<FHSSSyntheticIqSourceNode,
                                    FHSSSyntheticIqToken>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using OutputTokenType = FHSSSyntheticIqToken;

  FHSSSyntheticIqSourceNode() = default;
  explicit FHSSSyntheticIqSourceNode(FHSSSyntheticIqGeneratorConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSSyntheticIqGeneratorConfig config) {
    config_ = std::move(config);
    emitted_ = false;
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSSyntheticIqGeneratorConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    return FHSSFixtureParametersJson();
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &param_name) const override {
    return FHSSFixtureParameterDescription(param_name);
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return FHSSFixtureParameterNames();
  }

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 0>) override {
    if (emitted_) {
      return std::nullopt;
    }
    emitted_ = true;

    auto fixture = GenerateSyntheticIqFixture(config_);
    if (!fixture) {
      return std::nullopt;
    }

    auto samples =
        std::make_shared<const std::vector<std::complex<double>>>(
            std::move(fixture->samples));
    FHSSGraphXSampleTimeMap sample_time_map{};
    sample_time_map.input_packet_global_start_sample = 0;
    OutputTokenType token{};
    token.token_id = next_token_id_++;
    token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
        samples, samples->size(), sample_time_map);
    token.sidecar.truth_pulses = std::move(fixture->truth_pulses);
    token.sidecar.timing = fixture->timing;
    token.sidecar.truth_is_validation_only = true;
    return token;
  }

private:
  FHSSSyntheticIqGeneratorConfig config_{};
  bool emitted_{false};
  std::uint64_t next_token_id_{1};
};

} // namespace dsp::fhss
