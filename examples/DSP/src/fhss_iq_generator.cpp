// SPDX-License-Identifier: MIT

#include "FHSSIqArtifactGenerator.hpp"
#include "FHSSIqOutputTransaction.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr std::string_view kUsage =
    "Usage: graphx-dsp-fhss-iq-generator --message-json FILE --iq-output "
    "FILE --truth-output FILE [--sigmf-meta FILE] "
    "[--sample-format cf32_le|cf64_le] [--force]\n";

class Sha256 {
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
    for (int shift = 56; shift >= 0; shift -= 8) {
      buffer_[buffer_size_++] =
          static_cast<std::uint8_t>(message_bits >> shift);
    }
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
    for (std::size_t i = 0; i < 16; ++i) {
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8u) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
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

struct Options {
  std::filesystem::path message_json;
  std::filesystem::path iq_output;
  std::filesystem::path truth_output;
  std::optional<std::filesystem::path> sigmf_meta;
  std::string sample_format = "cf32_le";
  bool force = false;
  bool help = false;
};

[[nodiscard]] Options ParseOptions(int argc, char **argv) {
  Options options{};
  auto require_value = [&](int &index, std::string_view option) {
    if (++index >= argc) {
      throw std::invalid_argument("usage: missing value for " +
                                  std::string(option));
    }
    return std::filesystem::path(argv[index]);
  };
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else if (argument == "--message-json") {
      options.message_json = require_value(i, argument);
    } else if (argument == "--iq-output") {
      options.iq_output = require_value(i, argument);
    } else if (argument == "--truth-output") {
      options.truth_output = require_value(i, argument);
    } else if (argument == "--sigmf-meta") {
      options.sigmf_meta = require_value(i, argument);
    } else if (argument == "--sample-format") {
      options.sample_format = require_value(i, argument).string();
    } else if (argument == "--force") {
      options.force = true;
    } else {
      throw std::invalid_argument("usage: unknown option '" +
                                  std::string(argument) + "'");
    }
  }
  if (!options.help &&
      (options.message_json.empty() || options.iq_output.empty() ||
       options.truth_output.empty())) {
    throw std::invalid_argument(
        "usage: --message-json, --iq-output, and --truth-output are required");
  }
  if (options.sample_format != "cf32_le" &&
      options.sample_format != "cf64_le") {
    throw std::invalid_argument(
        "usage: --sample-format must be cf32_le or cf64_le");
  }
  return options;
}

[[nodiscard]] std::vector<std::byte>
ReadAllBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    throw std::runtime_error("io: failed to open " + path.string());
  const auto end = input.tellg();
  if (end < 0)
    throw std::runtime_error("io: failed to size " + path.string());
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!bytes.empty() &&
      !input.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("io: failed to read " + path.string());
  }
  return bytes;
}

[[nodiscard]] std::string HashBytes(std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.Update(bytes);
  return hash.Finish();
}

template <typename Float>
void AppendLittleEndian(std::vector<std::byte> &bytes, Float value) {
  using UInt =
      std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
  const auto bits = std::bit_cast<UInt>(value);
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    bytes.push_back(static_cast<std::byte>((bits >> (8u * i)) & 0xffu));
  }
}

[[nodiscard]] std::vector<std::byte>
EncodeIq(const std::vector<std::complex<double>> &samples,
         const std::string &sample_format) {
  const auto scalar_bytes = sample_format == "cf32_le" ? 4u : 8u;
  if (samples.size() >
      std::numeric_limits<std::size_t>::max() / (2u * scalar_bytes)) {
    throw std::runtime_error("configuration: IQ output exceeds host capacity");
  }
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

void WriteBytes(const std::filesystem::path &path,
                std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("io: failed to create " + path.string());
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output)
    throw std::runtime_error("io: failed to write " + path.string());
}

void WriteJson(const std::filesystem::path &path, const nlohmann::json &json) {
  const auto text = json.dump(2) + "\n";
  WriteBytes(path, std::as_bytes(std::span(text)));
}

[[nodiscard]] std::filesystem::path
TemporaryPath(const std::filesystem::path &target) {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return target.string() + ".tmp." + std::to_string(nonce);
}

