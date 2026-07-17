// SPDX-License-Identifier: MIT

#include "dsp/fhss/FHSSAcquisitionPulseDetectorNode.hpp"
#include "dsp/fhss/FHSSProductionCandidateChannelizerNode.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dsp::fhss::FHSSAcquiredPulseRange;
using dsp::fhss::FHSSAcquisitionPulseDetectorConfig;
using dsp::fhss::FHSSAcquisitionPulseDetectorKernel;
using dsp::fhss::FHSSAcquisitionPulseDetectorNode;
using dsp::fhss::FHSSChannelizedIqToken;
using dsp::fhss::FHSSFirChannelizerKernel;
using dsp::fhss::FHSSGraphXComplexEvidenceFromHostSamples;
using dsp::fhss::FHSSProductionCandidateChannelizerNode;
using dsp::fhss::FHSSProductionChannelizerConfig;
using dsp::fhss::FHSSProtocolConstants;

struct Options {
  std::filesystem::path profile;
  std::size_t trials = 1'000;
  std::string partition = "evaluation";
};

Options ParseOptions(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--profile" && i + 1 < argc) {
      options.profile = argv[++i];
    } else if (argument == "--trials" && i + 1 < argc) {
      options.trials = std::stoull(argv[++i]);
    } else if (argument == "--seed-partition" && i + 1 < argc) {
      options.partition = argv[++i];
    } else {
      throw std::runtime_error("unknown or incomplete argument: " +
                               std::string(argument));
    }
  }
  if (options.profile.empty() || options.trials == 0u ||
      (options.partition != "development" &&
       options.partition != "evaluation")) {
    throw std::runtime_error("usage: --profile PATH --trials N "
                             "--seed-partition development|evaluation");
  }
  return options;
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open profile: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

FHSSProductionChannelizerConfig
ChannelizerConfig(const nlohmann::json &profile) {
  FHSSProductionChannelizerConfig config{};
  const auto &band = profile.at("sampled_band_model");
  const auto &declared = profile.at("channelizer");
  config.frequency.sample_rate_hz = band.at("input_sample_rate_hz");
  config.frequency.occupied_bandwidth_hz = declared.at("occupied_bandwidth_hz");
  config.frequency.max_abs_cfo_hz = 1'000.0;
  config.receiver_frequency_indices = dsp::fhss::FHSSAllFrequencyIndices();
  config.channel_ids = dsp::fhss::FHSSAllFrequencyIndices();
  const double first = band.at("iq_offset_first_hz");
  const double spacing = band.at("channel_spacing_hz");
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    config.frequency.iq_offset_frequency_hz[index] =
        first + spacing * static_cast<double>(index);
  }
  config.decimation_factor = declared.at("decimation_factor");
  config.fir_tap_count = declared.at("fir_tap_count");
  config.passband_edge_hz = declared.at("passband_edge_hz");
  config.cutoff_frequency_hz = declared.at("cutoff_frequency_hz");
  config.guarded_nyquist_margin_hz = band.at("guarded_nyquist_margin_hz");
  config.max_input_samples =
      declared.at("candidate_max_input_samples_per_token");
  return config;
}

FHSSAcquisitionPulseDetectorConfig
DetectorConfig(const nlohmann::json &profile) {
  FHSSAcquisitionPulseDetectorConfig config{};
  const auto &declared = profile.at("detector");
  config.noise_power_quantile = declared.at("noise_power_quantile");
  config.threshold_above_noise_linear =
      declared.at("threshold_above_noise_linear");
  config.release_threshold_ratio = declared.at("release_threshold_ratio");
  config.min_absolute_power_linear =
      declared.at("candidate_min_absolute_power_linear");
  config.min_symbol_coherence = declared.at("minimum_symbol_coherence");
  config.smoothing_window_channel_samples =
      declared.at("smoothing_window_channel_samples");
  config.min_pulse_input_samples = declared.at("min_pulse_input_samples");
  config.max_pulse_input_samples = declared.at("max_pulse_input_samples");
  config.bridge_gap_input_samples = declared.at("bridge_gap_input_samples");
  config.duplicate_tolerance_input_samples =
      declared.at("duplicate_tolerance_input_samples");
  config.max_buffered_channel_samples =
      declared.at("candidate_max_buffered_channel_samples");
  config.nominal_bandwidth_hz = declared.at("nominal_bandwidth_hz");
  return config;
}

std::vector<std::complex<double>> Tone(std::size_t count, double frequency,
                                       double sample_rate,
                                       double amplitude = 1.0,
                                       double phase_offset = 0.0) {
  std::vector<std::complex<double>> samples;
  samples.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const double phase = phase_offset + 2.0 * std::numbers::pi * frequency *
                                            static_cast<double>(index) /
                                            sample_rate;
    samples.push_back(std::polar(amplitude, phase));
  }
  return samples;
}

