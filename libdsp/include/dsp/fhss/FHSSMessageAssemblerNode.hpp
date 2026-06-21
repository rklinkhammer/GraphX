#pragma once

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSMessageAssemblerNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSAssembledMessageToken>,
          graph::TypeList<FHSSAssembledMessageToken>,
          FHSSMessageAssemblerNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using InputTokenType = FHSSAssembledMessageToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSMessageAssemblerNode() = default;
  explicit FHSSMessageAssemblerNode(FHSSMessageAssemblerConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSMessageAssemblerConfig config) {
    config_ = std::move(config);
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSMessageAssemblerConfigFromJson(cfg));
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
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    std::vector<FHSSDecodedPulseWord> decoded;
    decoded.reserve(input.sidecar.ordered_pulses.size());
    for (const auto &packet : input.sidecar.ordered_pulses) {
      decoded.push_back(FHSSDecodedPulseWordFromGraphX(packet));
    }
    auto assembled =
        FHSSMessageAssemblerKernel::Assemble(std::move(decoded), config_);

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = FHSSGraphXAssembledMessageFromKernel(assembled);
    output.sidecar.ordered_pulses = input.sidecar.ordered_pulses;
    output.sidecar.diagnostics.unsupported_overlap_rejected = true;
    output.sidecar.diagnostics.unsupported_impairments_rejected = true;
    output.sidecar.diagnostics.synchronization_assumption =
        "known message_start_sample = 0";
    return output;
  }

private:
  FHSSMessageAssemblerConfig config_{};
};

} // namespace dsp::fhss
