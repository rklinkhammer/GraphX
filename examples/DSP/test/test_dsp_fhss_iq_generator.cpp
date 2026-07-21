// SPDX-License-Identifier: MIT

#include "FHSSIqOutputTransaction.hpp"
#include "FHSSHash.hpp"
#include "dsp/fhss/FHSSBinaryIqFileSourceNode.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH
#define DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH "./graphx-dsp-fhss-iq-generator"
#endif

#ifndef DSP_FHSS_BINARY_CONFIG_PATH
#define DSP_FHSS_BINARY_CONFIG_PATH                                            \
  "libdsp/config/fhss_cpsm_binary_iq_500msps.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

std::string ShellQuote(const std::filesystem::path &path) {
  std::string quoted{"'"};
  for (const char ch : path.string()) {
    quoted += ch == '\'' ? "'\\''" : std::string(1, ch);
  }
  return quoted + "'";
}

struct CommandResult {
  int exit_code = -1;
  std::string output;
};

CommandResult RunCommand(const std::string &command) {
  std::array<char, 512> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return {.exit_code = -1, .output = "popen failed"};
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output += buffer.data();
  }
  return {.exit_code = pclose(pipe), .output = std::move(output)};
}

class TempDirectory {
public:
  explicit TempDirectory(const std::string &name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] std::filesystem::path operator/(std::string_view child) const {
    return path_ / child;
  }
  [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

private:
  std::filesystem::path path_;
};

nlohmann::json ArchitectureSchedule(bool include_body = false) {
  constexpr std::uint32_t frequencies[]{24, 28, 32, 36};
  constexpr std::uint32_t words[]{0xaaaaaaaau, 0x77777777u, 0x12121212u,
                                  0x62626262u};
  nlohmann::json pulses = nlohmann::json::array();
  for (std::size_t i = 0; i < 16; ++i) {
    pulses.push_back({{"frequency_index", frequencies[i % 4]},
                      {"value", words[i % 4]},
                      {"role", "preamble"}});
  }
  if (include_body) {
    pulses.push_back(
        {{"frequency_index", 24}, {"value", 0x01020304u}, {"role", "body"}});
  }
  return {
      {"active_frequency_indices", {24, 28, 32, 36}},
      {"iq_center_frequency_hz", 1'240'000'000.0},
      {"messages", nlohmann::json::array({{{"message_id", 41},
                                           {"transmit_start_sample", 0},
                                           {"pulses", std::move(pulses)}}})},
      {"idle_duration_samples", 0},
      {"allow_overlap", false},
      {"enable_noise", false},
      {"enable_doppler", false},
      {"enable_multipath", false}};
}

void WriteJson(const std::filesystem::path &path, const nlohmann::json &json) {
  std::ofstream output(path);
  ASSERT_TRUE(output.good());
  output << json.dump(2);
  ASSERT_TRUE(output.good());
}

nlohmann::json ReadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  nlohmann::json result;
  input >> result;
  return result;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const auto size = input.tellg();
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

std::string GeneratorCommand(const std::filesystem::path &schedule,
                             const std::filesystem::path &iq,
                             const std::filesystem::path &truth,
                             const std::string &extra = {}) {
  return ShellQuote(DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH) +
         " --message-json " + ShellQuote(schedule) + " --iq-output " +
         ShellQuote(iq) + " --truth-output " + ShellQuote(truth) + " " + extra +
         " 2>&1";
}

template <typename UInt>
UInt LittleEndian(const std::vector<std::byte> &bytes, std::size_t offset) {
  UInt value = 0;
  for (std::size_t i = 0; i < sizeof(UInt); ++i) {
    value |= static_cast<UInt>(std::to_integer<unsigned>(bytes[offset + i]))
             << (8u * i);
  }
  return value;
}

template <typename Float>
Float LittleEndianFloat(const std::vector<std::byte> &bytes,
                        std::size_t scalar_index) {
  using UInt =
      std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
  return std::bit_cast<Float>(
      LittleEndian<UInt>(bytes, scalar_index * sizeof(Float)));
}

std::complex<double> IndependentArchitectureSample(std::uint32_t word,
                                                   double iq_offset_hz,
                                                   std::uint64_t pulse_start,
                                                   std::uint32_t local_sample) {
  constexpr double sample_rate_hz = 500'000'000.0;
  constexpr std::uint32_t samples_per_symbol = 100;
  constexpr double modulation_index = 0.5;
  const auto symbol_index = local_sample / samples_per_symbol;
  const auto sample_in_symbol = local_sample % samples_per_symbol;
  double completed_phase_pulse_sum = 0.0;
  for (std::uint32_t index = 0; index < symbol_index; ++index) {
    const auto bit = (word >> (31u - index)) & 1u;
    completed_phase_pulse_sum += 0.5 * (bit == 0u ? 1.0 : -1.0);
  }
  const auto bit = (word >> (31u - symbol_index)) & 1u;
  const double symbol = bit == 0u ? 1.0 : -1.0;
  const double q = 0.5 * static_cast<double>(sample_in_symbol) /
                   static_cast<double>(samples_per_symbol);
  const double theta = 2.0 * std::numbers::pi * modulation_index *
                       (completed_phase_pulse_sum + symbol * q);
  const auto global_sample = pulse_start + local_sample;
  const double hop_phase = 2.0 * std::numbers::pi * iq_offset_hz *
                           static_cast<double>(global_sample) / sample_rate_hz;
  return std::exp(std::complex<double>(0.0, theta + hop_phase));
}

std::shared_ptr<dsp::fhss::FHSSMessageSinkNode>
ResolveSink(const std::shared_ptr<graph::GraphManager> &manager) {
  for (const auto &node : manager->GetNodes()) {
    const auto wrapper =
        std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
    if (wrapper) {
      if (auto sink = wrapper->GetNode<dsp::fhss::FHSSMessageSinkNode>()) {
        return sink;
      }
    }
  }
  return nullptr;
}

} // namespace

TEST(DspFhssIqGeneratorExecutableTest, HelpAndStableUsageErrors) {
  ASSERT_TRUE(std::filesystem::exists(DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH));
  auto result = RunCommand(ShellQuote(DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH) +
                           " --help 2>&1");
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("--message-json"), std::string::npos);
  EXPECT_NE(result.output.find("--sample-format"), std::string::npos);

