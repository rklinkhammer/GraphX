#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/NamedNodes.hpp"

#include <type_traits>

namespace dsp::fhss {

class FHSSMessageSinkNode
    : public graph::NamedSinkNode<FHSSMessageSinkNode,
                                  FHSSAssembledMessageToken> {
public:
  using InputTokenType = FHSSAssembledMessageToken;

  bool Consume(const InputTokenType &input,
               std::integral_constant<std::size_t, 0>) override {
    last_diagnostics_ = input.sidecar.diagnostics;
    return true;
  }

  [[nodiscard]] const FHSSDiagnosticsPacket &last_diagnostics() const {
    return last_diagnostics_;
  }

private:
  FHSSDiagnosticsPacket last_diagnostics_{};
};

} // namespace dsp::fhss
