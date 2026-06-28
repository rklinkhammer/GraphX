/**
 * @file EdgeControl.hpp
 * @brief Domain-neutral typed edge completion state.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace graph {

struct EdgeEndOfStream {
  friend bool operator==(const EdgeEndOfStream &,
                         const EdgeEndOfStream &) = default;
};

struct EdgeWatermark {
  std::uint64_t position = 0;
  bool final = false;

  friend bool operator==(const EdgeWatermark &, const EdgeWatermark &) = default;
};

struct EdgeCancellation {
  std::string reason;

  friend bool operator==(const EdgeCancellation &,
                         const EdgeCancellation &) = default;
};

struct EdgeFailure {
  std::string message;

  friend bool operator==(const EdgeFailure &, const EdgeFailure &) = default;
};

using EdgeControl =
    std::variant<std::monostate, EdgeEndOfStream, EdgeWatermark,
                 EdgeCancellation, EdgeFailure>;

[[nodiscard]] inline bool IsTerminalEdgeControl(const EdgeControl &control) {
  if (std::holds_alternative<EdgeEndOfStream>(control) ||
      std::holds_alternative<EdgeCancellation>(control) ||
      std::holds_alternative<EdgeFailure>(control)) {
    return true;
  }
  if (const auto *watermark = std::get_if<EdgeWatermark>(&control)) {
    return watermark->final;
  }
  return false;
}

[[nodiscard]] inline bool IsSuccessfulTerminalEdgeControl(
    const EdgeControl &control) {
  return std::holds_alternative<EdgeEndOfStream>(control) ||
         (std::holds_alternative<EdgeWatermark>(control) &&
          std::get<EdgeWatermark>(control).final);
}

enum class RequiredInputOutcome {
  Open,
  Complete,
  Incomplete,
  Cancelled,
  Failed,
};

struct RequiredInputStatus {
  RequiredInputOutcome outcome = RequiredInputOutcome::Open;
  std::size_t required_inputs = 0;
  std::size_t terminal_inputs = 0;
  std::vector<std::size_t> missing_inputs;
  std::optional<std::size_t> problem_input;
  std::string detail;

  [[nodiscard]] bool IsComplete() const {
    return outcome == RequiredInputOutcome::Complete;
  }
};

template <std::size_t InputCount> class RequiredInputCompletion {
public:
  RequiredInputCompletion() { required_.fill(true); }

  bool SetRequired(std::size_t port, bool required) {
    std::lock_guard lock(mutex_);
    if (port >= InputCount || observed_[port]) {
      return false;
    }
    required_[port] = required;
    return true;
  }

  bool Observe(std::size_t port, const EdgeControl &control) {
    std::lock_guard lock(mutex_);
    if (port >= InputCount || !required_[port]) {
      return false;
    }

    if (terminal_[port]) {
      return controls_[port] == control;
    }

    if (std::holds_alternative<std::monostate>(control)) {
      observed_[port] = true;
      return true;
    }

    if (const auto *watermark = std::get_if<EdgeWatermark>(&control)) {
      if (watermarks_[port] && watermark->position < *watermarks_[port]) {
        return false;
      }
      watermarks_[port] = watermark->position;
    }

    observed_[port] = true;
    controls_[port] = control;
    terminal_[port] = IsTerminalEdgeControl(control);
    return true;
  }

  [[nodiscard]] EdgeControl Control(std::size_t port) const {
    std::lock_guard lock(mutex_);
    if (port >= InputCount) {
      return {};
    }
    return controls_[port];
  }

  [[nodiscard]] RequiredInputStatus Status() const {
    return BuildStatus(false);
  }

  [[nodiscard]] RequiredInputStatus Finalize() const {
    return BuildStatus(true);
  }

private:
  [[nodiscard]] RequiredInputStatus BuildStatus(bool finalizing) const {
    std::lock_guard lock(mutex_);
    RequiredInputStatus status{};

    for (std::size_t port = 0; port < InputCount; ++port) {
      if (!required_[port]) {
        continue;
      }
      ++status.required_inputs;
      if (terminal_[port]) {
        ++status.terminal_inputs;
      } else {
        status.missing_inputs.push_back(port);
      }
      if (!status.problem_input &&
          std::holds_alternative<EdgeFailure>(controls_[port])) {
        status.problem_input = port;
        status.detail = std::get<EdgeFailure>(controls_[port]).message;
        status.outcome = RequiredInputOutcome::Failed;
      }
    }

    if (status.outcome != RequiredInputOutcome::Failed) {
      for (std::size_t port = 0; port < InputCount; ++port) {
        if (required_[port] &&
            std::holds_alternative<EdgeCancellation>(controls_[port])) {
          status.problem_input = port;
          status.detail = std::get<EdgeCancellation>(controls_[port]).reason;
          status.outcome = RequiredInputOutcome::Cancelled;
          break;
        }
      }
    }

    if (status.outcome == RequiredInputOutcome::Failed ||
        status.outcome == RequiredInputOutcome::Cancelled) {
      return status;
    }
    if (status.missing_inputs.empty()) {
      status.outcome = RequiredInputOutcome::Complete;
    } else if (finalizing) {
      status.outcome = RequiredInputOutcome::Incomplete;
      status.detail = "required input did not reach a terminal state";
    }
    return status;
  }

  mutable std::mutex mutex_;
  std::array<bool, InputCount> required_{};
  std::array<bool, InputCount> observed_{};
  std::array<bool, InputCount> terminal_{};
  std::array<std::optional<std::uint64_t>, InputCount> watermarks_{};
  std::array<EdgeControl, InputCount> controls_{};
};

} // namespace graph
