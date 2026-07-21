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

// CLI fixture generation is intentionally bounded but permits the Phase 6
// long-replay fixture. Dashboard jobs apply their smaller 64 MiB service quota.
constexpr std::size_t kMaxCliSamples = 16u * 1024u * 1024u;
constexpr std::size_t kMaxCliIqBytes = 256u * 1024u * 1024u;

constexpr std::string_view kUsage =
    "Usage: graphx-dsp-fhss-iq-generator --message-json FILE --iq-output "
    "FILE --truth-output FILE [--sigmf-meta FILE] "
    "[--sample-format cf32_le|cf64_le] [--force]\n";

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

  graphx::examples::fhss::IqArtifactBundle bundle;
  try {
    bundle = graphx::examples::fhss::GenerateIqArtifacts(
        input, options.sample_format, kMaxCliSamples, kMaxCliIqBytes);
  } catch (const std::exception &error) {
    throw std::runtime_error("configuration: " + std::string(error.what()));
  }

  std::vector<graphx::examples::fhss::OutputFileTransaction> files;
  for (const auto &target : targets)
    files.emplace_back(TemporaryPath(target), target);
  try {
    WriteBytes(files[0].first, bundle.iq_bytes);
    WriteJson(files[1].first, bundle.truth);
    if (options.sigmf_meta)
      WriteJson(files[2].first, bundle.sigmf);
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