  result = RunCommand(ShellQuote(DSP_FHSS_IQ_GENERATOR_EXECUTABLE_PATH) +
                      " --not-a-real-flag 2>&1");
  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.output.starts_with("usage:")) << result.output;
}

TEST(DspFhssIqGeneratorExecutableTest, RejectsInvalidJsonAndOutputPaths) {
  const TempDirectory temp("graphx_fhss_iq_generator_io_errors");
  const auto invalid_json = temp / "invalid.json";
  {
    std::ofstream output(invalid_json);
    output << "{ definitely not JSON";
  }
  auto result = RunCommand(
      GeneratorCommand(invalid_json, temp / "bad.iq", temp / "bad.truth.json"));
  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.output.starts_with("configuration: invalid message JSON"))
      << result.output;
  EXPECT_FALSE(std::filesystem::exists(temp / "bad.iq"));

  const auto schedule = temp / "schedule.json";
  WriteJson(schedule, ArchitectureSchedule());
  result = RunCommand(GeneratorCommand(schedule, temp / "missing/out.iq",
                                       temp / "missing/out.truth.json"));
  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.output.starts_with("io:")) << result.output;
  EXPECT_FALSE(std::filesystem::exists(temp / "missing/out.iq"));
  EXPECT_FALSE(std::filesystem::exists(temp / "missing/out.truth.json"));
}

