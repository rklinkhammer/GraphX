// SPDX-License-Identifier: MIT

#pragma once

#include "core/ActiveQueue.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace graph::dashboard {

struct FHSSMessageCorrelation {
  std::string scenario_id;
  std::uint64_t message_id = 0;
  std::uint64_t release_sequence = 0;
};

enum class FHSSMessageInjectionKind {
  ScheduledMessage,
  WholeSchedule,
};

struct FHSSMessageInjectionRequest {
  FHSSMessageInjectionKind kind = FHSSMessageInjectionKind::WholeSchedule;
  FHSSMessageCorrelation correlation{};
  nlohmann::json scheduled_message = nullptr;
  bool end_of_stream_after_produce = false;
};

enum class FHSSMessageTerminalStatus {
  Completed,
  Rejected,
  Failed,
  TimedOut,
  Cancelled,
};

struct FHSSMessageTerminalResult {
  FHSSMessageCorrelation correlation{};
  FHSSMessageTerminalStatus status = FHSSMessageTerminalStatus::Completed;
  std::string code;
  std::string message;
  nlohmann::json diagnostics = nullptr;
};

class IFHSSMessageInjectionSource {
public:
  virtual ~IFHSSMessageInjectionSource() = default;

  [[nodiscard]] virtual core::ActiveQueue<FHSSMessageInjectionRequest> &
  GetMessageInjectionQueue() = 0;

  [[nodiscard]] virtual bool IsMessageInjectionQueueEnabled() const = 0;
  virtual void DisableMessageInjectionQueue() = 0;
  virtual void ResetMessageInjectionQueue() = 0;

  [[nodiscard]] virtual std::optional<FHSSMessageCorrelation>
  ProduceInjectedMessage() = 0;
};

} // namespace graph::dashboard
