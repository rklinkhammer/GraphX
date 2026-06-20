#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSMessageAssemblerNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDecodedPulseWordsToken>,
          graph::TypeList<FHSSAssembledMessageToken>,
          FHSSMessageAssemblerNode> {
public:
  using InputTokenType = FHSSDecodedPulseWordsToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSMessageAssemblerNode() = default;
  explicit FHSSMessageAssemblerNode(FHSSMessageAssemblerConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSMessageAssemblerConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    std::vector<FHSSDecodedPulseWord> decoded;
    decoded.reserve(input.sidecar.decoded_pulses.size());
    for (const auto &packet : input.sidecar.decoded_pulses) {
      decoded.push_back(FHSSDecodedPulseWordFromGraphX(packet));
    }
    auto assembled =
        FHSSMessageAssemblerKernel::Assemble(std::move(decoded), config_);

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = FHSSGraphXAssembledMessageFromKernel(assembled);
    return output;
  }

private:
  FHSSMessageAssemblerConfig config_{};
};

} // namespace dsp::fhss