TEST(DspFhssIqGeneratorExecutableTest,
     WritesAnalyticalGoldenBytesTruthAndSigmfForBothFormats) {
  const TempDirectory temp("graphx_fhss_iq_generator_golden");
  const auto schedule = temp / "schedule.json";
  WriteJson(schedule, ArchitectureSchedule());

  for (const std::string format : {"cf32_le", "cf64_le"}) {
    const auto iq = temp / (format + ".iq");
    const auto truth = temp / (format + ".truth.json");
    const auto sigmf = temp / (format + ".sigmf-meta");
    const auto result = RunCommand(GeneratorCommand(
        schedule, iq, truth,
        "--sample-format " + format + " --sigmf-meta " + ShellQuote(sigmf)));
    ASSERT_EQ(result.exit_code, 0) << result.output;

    const auto bytes = ReadBytes(iq);
    const auto scalar_bytes = format == "cf32_le" ? 4u : 8u;
    EXPECT_EQ(bytes.size(), 16u * 6500u * 2u * scalar_bytes);
    if (format == "cf32_le") {
      EXPECT_EQ(LittleEndian<std::uint32_t>(bytes, 0),
                std::bit_cast<std::uint32_t>(1.0F));
      EXPECT_EQ(LittleEndian<std::uint32_t>(bytes, 4), 0u);
    } else {
      EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, 0),
                std::bit_cast<std::uint64_t>(1.0));
      EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, 8), 0u);
    }
    const auto verify_sample = [&](std::uint64_t global_sample,
                                   std::complex<double> expected) {
      const auto scalar = static_cast<std::size_t>(global_sample) * 2u;
      if (format == "cf32_le") {
        const auto expected_i = static_cast<float>(expected.real());
        const auto expected_q = static_cast<float>(expected.imag());
        EXPECT_EQ(LittleEndian<std::uint32_t>(bytes, scalar * 4u),
                  std::bit_cast<std::uint32_t>(expected_i));
        EXPECT_EQ(LittleEndian<std::uint32_t>(bytes, (scalar + 1u) * 4u),
                  std::bit_cast<std::uint32_t>(expected_q));
      } else {
        EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, scalar * 8u),
                  std::bit_cast<std::uint64_t>(expected.real()));
        EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, (scalar + 1u) * 8u),
                  std::bit_cast<std::uint64_t>(expected.imag()));
      }
    };
    // Independent architecture equations: A's MSB is one (-1 symbol), the
    // next bit is zero (+1), and index 24 is -48 MHz at the 1.24 GHz center.
    for (const std::uint32_t local : {1u, 99u, 100u, 3199u}) {
      verify_sample(local, IndependentArchitectureSample(
                               0xaaaaaaaau, -48'000'000.0, 0, local));
    }
    // The second pulse starts exactly at 6500, hops on index 28 (-16 MHz),
    // and its first sample contains hop phase but no accumulated CPSM phase.
    verify_sample(6500, IndependentArchitectureSample(0x77777777u,
                                                      -16'000'000.0, 6500, 0));
    const auto gap_start = 3200u * 2u * scalar_bytes;
    EXPECT_TRUE(
        std::all_of(bytes.begin() + gap_start,
                    bytes.begin() + gap_start + 2u * scalar_bytes,
                    [](std::byte byte) { return byte == std::byte{0}; }));

    const auto truth_json = ReadJson(truth);
    EXPECT_EQ(truth_json.at("schema"), "graphx.fhss.iq-truth.v1");
    EXPECT_EQ(truth_json.at("sample_count"), 16u * 6500u);
    EXPECT_EQ(truth_json.at("truth_pulses").size(), 16u);
    EXPECT_EQ(truth_json.at("truth_pulses").at(0).at("word"), 0xaaaaaaaau);
    EXPECT_EQ(truth_json.at("input_sha256").get<std::string>().size(), 64u);
    EXPECT_EQ(truth_json.at("iq_sha256").get<std::string>().size(), 64u);
    const auto meta = ReadJson(sigmf);
    EXPECT_EQ(meta.at("global").at("core:datatype"), format);
    EXPECT_EQ(meta.at("global").at("core:sample_rate"), 500'000'000.0);
    EXPECT_EQ(meta.at("global").at("core:version"), "1.2.6");
    EXPECT_EQ(meta.at("captures").at(0).at("core:sample_start"), 0u);
    EXPECT_EQ(meta.at("captures").at(0).at("core:frequency"),
              1'240'000'000.0);
    EXPECT_EQ(meta.at("global").at("core:sha512"),
              graphx::examples::fhss::Sha512(std::span(bytes)));
  }
}

