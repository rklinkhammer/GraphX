// SPDX-License-Identifier: MIT

#pragma once

#include <nlohmann/json.hpp>

namespace graph::dashboard {

class GraphSnapshotCollector {
public:
  [[nodiscard]] nlohmann::json GetMetricsSnapshot() const;
  [[nodiscard]] nlohmann::json GetEdgeMetricsSnapshot() const;
};

} // namespace graph::dashboard
