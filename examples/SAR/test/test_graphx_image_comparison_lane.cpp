#include <gtest/gtest.h>

#include "sar/SarMaterializedImageSinkNode.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH
#define SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH "tools/sarpy/reference_image_from_gotcha.py"
#endif

#ifndef SARPY_COMPARE_IMAGES_TOOL_PATH
#define SARPY_COMPARE_IMAGES_TOOL_PATH "tools/sarpy/compare_images.py"
#endif

namespace {

std::string QuotePath(const std::filesystem::path& path) {
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

std::string PythonCommand() {
    return "PYTHONDONTWRITEBYTECODE=1 python3 -B";
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input{path};
    EXPECT_TRUE(input.good()) << "unable to open JSON file: " << path;
    nlohmann::json value{};
    input >> value;
    return value;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

sar::SarAccelControlToken MakeMaterializedImageToken() {
    sar::SarAccelControlToken token{};
    token.token_id = 1701u;
    token.sidecar.sequence_id = 17u;
    token.sidecar.tile_id = 4u;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.payload_byte_count = 16u * sizeof(float);
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1701u));
    token.host_view.bytes = token.sidecar.payload_byte_count;
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 16u;
    token.host_view.layout.stride[0] = 1u;
    token.host_view.allocator_id = 1u;
    token.has_host_view = true;
    token.kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
    token.kernel_ticket.kernel_id = 17017u;
    token.kernel_ticket.execution_queue_id = 1u;
    token.kernel_ticket.completion_event = 1702u;
    token.kernel_ticket.arg_count = 2u;
    token.has_kernel_ticket = true;
    return token;
}

std::vector<float> BuildTinyGraphxImage() {
    sar::SarMaterializedImageSinkNode sink{};
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));

    auto output = sink.Transfer(
        MakeMaterializedImageToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    EXPECT_TRUE(output.has_value());
    EXPECT_EQ(sink.capture_count(), 1u);
    EXPECT_TRUE(sink.has_materialized_image());
    return sink.last_materialized_image();
}

void WriteComplexGridJson(const std::filesystem::path& path,
                          const std::vector<float>& real_values,
                          std::uint32_t width,
                          std::uint32_t height) {
    ASSERT_EQ(real_values.size(), static_cast<std::size_t>(width) * height);

    nlohmann::json real = nlohmann::json::array();
    nlohmann::json imag = nlohmann::json::array();
    for (std::uint32_t y = 0; y < height; ++y) {
        nlohmann::json real_row = nlohmann::json::array();
        nlohmann::json imag_row = nlohmann::json::array();
        for (std::uint32_t x = 0; x < width; ++x) {
            real_row.push_back(real_values.at(static_cast<std::size_t>(y) * width + x));
            imag_row.push_back(0.0);
        }
        real.push_back(std::move(real_row));
        imag.push_back(std::move(imag_row));
    }

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    ASSERT_TRUE(output.good()) << "unable to write JSON fixture: " << path;
    output << nlohmann::json{{"real", real}, {"imag", imag}}.dump(2) << '\n';
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    ASSERT_TRUE(output.good()) << "unable to write JSON file: " << path;
    output << value.dump(2) << '\n';
}

int RunCommand(const std::string& command) {
    return std::system(command.c_str());
}

class GraphxImageComparisonLaneTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_image_comparison_lane_" + std::to_string(ticks));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    bool HasPythonImageDependencies() const {
        const auto probe_json = Path("probe.json");
        const std::string command =
            PythonCommand() + " " + QuotePath(std::filesystem::path{SARPY_COMPARE_IMAGES_TOOL_PATH}) +
            " probe-environment --output-json " + QuotePath(probe_json) +
            " > /dev/null";
        EXPECT_EQ(RunCommand(command), 0);
        const auto probe = LoadJson(probe_json);
        return probe.at("packages").at("numpy").at("installed").get<bool>() &&
            probe.at("packages").at("matplotlib").at("installed").get<bool>();
    }

    void GenerateNpy(const std::filesystem::path& input_json,
                     const std::filesystem::path& output_npy,
                     const std::filesystem::path& output_png,
                     const std::filesystem::path& output_metadata) const {
        const std::string command =
            PythonCommand() + " " + QuotePath(std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH}) +
            " generate-reference --input-json " + QuotePath(input_json) +
            " --output-reference-npy " + QuotePath(output_npy) +
            " --output-magnitude-png " + QuotePath(output_png) +
            " --output-metadata-json " + QuotePath(output_metadata) +
            " > /dev/null";
        ASSERT_EQ(RunCommand(command), 0);
    }

    void CompareImages(const std::filesystem::path& report_path) const {
        const std::string command =
            PythonCommand() + " " + QuotePath(std::filesystem::path{SARPY_COMPARE_IMAGES_TOOL_PATH}) +
            " compare --reference-npy " + QuotePath(Path("python_reference/reference_image.npy")) +
            " --candidate-npy " + QuotePath(Path("graphx_candidate/candidate_image.npy")) +
            " --output-report-json " + QuotePath(report_path) +
            " --output-diff-magnitude-png " + QuotePath(Path("comparison/difference_magnitude.png")) +
            " --output-phase-difference-png " + QuotePath(Path("comparison/phase_difference.png")) +
            " --reference-metadata-json " + QuotePath(Path("python_reference/reference_metadata.json")) +
            " --candidate-metadata-json " + QuotePath(Path("graphx_candidate/candidate_metadata.json")) +
            " > /dev/null";
        ASSERT_EQ(RunCommand(command), 0);
    }

    std::filesystem::path root_{};
};

} // namespace

