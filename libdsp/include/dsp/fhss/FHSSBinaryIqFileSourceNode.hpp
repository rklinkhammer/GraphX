/**
 * @file FHSSBinaryIqFileSourceNode.hpp
 * @brief Binary complex-IQ file source for FHSS receiver validation graphs.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <algorithm>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

namespace dsp::fhss {

enum class FHSSBinaryIqSampleFormat { Cf32Le, Cf64Le };

struct FHSSBinaryIqFileSourceConfig {
  static constexpr std::uint64_t kDefaultMaxReadComplexSamples = 4'194'304;

  std::filesystem::path file_path;
  FHSSBinaryIqSampleFormat sample_format = FHSSBinaryIqSampleFormat::Cf32Le;
  FHSSTimingConfig timing{};
  std::uint64_t input_packet_global_start_sample = 0;
  std::uint64_t first_complex_sample = 0;
  std::uint64_t max_complex_samples = 0;
  std::uint64_t max_read_complex_samples = kDefaultMaxReadComplexSamples;
};

[[nodiscard]] inline const char *
FHSSBinaryIqSampleFormatName(FHSSBinaryIqSampleFormat format) {
  switch (format) {
  case FHSSBinaryIqSampleFormat::Cf32Le:
    return "cf32_le";
  case FHSSBinaryIqSampleFormat::Cf64Le:
    return "cf64_le";
  }
  return "unknown";
}

[[nodiscard]] inline FHSSBinaryIqSampleFormat
FHSSBinaryIqSampleFormatFromString(const std::string &value) {
  if (value == "cf32_le") {
    return FHSSBinaryIqSampleFormat::Cf32Le;
  }
  if (value == "cf64_le") {
    return FHSSBinaryIqSampleFormat::Cf64Le;
  }
  throw graph::ConfigError(
      "FHSS binary IQ sample_format must be 'cf32_le' or 'cf64_le'");
}

[[nodiscard]] inline FHSSVoidResult ValidateFHSSBinaryIqFileSourceConfig(
    const FHSSBinaryIqFileSourceConfig &config) {
  if (config.file_path.empty()) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "FHSS binary IQ source requires a non-empty file_path"));
  }
  if (config.max_read_complex_samples == 0u) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS binary IQ source max_read_complex_samples must be positive"));
  }
  if (auto timing = DeriveTimingModel(config.timing); !timing) {
    return std::unexpected(timing.error());
  }
  return {};
}

[[nodiscard]] inline FHSSBinaryIqFileSourceConfig
FHSSBinaryIqFileSourceConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSBinaryIqFileSourceConfig config{};
  if (!json.contains("file_path") || !json.at("file_path").is_string()) {
    throw graph::ConfigError(
        "FHSS binary IQ source field 'file_path' must be a string");
  }
  config.file_path = json.at("file_path").get<std::string>();
  config.sample_format = FHSSBinaryIqSampleFormatFromString(
      json.value("sample_format", std::string{"cf32_le"}));
  config.timing.sample_rate_hz =
      json.value("sample_rate_hz", config.timing.sample_rate_hz);
  config.timing.bit_rate_hz =
      json.value("bit_rate_hz", config.timing.bit_rate_hz);
  config.timing.bits_per_pulse =
      json.value("bits_per_pulse", config.timing.bits_per_pulse);
  config.timing.pulse_gap_seconds =
      json.value("pulse_gap_seconds", config.timing.pulse_gap_seconds);
  config.input_packet_global_start_sample =
      json.value("input_packet_global_start_sample",
                 config.input_packet_global_start_sample);
  config.first_complex_sample =
      json.value("first_complex_sample", config.first_complex_sample);
  config.max_complex_samples =
      json.value("max_complex_samples", config.max_complex_samples);
  config.max_read_complex_samples =
      json.value("max_read_complex_samples", config.max_read_complex_samples);
  if (auto validation = ValidateFHSSBinaryIqFileSourceConfig(config);
      !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

template <typename UInt>
[[nodiscard]] inline UInt FHSSReadLittleEndianUnsigned(const std::byte *bytes) {
  static_assert(std::is_unsigned_v<UInt>);
  UInt value = 0;
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    value |= static_cast<UInt>(std::to_integer<unsigned int>(bytes[i]))
             << (8u * i);
  }
  return value;
}

template <typename Float>
[[nodiscard]] inline Float FHSSReadLittleEndianFloat(const std::byte *bytes) {
  using UInt = std::conditional_t<sizeof(Float) == sizeof(std::uint32_t),
                                  std::uint32_t, std::uint64_t>;
  return std::bit_cast<Float>(FHSSReadLittleEndianUnsigned<UInt>(bytes));
}

[[nodiscard]] inline FHSSResult<std::vector<std::complex<double>>>
ReadFHSSBinaryIqStream(std::istream &input, std::uint64_t byte_count,
                       const FHSSBinaryIqFileSourceConfig &config) {
  if (auto validation = ValidateFHSSBinaryIqFileSourceConfig(config);
      !validation) {
    return std::unexpected(validation.error());
  }
  const std::uint64_t bytes_per_scalar =
      config.sample_format == FHSSBinaryIqSampleFormat::Cf32Le ? 4u : 8u;
  const std::uint64_t bytes_per_complex = 2u * bytes_per_scalar;
  if (byte_count % bytes_per_complex != 0u) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS binary IQ file size is not aligned to complete complex samples"));
  }

  const auto total_complex_samples = byte_count / bytes_per_complex;
  if (config.first_complex_sample > total_complex_samples) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS binary IQ first_complex_sample exceeds file sample count"));
  }
  const auto available = total_complex_samples - config.first_complex_sample;
  const auto selected = config.max_complex_samples == 0u
                            ? available
                            : std::min(available, config.max_complex_samples);
  if (selected > config.max_read_complex_samples) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "FHSS binary IQ selection exceeds max_read_complex_samples"));
  }
  if (selected > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "FHSS binary IQ selection exceeds host container capacity"));
  }

  const auto byte_offset = config.first_complex_sample * bytes_per_complex;
  if (byte_offset >
      static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "FHSS binary IQ sample offset exceeds stream capacity"));
  }
  input.seekg(static_cast<std::streamoff>(byte_offset));
  if (!input) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "failed to seek to selected FHSS binary IQ samples"));
  }

  std::vector<std::complex<double>> samples;
  samples.reserve(static_cast<std::size_t>(selected));
  constexpr std::uint64_t kChunkComplexSamples = 4096;
  std::vector<std::byte> raw(
      static_cast<std::size_t>(kChunkComplexSamples * bytes_per_complex));
  std::uint64_t remaining = selected;
  while (remaining != 0u) {
    const auto chunk_samples = std::min(remaining, kChunkComplexSamples);
    const auto chunk_bytes = chunk_samples * bytes_per_complex;
    if (!input.read(reinterpret_cast<char *>(raw.data()),
                    static_cast<std::streamsize>(chunk_bytes))) {
      return std::unexpected(MakeError(FHSSValidationCode::InvalidGlobalTiming,
                                       "failed to read selected FHSS binary IQ "
                                       "samples; file was truncated"));
    }
    for (std::uint64_t index = 0; index < chunk_samples; ++index) {
      const auto *sample = raw.data() + index * bytes_per_complex;
      if (config.sample_format == FHSSBinaryIqSampleFormat::Cf32Le) {
        const auto i = FHSSReadLittleEndianFloat<float>(sample);
        const auto q =
            FHSSReadLittleEndianFloat<float>(sample + bytes_per_scalar);
        samples.emplace_back(static_cast<double>(i), static_cast<double>(q));
      } else {
        const auto i = FHSSReadLittleEndianFloat<double>(sample);
        const auto q =
            FHSSReadLittleEndianFloat<double>(sample + bytes_per_scalar);
        samples.emplace_back(i, q);
      }
    }
    remaining -= chunk_samples;
  }
  return samples;
}

[[nodiscard]] inline FHSSResult<std::vector<std::complex<double>>>
ReadFHSSBinaryIqFile(
    const FHSSBinaryIqFileSourceConfig &config,
    const std::function<void()> &after_open_snapshot_observer = {}) {
  if (auto validation = ValidateFHSSBinaryIqFileSourceConfig(config);
      !validation) {
    return std::unexpected(validation.error());
  }
  std::error_code status_error;
  const auto status = std::filesystem::status(config.file_path, status_error);
  if (status_error || !std::filesystem::is_regular_file(status)) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "FHSS binary IQ path is not a readable regular file: " +
                      config.file_path.string()));
  }
  std::ifstream input(config.file_path, std::ios::binary | std::ios::ate);
  if (!input) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidGlobalTiming,
                                     "failed to open FHSS binary IQ file: " +
                                         config.file_path.string()));
  }
  const auto end = input.tellg();
  if (end < 0) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "failed to determine FHSS binary IQ file size"));
  }
  if (after_open_snapshot_observer)
    after_open_snapshot_observer();
  return ReadFHSSBinaryIqStream(input, static_cast<std::uint64_t>(end), config);
}

class FHSSBinaryIqFileSourceNode
    : public graph::NamedSourceNode<FHSSBinaryIqFileSourceNode,
                                    FHSSSyntheticIqToken>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using OutputTokenType = FHSSSyntheticIqToken;

  FHSSBinaryIqFileSourceNode() = default;
  explicit FHSSBinaryIqFileSourceNode(FHSSBinaryIqFileSourceConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSBinaryIqFileSourceConfig config) {
    config_ = std::move(config);
    emitted_ = false;
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSBinaryIqFileSourceConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params{
        {"file_path", config_.file_path.string()},
        {"sample_format", FHSSBinaryIqSampleFormatName(config_.sample_format)},
        {"sample_rate_hz", config_.timing.sample_rate_hz},
        {"bit_rate_hz", config_.timing.bit_rate_hz},
        {"bits_per_pulse", config_.timing.bits_per_pulse},
        {"pulse_gap_seconds", config_.timing.pulse_gap_seconds},
        {"input_packet_global_start_sample",
         config_.input_packet_global_start_sample},
        {"first_complex_sample", config_.first_complex_sample},
        {"max_complex_samples", config_.max_complex_samples},
        {"max_read_complex_samples", config_.max_read_complex_samples}};
    return FHSSStableParameterJsonView(std::move(params));
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"file_path",
            "sample_format",
            "sample_rate_hz",
            "bit_rate_hz",
            "bits_per_pulse",
            "pulse_gap_seconds",
            "input_packet_global_start_sample",
            "first_complex_sample",
            "max_complex_samples",
            "max_read_complex_samples"};
  }

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 0>) override {
    if (emitted_) {
      return std::nullopt;
    }
    emitted_ = true;
    auto samples = ReadFHSSBinaryIqFile(config_);
    if (!samples) {
      return std::nullopt;
    }
    auto shared = std::make_shared<const std::vector<std::complex<double>>>(
        std::move(*samples));
    FHSSGraphXSampleTimeMap sample_time_map{};
    sample_time_map.input_packet_global_start_sample =
        config_.input_packet_global_start_sample;
    sample_time_map.output_start_sample =
        config_.input_packet_global_start_sample;
    sample_time_map.input_sample_rate_hz = config_.timing.sample_rate_hz;
    sample_time_map.output_sample_rate_hz = config_.timing.sample_rate_hz;

    OutputTokenType token{};
    token.token_id = 1;
    token.edge_control = graph::EdgeEndOfStream{};
    token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
        shared, shared->size(), sample_time_map);
    token.sidecar.timing = DeriveTimingModel(config_.timing).value();
    return token;
  }

private:
  FHSSBinaryIqFileSourceConfig config_{};
  bool emitted_ = false;
};

} // namespace dsp::fhss