TEST(DspFhssIqGeneratorExecutableTest,
     AllowOverlapSumsIndependentWaveformsAtSharedSamples) {
  const TempDirectory temp("graphx_fhss_iq_generator_overlap");
  auto schedule_json = ArchitectureSchedule();
  auto second = schedule_json.at("messages").at(0);
  second["message_id"] = 42;
  schedule_json["messages"].push_back(second);
  schedule_json["allow_overlap"] = true;
  const auto schedule = temp / "schedule.json";
  const auto iq = temp / "capture.cf64";
  const auto truth = temp / "capture.truth.json";
  WriteJson(schedule, schedule_json);
  const auto result = RunCommand(
      GeneratorCommand(schedule, iq, truth, "--sample-format cf64_le"));
  ASSERT_EQ(result.exit_code, 0) << result.output;
  const auto bytes = ReadBytes(iq);
  EXPECT_DOUBLE_EQ(LittleEndianFloat<double>(bytes, 0), 2.0);
  EXPECT_DOUBLE_EQ(LittleEndianFloat<double>(bytes, 1), 0.0);
  const auto expected =
      2.0 * IndependentArchitectureSample(0xaaaaaaaau, -48'000'000.0, 0, 1);
  EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, 16),
            std::bit_cast<std::uint64_t>(expected.real()));
  EXPECT_EQ(LittleEndian<std::uint64_t>(bytes, 24),
            std::bit_cast<std::uint64_t>(expected.imag()));
  EXPECT_EQ(ReadJson(truth).at("truth_pulses").size(), 32u);
}

TEST(DspFhssIqGeneratorExecutableTest,
     IsDeterministicProtectsOutputsAndCleansPartialFiles) {
  const TempDirectory temp("graphx_fhss_iq_generator_determinism");
  const auto schedule = temp / "schedule.json";
  WriteJson(schedule, ArchitectureSchedule(true));
  const auto iq_a = temp / "a.cf32";
  const auto truth_a = temp / "a.truth.json";
  const auto iq_b = temp / "b.cf32";
  const auto truth_b = temp / "b.truth.json";
  ASSERT_EQ(RunCommand(GeneratorCommand(schedule, iq_a, truth_a)).exit_code, 0);
  ASSERT_EQ(RunCommand(GeneratorCommand(schedule, iq_b, truth_b)).exit_code, 0);
  EXPECT_EQ(ReadBytes(iq_a), ReadBytes(iq_b));
  EXPECT_EQ(ReadJson(truth_a).at("iq_sha256"),
            ReadJson(truth_b).at("iq_sha256"));

  const auto overwrite = RunCommand(GeneratorCommand(schedule, iq_a, truth_a));
  EXPECT_NE(overwrite.exit_code, 0);
  EXPECT_NE(overwrite.output.find("refusing to overwrite"), std::string::npos);
  EXPECT_EQ(RunCommand(GeneratorCommand(schedule, iq_a, truth_a, "--force"))
                .exit_code,
            0);

  auto invalid = ArchitectureSchedule();
  invalid["enable_noise"] = true;
  const auto invalid_path = temp / "invalid.json";
  const auto bad_iq = temp / "bad.iq";
  const auto bad_truth = temp / "bad.truth.json";
  WriteJson(invalid_path, invalid);
  const auto rejected =
      RunCommand(GeneratorCommand(invalid_path, bad_iq, bad_truth));
  EXPECT_NE(rejected.exit_code, 0);
  EXPECT_TRUE(rejected.output.starts_with("configuration:")) << rejected.output;
  EXPECT_FALSE(std::filesystem::exists(bad_iq));
  EXPECT_FALSE(std::filesystem::exists(bad_truth));
}

