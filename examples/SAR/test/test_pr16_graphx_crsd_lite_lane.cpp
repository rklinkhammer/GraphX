#include <gtest/gtest.h>

#include "sar/io/GraphxCrsdLiteIO.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH
#define SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH "./graphx-gotcha-to-crsd"
#endif

namespace {

std::string ShellQuote(const std::filesystem::path& path) {
    std::string raw = path.string();
    std::string quoted{"'"};
    for (const char ch : raw) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

struct CommandResult {
    int exit_code{};
    std::string output{};
};

CommandResult RunCommand(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output{};
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return CommandResult{.exit_code = -1, .output = "popen failed"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    return CommandResult{.exit_code = pclose(pipe), .output = output};
}

class Pr16GraphxCrsdLiteLaneTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_pr16_lite_lane_" + std::to_string(ticks));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    [[nodiscard]] std::string CliBase() const {
        return ShellQuote(std::filesystem::path{SAR_GOTCHA_TO_CRSD_EXECUTABLE_PATH});
    }

    void WriteMatStub(const std::string& relative) const {
        const auto path = Path(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary};
        ASSERT_TRUE(stream.good());
        const std::array<unsigned char, 8> signature{
            0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
        stream.write(reinterpret_cast<const char*>(signature.data()),
                     static_cast<std::streamsize>(signature.size()));
        stream << " synthetic mat v7.3 fixture";
    }

    void WriteSidecar(const std::string& mat_relative, int base) const {
        const auto sidecar_path = Path(mat_relative + ".json");
        std::filesystem::create_directories(sidecar_path.parent_path());

        const nlohmann::json sidecar{
            {"carrier_hz", 9.6e9},
            {"bandwidth_hz", 640.0e6},
            {"sample_rate_hz", 1.0e9},
            {"frequency_axis_hz", nlohmann::json::array({9.599e9, 9.600e9, 9.601e9})},
            {"platform_position_m", nlohmann::json::array({
                                        static_cast<double>(base),
                                        static_cast<double>(base + 1),
                                        static_cast<double>(base + 2),
                                    })},
            {"platform_velocity_mps", nlohmann::json::array({0.1, 0.2, 0.3})},
            {"pulse_time_seconds", static_cast<double>(base) * 0.5},
            {"range_sample_start", static_cast<std::uint64_t>(base * 10)},
            {"polarization", "HH"},
            {"iq_samples", nlohmann::json::array({
                               nlohmann::json{{"real", static_cast<float>(base + 1)}, {"imag", -1.0f}},
                               nlohmann::json{{"real", static_cast<float>(base + 2)}, {"imag", -2.0f}},
                               nlohmann::json{{"real", static_cast<float>(base + 3)}, {"imag", -3.0f}},
                           })},
            {"source_field_names", nlohmann::json{
                                       {"iq_samples", "DATA.IQ"},
                                       {"platform_position_m", "DATA.AntXyz"},
                                   }},
        };

        std::ofstream stream{sidecar_path};
        ASSERT_TRUE(stream.good());
        stream << sidecar.dump(2) << '\n';
    }

    void WriteTinySyntheticInput() const {
        WriteMatStub("input/pulse_02.mat");
        WriteSidecar("input/pulse_02.mat", 20);
        WriteMatStub("input/pulse_01.mat");
        WriteSidecar("input/pulse_01.mat", 10);
    }

    [[nodiscard]] CommandResult RunLiteConversion(const std::string& output_relative) const {
        const auto command = CliBase() +
            " --input-dir " + ShellQuote(Path("input")) +
            " --output-dir " + ShellQuote(Path(output_relative)) +
            " --collection-id pr16-tiny-collection"
            " --max-output-size-mb 1"
            " --sort lexical"
            " --mode graphx-crsd-lite"
            " --validate"
            " --emit-index 2>&1";
        return RunCommand(command);
    }

    [[nodiscard]] static nlohmann::json ReadJson(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream.good()) << path;
        nlohmann::json value{};
        stream >> value;
        return value;
    }

    [[nodiscard]] static std::string ReadText(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream.good()) << path;
        return std::string{
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}};
    }

    std::filesystem::path root_{};
};

} // namespace

