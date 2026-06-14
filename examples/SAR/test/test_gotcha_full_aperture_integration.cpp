#include <gtest/gtest.h>

#include "sar/io/GotchaMatReader.hpp"
#include "sar/io/GraphxSarNormalizedIO.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

class GotchaFullApertureIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_full_aperture_integration_" + std::to_string(ticks));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] static std::filesystem::path FixtureDir() {
        return std::filesystem::path{__FILE__}.parent_path() / "fixtures" / "gotcha_full_aperture_synthetic";
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    [[nodiscard]] static nlohmann::json ReadJson(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream) << path;
        nlohmann::json value{};
        stream >> value;
        return value;
    }

    [[nodiscard]] static std::string ReadText(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream) << path;
        return std::string{
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
    }

    static void WriteMatStub(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary};
        ASSERT_TRUE(stream.good()) << path;
        const std::array<unsigned char, 8> signature{
            0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
        stream.write(reinterpret_cast<const char*>(signature.data()),
                     static_cast<std::streamsize>(signature.size()));
        stream << " synthetic mat v7.3 fixture";
    }

    static void MaterializeFixture(
        const std::filesystem::path& fixture_spec_path,
        const std::filesystem::path& output_dir) {
        const auto spec = ReadJson(fixture_spec_path);
        ASSERT_TRUE(spec.contains("files"));
        ASSERT_TRUE(spec.at("files").is_array());

        std::filesystem::create_directories(output_dir);
        for (const auto& file : spec.at("files")) {
            const auto mat_relative = file.at("path").get<std::string>();
            const auto mat_path = output_dir / mat_relative;
            WriteMatStub(mat_path);

            const auto ant_x = file.at("AntX").get<double>();
            const auto ant_y = file.at("AntY").get<double>();
            const auto ant_z = file.at("AntZ").get<double>();
            nlohmann::json sidecar{
                {"Np", file.at("Np")},
                {"K", file.at("K")},
                {"deltaF", file.at("deltaF")},
                {"minF", file.at("minF")},
                {"AntX", ant_x},
                {"AntY", ant_y},
                {"AntZ", ant_z},
                {"R0", file.at("R0")},
                {"phdata", "synthetic_phdata"},
                {"sample_rate_hz", 1000000.0},
                {"platform_position_m", nlohmann::json::array({ant_x, ant_y, ant_z})},
                {"platform_velocity_mps", nlohmann::json::array({0.1, 0.2, 0.3})},
                {"pulse_time_seconds", 0.0},
                {"range_sample_start", 0u},
                {"polarization", "HH"},
                {"iq_samples", file.at("iq_samples")},
                {"source_field_names", nlohmann::json{{"iq_samples", "DATA.IQ"}}},
            };

            const auto sidecar_path = std::filesystem::path{mat_path.string() + ".json"};
            std::ofstream stream{sidecar_path};
            ASSERT_TRUE(stream.good()) << sidecar_path;
            stream << sidecar.dump(2) << '\n';
        }
    }

    std::filesystem::path root_{};
};

TEST_F(GotchaFullApertureIntegrationTest, TwoFileFullApertureReadAndConvertToNormalized) {
    const auto fixture_spec = FixtureDir() / "2file_10pulse_each.json";
    const auto input_dir = Path("two_file_input");
    MaterializeFixture(fixture_spec, input_dir);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "fixture_2file",
            .product_id = "fixture_2file_product",
        }};

    const auto read = reader.ReadDetailed(input_dir);
    ASSERT_TRUE(read.success) << read.message;
    ASSERT_FALSE(read.product.channels.empty());
    EXPECT_EQ(read.product.channels[0].pulses.size(), 20u);

    const auto out_dir = Path("two_file_out");
    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto write = writer.Write(out_dir, read.product);
    ASSERT_TRUE(write.success) << write.message;

    const auto metadata = ReadJson(out_dir / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile);
    EXPECT_EQ(metadata.at("shape").at("pulse_count"), 20);
    EXPECT_EQ(metadata.at("collection").at("coordinate_frame"), "gotcha_local_cartesian");

    const auto waveform = metadata.at("channels").at(0).at("waveform");
    EXPECT_DOUBLE_EQ(waveform.at("carrier_hz").get<double>(), 9.593e9);
    EXPECT_DOUBLE_EQ(waveform.at("bandwidth_hz").get<double>(), 8.0e6);
    ASSERT_EQ(waveform.at("frequency_axis_hz").size(), 4u);

    const auto first_pulse = metadata.at("channels").at(0).at("pulses").at(0);
    const auto last_pulse = metadata.at("channels").at(0).at("pulses").at(19);
    EXPECT_DOUBLE_EQ(first_pulse.at("antenna_xyz").at(0).get<double>(), 10.0);
    EXPECT_DOUBLE_EQ(last_pulse.at("antenna_xyz").at(0).get<double>(), 20.0);
    EXPECT_DOUBLE_EQ(first_pulse.at("reference_range_m").get<double>(), 1010.0);
    EXPECT_DOUBLE_EQ(last_pulse.at("reference_range_m").get<double>(), 2020.0);
}

