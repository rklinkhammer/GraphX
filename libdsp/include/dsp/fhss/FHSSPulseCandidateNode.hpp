#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>

namespace dsp::fhss {

class FHSSPulseCandidateNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSPulseCandidateToken>,
          graph::TypeList<FHSSPulseCandidateToken>, FHSSPulseCandidateNode> {
public:
  using TokenType = FHSSPulseCandidateToken;

  std::optional<TokenType>
  Transfer(const TokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    return input;
  }
};

} // namespace dsp::fhss
