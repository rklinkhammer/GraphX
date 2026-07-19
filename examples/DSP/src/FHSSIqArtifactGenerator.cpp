// SPDX-License-Identifier: MIT
#include "FHSSIqArtifactGenerator.hpp"

#include "dsp/fhss/FHSSGraphXConfig.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace graphx::examples::fhss {
namespace {

class Sha256State {
public:
  void Update(std::span<const std::byte> bytes) {
    bit_count_ += static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (const auto byte : bytes) {
      buffer_[buffer_size_++] = std::to_integer<std::uint8_t>(byte);
      if (buffer_size_ == buffer_.size()) {
        Transform(buffer_);
        buffer_size_ = 0;
      }
    }
  }

  [[nodiscard]] std::string Finish() {
    const auto message_bits = bit_count_;
    buffer_[buffer_size_++] = 0x80u;
    if (buffer_size_ > 56u) {
      while (buffer_size_ < buffer_.size())
        buffer_[buffer_size_++] = 0u;
      Transform(buffer_);
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56u)
      buffer_[buffer_size_++] = 0u;
    for (int shift = 56; shift >= 0; shift -= 8)
      buffer_[buffer_size_++] =
          static_cast<std::uint8_t>(message_bits >> shift);
    Transform(buffer_);
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto word : state_)
      result << std::setw(8) << word;
    return result.str();
  }

private:
  static constexpr std::array<std::uint32_t, 64> kRoundConstants{
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

  static std::uint32_t RotateRight(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
  }
  void Transform(const std::array<std::uint8_t, 64> &block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8u) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto s0 = RotateRight(words[i - 15], 7) ^
                      RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3u);
      const auto s1 = RotateRight(words[i - 2], 17) ^
                      RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10u);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const auto s1 =
          RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto temp1 = h + s1 + choose + kRoundConstants[i] + words[i];
      const auto s0 =
          RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
  std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                      0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                      0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_size_ = 0;
  std::uint64_t bit_count_ = 0;
};

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
          const std::string &iq_hash) {
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
            {"core:frequency", center_frequency_hz},
            {"core:version", "1.0.0"},
            {"graphx:generator", "graphx-dsp-fhss-iq-generator"},
            {"graphx:generator_version", 1},
            {"graphx:sample_count", sample_count},
            {"graphx:iq_sha256", iq_hash}}},
          {"captures", nlohmann::json::array({{{"core:sample_start", 0}}})},
          {"annotations", nlohmann::json::array()}};
}

} // namespace

std::string Sha256(std::span<const std::byte> bytes) {
  Sha256State hash;
  hash.Update(bytes);
  return hash.Finish();
}

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
  auto truth = TruthJson(*generated, sample_format, input_hash, iq_hash);
  auto sigmf = SigmfJson(input, config, sample_format,
                         generated->samples.size(), iq_hash);
  return {.fixture = std::move(*generated),
          .iq_bytes = std::move(iq_bytes),
          .truth = std::move(truth),
          .sigmf = std::move(sigmf),
          .input_sha256 = input_hash,
          .iq_sha256 = iq_hash};
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
