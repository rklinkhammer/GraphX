#include "dsp/fhss/FHSSBinaryIqFileSourceNode.hpp"

#include <array>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

class ScopedBinaryIqFile {
public:
  explicit ScopedBinaryIqFile(const std::string &name)
      : path_(std::filesystem::temp_directory_path() / name) {}

  ~ScopedBinaryIqFile() { std::filesystem::remove(path_); }

  void WriteCf32Le(const std::vector<std::complex<float>> &samples) const {
    Write(samples);
  }

  void WriteCf64Le(const std::vector<std::complex<double>> &samples) const {
    Write(samples);
  }

  void WriteBytes(const std::vector<std::byte> &bytes) const {
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
  }

  void WriteEmpty() const { WriteBytes({}); }

  template <typename Float>
  void Write(const std::vector<std::complex<Float>> &samples) const {
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    for (const auto sample : samples) {
      WriteScalar(output, sample.real());
      WriteScalar(output, sample.imag());
    }
    ASSERT_TRUE(output.good());
  }

  [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

private:
  template <typename Float>
  static void WriteScalar(std::ofstream &output, Float value) {
    using UInt =
        std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
    const auto bits = std::bit_cast<UInt>(value);
    for (std::size_t i = 0; i < sizeof(bits); ++i) {
      const auto byte = static_cast<char>((bits >> (8u * i)) & 0xffu);
      output.write(&byte, 1);
    }
  }

  std::filesystem::path path_;
};

class ScopedDirectory {
public:
  explicit ScopedDirectory(const std::string &name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directories(path_);
  }
  ~ScopedDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

private:
  std::filesystem::path path_;
};

TEST(FHSSBinaryIqFileSourceTest, ReadsCf32LeAndPublishesFhssEvidence) {
  const ScopedBinaryIqFile input("graphx_fhss_binary_source_cf32_le.iq");
  input.WriteCf32Le({{1.0F, -2.0F}, {0.25F, 0.5F}, {-3.0F, 4.0F}});

  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = input.Path();
  config.input_packet_global_start_sample = 12'345;
  config.first_complex_sample = 1;
  config.max_complex_samples = 1;
  dsp::fhss::FHSSBinaryIqFileSourceNode source(config);

  const auto token = source.Produce(std::integral_constant<std::size_t, 0>{});

  ASSERT_TRUE(token.has_value());
  ASSERT_TRUE(token->sidecar.iq.host_complex64_samples);
  ASSERT_EQ(token->sidecar.iq.host_complex64_samples->size(), 1u);
  EXPECT_EQ(token->sidecar.iq.host_complex64_samples->front(),
            std::complex<double>(0.25, 0.5));
  EXPECT_EQ(token->sidecar.iq.sample_count, 1u);
  EXPECT_EQ(token->sidecar.iq.sample_time_map.input_packet_global_start_sample,
            12'345u);
  EXPECT_DOUBLE_EQ(token->sidecar.iq.sample_time_map.input_sample_rate_hz,
                   500'000'000.0);
  EXPECT_TRUE(
      std::holds_alternative<graph::EdgeEndOfStream>(token->edge_control));
  EXPECT_FALSE(
      source.Produce(std::integral_constant<std::size_t, 0>{}).has_value());
}

TEST(FHSSBinaryIqFileSourceTest, ReadsCf64LeExactly) {
  const ScopedBinaryIqFile input("graphx_fhss_binary_source_cf64_le.iq");
  input.WriteCf64Le({{1.25, -2.5}, {-0.0, 0.125}});
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = input.Path();
  config.sample_format = dsp::fhss::FHSSBinaryIqSampleFormat::Cf64Le;

  const auto samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  ASSERT_EQ(samples->size(), 2u);
  EXPECT_EQ((*samples)[0], std::complex<double>(1.25, -2.5));
  EXPECT_EQ((*samples)[1], std::complex<double>(-0.0, 0.125));
}

TEST(FHSSBinaryIqFileSourceTest,
     DecodesLiteralCrossPlatformGoldenVectorsForBothFormats) {
  const std::array<std::byte, 16> cf32_bytes{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc0},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xbe}};
  std::istringstream cf32_stream(
      std::string(reinterpret_cast<const char *>(cf32_bytes.data()),
                  cf32_bytes.size()),
      std::ios::binary);
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = "literal-cf32-golden";
  config.max_read_complex_samples = 2;
  auto decoded =
      dsp::fhss::ReadFHSSBinaryIqStream(cf32_stream, cf32_bytes.size(), config);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded,
            (std::vector<std::complex<double>>{{1.0, -2.0}, {0.5, -0.25}}));

  const std::array<std::byte, 32> cf64_bytes{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xf0}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc0},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xe0}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xd0}, std::byte{0xbf}};
  std::istringstream cf64_stream(
      std::string(reinterpret_cast<const char *>(cf64_bytes.data()),
                  cf64_bytes.size()),
      std::ios::binary);
  config.file_path = "literal-cf64-golden";
  config.sample_format = dsp::fhss::FHSSBinaryIqSampleFormat::Cf64Le;
  decoded =
      dsp::fhss::ReadFHSSBinaryIqStream(cf64_stream, cf64_bytes.size(), config);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded,
            (std::vector<std::complex<double>>{{1.0, -2.0}, {0.5, -0.25}}));
}

