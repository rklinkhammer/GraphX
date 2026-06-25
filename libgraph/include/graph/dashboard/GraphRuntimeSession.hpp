// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <string>

namespace graph::dashboard {

class GraphRuntimeSession {
public:
  enum class State { initializing, ready, shutting_down };

  GraphRuntimeSession();

  [[nodiscard]] State GetState() const;
  [[nodiscard]] bool IsReady() const;
  [[nodiscard]] std::string StateString() const;

  void MarkReady();
  void MarkShuttingDown();

private:
  std::atomic<State> state_;
};

} // namespace graph::dashboard