TEST_F(GraphxImageComparisonLaneTest, TinyGraphxImageMatchesPythonReferenceDeterministically) {
    if (!HasPythonImageDependencies()) {
        GTEST_SKIP() << "numpy/matplotlib not installed in local environment";
    }

    const auto graphx_image = BuildTinyGraphxImage();
    ASSERT_EQ(graphx_image.size(), 16u);

    std::filesystem::create_directories(Path("python_reference"));
    std::filesystem::create_directories(Path("graphx_candidate"));
    std::filesystem::create_directories(Path("comparison"));

    WriteComplexGridJson(Path("python_reference/reference_fixture.json"), graphx_image, 4u, 4u);
    WriteComplexGridJson(Path("graphx_candidate/graphx_output_fixture.json"), graphx_image, 4u, 4u);

    const nlohmann::json ordered_inputs = nlohmann::json::array({
        "segment_000/product.crsd",
        "segment_001/product.crsd",
        "segment_002/product.crsd",
    });
    const nlohmann::json per_segment_checksums = nlohmann::json::array({
        "a111", "b222", "c333",
    });
    const nlohmann::json geometry = nlohmann::json{
        {"frame", "range_azimuth"},
        {"pixel_spacing_m", 1.5},
        {"scene_center_x_m", 0.0},
        {"scene_center_y_m", 0.0}
    };

    GenerateNpy(
        Path("python_reference/reference_fixture.json"),
        Path("python_reference/reference_image.npy"),
        Path("python_reference/reference_magnitude.png"),
        Path("python_reference/reference_metadata.json"));
    GenerateNpy(
        Path("graphx_candidate/graphx_output_fixture.json"),
        Path("graphx_candidate/candidate_image.npy"),
        Path("graphx_candidate/candidate_magnitude.png"),
        Path("graphx_candidate/candidate_metadata.json"));

    WriteJson(
        Path("python_reference/reference_metadata.json"),
        nlohmann::json{
            {"ordered_crsd_inputs", ordered_inputs},
            {"ordered_crsd_input_checksums", per_segment_checksums},
            {"ordered_set_hash", "sethash001"},
            {"focused_output_sha256", "referencehash001"},
            {"algorithm", "independent_local_focused_surrogate_fft"},
            {"geometry_assumptions", geometry}
        });

    WriteJson(
        Path("graphx_candidate/candidate_metadata.json"),
        nlohmann::json{
            {"ordered_crsd_inputs", ordered_inputs},
            {"per_segment_input_hashes", per_segment_checksums},
            {"ordered_set_hash", "sethash001"},
            {"output_hash", "graphxhash001"},
            {"algorithm", "graphx_crsd_backprojection"},
            {"geometry_assumptions", geometry}
        });

    CompareImages(Path("comparison/comparison_report.json"));
    const auto first_report = LoadJson(Path("comparison/comparison_report.json"));
    const auto first_diff_png = ReadText(Path("comparison/difference_magnitude.png"));
    const auto first_phase_png = ReadText(Path("comparison/phase_difference.png"));

    CompareImages(Path("comparison/comparison_report.json"));
    const auto second_report = LoadJson(Path("comparison/comparison_report.json"));
    const auto second_diff_png = ReadText(Path("comparison/difference_magnitude.png"));
    const auto second_phase_png = ReadText(Path("comparison/phase_difference.png"));

    ASSERT_TRUE(std::filesystem::exists(Path("comparison/comparison_report.json")));
    ASSERT_TRUE(std::filesystem::exists(Path("comparison/difference_magnitude.png")));
    ASSERT_TRUE(std::filesystem::exists(Path("comparison/phase_difference.png")));
    EXPECT_FALSE(first_diff_png.empty());
    EXPECT_FALSE(first_phase_png.empty());
    EXPECT_EQ(first_report, second_report);
    EXPECT_EQ(first_diff_png, second_diff_png);
    EXPECT_EQ(first_phase_png, second_phase_png);

    EXPECT_EQ(first_report.at("schema"), "graphx.sar.image_comparison_report.v3");
    EXPECT_TRUE(first_report.at("local_only").get<bool>());
    EXPECT_EQ(first_report.at("shape"), nlohmann::json::array({4, 4}));
    EXPECT_EQ(
        std::filesystem::path{first_report.at("difference_magnitude_png").get<std::string>()}.filename(),
        std::filesystem::path{"difference_magnitude.png"});
    EXPECT_EQ(
        std::filesystem::path{first_report.at("phase_difference_png").get<std::string>()}.filename(),
        std::filesystem::path{"phase_difference.png"});

    const auto metrics = first_report.at("metrics");
    EXPECT_NEAR(metrics.at("rmse_magnitude").get<double>(), 0.0, 1.0e-9);
    EXPECT_NEAR(metrics.at("phase_rmse_radians").get<double>(), 0.0, 1.0e-9);
    EXPECT_NEAR(metrics.at("peak_error_magnitude").get<double>(), 0.0, 1.0e-9);
    EXPECT_NEAR(metrics.at("magnitude_correlation").get<double>(), 1.0, 1.0e-9);
    ASSERT_TRUE(metrics.contains("ssim_magnitude"));
    EXPECT_NEAR(metrics.at("ssim_magnitude").get<double>(), 1.0, 1.0e-9);

    const auto lineage = first_report.at("lineage");
    EXPECT_EQ(
        lineage.at("ordered_crsd_inputs").at("graphx"),
        lineage.at("ordered_crsd_inputs").at("reference"));
    EXPECT_EQ(
        lineage.at("ordered_set_checksum").at("graphx").get<std::string>(),
        "sethash001");
    EXPECT_TRUE(lineage.at("ordered_set_checksum").at("match").get<bool>());
    EXPECT_EQ(lineage.at("graphx_output_hash").get<std::string>(), "graphxhash001");
    EXPECT_EQ(lineage.at("reference_output_hash").get<std::string>(), "referencehash001");
    EXPECT_EQ(
        lineage.at("algorithm").at("graphx").get<std::string>(),
        "graphx_crsd_backprojection");
}