TEST(DspFhssIqGeneratorExecutableTest,
     PostWriteFailureCleansTempsAndForcePreservesExistingOutputs) {
  const TempDirectory temp("graphx_fhss_iq_generator_transaction");
  const auto schedule = temp / "schedule.json";
  WriteJson(schedule, ArchitectureSchedule());

  const auto assert_no_internal_artifacts = [&] {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(temp.Path())) {
      const auto filename = entry.path().filename().string();
      EXPECT_EQ(filename.find(".tmp."), std::string::npos) << entry.path();
      EXPECT_EQ(filename.find(".backup"), std::string::npos) << entry.path();
    }
  };

  const auto iq = temp / "new.cf32";
  const auto truth = temp / "new.truth.json";
  const auto unavailable_meta = temp / "missing/meta.sigmf-meta";
  auto result = RunCommand(GeneratorCommand(
      schedule, iq, truth, "--sigmf-meta " + ShellQuote(unavailable_meta)));
  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.output.starts_with("io:")) << result.output;
  EXPECT_FALSE(std::filesystem::exists(iq));
  EXPECT_FALSE(std::filesystem::exists(truth));
  EXPECT_FALSE(std::filesystem::exists(unavailable_meta));
  assert_no_internal_artifacts();

  const auto existing_iq = temp / "existing.cf32";
  const auto existing_truth = temp / "existing.truth.json";
  const std::vector<std::byte> original_iq{std::byte{0xde}, std::byte{0xad}};
  {
    std::ofstream output(existing_iq, std::ios::binary);
    output.write(reinterpret_cast<const char *>(original_iq.data()),
                 static_cast<std::streamsize>(original_iq.size()));
  }
  const nlohmann::json original_truth{{"sentinel", "preserve-me"}};
  WriteJson(existing_truth, original_truth);
  result = RunCommand(GeneratorCommand(
      schedule, existing_iq, existing_truth,
      "--sigmf-meta " + ShellQuote(unavailable_meta) + " --force"));
  EXPECT_NE(result.exit_code, 0);
  EXPECT_EQ(ReadBytes(existing_iq), original_iq);
  EXPECT_EQ(ReadJson(existing_truth), original_truth);
  EXPECT_FALSE(std::filesystem::exists(unavailable_meta));
  assert_no_internal_artifacts();
}

TEST(DspFhssIqGeneratorExecutableTest,
     CommitFailureRollsBackAlreadyReplacedOutputs) {
  const TempDirectory temp("graphx_fhss_iq_generator_commit_rollback");
  const auto target_iq = temp / "capture.cf32";
  const auto target_truth = temp / "capture.truth.json";
  const auto temporary_iq = temp / "capture.cf32.tmp.test";
  const auto temporary_truth = temp / "capture.truth.json.tmp.test";
  {
    std::ofstream(target_iq) << "original-iq";
    std::ofstream(target_truth) << "original-truth";
    std::ofstream(temporary_iq) << "replacement-iq";
    std::ofstream(temporary_truth) << "replacement-truth";
  }
  const std::vector<graphx::examples::fhss::OutputFileTransaction> files{
      {temporary_iq, target_iq}, {temporary_truth, target_truth}};
  EXPECT_THROW(graphx::examples::fhss::CommitOutputFiles(
                   files, true,
                   [](std::size_t index) {
                     if (index == 0u)
                       throw std::runtime_error("injected commit failure");
                   }),
               std::runtime_error);
  const auto restored_iq = ReadBytes(target_iq);
  const auto restored_truth = ReadBytes(target_truth);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(restored_iq.data()),
                        restored_iq.size()),
            "original-iq");
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(restored_truth.data()),
                        restored_truth.size()),
            "original-truth");
  EXPECT_FALSE(std::filesystem::exists(temporary_iq));
  EXPECT_FALSE(std::filesystem::exists(temporary_truth));
  for (const auto &entry : std::filesystem::directory_iterator(temp.Path())) {
    EXPECT_EQ(entry.path().filename().string().find(".backup."),
              std::string::npos)
        << entry.path();
  }
}

