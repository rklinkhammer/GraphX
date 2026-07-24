// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

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
  struct PreviousRateSample {
    std::uint64_t generation = 0;
    std::uint64_t run_epoch = 0;
    std::uint64_t config_revision = 0;
    std::string config_etag;
    std::uint64_t sampled_at_monotonic_ms = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t dequeued = 0;
    bool valid = false;
  };

  [[nodiscard]] bool ConsumeCollectionInterruption() const;

  std::weak_ptr<GraphRuntimeSession> runtime_session_;
  mutable std::atomic<bool> interrupt_next_collection_{false};
  mutable std::mutex rate_mutex_;
  mutable PreviousRateSample previous_rate_sample_;
};

} // namespace graph::dashboard