double Rms(const std::vector<std::complex<double>> &samples) {
  if (samples.empty()) {
    return 0.0;
  }
  const double power = std::accumulate(
      samples.begin(), samples.end(), 0.0,
      [](double sum, const auto &sample) { return sum + std::norm(sample); });
  return std::sqrt(power / static_cast<double>(samples.size()));
}

double Db20(double value) {
  return 20.0 * std::log10(std::max(value, 1.0e-300));
}

double MeasureTone(const std::vector<double> &taps, std::uint32_t decimation,
                   double frequency, double sample_rate,
                   std::size_t count = 4'096) {
  FHSSFirChannelizerKernel kernel(taps, decimation, 0.0, sample_rate);
  const auto output = kernel.Process(Tone(count, frequency, sample_rate), 0u);
  if (!output) {
    throw std::runtime_error("actual channelizer rejected tone vector");
  }
  return Rms(output->samples);
}

std::vector<std::complex<double>>
IndependentFirDecimate(const std::vector<std::complex<double>> &input,
                       const std::vector<double> &taps,
                       std::uint32_t decimation) {
  std::vector<std::complex<double>> output;
  for (std::size_t sample = taps.size() - 1u; sample < input.size(); ++sample) {
    if (sample % decimation != 0u) {
      continue;
    }
    std::complex<double> value{0.0, 0.0};
    for (std::size_t tap = 0; tap < taps.size(); ++tap) {
      value += taps[tap] * input[sample - tap];
    }
    output.push_back(value);
  }
  return output;
}

struct VectorMeasurement {
  double input_rms = 0.0;
  double candidate_output_rms = 0.0;
  double oracle_output_rms = 0.0;
  double maximum_oracle_error = 0.0;
  std::size_t allocation_high_water_bytes = 0u;
};

VectorMeasurement MeasureVector(const std::vector<double> &taps,
                                std::uint32_t decimation,
                                const std::vector<std::complex<double>> &input,
                                double sample_rate) {
  FHSSFirChannelizerKernel kernel(taps, decimation, 0.0, sample_rate);
  const auto actual = kernel.Process(input, 0u);
  if (!actual) {
    throw std::runtime_error("actual channelizer rejected alias vector");
  }
  const auto oracle = IndependentFirDecimate(input, taps, decimation);
  if (actual->samples.size() != oracle.size()) {
    throw std::runtime_error("channelizer/oracle sample-count mismatch");
  }
  double maximum_error = 0.0;
  for (std::size_t index = 0; index < oracle.size(); ++index) {
    maximum_error = std::max(maximum_error,
                             std::abs(actual->samples[index] - oracle[index]));
  }
  return {.input_rms = Rms(input),
          .candidate_output_rms = Rms(actual->samples),
          .oracle_output_rms = Rms(oracle),
          .maximum_oracle_error = maximum_error,
          .allocation_high_water_bytes = kernel.AllocationHighWaterBytes()};
}

std::size_t MeasureChannelizerNodeAllocationHighWater(
    const FHSSProductionChannelizerConfig &config) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      Tone(4'096, 0.0, config.frequency.sample_rate_hz));
  dsp::fhss::FHSSDownconvertedIqToken input{};
  input.sidecar.downconverter.passthrough = true;
  input.sidecar.iq.sample_time_map.input_packet_global_start_sample = 0u;
  input.sidecar.iq.sample_time_map.input_sample_rate_hz =
      config.frequency.sample_rate_hz;
  input.sidecar.iq.sample_time_map.output_sample_rate_hz =
      config.frequency.sample_rate_hz;
  input.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
      samples, samples->size(), input.sidecar.iq.sample_time_map);
  FHSSProductionCandidateChannelizerNode node(config);
  if (!node.ConsumeInput<0>(input)) {
    throw std::runtime_error(
        "production channelizer node rejected memory vector");
  }
  return node.AllocationHighWaterBytes();
}

