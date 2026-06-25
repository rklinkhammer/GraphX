#pragma once

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "graph/dashboard/FHSSStepping.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace dsp::fhss {

class FHSSSyntheticIqSourceNode
    : public graph::NamedSourceNode<FHSSSyntheticIqSourceNode,
                                    FHSSSyntheticIqToken>,
      public graph::IConfigurable,
  public graph::IParameterized,
  public graph::dashboard::IFHSSMessageInjectionSource {
public:
  using OutputTokenType = FHSSSyntheticIqToken;

  FHSSSyntheticIqSourceNode() = default;
  explicit FHSSSyntheticIqSourceNode(FHSSSyntheticIqGeneratorConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSSyntheticIqGeneratorConfig config) {
    config_ = std::move(config);
    emitted_ = false;
    compatibility_default_request_pending_ = true;
    message_queue_.Clear();
    message_queue_.Enable();
    message_queue_enabled_ = true;
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSSyntheticIqGeneratorConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    return FHSSFixtureParametersJson();
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &param_name) const override {
    return FHSSFixtureParameterDescription(param_name);
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return FHSSFixtureParameterNames();
  }

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 0>) override {
    if (compatibility_default_request_pending_ && message_queue_.Size() == 0) {
      graph::dashboard::FHSSMessageInjectionRequest request;
      request.kind = graph::dashboard::FHSSMessageInjectionKind::WholeSchedule;
      request.end_of_stream_after_produce = true;
      if (!message_queue_.Enqueue(request)) {
        return std::nullopt;
      }
      compatibility_default_request_pending_ = false;
    }
    auto token = ProduceFromQueue();
    if (token) {
      emitted_ = true;
    }
    return token;
  }

  [[nodiscard]] core::ActiveQueue<graph::dashboard::FHSSMessageInjectionRequest> &
  GetMessageInjectionQueue() override {
    compatibility_default_request_pending_ = false;
    return message_queue_;
  }

  [[nodiscard]] bool IsMessageInjectionQueueEnabled() const override {
    return message_queue_enabled_;
  }

  void DisableMessageInjectionQueue() override {
    message_queue_enabled_ = false;
    compatibility_default_request_pending_ = false;
    message_queue_.Disable();
  }

  void ResetMessageInjectionQueue() override {
    message_queue_.Disable();
    message_queue_.Clear();
    message_queue_.Enable();
    message_queue_enabled_ = true;
    compatibility_default_request_pending_ = false;
    emitted_ = false;
  }

  [[nodiscard]] std::optional<graph::dashboard::FHSSMessageCorrelation>
  ProduceInjectedMessage() override {
    auto token = ProduceFromQueue();
    if (!token) {
      return std::nullopt;
    }
    emitted_ = true;
    return token->sidecar.correlation;
  }

private:
  [[nodiscard]] std::optional<OutputTokenType> ProduceFromQueue() {
    graph::dashboard::FHSSMessageInjectionRequest request;
    if (!message_queue_.Dequeue(request)) {
      message_queue_enabled_ = false;
      return std::nullopt;
    }

    auto fixture = GenerateFixtureForRequest(request);
    if (!fixture) {
      return std::nullopt;
    }

    auto samples =
        std::make_shared<const std::vector<std::complex<double>>>(
            std::move(fixture->samples));
    FHSSGraphXSampleTimeMap sample_time_map{};
    sample_time_map.input_packet_global_start_sample = 0;
    OutputTokenType token{};
    token.token_id = next_token_id_++;
    token.sidecar.correlation = request.correlation;
    token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
        samples, samples->size(), sample_time_map);
    token.sidecar.truth_pulses = std::move(fixture->truth_pulses);
    token.sidecar.timing = fixture->timing;
    token.sidecar.truth_is_validation_only = true;
    if (request.end_of_stream_after_produce) {
      DisableMessageInjectionQueue();
    }
    return token;
  }

  [[nodiscard]] std::optional<FHSSSyntheticIqFixture>
  GenerateFixtureForRequest(
      const graph::dashboard::FHSSMessageInjectionRequest &request) const {
    if (request.kind == graph::dashboard::FHSSMessageInjectionKind::WholeSchedule) {
      auto fixture = GenerateSyntheticIqFixture(config_);
      if (!fixture) {
        return std::nullopt;
      }
      return fixture.value();
    }
    FHSSSyntheticIqGeneratorConfig config = config_;
    config.messages = FHSSMessagesFromJson(
        nlohmann::json{{"messages", nlohmann::json::array({request.scheduled_message})}});
    auto fixture = GenerateSyntheticIqFixture(config);
    if (!fixture) {
      return std::nullopt;
    }
    return fixture.value();
  }

  FHSSSyntheticIqGeneratorConfig config_{};
  bool emitted_{false};
  bool compatibility_default_request_pending_{true};
  std::uint64_t next_token_id_{1};
  bool message_queue_enabled_{true};
  core::ActiveQueue<graph::dashboard::FHSSMessageInjectionRequest> message_queue_{1, false};
};

} // namespace dsp::fhss
