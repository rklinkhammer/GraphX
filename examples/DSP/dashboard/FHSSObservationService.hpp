// SPDX-License-Identifier: MIT
#pragma once

#include "FHSSObservationModels.hpp"
#include "dsp/fhss/FHSSReceiverObservationSource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace graph::dashboard {
class GraphConfigurationService;
class GraphRuntimeSession;
}

namespace dsp::fhss::dashboard {

class FHSSObservationService {
public:
  static constexpr std::size_t kMaxObservedPulses = 512;
  static constexpr std::size_t kMaxExpectedMessages = 64;
  static constexpr std::size_t kMaxSpectrumBins = 256;
  static constexpr std::size_t kMaxHistoryEntries = 1;
  static constexpr std::size_t kMaxObservationSources = 128;
  static constexpr std::size_t kMaxProvenanceRecords = 256;
  static constexpr std::size_t kMaxResponseBytes = 1u << 20;
  static constexpr std::uint64_t kTimingToleranceSamples = 64;

  FHSSObservationService(
      std::shared_ptr<graph::dashboard::GraphConfigurationService>
          configuration_service,
      std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session);

  [[nodiscard]] FHSSExpectedTruth ExpectedTruth() const;
  [[nodiscard]] FHSSReceiverObservation ReceiverObservation() const;
  [[nodiscard]] FHSSComparisonResult Comparison() const;
  [[nodiscard]] static FHSSComparisonResult
  CompareDocuments(const nlohmann::json &expected,
                   const nlohmann::json &observed);
  [[nodiscard]] static nlohmann::json
  SpectrumFromCapture(const dsp::fhss::FHSSReceiverSampleCapture &capture,
                      std::size_t fft_size);
  [[nodiscard]] nlohmann::json Spectrum(
                                        std::optional<std::uint32_t> physical_channel_index,
                                        std::size_t fft_size) const;
  [[nodiscard]] nlohmann::json Provenance() const;
  [[nodiscard]] nlohmann::json History() const;

private:
  std::shared_ptr<graph::dashboard::GraphConfigurationService>
      configuration_service_;
  std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session_;
};

} // namespace dsp::fhss::dashboard
