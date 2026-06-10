#pragma once

#include "sar/SarMessages.hpp"

#include "graph/PortSpec.hpp"
#include "graph/SplitNode.hpp"

#include <string>

namespace sar {

class SarPulseFanoutNode : public graph::SplitNode4<SarAccelControlToken> {
public:
    static constexpr char kInput[] = "In";
    static constexpr char kOutput0[] = "Tile0";
    static constexpr char kOutput1[] = "Tile1";
    static constexpr char kOutput2[] = "Tile2";
    static constexpr char kOutput3[] = "Tile3";

    using Ports = std::tuple<
        graph::PortSpec<0, SarAccelControlToken, graph::PortDirection::Input, kInput>,
        graph::PortSpec<0, SarAccelControlToken, graph::PortDirection::Output, kOutput0>,
        graph::PortSpec<1, SarAccelControlToken, graph::PortDirection::Output, kOutput1>,
        graph::PortSpec<2, SarAccelControlToken, graph::PortDirection::Output, kOutput2>,
        graph::PortSpec<3, SarAccelControlToken, graph::PortDirection::Output, kOutput3>
    >;

    SarPulseFanoutNode() = default;
    ~SarPulseFanoutNode() override = default;

    std::string GetNodeTypeName() const {
        return "SarPulseFanoutNode";
    }
};

} // namespace sar
