// SPDX-License-Identifier: MIT
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace dsp::fhss::dashboard {

enum class FHSSAvailabilityState { available, unavailable };

struct FHSSObservableAvailability {
  FHSSAvailabilityState state = FHSSAvailabilityState::unavailable;
  std::string reason = "generation_not_available";
  [[nodiscard]] nlohmann::json ToJson() const;
};

struct FHSSObservationProvenance {
  std::uint64_t generation = 0;
  std::uint64_t run_epoch = 0;
  std::string node_id;
  std::string node_class;
  std::string source_schema;
  std::string packet_field;
  std::string sample_interval;
  std::string unit;
  std::string capture_time;
  std::string transformation;
  [[nodiscard]] nlohmann::json ToJson() const;
};

struct FHSSExpectedTruth {
  nlohmann::json document;
};

struct FHSSReceiverObservation {
  nlohmann::json document;
};

struct FHSSComparisonResult {
  nlohmann::json document;
};

} // namespace dsp::fhss::dashboard