TEST(DspFhssIqGeneratorExecutableTest,
     RejectsEveryArchitectureValidationFamilyAtExecutableBoundary) {
  const TempDirectory temp("graphx_fhss_iq_generator_validation_families");
  struct InvalidCase {
    std::string name;
    nlohmann::json schedule;
  };
  std::vector<InvalidCase> cases;
  const auto add = [&](std::string name, nlohmann::json schedule) {
    cases.push_back({std::move(name), std::move(schedule)});
  };

  auto schedule = ArchitectureSchedule(true);
  schedule["sample_rate_hz"] = 499'000'000.0;
  add("sample_rate_value", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["sample_rate_hz"] = "500000000";
  add("sample_rate_type", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["bit_rate_hz"] = 4'000'000.0;
  add("bit_rate_value", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["bit_rate_hz"] = "5000000";
  add("bit_rate_type", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["bits_per_pulse"] = 31;
  add("bits_per_pulse_value", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["bits_per_pulse"] = "32";
  add("bits_per_pulse_type", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["pulse_gap_seconds"] = 6.5e-6;
  add("pulse_gap_value", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["pulse_gap_seconds"] = "6.6e-6";
  add("pulse_gap_type", schedule);

  schedule = ArchitectureSchedule(true);
  schedule["active_frequency_indices"] = {24, 28, 32};
  add("active_count", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["active_frequency_indices"] = {24, 28, 32, 32};
  add("active_duplicate", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["active_frequency_indices"] = {0, 28, 32, 36};
  add("active_reserved", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["messages"][0]["pulses"][0]["frequency_index"] = 25;
  add("selectable_preamble_outside_active_set", schedule);

  schedule = ArchitectureSchedule();
  schedule["messages"][0]["pulses"].erase(
      schedule["messages"][0]["pulses"].end() - 1);
  add("short_preamble", schedule);
  schedule = ArchitectureSchedule();
  while (schedule["messages"][0]["pulses"].size() < 257u) {
    schedule["messages"][0]["pulses"].push_back(
        {{"frequency_index", 24}, {"value", 0u}, {"role", "body"}});
  }
  add("too_many_pulses", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["messages"][0]["pulses"][0]["role"] = "body";
  add("preamble_role", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["messages"][0]["pulses"][16]["role"] = "preamble";
  add("body_role", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["messages"][0]["pulses"][4]["value"] = 0x55555555u;
  add("preamble_word_consistency", schedule);
  schedule = ArchitectureSchedule(true);
  schedule["messages"][0]["pulses"][16]["frequency_index"] = 25;
  add("payload_membership", schedule);

  schedule = ArchitectureSchedule();
  schedule["enable_doppler"] = true;
  add("doppler_without_realistic", schedule);
  schedule = ArchitectureSchedule();
  schedule["enable_noise"] = true;
  add("unsupported_noise", schedule);
  schedule = ArchitectureSchedule();
  schedule["enable_multipath"] = true;
  add("unsupported_multipath", schedule);

  const auto explicit_offsets = [](nlohmann::json value) {
    value.erase("iq_center_frequency_hz");
    value["iq_offsets"] = nlohmann::json::array(
        {{{"index", 24}, {"iq_offset_frequency_hz", -48'000'000.0}},
         {{"index", 28}, {"iq_offset_frequency_hz", -16'000'000.0}},
         {{"index", 32}, {"iq_offset_frequency_hz", 16'000'000.0}},
         {{"index", 36}, {"iq_offset_frequency_hz", 48'000'000.0}}});
    return value;
  };
  schedule = explicit_offsets(ArchitectureSchedule());
  schedule["iq_offsets"][1]["iq_offset_frequency_hz"] = -48'000'000.0;
  add("duplicate_iq_offset", schedule);
  schedule = explicit_offsets(ArchitectureSchedule());
  schedule["iq_offsets"][0]["iq_offset_frequency_hz"] = 300'000'000.0;
  add("nyquist_iq_offset", schedule);

  schedule = ArchitectureSchedule();
  schedule["messages"][0]["transmit_start_sample"] =
      std::numeric_limits<std::uint64_t>::max();
  add("schedule_overflow", schedule);
  schedule = ArchitectureSchedule();
  schedule["messages"].push_back(schedule["messages"][0]);
  add("overlap_rejected", schedule);

  schedule = ArchitectureSchedule();
  schedule["realistic"] = {{"enabled", true},
                           {"missing_pulse_probability", 1.5}};
  add("realistic_probability", schedule);
  schedule = ArchitectureSchedule();
  schedule["realistic"] = {{"enabled", true},
                           {"timing_jitter_stddev_samples", -1.0}};
  add("realistic_negative_jitter", schedule);
  schedule = ArchitectureSchedule();
  schedule["realistic"] = {
      {"enabled", true},
      {"transmitter_paths",
       nlohmann::json::array(
           {{{"message_id", 41},
             {"waypoints",
              nlohmann::json::array(
                  {{{"time_seconds", 1.0}}, {{"time_seconds", 0.5}}})}}})}};
  add("realistic_unsorted_waypoints", schedule);

  for (const auto &[name, invalid] : cases) {
    const auto input = temp / (name + ".json");
    const auto iq = temp / (name + ".iq");
    const auto truth = temp / (name + ".truth.json");
    WriteJson(input, invalid);
    const auto result = RunCommand(GeneratorCommand(input, iq, truth));
    EXPECT_NE(result.exit_code, 0) << name << ": " << result.output;
    EXPECT_TRUE(result.output.starts_with("configuration:"))
        << name << ": " << result.output;
    EXPECT_FALSE(std::filesystem::exists(iq)) << name;
    EXPECT_FALSE(std::filesystem::exists(truth)) << name;
  }

  // JSON cannot represent non-finite values. A numeric overflow literal must
  // nevertheless fail at the executable boundary with the same stable class.
  const auto nonfinite = temp / "nonfinite.json";
  {
    auto text = explicit_offsets(ArchitectureSchedule()).dump();
    const auto position = text.find("-48000000.0");
    ASSERT_NE(position, std::string::npos);
    text.replace(position, std::string("-48000000.0").size(), "1e400");
    std::ofstream output(nonfinite);
    output << text;
  }
  const auto nonfinite_result = RunCommand(GeneratorCommand(
      nonfinite, temp / "nonfinite.iq", temp / "nonfinite.truth.json"));
  EXPECT_NE(nonfinite_result.exit_code, 0);
  EXPECT_TRUE(nonfinite_result.output.starts_with("configuration:"))
      << nonfinite_result.output;
}

TEST(DspFhssIqGeneratorExecutableTest,
     SerializesValidRealisticTruthAtExecutableBoundary) {
  const TempDirectory temp("graphx_fhss_iq_generator_realistic");
  auto schedule_json = ArchitectureSchedule();
  schedule_json["enable_doppler"] = true;
  schedule_json["realistic"] = {
      {"enabled", true},
      {"rng_seed", 1234},
      {"missing_pulse_probability", 0.0},
      {"receiver", {{"position_m", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}}}},
      {"transmitter_paths",
       nlohmann::json::array(
           {{{"message_id", 41},
             {"waypoints",
              nlohmann::json::array(
                  {{{"time_seconds", 0.0},
                    {"position_m", {{"x", 1000.0}, {"y", 0.0}, {"z", 0.0}}}},
                   {{"time_seconds", 1.0},
                    {"position_m",
                     {{"x", 1100.0}, {"y", 0.0}, {"z", 0.0}}}}})}}})}};
  const auto schedule = temp / "realistic.json";
  const auto iq = temp / "realistic.cf32";
  const auto truth = temp / "realistic.truth.json";
  WriteJson(schedule, schedule_json);
  const auto result = RunCommand(GeneratorCommand(schedule, iq, truth));
  ASSERT_EQ(result.exit_code, 0) << result.output;
  const auto first = ReadJson(truth).at("truth_pulses").at(0);
  EXPECT_GT(first.at("range_m").get<double>(), 0.0);
  EXPECT_GT(first.at("propagation_delay_seconds").get<double>(), 0.0);
  EXPECT_NE(first.at("doppler_hz").get<double>(), 0.0);
  EXPECT_FALSE(first.at("dropped").get<bool>());
}

TEST(DspFhssIqGeneratorExecutableTest,
     GeneratedFileFeedsMessageFreeReceiverWithoutTruthAccess) {
  const TempDirectory temp("graphx_fhss_iq_generator_round_trip");
  const auto schedule = temp / "schedule.json";
  const auto iq = temp / "capture.cf32";
  const auto truth = temp / "capture.truth.json";
  WriteJson(schedule, ArchitectureSchedule(true));
  const auto generated = RunCommand(GeneratorCommand(schedule, iq, truth));
  ASSERT_EQ(generated.exit_code, 0) << generated.output;

  dsp::fhss::FHSSBinaryIqFileSourceConfig source_config{};
  source_config.file_path = iq;
  const auto replay_samples = dsp::fhss::ReadFHSSBinaryIqFile(source_config);
  ASSERT_TRUE(replay_samples.has_value());
  EXPECT_EQ(replay_samples->size(), 17u * 6500u);

  const auto original_graph = ReadJson(DSP_FHSS_BINARY_CONFIG_PATH);
  auto graph_json = original_graph;
  ASSERT_EQ(graph_json.dump().find("\"messages\""), std::string::npos);
  ASSERT_EQ(graph_json.dump().find("truth"), std::string::npos);
  ASSERT_EQ(graph_json.dump().find("transmitted_"), std::string::npos);
  graph_json.at("nodes").at(0).at("node_config")["file_path"] = iq.string();
  auto expected_graph = original_graph;
  expected_graph.at("nodes").at(0).at("node_config")["file_path"] = iq.string();
  EXPECT_EQ(graph_json, expected_graph)
      << "round trip may patch only the binary source file_path";
  const auto graph_path = temp / "replay.json";
  WriteJson(graph_path, graph_json);

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(graph_path.string())
                      .WithPluginDirectory(DSP_PLUGIN_OUTPUT_DIRECTORY)
                      .WithExecutorTimeout(std::chrono::seconds(20))
                      .Build();
  ASSERT_NE(executor, nullptr);
  ASSERT_NE(executor->GetGraphManager(), nullptr);
  auto sink = ResolveSink(executor->GetGraphManager());
  ASSERT_NE(sink, nullptr);
  const auto result = executor->Execute();
  EXPECT_TRUE(result.success) << result.message;
  const auto diagnostics = sink->GetDiagnostics().Raw();
  EXPECT_TRUE(diagnostics.at("preamble_lock").get<bool>())
      << diagnostics.dump(2);
  EXPECT_EQ(diagnostics.at("rejected_count").get<std::size_t>(), 0u);
  const auto truth_json = ReadJson(truth);
  const auto &truth_pulses = truth_json.at("truth_pulses");
  const auto &decoded_pulses = diagnostics.at("decoded_pulses");
  ASSERT_EQ(decoded_pulses.size(), truth_pulses.size());
  for (std::size_t index = 0; index < truth_pulses.size(); ++index) {
    const auto &truth_pulse = truth_pulses.at(index);
    const auto &decoded = decoded_pulses.at(index);
    EXPECT_EQ(decoded.at("global_start_sample"),
              truth_pulse.at("received_global_start_sample"));
    EXPECT_EQ(decoded.at("duration_samples"),
              truth_pulse.at("duration_samples"));
    EXPECT_EQ(decoded.at("frequency_index"), truth_pulse.at("frequency_index"));
    EXPECT_EQ(decoded.at("decoded_value"), truth_pulse.at("word"));
    EXPECT_EQ(decoded.at("rf_frequency_hz"), truth_pulse.at("rf_frequency_hz"));
    EXPECT_EQ(decoded.at("iq_offset_frequency_hz"),
              truth_pulse.at("iq_offset_frequency_hz"));
    EXPECT_EQ(truth_pulse.at("role"), index < 16u ? "preamble" : "body");
  }
}