TEST_F(Pr16GraphxCrsdLiteLaneTest, EndToEndTinySyntheticConversionEmitsReportsAndChecksums) {
    WriteTinySyntheticInput();

    const auto result = RunLiteConversion("run_a");
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("conversion_successful: mode=graphx-crsd-lite"), std::string::npos);

    const auto output_dir = Path("run_a");
    const auto chunk_dir = output_dir / "gotcha_crsd_chunk_0000.graphx-crsd-lite";
    const auto signal_path = chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kSignalFile;

    EXPECT_TRUE(std::filesystem::exists(signal_path));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kMetadataFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kIndexFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kConversionReportFile));
    EXPECT_TRUE(std::filesystem::exists(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kWarningsLogFile));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "gotcha_crsd_index.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "conversion_report.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "conversion_warnings.log"));

    const auto metadata = ReadJson(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kMetadataFile);
    const auto chunk_index = ReadJson(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kIndexFile);
    const auto chunk_report = ReadJson(chunk_dir / graphx::sar::GraphxCrsdLiteWriter::kConversionReportFile);
    const auto root_index = ReadJson(output_dir / "gotcha_crsd_index.json");
    const auto root_report = ReadJson(output_dir / "conversion_report.json");
    const auto checksum = graphx::sar::GraphxCrsdLiteWriter::ComputeSignalChecksum(signal_path);

    EXPECT_EQ(metadata.at("format"), "graphx-crsd-lite");
    EXPECT_EQ(metadata.at("label"), "NON-STANDARD");
    EXPECT_EQ(metadata.at("shape").at("pulse_count"), 2);
    EXPECT_EQ(metadata.at("shape").at("channel_count"), 1);
    EXPECT_EQ(metadata.at("shape").at("max_sample_count"), 3);
    EXPECT_EQ(chunk_index.at("signal_checksum_fnv1a64"), checksum);
    EXPECT_EQ(chunk_report.at("outputs").at(0).at("checksum_fnv1a64"), checksum);
    EXPECT_EQ(root_index.at("outputs").at(0).at("checksum_fnv1a64"), checksum);
    EXPECT_EQ(root_report.at("outputs").at(0).at("checksum_fnv1a64"), checksum);
    EXPECT_EQ(root_report.at("format"), "graphx-crsd-lite");
    EXPECT_EQ(root_report.at("label"), "NON-STANDARD");
    EXPECT_EQ(root_report.at("selected_mode"), "graphx-crsd-lite");
    EXPECT_EQ(root_report.at("validation_status"), "ok");
    EXPECT_EQ(root_index.at("outputs").at(0).at("pulse_range").at("start"), 0);
    EXPECT_EQ(root_index.at("outputs").at(0).at("pulse_range").at("end"), 1);
    EXPECT_EQ(ReadText(output_dir / "conversion_warnings.log"), "none\n");
}

TEST_F(Pr16GraphxCrsdLiteLaneTest, RepeatedTinySyntheticConversionIsDeterministic) {
    WriteTinySyntheticInput();

    const auto first = RunLiteConversion("run_a");
    ASSERT_EQ(first.exit_code, 0) << first.output;
    const auto second = RunLiteConversion("run_b");
    ASSERT_EQ(second.exit_code, 0) << second.output;

    const auto chunk_a = Path("run_a/gotcha_crsd_chunk_0000.graphx-crsd-lite");
    const auto chunk_b = Path("run_b/gotcha_crsd_chunk_0000.graphx-crsd-lite");

    EXPECT_EQ(
        ReadJson(Path("run_a/gotcha_crsd_index.json")),
        ReadJson(Path("run_b/gotcha_crsd_index.json")));
    EXPECT_EQ(
        ReadJson(Path("run_a/conversion_report.json")),
        ReadJson(Path("run_b/conversion_report.json")));
    EXPECT_EQ(
        ReadJson(chunk_a / graphx::sar::GraphxCrsdLiteWriter::kMetadataFile),
        ReadJson(chunk_b / graphx::sar::GraphxCrsdLiteWriter::kMetadataFile));
    EXPECT_EQ(
        ReadJson(chunk_a / graphx::sar::GraphxCrsdLiteWriter::kIndexFile),
        ReadJson(chunk_b / graphx::sar::GraphxCrsdLiteWriter::kIndexFile));
    EXPECT_EQ(
        ReadJson(chunk_a / graphx::sar::GraphxCrsdLiteWriter::kConversionReportFile),
        ReadJson(chunk_b / graphx::sar::GraphxCrsdLiteWriter::kConversionReportFile));
    EXPECT_EQ(
        ReadText(Path("run_a/conversion_warnings.log")),
        ReadText(Path("run_b/conversion_warnings.log")));
    EXPECT_EQ(
        graphx::sar::GraphxCrsdLiteWriter::ComputeSignalChecksum(
            chunk_a / graphx::sar::GraphxCrsdLiteWriter::kSignalFile),
        graphx::sar::GraphxCrsdLiteWriter::ComputeSignalChecksum(
            chunk_b / graphx::sar::GraphxCrsdLiteWriter::kSignalFile));
}