[[nodiscard]] nlohmann::json
TruthJson(const dsp::fhss::FHSSSyntheticIqFixture &fixture,
          const std::string &sample_format, const std::string &input_hash,
          const std::string &iq_hash) {
  nlohmann::json pulses = nlohmann::json::array();
  for (const auto &pulse : fixture.truth_pulses) {
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
  }
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

[[nodiscard]] nlohmann::json
SigmfJson(const nlohmann::json &input,
          const dsp::fhss::FHSSSyntheticIqGeneratorConfig &config,
          const std::string &sample_format, std::size_t sample_count,
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
  nlohmann::json global{{"core:datatype", sample_format},
                        {"core:sample_rate", 500'000'000.0},
                        {"core:frequency", center_frequency_hz},
                        {"core:version", "1.0.0"},
                        {"graphx:generator", "graphx-dsp-fhss-iq-generator"},
                        {"graphx:generator_version", 1},
                        {"graphx:sample_count", sample_count},
                        {"graphx:iq_sha256", iq_hash}};
  return {{"global", std::move(global)},
          {"captures", nlohmann::json::array({{{"core:sample_start", 0}}})},
          {"annotations", nlohmann::json::array()}};
}

void RequireArchitectureTiming(const nlohmann::json &json) {
  const auto require_equal = [&](std::string_view key, double expected) {
    if (json.contains(key) &&
        (!json.at(key).is_number() || json.at(key).get<double>() != expected)) {
      throw std::runtime_error("configuration: " + std::string(key) +
                               " must match the FHSS architecture profile");
    }
  };
  require_equal("sample_rate_hz", 500'000'000.0);
  require_equal("bit_rate_hz", 5'000'000.0);
  require_equal("bits_per_pulse", 32.0);
  require_equal("pulse_gap_seconds", 6.6e-6);
}

int Run(const Options &options) {
  const auto input_bytes = ReadAllBytes(options.message_json);
  nlohmann::json input;
  try {
    input = nlohmann::json::parse(input_bytes.begin(), input_bytes.end());
  } catch (const std::exception &error) {
    throw std::runtime_error("configuration: invalid message JSON: " +
                             std::string(error.what()));
  }
  RequireArchitectureTiming(input);

  dsp::fhss::FHSSSyntheticIqGeneratorConfig config;
  try {
    config = dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
        graph::JsonView(input));
  } catch (const std::exception &error) {
    throw std::runtime_error("configuration: " + std::string(error.what()));
  }
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);
  if (!fixture) {
    throw std::runtime_error("configuration: " + fixture.error().message);
  }

  std::vector<std::filesystem::path> targets{options.iq_output,
                                             options.truth_output};
  if (options.sigmf_meta)
    targets.push_back(*options.sigmf_meta);
  for (std::size_t i = 0; i < targets.size(); ++i) {
    for (std::size_t j = i + 1; j < targets.size(); ++j) {
      if (targets[i] == targets[j]) {
        throw std::runtime_error("io: output paths must be distinct");
      }
    }
    if (!options.force && std::filesystem::exists(targets[i])) {
      throw std::runtime_error("io: refusing to overwrite " +
                               targets[i].string() + " without --force");
    }
    if (options.force && std::filesystem::exists(targets[i]) &&
        !std::filesystem::is_regular_file(targets[i])) {
      throw std::runtime_error("io: existing output is not a regular file: " +
                               targets[i].string());
    }
  }

  const auto iq_bytes =
      graphx::examples::fhss::EncodeIq(fixture->samples, options.sample_format);
  const auto input_hash = HashBytes(input_bytes);
  const auto iq_hash = HashBytes(iq_bytes);
  const auto truth =
      TruthJson(*fixture, options.sample_format, input_hash, iq_hash);
  const auto sigmf = SigmfJson(input, config, options.sample_format,
                               fixture->samples.size(), iq_hash);

  std::vector<graphx::examples::fhss::OutputFileTransaction> files;
  for (const auto &target : targets)
    files.emplace_back(TemporaryPath(target), target);
  try {
    WriteBytes(files[0].first, iq_bytes);
    WriteJson(files[1].first, truth);
    if (options.sigmf_meta)
      WriteJson(files[2].first, sigmf);
    graphx::examples::fhss::CommitOutputFiles(files, options.force);
  } catch (...) {
    for (const auto &[temporary, target] : files) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    }
    throw;
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = ParseOptions(argc, argv);
    if (options.help) {
      std::cout << kUsage;
      return 0;
    }
    return Run(options);
  } catch (const std::invalid_argument &error) {
    std::cerr << error.what() << '\n' << kUsage;
    return 2;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
