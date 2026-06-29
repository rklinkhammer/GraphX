// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>

namespace graph {

struct GraphMetrics;

// Stores graph-level metrics configuration and aggregate snapshot state.
struct GraphManagerMetricsSnapshot {
  std::atomic<bool> metrics_enabled{false};
};

} // namespace graph
