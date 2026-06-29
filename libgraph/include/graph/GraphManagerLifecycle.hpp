// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <mutex>

namespace graph {

// Tracks graph lifecycle synchronization and state flags.
struct GraphManagerLifecycleState {
  mutable std::mutex lifecycle_mtx;
  std::atomic<bool> initialized{false};
  std::atomic<bool> started{false};
};

} // namespace graph
