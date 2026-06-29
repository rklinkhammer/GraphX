// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <vector>

namespace graph {

struct EdgeMetadata;
struct EdgeMetrics;

// Keeps per-edge metadata and metrics aligned with edge creation order.
struct GraphManagerEdgeOwnership {
  std::vector<EdgeMetadata> edge_metadata;
  std::vector<std::shared_ptr<EdgeMetrics>> edge_metrics;
};

} // namespace graph
