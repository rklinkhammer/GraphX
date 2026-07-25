/**
 * @file FHSSConfigurationDeriver.hpp
 * @brief Deterministic FHSS configuration derivation engine.
 *
 * @details Computes all 12 generated fields from 18 authoritative source fields
 * with byte-identical output for identical inputs. No randomness, no epsilon-based
 * decisions. Fixed key ordering and stable floating-point operations.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <expected>

namespace dsp::fhss {

/**
 * @struct SourceConfiguration
 * @brief 18 authoritative fields provided by user or configuration service.
 */
struct SourceConfiguration {
  // Message payload
  std::vector<nlohmann::json> messages;
  
  // Frequency parameters
  double iq_center_frequency_hz = 0.0;
  std::vector<double> iq_offsets;
  
  // Idle mode configuration
  enum class IdleMode { Standby, LowPower, Monitoring } idle_mode = IdleMode::Standby;
  int32_t idle_duration_samples = 0;
  
  // Signal characteristics
  double occupied_bandwidth_hz = 0.0;
  double max_abs_cfo_hz = 0.0;
  
  // Impairment modeling
  bool enable_noise = false;
  bool enable_doppler = false;
  bool enable_multipath = false;
  
  // Transmission parameters
  bool allow_overlap = false;
  std::string message_id;
  int32_t transmit_start_sample = 0;
  int32_t frequency_index = 0;
  std::string value;
  
  // Role assignment
  enum class Role { Source, Sink, Processor } role = Role::Processor;
};

/**
 * @struct PreamblePulse
 * @brief Generated preamble pulse configuration.
 */
struct PreamblePulse {
  uint32_t frequency_index = 0;
  uint32_t word_value = 0;
  uint32_t pulse_index = 0;
  
  bool operator==(const PreamblePulse& other) const {
    return frequency_index == other.frequency_index &&
           word_value == other.word_value &&
           pulse_index == other.pulse_index;
  }
};

/**
 * @struct RfCopy
 * @brief Generated RF copy configuration.
 */
struct RfCopy {
  uint32_t channel_id = 0;
  double rf_frequency_hz = 0.0;
  double bandwidth_hz = 0.0;
  
  bool operator==(const RfCopy& other) const {
    return channel_id == other.channel_id &&
           rf_frequency_hz == other.rf_frequency_hz &&
           bandwidth_hz == other.bandwidth_hz;
  }
};

/**
 * @struct ImpairmentCopy
 * @brief Generated impairment copy configuration.
 */
struct ImpairmentCopy {
  std::string impairment_type;
  double parameter_value = 0.0;
  
  bool operator==(const ImpairmentCopy& other) const {
    return impairment_type == other.impairment_type &&
           parameter_value == other.parameter_value;
  }
};

/**
 * @struct MessageAssemblerConfig
 * @brief Generated message assembler configuration.
 */
struct MessageAssemblerConfig {
  uint32_t max_pulses_per_message = 256;
  uint32_t timeout_samples = 10000;
  bool allow_partial_assembly = false;
  
  bool operator==(const MessageAssemblerConfig& other) const {
    return max_pulses_per_message == other.max_pulses_per_message &&
           timeout_samples == other.timeout_samples &&
           allow_partial_assembly == other.allow_partial_assembly;
  }
};

/**
 * @struct EffectiveConfiguration
 * @brief All 18 source fields plus 12 derived fields (30 total).
 */
struct EffectiveConfiguration {
  // Source fields (passthrough)
  SourceConfiguration source;
  
  // Derived fields (12 total)
  std::vector<int32_t> active_frequency_indices_source;
  std::vector<int32_t> active_frequency_indices_preamble;
  std::vector<int32_t> active_frequency_indices_channelizer;
  std::vector<PreamblePulse> preamble_pulses;
  std::vector<RfCopy> rf_copies;
  std::vector<ImpairmentCopy> impairment_copies;
  MessageAssemblerConfig message_assembler_config;
  std::vector<int32_t> pulse_frequency_indices_source;
  std::vector<int32_t> pulse_frequency_indices_preamble;
  std::vector<int32_t> pulse_frequency_indices_channelizer;
  
  // Metadata
  uint64_t revision = 0;
  std::string etag;
};

/**
 * @struct ConfigurationDerivationError
 * @brief Error details from derivation process.
 */
struct ConfigurationDerivationError {
  std::string code;
  std::string message;
  std::string field;
};

/**
 * @class FHSSConfigurationDeriver
 * @brief Deterministically derives EffectiveConfiguration from SourceConfiguration.
 *
 * **Key Properties:**
 * - Byte-identical output for identical inputs (verified by determinism tests)
 * - All floating-point operations stable (no epsilon branching)
 * - JSON serialization uses sorted keys
 * - No random iteration order
 * - Validation deferred to FHSSCrossNodeValidator
 */
class FHSSConfigurationDeriver {
public:
  /**
   * @brief Derive effective configuration from source configuration.
   *
   * @param source Source configuration with 18 authoritative fields
   * @return Expected containing derived configuration or error
   */
  [[nodiscard]] static std::expected<EffectiveConfiguration, ConfigurationDerivationError>
  Derive(const SourceConfiguration& source);

  /**
   * @brief Derive active frequency indices for source nodes.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DeriveActiveFrequencyIndicesSource(const SourceConfiguration& source);

  /**
   * @brief Derive active frequency indices for preamble.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DeriveActiveFrequencyIndicesPreamble(const SourceConfiguration& source);

  /**
   * @brief Derive active frequency indices for channelizer.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DeriveActiveFrequencyIndicesChannelizer(const SourceConfiguration& source);

  /**
   * @brief Derive preamble pulses.
   *
   * @param source Source configuration
   * @return Vector of preamble pulses or error
   */
  [[nodiscard]] static std::expected<std::vector<PreamblePulse>, ConfigurationDerivationError>
  DerivePreamblePulses(const SourceConfiguration& source);

  /**
   * @brief Derive RF copies.
   *
   * @param source Source configuration
   * @return Vector of RF copies or error
   */
  [[nodiscard]] static std::expected<std::vector<RfCopy>, ConfigurationDerivationError>
  DeriveRfCopies(const SourceConfiguration& source);

  /**
   * @brief Derive impairment copies.
   *
   * @param source Source configuration
   * @return Vector of impairment copies or error
   */
  [[nodiscard]] static std::expected<std::vector<ImpairmentCopy>, ConfigurationDerivationError>
  DeriveImpairmentCopies(const SourceConfiguration& source);

  /**
   * @brief Derive message assembler configuration.
   *
   * @param source Source configuration
   * @return Message assembler config or error
   */
  [[nodiscard]] static std::expected<MessageAssemblerConfig, ConfigurationDerivationError>
  DeriveMessageAssemblerConfig(const SourceConfiguration& source);

  /**
   * @brief Derive pulse frequency indices for source.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DerivePulseFrequencyIndicesSource(const SourceConfiguration& source);

  /**
   * @brief Derive pulse frequency indices for preamble.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DerivePulseFrequencyIndicesPreamble(const SourceConfiguration& source);

  /**
   * @brief Derive pulse frequency indices for channelizer.
   *
   * @param source Source configuration
   * @return Vector of frequency indices or error
   */
  [[nodiscard]] static std::expected<std::vector<int32_t>, ConfigurationDerivationError>
  DerivePulseFrequencyIndicesChannelizer(const SourceConfiguration& source);

private:
  /// Helper to create error with consistent formatting
  [[nodiscard]] static ConfigurationDerivationError
  MakeError(const std::string& code, const std::string& message,
            const std::string& field = "");
};

}  // namespace dsp::fhss
