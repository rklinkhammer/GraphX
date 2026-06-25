// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <memory>

#include <nlohmann/json.hpp>

namespace graph::dashboard {

class GraphRuntimeSession;

class GraphSnapshotCollector {
public:
  GraphSnapshotCollector() = default;

  void BindRuntimeSession(std::shared_ptr<GraphRuntimeSession> runtime_session);
  void InjectNextCollectionInterruptionForTesting();

  [[nodiscard]] nlohmann::json GetMetricsSnapshot() const;
  [[nodiscard]] nlohmann::json GetEdgeMetricsSnapshot() const;
  [[nodiscard]] nlohmann::json GetDiagnosticsSnapshot() const;

private:
  [[nodiscard]] bool ConsumeCollectionInterruption() const;

  std::weak_ptr<GraphRuntimeSession> runtime_session_;
  mutable std::atomic<bool> interrupt_next_collection_{false};
};

} // namespace graph::dashboard