TEST(FHSSBinaryIqFileSourceTest, AcceptsEmptyAndOneSampleFiles) {
  const ScopedBinaryIqFile empty("graphx_fhss_binary_source_empty.iq");
  empty.WriteEmpty();
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = empty.Path();
  auto samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_TRUE(samples->empty());

  const ScopedBinaryIqFile one("graphx_fhss_binary_source_one.iq");
  one.WriteCf32Le({{-1.0F, 2.0F}});
  config.file_path = one.Path();
  samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  ASSERT_EQ(samples->size(), 1u);
  EXPECT_EQ(samples->front(), std::complex<double>(-1.0, 2.0));
}

TEST(FHSSBinaryIqFileSourceTest, RejectsMissingAndMisalignedFiles) {
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = std::filesystem::temp_directory_path() /
                     "graphx_fhss_binary_source_does_not_exist.iq";
  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value());

  const ScopedBinaryIqFile malformed("graphx_fhss_binary_source_partial.iq");
  malformed.WriteBytes({std::byte{0}, std::byte{1}, std::byte{2}});
  config.file_path = malformed.Path();
  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value());
}

TEST(FHSSBinaryIqFileSourceTest, RejectsUnreadableNonRegularPath) {
  const ScopedDirectory directory("graphx_fhss_binary_source_directory.iq");
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = directory.Path();
  const auto result = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("not a readable regular file"),
            std::string::npos);
}

TEST(FHSSBinaryIqFileSourceTest,
     OpenedStreamSnapshotSurvivesActualOnDiskPathReplacement) {
  const ScopedBinaryIqFile original("graphx_fhss_binary_source_snapshot.iq");
  const ScopedBinaryIqFile replacement(
      "graphx_fhss_binary_source_snapshot_replacement.iq");
  const ScopedBinaryIqFile retired(
      "graphx_fhss_binary_source_snapshot_retired.iq");
  original.WriteCf32Le({{1.0F, 2.0F}});
  replacement.WriteCf32Le({{9.0F, 10.0F}});
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = original.Path();
  config.max_read_complex_samples = 1;

  const auto result = dsp::fhss::ReadFHSSBinaryIqFile(config, [&] {
    // The preferred rename atomically replaces the directory entry on POSIX.
    // Platforms that do not replace an existing destination use a guarded
    // retire/install sequence, with rollback if installation fails.
    std::error_code error;
    std::filesystem::rename(replacement.Path(), original.Path(), error);
    if (!error)
      return;

    error.clear();
    std::filesystem::rename(original.Path(), retired.Path(), error);
    if (error) {
      throw std::runtime_error("failed to retire opened snapshot path: " +
                               error.message());
    }
    std::filesystem::rename(replacement.Path(), original.Path(), error);
    if (error) {
      std::error_code rollback_error;
      std::filesystem::rename(retired.Path(), original.Path(), rollback_error);
      throw std::runtime_error("failed to install snapshot replacement: " +
                               error.message());
    }
  });
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ(result->front(), std::complex<double>(1.0, 2.0));

  const auto fresh = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(fresh.has_value());
  ASSERT_EQ(fresh->size(), 1u);
  EXPECT_EQ(fresh->front(), std::complex<double>(9.0, 10.0));
  EXPECT_FALSE(std::filesystem::exists(replacement.Path()));

  std::error_code ignored;
  std::filesystem::remove(retired.Path(), ignored);
  EXPECT_FALSE(std::filesystem::exists(retired.Path()));
}

TEST(FHSSBinaryIqFileSourceTest, RejectsFileTruncatedAfterSizeObservation) {
  std::istringstream truncated(std::string(8, '\0'), std::ios::binary);
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = "observed-size-only.iq";
  config.max_read_complex_samples = 2;
  const auto result = dsp::fhss::ReadFHSSBinaryIqStream(truncated, 16u, config);
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("truncated"), std::string::npos);
}