TEST_F(GotchaFullApertureIntegrationTest, TenFileFullApertureReadUsingManifestAndValidateCount) {
    const auto fixture_spec = FixtureDir() / "10file_5pulse_each.json";
    const auto fixture_manifest = FixtureDir() / "manifest.json";
    const auto fixture_checksums = FixtureDir() / "checksums.sha256";

    const auto input_dir = Path("ten_file_input");
    MaterializeFixture(fixture_spec, input_dir);

    const auto manifest_path = input_dir / "manifest.json";
    std::filesystem::copy_file(fixture_manifest, manifest_path, std::filesystem::copy_options::overwrite_existing);

    const auto checksums_text = ReadText(fixture_checksums);
    EXPECT_NE(checksums_text.find("10file_5pulse_each.json"), std::string::npos);
    EXPECT_NE(checksums_text.find("2file_10pulse_each.json"), std::string::npos);
    EXPECT_NE(checksums_text.find("manifest.json"), std::string::npos);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Manifest,
            .manifest_path = manifest_path,
            .collection_id = "fixture_10file",
            .product_id = "fixture_10file_product",
        }};

    const auto read = reader.ReadDetailed(input_dir);
    ASSERT_TRUE(read.success) << read.message;
    ASSERT_FALSE(read.product.channels.empty());
    EXPECT_EQ(read.product.channels[0].pulses.size(), 50u);

    const auto out_dir = Path("ten_file_out");
    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto write = writer.Write(out_dir, read.product);
    ASSERT_TRUE(write.success) << write.message;

    const auto metadata = ReadJson(out_dir / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile);
    EXPECT_EQ(metadata.at("shape").at("pulse_count"), 50);
    const auto waveform = metadata.at("channels").at(0).at("waveform");
    EXPECT_DOUBLE_EQ(waveform.at("bandwidth_hz").get<double>(), 3.0e6);
    ASSERT_EQ(waveform.at("frequency_axis_hz").size(), 3u);
}

TEST_F(GotchaFullApertureIntegrationTest, RepeatedFullApertureConversionIsDeterministic) {
    const auto fixture_spec = FixtureDir() / "10file_5pulse_each.json";
    const auto fixture_manifest = FixtureDir() / "manifest.json";
    const auto input_dir = Path("determinism_input");
    MaterializeFixture(fixture_spec, input_dir);

    const auto manifest_path = input_dir / "manifest.json";
    std::filesystem::copy_file(fixture_manifest, manifest_path, std::filesystem::copy_options::overwrite_existing);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Manifest,
            .manifest_path = manifest_path,
            .collection_id = "fixture_deterministic",
            .product_id = "fixture_deterministic_product",
        }};

    const auto read = reader.ReadDetailed(input_dir);
    ASSERT_TRUE(read.success) << read.message;

    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto out_a = Path("deterministic_a");
    const auto out_b = Path("deterministic_b");
    ASSERT_TRUE(writer.Write(out_a, read.product).success);
    ASSERT_TRUE(writer.Write(out_b, read.product).success);

    EXPECT_EQ(
        ReadJson(out_a / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile),
        ReadJson(out_b / graphx::sar::GraphxSarNormalizedWriter::kMetadataFile));
    EXPECT_EQ(
        ReadJson(out_a / graphx::sar::GraphxSarNormalizedWriter::kIndexFile),
        ReadJson(out_b / graphx::sar::GraphxSarNormalizedWriter::kIndexFile));
    EXPECT_EQ(
        ReadJson(out_a / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile),
        ReadJson(out_b / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile));

    const auto checksum_a = graphx::sar::GraphxSarNormalizedWriter::ComputeSignalChecksum(
        out_a / graphx::sar::GraphxSarNormalizedWriter::kSignalFile);
    const auto checksum_b = graphx::sar::GraphxSarNormalizedWriter::ComputeSignalChecksum(
        out_b / graphx::sar::GraphxSarNormalizedWriter::kSignalFile);
    EXPECT_EQ(checksum_a, checksum_b);
}

} // namespace
