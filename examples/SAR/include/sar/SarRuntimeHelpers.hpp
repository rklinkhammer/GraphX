#pragma once

#include "graph/GraphManager.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

namespace sar::runtime {

using SteadyClock = std::chrono::steady_clock;

inline std::uint64_t ElapsedUs(const SteadyClock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - start);
    const auto count = static_cast<std::uint64_t>(elapsed.count());
    return (count == 0u) ? 1u : count;
}

inline std::shared_ptr<SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SarDiagnosticsSinkNode") {
            continue;
        }
        return wrapper->GetNode<SarDiagnosticsSinkNode>();
    }

    return nullptr;
}

} // namespace sar::runtime