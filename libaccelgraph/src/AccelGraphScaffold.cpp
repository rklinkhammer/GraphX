// SPDX-License-Identifier: MIT

#include "accelgraph/AccelGraphScaffold.hpp"

#include <sstream>

#include "graph/CapabilityBus.hpp"
#include "graph/NodeProvider.hpp"

namespace accelgraph {

std::string BuildScaffoldSummary() {
    graph::CapabilityBus capability_bus;
    const bool has_node_provider = capability_bus.Has<graph::INodeProvider>();

    std::ostringstream output;
    output << "libaccelgraph scaffold"
           << ";node_provider_registered=" << (has_node_provider ? "true" : "false")
           << ";metal_enabled=" << (ACCELGRAPH_ENABLE_METAL ? "true" : "false")
           << ";cuda_enabled=" << (ACCELGRAPH_ENABLE_CUDA ? "true" : "false");
    return output.str();
}

}  // namespace accelgraph
