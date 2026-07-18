// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/GraphConfigurationService.hpp"

namespace dsp::fhss::dashboard {

class FHSSDashboardConfigurationPolicy final
    : public graph::dashboard::GraphConfigurationPolicy {
public:
  FHSSDashboardConfigurationPolicy();
  explicit FHSSDashboardConfigurationPolicy(nlohmann::json receiver_template);
  [[nodiscard]] nlohmann::json
  ExtractAuthoritative(const nlohmann::json &document) const override;
  [[nodiscard]] nlohmann::json
  DeriveEffective(const nlohmann::json &base_document,
                  const nlohmann::json &authoritative) const override;
  [[nodiscard]] nlohmann::json
  Validate(const nlohmann::json &authoritative) const override;
  [[nodiscard]] nlohmann::json GeneratedPaths() const override;
  [[nodiscard]] nlohmann::json Provenance() const override;
  [[nodiscard]] nlohmann::json
  ReceiverMinimalGraph(const nlohmann::json &effective) const override;
  [[nodiscard]] std::optional<std::string>
  NormalizePatchPath(std::string_view path) const override;
  [[nodiscard]] std::string AuthoritativeRootPointer() const override;

private:
  nlohmann::json receiver_template_;
};

} // namespace dsp::fhss::dashboard
