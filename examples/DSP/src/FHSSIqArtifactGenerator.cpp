// SPDX-License-Identifier: MIT
#include "FHSSIqArtifactGenerator.hpp"

#include "dsp/fhss/FHSSGraphXConfig.hpp"

#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace graphx::examples::fhss {
namespace {

template <typename Float>
void AppendLittleEndian(std::vector<std::byte> &bytes, Float value) {
  using UInt =
      std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
  const auto bits = std::bit_cast<UInt>(value);
  for (std::size_t i = 0; i < sizeof(UInt); ++i)
    bytes.push_back(static_cast<std::byte>((bits >> (8u * i)) & 0xffu));
}

nlohmann::json TruthJson(const dsp::fhss::FHSSSyntheticIqFixture &fixture,
                         std::string_view sample_format,
                         const std::string &input_hash,
                         const std::string &iq_hash) {
  nlohmann::json pulses = nlohmann::json::array();
  for (const auto &pulse : fixture.truth_pulses)
    pulses.push_back(
        {{"message_id", pulse.message_id},
         {"nominal_global_start_sample", pulse.nominal_global_start_sample},
         {"received_global_start_sample", pulse.global_start_sample},
         {"duration_samples", pulse.duration_samples},
         {"frequency_index", pulse.frequency_index},
         {"rf_frequency_hz", pulse.rf_frequency_hz},
         {"iq_offset_frequency_hz", pulse.iq_offset_frequency_hz},
         {"doppler_hz", pulse.doppler_hz},
         {"propagation_delay_seconds", pulse.propagation_delay_seconds},
         {"range_m", pulse.range_m},
         {"amplitude", pulse.amplitude},
         {"dropped", pulse.dropped},
         {"word", pulse.value},
         {"role", pulse.is_preamble ? "preamble" : "body"}});
  return {{"schema", "graphx.fhss.iq-truth.v1"},
          {"generator", "graphx-dsp-fhss-iq-generator"},
          {"generator_version", 1},
          {"sample_format", sample_format},
          {"sample_rate_hz", 500'000'000.0},
          {"sample_count", fixture.samples.size()},
          {"input_sha256", input_hash},
          {"iq_sha256", iq_hash},
          {"truth_pulses", std::move(pulses)}};
}

nlohmann::json
SigmfJson(const nlohmann::json &input,
          const dsp::fhss::FHSSSyntheticIqGeneratorConfig &config,
          std::string_view sample_format, std::size_t sample_count,
          const std::string &iq_sha512) {
  const auto first_active =
      config.decode_config.active_frequency_indices.front();
  const double center_frequency_hz =
      input.contains("iq_center_frequency_hz")
          ? input.at("iq_center_frequency_hz").get<double>()
          : dsp::fhss::RfFrequencyHz(first_active,
                                     config.decode_config.frequency) -
                config.decode_config.frequency
                    .iq_offset_frequency_hz[first_active];
  return {{"global",
           {{"core:datatype", sample_format},
            {"core:sample_rate", 500'000'000.0},
            {"core:version", "1.2.6"},
            {"core:sha512", iq_sha512},
            {"core:recorder", "graphx-dsp-fhss-iq-generator"},
            {"core:description", "GraphX architecture-conformant synthetic FHSS IQ"}}},
          {"captures", nlohmann::json::array(
                           {{{"core:sample_start", 0},
                             {"core:frequency", center_frequency_hz}}})},
          {"annotations", nlohmann::json::array(
                              {{{"core:sample_start", 0},
                                {"core:sample_count", sample_count},
                                {"core:comment", "Synthetic software evidence; no HWIL or production-RF qualification"}}})}};
}

} // namespace

std::vector<std::byte>
EncodeIq(const std::vector<std::complex<double>> &samples,
         std::string_view sample_format) {
  if (sample_format != "cf32_le" && sample_format != "cf64_le")
    throw std::invalid_argument("sample_format must be cf32_le or cf64_le");
  const std::size_t scalar_bytes = sample_format == "cf32_le" ? 4u : 8u;
  if (samples.size() >
      std::numeric_limits<std::size_t>::max() / (2u * scalar_bytes))
    throw std::overflow_error("IQ output exceeds host capacity");
  std::vector<std::byte> bytes;
  bytes.reserve(samples.size() * 2u * scalar_bytes);
  for (const auto sample : samples) {
    if (sample_format == "cf32_le") {
      AppendLittleEndian(bytes, static_cast<float>(sample.real()));
      AppendLittleEndian(bytes, static_cast<float>(sample.imag()));
    } else {
      AppendLittleEndian(bytes, sample.real());
      AppendLittleEndian(bytes, sample.imag());
    }
  }
  return bytes;
}

IqArtifactBundle GenerateIqArtifacts(const nlohmann::json &input,
                                     std::string_view sample_format,
                                     std::size_t max_samples,
                                     std::size_t max_iq_bytes) {
  auto config =
      dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(graph::JsonView(input));
  auto generated = dsp::fhss::GenerateSyntheticIqFixture(config);
  if (!generated)
    throw std::invalid_argument(generated.error().message);
  if (generated->samples.size() > max_samples)
    throw std::length_error("generated IQ exceeds sample quota");
  auto iq_bytes = EncodeIq(generated->samples, sample_format);
  if (iq_bytes.size() > max_iq_bytes)
    throw std::length_error("generated IQ exceeds byte quota");
  const auto input_text = input.dump();
  const auto input_hash =
      Sha256(std::as_bytes(std::span(input_text.data(), input_text.size())));
  const auto iq_hash = Sha256(iq_bytes);
  const auto iq_sha512 = Sha512(iq_bytes);
  auto truth = TruthJson(*generated, sample_format, input_hash, iq_hash);
  auto sigmf = SigmfJson(input, config, sample_format,
                         generated->samples.size(), iq_sha512);
  return {.fixture = std::move(*generated),
          .iq_bytes = std::move(iq_bytes),
          .truth = std::move(truth),
          .sigmf = std::move(sigmf),
          .input_sha256 = input_hash,
          .iq_sha256 = iq_hash,
          .iq_sha512 = iq_sha512};
}

void WriteBytes(const std::filesystem::path &path,
                std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("failed to create artifact");
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output)
    throw std::runtime_error("failed to write artifact");
}

void WriteJson(const std::filesystem::path &path, const nlohmann::json &json) {
  const auto text = json.dump(2) + "\n";
  WriteBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}

} // namespace graphx::examples::fhss
