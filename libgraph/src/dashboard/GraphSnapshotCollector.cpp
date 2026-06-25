// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphSnapshotCollector.hpp"

namespace graph::dashboard {

nlohmann::json GraphSnapshotCollector::GetMetricsSnapshot() const {
  return nlohmann::json{{"schema", "graphx.dashboard.metrics.v1"},
                        {"graph", {{"total_items_processed", 0},
                                   {"total_items_rejected", 0},
                                   {"total_messages_processed", 0},
                                   {"graph_total_enqueued", 0},
                                   {"graph_total_dequeued", 0},
                                   {"backpressure_events", 0},
                                   {"peak_queue_depth", 0},
                                   {"peak_active_threads", 0}}},
                        {"nodes", nlohmann::json::array()},
                        {"edges", nlohmann::json::array()}};
}

nlohmann::json GraphSnapshotCollector::GetEdgeMetricsSnapshot() const {
  return nlohmann::json{{"schema", "graphx.dashboard.edge_metrics.v1"},
                        {"edges", nlohmann::json::array()}};
}

} // namespace graph::dashboard
