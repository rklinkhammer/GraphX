// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphRuntimeSession.hpp"

namespace graph::dashboard {

GraphRuntimeSession::GraphRuntimeSession() : state_(State::initializing) {}

GraphRuntimeSession::State GraphRuntimeSession::GetState() const {
  return state_.load();
}

bool GraphRuntimeSession::IsReady() const {
  const auto state = state_.load();
  return state == State::ready;
}

std::string GraphRuntimeSession::StateString() const {
  switch (state_.load()) {
  case State::initializing:
    return "initializing";
  case State::ready:
    return "ready";
  case State::shutting_down:
    return "shutting_down";
  }
  return "unknown";
}

void GraphRuntimeSession::MarkReady() { state_.store(State::ready); }

void GraphRuntimeSession::MarkShuttingDown() {
  state_.store(State::shutting_down);
}

} // namespace graph::dashboard
