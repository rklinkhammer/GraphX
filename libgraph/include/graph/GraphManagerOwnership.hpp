// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <vector>

namespace graph {

class INode;
class IDynamicEdge;

// Owns the canonical graph topology objects (nodes and edges).
struct GraphManagerOwnership {
  std::vector<std::shared_ptr<INode>> nodes;
  std::vector<std::shared_ptr<IDynamicEdge>> edges;
};

} // namespace graph
