/**
 * @file FHSSFixtureUtils.hpp
 * @brief Stable JSON views used by deterministic FHSS fixture configuration.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "config/JsonView.hpp"

#include <utility>

#include <nlohmann/json.hpp>

namespace dsp::fhss {

[[nodiscard]] inline graph::JsonView
FHSSStableParameterJsonView(nlohmann::json value) {
  static thread_local nlohmann::json storage;
  storage = std::move(value);
  return graph::JsonView(storage);
}

[[nodiscard]] inline graph::JsonView
FHSSStableParameterDescriptionJsonView(nlohmann::json value) {
  static thread_local nlohmann::json storage;
  storage = std::move(value);
  return graph::JsonView(storage);
}

} // namespace dsp::fhss