nlohmann::json CharacterizeChannelizer(const nlohmann::json &profile) {
  const auto config = ChannelizerConfig(profile);
  if (!dsp::fhss::ValidateFHSSProductionChannelizerConfig(config)) {
    throw std::runtime_error(
        "profile does not configure the production candidate");
  }
  const auto taps = dsp::fhss::DesignFHSSHammingLowpass(
      config.fir_tap_count, config.cutoff_frequency_hz,
      config.frequency.sample_rate_hz);
  const auto started = std::chrono::steady_clock::now();
  const double center = MeasureTone(taps, config.decimation_factor, 0.0,
                                    config.frequency.sample_rate_hz);

  nlohmann::json passband = nlohmann::json::array();
  double min_gain = std::numeric_limits<double>::infinity();
  double max_gain = 0.0;
  for (int step = -40; step <= 40; ++step) {
    const double frequency = config.passband_edge_hz * step / 40.0;
    const double gain = MeasureTone(taps, config.decimation_factor, frequency,
                                    config.frequency.sample_rate_hz);
    min_gain = std::min(min_gain, gain);
    max_gain = std::max(max_gain, gain);
    passband.push_back({{"frequency_hz", frequency},
                        {"measured_gain_db", Db20(gain / center)}});
  }

  const double spacing =
      profile.at("sampled_band_model").at("channel_spacing_hz");
  std::array<double, 127> relative_gain{};
  for (int difference = -63; difference <= 63; ++difference) {
    relative_gain[static_cast<std::size_t>(difference + 63)] =
        MeasureTone(taps, config.decimation_factor, spacing * difference,
                    config.frequency.sample_rate_hz) /
        center;
  }
  nlohmann::json leakage = nlohmann::json::array();
  for (int source = 0; source < 64; ++source) {
    nlohmann::json row = nlohmann::json::array();
    for (int destination = 0; destination < 64; ++destination) {
      const int difference = source - destination;
      row.push_back(
          Db20(relative_gain[static_cast<std::size_t>(difference + 63)]));
    }
    leakage.push_back(std::move(row));
  }

  const double stop_start =
      profile.at("channelizer").at("transition_band_end_hz");
  const double output_nyquist =
      config.frequency.sample_rate_hz / (2.0 * config.decimation_factor);
  double worst_stopband = 0.0;
  nlohmann::json stopband = nlohmann::json::array();
  for (int step = 0; step <= 80; ++step) {
    const double frequency =
        stop_start + (output_nyquist - stop_start) * step / 80.0;
    const double before =
        MeasureTone(taps, 1u, frequency, config.frequency.sample_rate_hz) /
        MeasureTone(taps, 1u, 0.0, config.frequency.sample_rate_hz);
    const double after = MeasureTone(taps, config.decimation_factor, frequency,
                                     config.frequency.sample_rate_hz) /
                         center;
    worst_stopband = std::max(worst_stopband, after);
    stopband.push_back({{"frequency_hz", frequency},
                        {"measured_before_decimation_db", Db20(before)},
                        {"measured_after_decimation_db", Db20(after)}});
  }

  const double stopband_limit = profile.at("channelizer")
                                    .at("requirements")
                                    .at("minimum_stopband_attenuation_db");
  nlohmann::json transition_sweep = nlohmann::json::array();
  double transition_stop_frequency =
      profile.at("channelizer").at("transition_band_end_hz");
  bool transition_found = false;
  for (int step = 0; step <= 240; ++step) {
    const double frequency = 12'000'000.0 * step / 240.0;
    const double gain = MeasureTone(taps, config.decimation_factor, frequency,
                                    config.frequency.sample_rate_hz) /
                        center;
    const double gain_db = Db20(gain);
    transition_sweep.push_back(
        {{"frequency_hz", frequency}, {"measured_gain_db", gain_db}});
    if (!transition_found && frequency >= config.passband_edge_hz &&
        gain_db <= -stopband_limit) {
      transition_stop_frequency = frequency;
      transition_found = true;
    }
  }
  const double measured_transition_width =
      transition_stop_frequency - config.passband_edge_hz;

  const std::array<std::pair<double, double>, 6> folding_bands{
      {{12'000'000.0, 25'000'000.0},
       {25'000'000.0, 75'000'000.0},
       {75'000'000.0, 125'000'000.0},
       {125'000'000.0, 175'000'000.0},
       {175'000'000.0, 225'000'000.0},
       {225'000'000.0, 250'000'000.0}}};
  nlohmann::json alias_bands = nlohmann::json::array();
  double worst_alias_power = 0.0;
  double integrated_alias_output_power = 0.0;
  double integrated_alias_input_power = 0.0;
  double maximum_alias_oracle_error = 0.0;
  std::size_t kernel_allocation_high_water = 0u;
  for (std::size_t band = 0; band < folding_bands.size(); ++band) {
    const auto [low, high] = folding_bands[band];
    nlohmann::json tones = nlohmann::json::array();
    double band_worst = 0.0;
    double band_output_power = 0.0;
    double band_input_power = 0.0;
    std::size_t tone_count = 0u;
    for (int sign : {-1, 1}) {
      for (int step = 0; step <= 8; ++step) {
        const double frequency = sign * (low + (high - low) * step / 8.0);
        const auto measurement = MeasureVector(
            taps, config.decimation_factor,
            Tone(4'096, frequency, config.frequency.sample_rate_hz),
            config.frequency.sample_rate_hz);
        const double input_power =
            measurement.input_rms * measurement.input_rms;
        const double output_power =
            measurement.candidate_output_rms * measurement.candidate_output_rms;
        const double power_dbc =
            10.0 * std::log10(std::max(output_power / input_power, 1.0e-300));
        band_worst = std::max(band_worst, output_power / input_power);
        worst_alias_power =
            std::max(worst_alias_power, output_power / input_power);
        band_output_power += output_power;
        band_input_power += input_power;
        integrated_alias_output_power += output_power;
        integrated_alias_input_power += input_power;
        maximum_alias_oracle_error = std::max(maximum_alias_oracle_error,
                                              measurement.maximum_oracle_error);
        kernel_allocation_high_water =
            std::max(kernel_allocation_high_water,
                     measurement.allocation_high_water_bytes);
        ++tone_count;
        tones.push_back(
            {{"input_frequency_hz", frequency},
             {"input_power_linear", input_power},
             {"candidate_post_filter_decimated_power_linear", output_power},
             {"candidate_alias_power_dbc", power_dbc},
             {"oracle_post_filter_decimated_power_linear",
              measurement.oracle_output_rms * measurement.oracle_output_rms},
             {"maximum_candidate_oracle_complex_error",
              measurement.maximum_oracle_error}});
      }
    }
    alias_bands.push_back(
        {{"folding_band", band},
         {"absolute_frequency_low_hz", low},
         {"absolute_frequency_high_hz", high},
         {"tone_count", tone_count},
         {"worst_alias_power_dbc", 10.0 * std::log10(band_worst)},
         {"integrated_alias_power_dbc",
          10.0 * std::log10(band_output_power / band_input_power)},
         {"tones", std::move(tones)}});
  }

  const auto multitone_for = [&](std::size_t tones_per_band) {
    std::vector<std::complex<double>> input(8'192, {0.0, 0.0});
    const double amplitude =
        1.0 /
        std::sqrt(static_cast<double>(tones_per_band * folding_bands.size()));
    std::size_t tone_index = 0u;
    for (const auto &[low, high] : folding_bands) {
      for (std::size_t tone = 0; tone < tones_per_band; ++tone) {
        const double fraction =
            (static_cast<double>(tone) + 0.5) / tones_per_band;
        const double frequency = low + (high - low) * fraction;
        const auto component =
            Tone(input.size(), frequency, config.frequency.sample_rate_hz,
                 amplitude, 0.37 * static_cast<double>(tone_index++));
        for (std::size_t sample = 0; sample < input.size(); ++sample) {
          input[sample] += component[sample];
        }
      }
    }
    return MeasureVector(taps, config.decimation_factor, input,
                         config.frequency.sample_rate_hz);
  };
  const auto folding_multitone = multitone_for(1u);
  const auto stopband_broadband = multitone_for(16u);

  std::vector<std::complex<double>> impulse(1'000, {0.0, 0.0});
  constexpr std::size_t kImpulseIndex = 500;
  impulse[kImpulseIndex] = {1.0, 0.0};
  FHSSFirChannelizerKernel impulse_kernel(taps, 1u, 0.0,
                                          config.frequency.sample_rate_hz);
  const auto impulse_output = impulse_kernel.Process(impulse, 0u);
  if (!impulse_output || impulse_output->samples.empty()) {
    throw std::runtime_error("actual channelizer rejected impulse vector");
  }
  const auto peak = std::max_element(impulse_output->samples.begin(),
                                     impulse_output->samples.end(),
                                     [](const auto &lhs, const auto &rhs) {
                                       return std::norm(lhs) < std::norm(rhs);
                                     });
  const auto peak_global = impulse_output->first_causal_input_global_sample +
                           static_cast<std::uint64_t>(std::distance(
                               impulse_output->samples.begin(), peak));
  const double measured_delay =
      static_cast<double>(peak_global - kImpulseIndex);
  const auto impulse_sum = std::accumulate(impulse_output->samples.begin(),
                                           impulse_output->samples.end(),
                                           std::complex<double>{0.0, 0.0});
  const double impulse_energy = std::accumulate(
      impulse_output->samples.begin(), impulse_output->samples.end(), 0.0,
      [](double sum, const auto &sample) { return sum + std::norm(sample); });
  const double measured_enbw =
      config.frequency.sample_rate_hz * impulse_energy / std::norm(impulse_sum);

  auto packet_input =
      Tone(5'003, 1'250'000.0, config.frequency.sample_rate_hz, 0.7);
  FHSSFirChannelizerKernel one_shot(taps, config.decimation_factor, 0.0,
                                    config.frequency.sample_rate_hz);
  const auto one = one_shot.Process(packet_input, 0u);
  double packet_error = 0.0;
  for (const std::size_t split : {1u, 59u, 1'003u, 2'501u, 5'002u}) {
    FHSSFirChannelizerKernel packetized(taps, config.decimation_factor, 0.0,
                                        config.frequency.sample_rate_hz);
    const std::vector<std::complex<double>> first(packet_input.begin(),
                                                  packet_input.begin() + split);
    const std::vector<std::complex<double>> second(packet_input.begin() + split,
                                                   packet_input.end());
    const auto lhs = packetized.Process(first, 0u);
    const auto rhs = packetized.Process(second, split);
    if (!lhs || !rhs || !one) {
      throw std::runtime_error("actual packetized channelizer failed");
    }
    auto actual = lhs->samples;
    actual.insert(actual.end(), rhs->samples.begin(), rhs->samples.end());
    if (actual.size() != one->samples.size()) {
      throw std::runtime_error(
          "actual packetized channelizer changed sample count");
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
      packet_error =
          std::max(packet_error, std::abs(actual[index] - one->samples[index]));
    }
  }

  nlohmann::json near_far = nlohmann::json::array();
  double maximum_supported = 0.0;
  const double minimum_output_sir =
      profile.at("channelizer")
          .at("requirements")
          .at("minimum_post_filter_wanted_to_interference_db");
  for (const double ratio_db : {0.0, 10.0, 20.0, 30.0}) {
    auto wanted = Tone(4'096, 0.0, config.frequency.sample_rate_hz);
    const auto blocker = Tone(4'096, spacing, config.frequency.sample_rate_hz,
                              std::pow(10.0, ratio_db / 20.0));
    for (std::size_t index = 0; index < wanted.size(); ++index) {
      wanted[index] += blocker[index];
    }
    FHSSFirChannelizerKernel kernel(taps, config.decimation_factor, 0.0,
                                    config.frequency.sample_rate_hz);
    const auto output = kernel.Process(wanted, 0u);
    if (!output) {
      throw std::runtime_error("actual near/far channelizer failed");
    }
    const double residual = std::sqrt(
        std::accumulate(output->samples.begin(), output->samples.end(), 0.0,
                        [](double sum, const auto &sample) {
                          return sum + std::norm(sample - std::complex<double>{
                                                              1.0, 0.0});
                        }) /
        static_cast<double>(output->samples.size()));
    const double output_sir = -Db20(residual);
    if (output_sir >= minimum_output_sir) {
      maximum_supported = ratio_db;
    }
    near_far.push_back(
        {{"input_near_far_db", ratio_db},
         {"measured_output_wanted_to_interference_db", output_sir}});
  }

  const auto elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  return {{"measurement_source", "actual FHSSFirChannelizerKernel output"},
          {"dc_gain_db", Db20(center)},
          {"passband_sweep", std::move(passband)},
          {"passband_ripple_db", Db20(max_gain / min_gain)},
          {"stopband_sweep", std::move(stopband)},
          {"stopband_attenuation_db", -Db20(worst_stopband)},
          {"transition_sweep", std::move(transition_sweep)},
          {"measured_transition_width_hz", measured_transition_width},
          {"measured_equivalent_noise_bandwidth_hz", measured_enbw},
          {"adjacent_channel_rejection_db", -Db20(relative_gain[64])},
          {"alternate_channel_rejection_db", -Db20(relative_gain[65])},
          {"alias_power_dbc", 10.0 * std::log10(worst_alias_power)},
          {"integrated_alias_power_dbc",
           10.0 * std::log10(integrated_alias_output_power /
                             integrated_alias_input_power)},
          {"alias_folding_bands", std::move(alias_bands)},
          {"folding_band_multitone_alias_power_dbc",
           Db20(folding_multitone.candidate_output_rms /
                folding_multitone.input_rms)},
          {"stopband_broadband_alias_power_dbc",
           Db20(stopband_broadband.candidate_output_rms /
                stopband_broadband.input_rms)},
          {"maximum_alias_candidate_oracle_complex_error",
           maximum_alias_oracle_error},
          {"measured_group_delay_input_samples", measured_delay},
          {"packetized_one_shot_max_error", packet_error},
          {"leakage_matrix_db", std::move(leakage)},
          {"near_far_sweep", std::move(near_far)},
          {"maximum_supported_near_far_ratio_db", maximum_supported},
          {"warmup_input_samples", config.fir_tap_count - 1u},
          {"zero_padded_flush_samples", 0},
          {"kernel_runtime_allocation_high_water_bytes",
           kernel_allocation_high_water},
          {"production_node_runtime_allocation_high_water_bytes",
           MeasureChannelizerNodeAllocationHighWater(config)},
          {"runtime_seconds", elapsed}};
}

struct TruthEvent {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  double snr_db = 0.0;
  std::uint32_t frequency_index = 0;
};

struct DetectorVector {
  std::vector<std::complex<double>> samples;
  TruthEvent truth;
};

DetectorVector MakeDetectorVector(std::uint64_t seed, double snr_db,
                                  std::uint32_t frequency_index,
                                  bool include_pulse) {
  constexpr std::uint64_t kCount = 1'800;
  constexpr std::uint32_t kDecimation = 10;
  constexpr std::uint64_t kWidth =
      FHSSProtocolConstants::kPulseWidthSamples / kDecimation;
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::uint64_t> start_distribution(300, 1'180);
  std::uniform_int_distribution<std::uint32_t> word_distribution;
  std::uniform_real_distribution<double> phase_distribution(-std::numbers::pi,
                                                            std::numbers::pi);
  std::uniform_real_distribution<double> cfo_distribution(-25'000.0, 25'000.0);
  const double amplitude = 0.1 + 1.9 * std::generate_canonical<double, 53>(rng);
  const double noise_power =
      amplitude * amplitude / std::pow(10.0, snr_db / 10.0);
  std::normal_distribution<double> noise(0.0, std::sqrt(noise_power / 2.0));
  DetectorVector vector;
  vector.samples.resize(kCount);
  for (auto &sample : vector.samples) {
    sample = {noise(rng), noise(rng)};
  }
  vector.truth = {.begin = start_distribution(rng),
                  .end = 0,
                  .snr_db = snr_db,
                  .frequency_index = frequency_index};
  vector.truth.end = vector.truth.begin + kWidth;
  if (!include_pulse) {
    vector.truth.begin = vector.truth.end = 0;
    return vector;
  }
  const auto word = word_distribution(rng);
  const double cfo = cfo_distribution(rng);
  double phase = phase_distribution(rng);
  constexpr std::uint32_t kSamplesPerSymbol =
      FHSSProtocolConstants::kSamplesPerSymbol / kDecimation;
  for (std::uint64_t offset = 0; offset < kWidth; ++offset) {
    const auto bit = static_cast<std::uint32_t>(offset / kSamplesPerSymbol);
    const double symbol = ((word >> (31u - bit)) & 1u) == 0u ? 1.0 : -1.0;
    phase += symbol * std::numbers::pi /
             (2.0 * static_cast<double>(kSamplesPerSymbol));
    phase += 2.0 * std::numbers::pi * cfo / 50'000'000.0;
    vector.samples[vector.truth.begin + offset] += std::polar(amplitude, phase);
  }
  return vector;
}

bool Matches(const FHSSAcquiredPulseRange &detection, const TruthEvent &truth) {
  return detection.begin < truth.end && detection.end > truth.begin;
}

FHSSChannelizedIqToken ChannelToken(std::vector<std::complex<double>> samples,
                                    std::uint32_t frequency_index,
                                    std::uint64_t input_global_start,
                                    graph::EdgeControl control = {}) {
  FHSSChannelizedIqToken token{};
  token.token_id = input_global_start + 1u;
  token.edge_control = std::move(control);
  auto &channel = token.sidecar.channel;
  channel.channel_id = frequency_index;
  channel.frequency_index = frequency_index;
  channel.rf_frequency_hz = dsp::fhss::RfFrequencyHz(frequency_index);
  channel.iq_offset_frequency_hz =
      -236'250'000.0 + 7'500'000.0 * frequency_index;
  channel.channel_sample_rate_hz = 50'000'000.0;
  channel.decimation_factor = 10u;
  channel.input_global_start_sample = input_global_start;
  channel.channel_global_start_sample = input_global_start;
  channel.sample_time_map.input_packet_global_start_sample = input_global_start;
  channel.sample_time_map.output_start_sample = 0u;
  channel.sample_time_map.decimation_factor = 10u;
  channel.sample_time_map.group_delay_input_samples = 0;
  channel.sample_time_map.input_sample_rate_hz = 500'000'000.0;
  channel.sample_time_map.output_sample_rate_hz = 50'000'000.0;
  auto shared = std::make_shared<const std::vector<std::complex<double>>>(
      std::move(samples));
  token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
      shared, shared->size(), channel.sample_time_map);
  return token;
}

std::optional<dsp::fhss::FHSSPerChannelPulseEvidenceToken>
RunNode(FHSSAcquisitionPulseDetectorNode &node,
        const FHSSChannelizedIqToken &token) {
  return node.Transfer(token, std::integral_constant<std::size_t, 0>{},
                       std::integral_constant<std::size_t, 0>{});
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      percentile * static_cast<double>(values.size() - 1u));
  return values[index];
}

nlohmann::json CharacterizeDetector(const nlohmann::json &profile,
                                    const Options &options) {
  const auto config = DetectorConfig(profile);
  if (!dsp::fhss::ValidateFHSSAcquisitionPulseDetectorConfig(config)) {
    throw std::runtime_error(
        "profile does not configure the detector candidate");
  }
  const auto seeds = profile.at("statistics")
                         .at(options.partition + "_seeds")
                         .get<std::vector<std::uint64_t>>();
  const auto snr_points =
      profile.at("detector").at("evaluation_snr_db").get<std::vector<double>>();
  nlohmann::json roc = nlohmann::json::array();
  std::uint64_t total_false = 0;
  std::uint64_t total_searched = 0;
  std::uint64_t total_missed = 0;
  std::vector<double> all_timing_errors;
  std::vector<double> matched_confidences;
  std::vector<double> false_confidences;
  double runtime_seconds = 0.0;
  std::size_t kernel_allocation_high_water = 0u;
  std::size_t node_allocation_high_water = 0u;
  for (const double snr_db : snr_points) {
    std::uint64_t matched = 0;
    std::uint64_t false_detections = 0;
    std::vector<double> timing_errors;
    std::vector<double> snr_bias;
    for (std::size_t trial = 0; trial < options.trials; ++trial) {
      const std::uint64_t seed = seeds[trial % seeds.size()] +
                                 104'729u * trial +
                                 static_cast<std::uint64_t>(snr_db * 100.0);
      const auto frequency = static_cast<std::uint32_t>((seed + trial) % 64u);
      for (const bool include_pulse : {true, false}) {
        const auto vector = MakeDetectorVector(
            seed + (include_pulse ? 0u : 1u), snr_db, frequency, include_pulse);
        FHSSAcquisitionPulseDetectorKernel kernel(config);
        const auto started = std::chrono::steady_clock::now();
        const auto detections = kernel.Detect(vector.samples, 10u);
        runtime_seconds += std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
        if (!detections) {
          throw std::runtime_error(
              "actual detector rejected valid characterization vector");
        }
        kernel_allocation_high_water = std::max(
            kernel_allocation_high_water, kernel.AllocationHighWaterBytes());
        total_searched += vector.samples.size();
        bool truth_matched = false;
        for (const auto &detection : *detections) {
          if (include_pulse && !truth_matched &&
              Matches(detection, vector.truth)) {
            truth_matched = true;
            ++matched;
            const double timing_error =
                std::abs(static_cast<double>(detection.begin) -
                         static_cast<double>(vector.truth.begin)) *
                10.0;
            timing_errors.push_back(timing_error);
            all_timing_errors.push_back(timing_error);
            matched_confidences.push_back(detection.confidence);
            const double signal_power =
                std::max(0.0, detection.mean_power_linear -
                                  detection.noise_power_linear);
            const double measured_snr =
                10.0 *
                std::log10(std::max(signal_power / detection.noise_power_linear,
                                    1.0e-300));
            snr_bias.push_back(measured_snr - vector.truth.snr_db);
          } else {
            ++false_detections;
            false_confidences.push_back(detection.confidence);
          }
        }
        if (include_pulse && !truth_matched) {
          ++total_missed;
        }
      }
    }
    total_false += false_detections;
    roc.push_back(
        {{"snr_db", snr_db},
         {"truth_events", options.trials},
         {"matched_events", matched},
         {"missed_events", options.trials - matched},
         {"false_detections", false_detections},
         {"timing_error_input_samples_p50", Percentile(timing_errors, 0.50)},
         {"timing_error_input_samples_p95", Percentile(timing_errors, 0.95)},
         {"snr_bias_db_mean",
          snr_bias.empty()
              ? 0.0
              : std::accumulate(snr_bias.begin(), snr_bias.end(), 0.0) /
                    static_cast<double>(snr_bias.size())}});
  }

  std::uint64_t duplicate_excess = 0;
  std::uint64_t interferer_trials = 0;
  for (std::size_t trial = 0; trial < 100u; ++trial) {
    auto vector = MakeDetectorVector(seeds[trial % seeds.size()] + trial, 20.0,
                                     24u, true);
    const auto second = MakeDetectorVector(
        seeds[trial % seeds.size()] + trial + 9'001u, 20.0, 24u, true);
    const auto shift = static_cast<std::int64_t>(vector.truth.begin) -
                       static_cast<std::int64_t>(second.truth.begin) + 20;
    for (std::size_t index = 0; index < second.samples.size(); ++index) {
      const auto target = static_cast<std::int64_t>(index) + shift;
      if (target >= 0 &&
          target < static_cast<std::int64_t>(vector.samples.size())) {
        vector.samples[static_cast<std::size_t>(target)] +=
            second.samples[index];
      }
    }
    FHSSAcquisitionPulseDetectorKernel kernel(config);
    const auto detections = kernel.Detect(vector.samples, 10u);
    if (!detections) {
      throw std::runtime_error(
          "actual detector rejected same-channel interferer vector");
    }
    duplicate_excess += detections->size() > 1u ? detections->size() - 1u : 0u;
    ++interferer_trials;
  }

  nlohmann::json confusion = nlohmann::json::array();
  for (std::uint32_t row = 0; row < 64u; ++row) {
    auto node_config = config;
    node_config.detector_id = row;
    FHSSAcquisitionPulseDetectorNode node(node_config);
    const auto vector =
        MakeDetectorVector(seeds[row % seeds.size()] + row, 30.0, row, true);
    const auto output =
        RunNode(node, ChannelToken(vector.samples, row, 100'000u,
                                   graph::EdgeEndOfStream{}));
    if (!output || output->sidecar.detected_pulses.size() != 1u) {
      throw std::runtime_error(
          "actual detector node failed frequency identity vector");
    }
    node_allocation_high_water =
        std::max(node_allocation_high_water, node.AllocationHighWaterBytes());
    nlohmann::json counts = std::vector<std::uint64_t>(64u, 0u);
    const auto observed =
        output->sidecar.detected_pulses.front().frequency.frequency_index;
    if (observed >= 64u) {
      throw std::runtime_error("detector node emitted invalid frequency");
    }
    counts[observed] = 1u;
    confusion.push_back(std::move(counts));
  }

  const auto contract_vector =
      MakeDetectorVector(seeds.front() + 84'211u, 30.0, 24u, true);
  const auto midpoint = contract_vector.samples.size() / 2u;
  const std::vector<std::complex<double>> first_half(
      contract_vector.samples.begin(),
      contract_vector.samples.begin() + midpoint);
  const std::vector<std::complex<double>> second_half(
      contract_vector.samples.begin() + midpoint,
      contract_vector.samples.end());
  const auto full_eos =
      ChannelToken(contract_vector.samples, 24u, 0u, graph::EdgeEndOfStream{});
  const auto check_terminal_reset = [&](graph::EdgeControl terminal) {
    FHSSAcquisitionPulseDetectorNode node(config);
    const auto first = RunNode(node, ChannelToken(first_half, 24u, 0u));
    const auto terminated =
        RunNode(node, ChannelToken(second_half, 24u, midpoint * 10u, terminal));
    const auto restarted = RunNode(node, full_eos);
    return first.has_value() && terminated.has_value() &&
           terminated->sidecar.detected_pulses.empty() &&
           restarted.has_value() &&
           restarted->sidecar.detected_pulses.size() == 1u;
  };
  FHSSAcquisitionPulseDetectorNode eos_node(config);
  const auto eos_output = RunNode(eos_node, full_eos);
  node_allocation_high_water =
      std::max(node_allocation_high_water, eos_node.AllocationHighWaterBytes());
  const bool eos_flushes = eos_output.has_value() &&
                           eos_output->sidecar.detected_pulses.size() == 1u;
  FHSSAcquisitionPulseDetectorNode malformed_node(config);
  const auto buffered =
      RunNode(malformed_node, ChannelToken(first_half, 24u, 0u));
  auto malformed =
      ChannelToken(second_half, 24u, midpoint * 10u, graph::EdgeEndOfStream{});
  malformed.sidecar.channel.rf_frequency_hz += 1.0;
  const auto rejected = RunNode(malformed_node, malformed);
  const auto after_rejection = RunNode(malformed_node, full_eos);
  const bool malformed_rejects_and_resets =
      buffered.has_value() && !rejected.has_value() &&
      after_rejection.has_value() &&
      after_rejection->sidecar.detected_pulses.size() == 1u;
  const nlohmann::json terminal_contract{
      {"eos_flushes", eos_flushes},
      {"cancellation_resets",
       check_terminal_reset(graph::EdgeCancellation{"characterization"})},
      {"failure_resets",
       check_terminal_reset(graph::EdgeFailure{"characterization"})},
      {"malformed_input_rejects_and_resets", malformed_rejects_and_resets}};
  nlohmann::json calibration = nlohmann::json::array();
  for (int bin = 0; bin < 4; ++bin) {
    const double low = bin / 4.0;
    const double high = (bin + 1) / 4.0;
    const auto in_bin = [low, high, bin](double value) {
      return value >= low && (bin == 3 ? value <= high : value < high);
    };
    const auto matched = std::count_if(matched_confidences.begin(),
                                       matched_confidences.end(), in_bin);
    const auto false_count = std::count_if(false_confidences.begin(),
                                           false_confidences.end(), in_bin);
    const auto total = matched + false_count;
    calibration.push_back(
        {{"confidence_low", low},
         {"confidence_high", high},
         {"detections", total},
         {"empirical_precision", total == 0 ? 0.0
                                            : static_cast<double>(matched) /
                                                  static_cast<double>(total)}});
  }
  return {
      {"measurement_source", "actual FHSSAcquisitionPulseDetectorKernel output "
                             "with node metadata identity checks"},
      {"event_matching", "one-to-one temporal overlap; unmatched candidate is "
                         "false; unmatched truth is missed"},
      {"roc", std::move(roc)},
      {"truth_events", options.trials * snr_points.size()},
      {"missed_events", total_missed},
      {"false_detections", total_false},
      {"searched_channel_samples", total_searched},
      {"searched_seconds", static_cast<double>(total_searched) / 50'000'000.0},
      {"timing_error_input_samples_p50", Percentile(all_timing_errors, 0.50)},
      {"timing_error_input_samples_p95", Percentile(all_timing_errors, 0.95)},
      {"timing_error_input_samples_max", Percentile(all_timing_errors, 1.0)},
      {"frequency_index_confusion_matrix", std::move(confusion)},
      {"terminal_contract", terminal_contract},
      {"confidence_calibration", std::move(calibration)},
      {"same_channel_interferer_trials", interferer_trials},
      {"duplicate_excess_detections", duplicate_excess},
      {"kernel_runtime_allocation_high_water_bytes",
       kernel_allocation_high_water},
      {"node_runtime_allocation_high_water_bytes", node_allocation_high_water},
      {"runtime_seconds", runtime_seconds},
      {"mean_runtime_microseconds_per_capture",
       runtime_seconds * 1.0e6 /
           static_cast<double>(options.trials * snr_points.size() * 2u)},
      {"report_latency", "terminal EOS"}};
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = ParseOptions(argc, argv);
    const auto profile = LoadJson(options.profile);
    nlohmann::json output{
        {"schema", "graphx.fhss.phase2-raw-candidate-measurements"},
        {"version", 1},
        {"seed_partition", options.partition},
        {"trials_per_snr_point", options.trials},
        {"channelizer", CharacterizeChannelizer(profile)},
        {"detector", CharacterizeDetector(profile, options)}};
    std::cout << output.dump(2) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fhss-phase2-characterize: " << error.what() << '\n';
    return 2;
  }
}