TEST(FHSSBinaryIqFileSourceTest, SelectsBoundedRangesAndHandlesEof) {
  const ScopedBinaryIqFile input("graphx_fhss_binary_source_ranges.iq");
  input.WriteCf32Le({{1.0F, 2.0F}, {3.0F, 4.0F}, {5.0F, 6.0F}});
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = input.Path();
  config.first_complex_sample = 1;
  config.max_complex_samples = 99;
  auto samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  ASSERT_EQ(samples->size(), 2u);
  EXPECT_EQ(samples->front(), std::complex<double>(3.0, 4.0));

  config.first_complex_sample = 3;
  samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_TRUE(samples->empty());

  config.first_complex_sample = 4;
  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value());
  config.first_complex_sample = std::numeric_limits<std::uint64_t>::max();
  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value());
}

TEST(FHSSBinaryIqFileSourceTest, EnforcesAllocationBoundBeforeReading) {
  const ScopedBinaryIqFile input("graphx_fhss_binary_source_bound.iq");
  input.WriteCf32Le({{1.0F, 2.0F}, {3.0F, 4.0F}, {5.0F, 6.0F}});
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = input.Path();
  config.max_read_complex_samples = 2;

  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value())
      << "the default whole-file selection must still obey the safety bound";
  config.max_complex_samples = 2;
  auto samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_EQ(samples->size(), 2u);

  config.max_complex_samples = 3;
  EXPECT_FALSE(dsp::fhss::ReadFHSSBinaryIqFile(config).has_value());
  config.max_read_complex_samples = 3;
  samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_EQ(samples->size(), 3u);

  config.max_complex_samples = 99;
  samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_EQ(samples->size(), 3u);
}

TEST(FHSSBinaryIqFileSourceTest, RejectsDisabledBoundAndReadErrorOnlyOnce) {
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = "does-not-matter.iq";
  config.max_read_complex_samples = 0;
  EXPECT_FALSE(
      dsp::fhss::ValidateFHSSBinaryIqFileSourceConfig(config).has_value());

  config.max_read_complex_samples = 1;
  config.file_path = std::filesystem::temp_directory_path() /
                     "graphx_fhss_binary_source_missing_twice.iq";
  dsp::fhss::FHSSBinaryIqFileSourceNode source(config);
  EXPECT_FALSE(
      source.Produce(std::integral_constant<std::size_t, 0>{}).has_value());
  EXPECT_FALSE(
      source.Produce(std::integral_constant<std::size_t, 0>{}).has_value());
}

TEST(FHSSBinaryIqFileSourceTest, SupportsAbsoluteAndRelativePaths) {
  const ScopedBinaryIqFile input("graphx_fhss_binary_source_paths.iq");
  input.WriteCf32Le({{7.0F, -8.0F}});
  dsp::fhss::FHSSBinaryIqFileSourceConfig config{};
  config.file_path = std::filesystem::absolute(input.Path());
  auto samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_EQ(samples->front(), std::complex<double>(7.0, -8.0));

  config.file_path =
      std::filesystem::relative(input.Path(), std::filesystem::current_path());
  samples = dsp::fhss::ReadFHSSBinaryIqFile(config);
  ASSERT_TRUE(samples.has_value());
  EXPECT_EQ(samples->front(), std::complex<double>(7.0, -8.0));
}

TEST(FHSSBinaryIqFileSourceTest, ParsesMessageFreeFileSourceConfiguration) {
  const nlohmann::json json{
      {"file_path", "captures/example.cf32"},
      {"sample_format", "cf32_le"},
      {"sample_rate_hz", 500'000'000.0},
      {"bit_rate_hz", 5'000'000.0},
      {"bits_per_pulse", 32},
      {"pulse_gap_seconds", 6.6e-6},
      {"input_packet_global_start_sample", std::uint64_t{77}},
      {"first_complex_sample", std::uint64_t{10}},
      {"max_complex_samples", std::uint64_t{20}},
      {"max_read_complex_samples", std::uint64_t{200}}};

  const auto config =
      dsp::fhss::FHSSBinaryIqFileSourceConfigFromJson(graph::JsonView(json));

  EXPECT_EQ(config.file_path, "captures/example.cf32");
  EXPECT_EQ(config.sample_format, dsp::fhss::FHSSBinaryIqSampleFormat::Cf32Le);
  EXPECT_EQ(config.input_packet_global_start_sample, 77u);
  EXPECT_EQ(config.first_complex_sample, 10u);
  EXPECT_EQ(config.max_complex_samples, 20u);
  EXPECT_EQ(config.max_read_complex_samples, 200u);
  EXPECT_FALSE(json.contains("messages"));

  auto invalid = json;
  invalid["max_read_complex_samples"] = 0;
  EXPECT_THROW((void)dsp::fhss::FHSSBinaryIqFileSourceConfigFromJson(
                   graph::JsonView(invalid)),
               graph::ConfigError);
}

} // namespace
