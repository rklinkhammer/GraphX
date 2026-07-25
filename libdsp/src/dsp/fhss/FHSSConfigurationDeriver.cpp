/**
 * @file FHSSConfigurationDeriver.cpp
 * @brief Implementation of deterministic FHSS configuration derivation.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/fhss/FHSSConfigurationDeriver.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dsp::fhss {

// ============================================================================
// Error Handling
// ============================================================================

ConfigurationDerivationError FHSSConfigurationDeriver::MakeError(
    const std::string& code, const std::string& message,
    const std::string& field) {
  return ConfigurationDerivationError{code, message, field};
}

// ============================================================================
// Main Derivation Function
// ============================================================================

std::expected<EffectiveConfiguration, ConfigurationDerivationError>
FHSSConfigurationDeriver::Derive(const SourceConfiguration& source) {
  EffectiveConfiguration effective;
  effective.source = source;
  effective.revision = 1;
  effective.etag = "Rev:1";

  // Derive all 12 fields in order
  auto active_freq_src = DeriveActiveFrequencyIndicesSource(source);
  if (!active_freq_src.has_value()) return std::unexpected(active_freq_src.error());
  effective.active_frequency_indices_source = active_freq_src.value();

  auto active_freq_preamble = DeriveActiveFrequencyIndicesPreamble(source);
  if (!active_freq_preamble.has_value()) return std::unexpected(active_freq_preamble.error());
  effective.active_frequency_indices_preamble = active_freq_preamble.value();

  auto active_freq_chan = DeriveActiveFrequencyIndicesChannelizer(source);
  if (!active_freq_chan.has_value()) return std::unexpected(active_freq_chan.error());
  effective.active_frequency_indices_channelizer = active_freq_chan.value();

  auto preamble = DerivePreamblePulses(source);
  if (!preamble.has_value()) return std::unexpected(preamble.error());
  effective.preamble_pulses = preamble.value();

  auto rf_copies = DeriveRfCopies(source);
  if (!rf_copies.has_value()) return std::unexpected(rf_copies.error());
  effective.rf_copies = rf_copies.value();

  auto impairments = DeriveImpairmentCopies(source);
  if (!impairments.has_value()) return std::unexpected(impairments.error());
  effective.impairment_copies = impairments.value();

  auto msg_config = DeriveMessageAssemblerConfig(source);
  if (!msg_config.has_value()) return std::unexpected(msg_config.error());
  effective.message_assembler_config = msg_config.value();

  auto pulse_freq_src = DerivePulseFrequencyIndicesSource(source);
  if (!pulse_freq_src.has_value()) return std::unexpected(pulse_freq_src.error());
  effective.pulse_frequency_indices_source = pulse_freq_src.value();

  auto pulse_freq_preamble = DerivePulseFrequencyIndicesPreamble(source);
  if (!pulse_freq_preamble.has_value()) return std::unexpected(pulse_freq_preamble.error());
  effective.pulse_frequency_indices_preamble = pulse_freq_preamble.value();

  auto pulse_freq_chan = DerivePulseFrequencyIndicesChannelizer(source);
  if (!pulse_freq_chan.has_value()) return std::unexpected(pulse_freq_chan.error());
  effective.pulse_frequency_indices_channelizer = pulse_freq_chan.value();

  return effective;
}

// ============================================================================
// Derivation Functions (12 fields)
// ============================================================================

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesSource(
    const SourceConfiguration& source) {
  std::vector<int32_t> indices;

  // For each message, extract the frequency_index
  for (const auto& msg : source.messages) {
    if (msg.contains("frequency_index") && msg["frequency_index"].is_number_integer()) {
      int32_t idx = msg["frequency_index"].get<int32_t>();
      if (idx >= 0 && idx <= 63) {  // Valid frequency range
        indices.push_back(idx);
      }
    }
  }

  // Sort to ensure deterministic ordering (required for determinism guarantee)
  std::sort(indices.begin(), indices.end());
  // Remove duplicates while keeping sorted order
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  return indices;
}

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesPreamble(
    const SourceConfiguration& source) {
  // Preamble frequencies are subset of active frequencies with specific pattern
  auto active = DeriveActiveFrequencyIndicesSource(source);
  if (!active.has_value()) return std::unexpected(active.error());

  // For determinism: preamble uses every other active frequency, sorted
  std::vector<int32_t> preamble_indices;
  for (size_t i = 0; i < active->size(); i += 2) {
    preamble_indices.push_back((*active)[i]);
  }

  return preamble_indices;
}

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesChannelizer(
    const SourceConfiguration& source) {
  // Channelizer uses all active frequencies in sorted order
  auto active = DeriveActiveFrequencyIndicesSource(source);
  if (!active.has_value()) return std::unexpected(active.error());

  return active.value();  // Already sorted and deduplicated
}

std::expected<std::vector<PreamblePulse>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DerivePreamblePulses(
    const SourceConfiguration& source) {
  std::vector<PreamblePulse> pulses;

  auto preamble_freqs = DeriveActiveFrequencyIndicesPreamble(source);
  if (!preamble_freqs.has_value()) return std::unexpected(preamble_freqs.error());

  // Create preamble pulses with deterministic word values (based on index)
  uint32_t pulse_index = 0;
  for (int32_t freq_idx : *preamble_freqs) {
    // Word value = deterministic hash of frequency index
    uint32_t word_value = (static_cast<uint32_t>(freq_idx) * 0x12345679u) ^ 0x12345678u;

    PreamblePulse pulse;
    pulse.frequency_index = static_cast<uint32_t>(freq_idx);
    pulse.word_value = word_value;
    pulse.pulse_index = pulse_index++;

    pulses.push_back(pulse);
  }

  return pulses;
}

std::expected<std::vector<RfCopy>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveRfCopies(const SourceConfiguration& source) {
  std::vector<RfCopy> copies;

  // RF copies based on active frequencies and bandwidth
  auto active_freqs = DeriveActiveFrequencyIndicesSource(source);
  if (!active_freqs.has_value()) return std::unexpected(active_freqs.error());

  // Create one RF copy per active frequency
  for (size_t i = 0; i < active_freqs->size(); ++i) {
    RfCopy copy;
    copy.channel_id = static_cast<uint32_t>(i);
    
    // RF frequency based on center and index (simplified computation for determinism)
    double base_freq = 1'000'000'000.0;  // 1 GHz base
    double spacing = 8'000'000.0;  // 8 MHz spacing
    copy.rf_frequency_hz = base_freq + ((*active_freqs)[i] * spacing);
    copy.bandwidth_hz = source.occupied_bandwidth_hz > 0 ? source.occupied_bandwidth_hz : 5'000'000.0;

    copies.push_back(copy);
  }

  return copies;
}

std::expected<std::vector<ImpairmentCopy>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveImpairmentCopies(const SourceConfiguration& source) {
  std::vector<ImpairmentCopy> impairments;

  // Add impairments based on flags (deterministic order: noise, doppler, multipath)
  if (source.enable_noise) {
    ImpairmentCopy noise;
    noise.impairment_type = "AWGN";
    noise.parameter_value = -100.0;  // SNR in dB
    impairments.push_back(noise);
  }

  if (source.enable_doppler) {
    ImpairmentCopy doppler;
    doppler.impairment_type = "Doppler";
    doppler.parameter_value = source.max_abs_cfo_hz / 2.0;  // Hz
    impairments.push_back(doppler);
  }

  if (source.enable_multipath) {
    ImpairmentCopy multipath;
    multipath.impairment_type = "Multipath";
    multipath.parameter_value = 3.0;  // Number of paths
    impairments.push_back(multipath);
  }

  return impairments;
}

std::expected<MessageAssemblerConfig, ConfigurationDerivationError>
FHSSConfigurationDeriver::DeriveMessageAssemblerConfig(
    const SourceConfiguration& source) {
  MessageAssemblerConfig config;

  // Derive assembler configuration from source
  config.max_pulses_per_message = 256;  // Fixed constant
  config.timeout_samples = 10000;  // Fixed constant
  config.allow_partial_assembly = source.allow_overlap;

  return config;
}

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DerivePulseFrequencyIndicesSource(
    const SourceConfiguration& source) {
  // Pulse frequencies for source extraction
  auto active = DeriveActiveFrequencyIndicesSource(source);
  if (!active.has_value()) return std::unexpected(active.error());

  // Source pulses cycle through active frequencies
  std::vector<int32_t> pulse_indices;
  if (!active->empty()) {
    // Repeat active frequencies to create pulse pattern (deterministic)
    for (int i = 0; i < 5 && pulse_indices.size() < 20; ++i) {
      for (int32_t freq : *active) {
        pulse_indices.push_back(freq);
        if (pulse_indices.size() >= 20) break;
      }
    }
  }

  return pulse_indices;
}

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DerivePulseFrequencyIndicesPreamble(
    const SourceConfiguration& source) {
  // Preamble pulses follow specific pattern
  auto preamble_freqs = DeriveActiveFrequencyIndicesPreamble(source);
  if (!preamble_freqs.has_value()) return std::unexpected(preamble_freqs.error());

  // Preamble pattern: repeat each frequency 4 times, then cycle
  std::vector<int32_t> pulse_indices;
  if (!preamble_freqs->empty()) {
    for (int i = 0; i < 4 && pulse_indices.size() < 16; ++i) {
      for (int32_t freq : *preamble_freqs) {
        pulse_indices.push_back(freq);
        if (pulse_indices.size() >= 16) break;
      }
    }
  }

  return pulse_indices;
}

std::expected<std::vector<int32_t>, ConfigurationDerivationError>
FHSSConfigurationDeriver::DerivePulseFrequencyIndicesChannelizer(
    const SourceConfiguration& source) {
  // Channelizer processes all active frequencies deterministically
  auto active = DeriveActiveFrequencyIndicesChannelizer(source);
  if (!active.has_value()) return std::unexpected(active.error());

  // Channelizer indices same as active indices (for parallel processing)
  return active.value();
}

}  // namespace dsp::fhss
