#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SAR_EXAMPLE_EXECUTABLE_PATH
#define SAR_EXAMPLE_EXECUTABLE_PATH "./sar_example"
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_CRSD_GOTCHA_LOCAL_VALIDATION_CONFIG_JSON
#define SAR_CRSD_GOTCHA_LOCAL_VALIDATION_CONFIG_JSON "examples/SAR/config/sar_crsd_gotcha_local_validation.json"
#endif

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
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

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;
    nlohmann::json value;
    input >> value;
    return value;
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write json file: " << path;
    output << value.dump(2) << '\n';
}

std::vector<float> ReadFocusedPixels(const std::filesystem::path& binary_path,
                                     std::uint32_t width,
                                     std::uint32_t height) {
    std::ifstream input(binary_path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    const std::size_t pixel_bytes = static_cast<std::size_t>(width) * height * sizeof(float);
    if (size < pixel_bytes) {
        return {};
    }

    std::vector<float> pixels(static_cast<std::size_t>(width) * height, 0.0F);
    input.seekg(static_cast<std::streamoff>(size - pixel_bytes), std::ios::beg);
    input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixel_bytes));
    if (!input.good()) {
        return {};
    }
    return pixels;
}

class LocalGotchaValidationLaneTest : public ::testing::Test {
protected:
    [[nodiscard]] bool IsOptedIn() const {
        const char* env = std::getenv("GRAPHX_SAR_CRSD_ROOT");
        return env != nullptr && std::string{env}.size() > 0u;
    }

    [[nodiscard]] std::filesystem::path ResolveCrsdRoot() const {
        const char* env = std::getenv("GRAPHX_SAR_CRSD_ROOT");
        if (env != nullptr && std::string{env}.size() > 0u) {
            return std::filesystem::path{env};
        }
        return std::filesystem::path{GRAPHX_SOURCE_ROOT} / "data" / "crsd";
    }

    [[nodiscard]] std::vector<std::filesystem::path> OrderedSubDataProductPaths(
        const std::filesystem::path& root) const {
        std::vector<std::filesystem::path> ordered;
        ordered.reserve(10u);
        for (int i = 1; i <= 10; ++i) {
            const std::string idx = (i < 10) ? "0" + std::to_string(i) : std::to_string(i);
            ordered.push_back(
                root /
                ("subData" + idx + ".crsd_output") /
                "gotcha_crsd_chunk_0000.crsd" /
                "product.crsd");
        }
        return ordered;
    }

    [[nodiscard]] bool HasExpectedCrsdLayout(std::vector<std::filesystem::path>* paths_out) const {
        const auto root = ResolveCrsdRoot();
        const auto paths = OrderedSubDataProductPaths(root);
        if (paths_out != nullptr) {
            *paths_out = paths;
        }
        for (const auto& path : paths) {
            if (!std::filesystem::exists(path)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] int RunSarExample(const std::filesystem::path& config_path) const {
        const std::filesystem::path executable{SAR_EXAMPLE_EXECUTABLE_PATH};
        const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
        const std::string command =
            QuotePath(executable) + " " +
            QuotePath(config_path) + " " +
            QuotePath(plugin_dir) + " > /dev/null 2>&1";
        return std::system(command.c_str());
    }

    struct ArtifactSummary {
        bool ok{false};
        std::filesystem::path json_path{};
        std::filesystem::path bin_path{};
        std::filesystem::path pgm_path{};
        nlohmann::json artifact{};
        double max_abs_pixel{0.0};
    };

    [[nodiscard]] ArtifactSummary LoadArtifactSummary(const std::filesystem::path& output_dir) const {
        ArtifactSummary summary{};

        std::vector<std::filesystem::path> json_files;
        std::vector<std::filesystem::path> bin_files;
        std::vector<std::filesystem::path> pgm_files;

        for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto ext = entry.path().extension().string();
            if (ext == ".json") {
                json_files.push_back(entry.path());
            } else if (ext == ".bin") {
                bin_files.push_back(entry.path());
            } else if (ext == ".pgm") {
                pgm_files.push_back(entry.path());
            }
        }

        if (json_files.size() != 1u || bin_files.size() != 1u || pgm_files.size() != 1u) {
            return summary;
        }

        summary.json_path = json_files.front();
        summary.bin_path = bin_files.front();
        summary.pgm_path = pgm_files.front();
        summary.artifact = LoadJson(summary.json_path);

        const auto width = summary.artifact.at("shape").at("width").get<std::uint32_t>();
        const auto height = summary.artifact.at("shape").at("height").get<std::uint32_t>();
        const auto pixels = ReadFocusedPixels(summary.bin_path, width, height);
        if (pixels.empty()) {
            return summary;
        }

        for (const float pixel : pixels) {
            summary.max_abs_pixel = std::max(summary.max_abs_pixel, std::abs(static_cast<double>(pixel)));
        }

        summary.ok = true;
        return summary;
    }
};

TEST_F(LocalGotchaValidationLaneTest, OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet) {
    if (!IsOptedIn()) {
        GTEST_SKIP() << "GRAPHX_SAR_CRSD_ROOT is not set; PR9 local validation lane is opt-in";
    }

    std::vector<std::filesystem::path> baseline_paths;
    if (!HasExpectedCrsdLayout(&baseline_paths)) {
        GTEST_SKIP() << "Expected subData01..subData10 *.crsd_output/.../product.crsd layout not found under GRAPHX_SAR_CRSD_ROOT";
    }

    const auto config_template = std::filesystem::path{SAR_CRSD_GOTCHA_LOCAL_VALIDATION_CONFIG_JSON};
    ASSERT_TRUE(std::filesystem::exists(config_template)) << config_template;

    const auto temp_root = std::filesystem::temp_directory_path() / "graphx_pr9_local_validation";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_root));

    auto base_config = LoadJson(config_template);
    ASSERT_TRUE(base_config.contains("nodes"));

    auto write_run_config = [&](const std::string& run_name,
                                const std::vector<std::filesystem::path>& paths) {
        auto config = base_config;
        auto output_dir = temp_root / run_name;
        std::filesystem::remove_all(output_dir, error);
        std::filesystem::create_directories(output_dir, error);

        for (auto& node : config.at("nodes")) {
            const auto type = node.at("type").get<std::string>();
            if (type == "OrderedCrsdSetInputSourceNode") {
                nlohmann::json path_array = nlohmann::json::array();
                for (const auto& path : paths) {
                    path_array.push_back(path.string());
                }
                node["node_config"]["crsd_paths"] = std::move(path_array);
            } else if (type == "CrsdFocusedImageSinkNode") {
                node["node_config"]["output_dir"] = output_dir.string();
                node["node_config"]["artifact_stem"] = run_name;
            }
        }

        const auto config_path = temp_root / (run_name + ".json");
        WriteJson(config_path, config);
        return std::pair{config_path, output_dir};
    };

    const auto [baseline_config, baseline_output_dir] = write_run_config("baseline", baseline_paths);
    ASSERT_EQ(RunSarExample(baseline_config), 0) << "Baseline local validation run failed";

    const auto baseline = LoadArtifactSummary(baseline_output_dir);
    ASSERT_TRUE(baseline.ok) << "Expected one focused-image artifact set (.bin/.json/.pgm)";

    const auto& artifact = baseline.artifact;
    ASSERT_TRUE(artifact.contains("per_segment_input_hashes"));
    ASSERT_TRUE(artifact.contains("ordered_set_hash"));
    ASSERT_TRUE(artifact.contains("output_hash"));
    ASSERT_TRUE(artifact.contains("lineage"));

    const auto per_segment = artifact.at("per_segment_input_hashes").get<std::vector<std::uint64_t>>();
    EXPECT_EQ(per_segment.size(), baseline_paths.size());
    EXPECT_EQ(artifact.at("ordered_crsd_segments").size(), baseline_paths.size());
    EXPECT_TRUE(artifact.at("lineage").at("complete_aperture").get<bool>());
    EXPECT_EQ(artifact.at("lineage").at("segment_count").get<std::size_t>(), baseline_paths.size());

    const auto ordered_set_checksum = artifact.at("ordered_set_hash").get<std::uint64_t>();
    const auto output_checksum = artifact.at("output_hash").get<std::uint64_t>();
    EXPECT_GT(ordered_set_checksum, 0u);
    EXPECT_GT(output_checksum, 0u);
    EXPECT_GT(baseline.max_abs_pixel, 0.0) << "Focused image should contain nonzero magnitude response";

    auto dropped_paths = baseline_paths;
    dropped_paths.pop_back();
    const auto [drop_config, drop_output_dir] = write_run_config("drop_one_segment", dropped_paths);
    const int drop_exit = RunSarExample(drop_config);

    bool drop_different = (drop_exit != 0);
    if (drop_exit == 0) {
        const auto dropped = LoadArtifactSummary(drop_output_dir);
        ASSERT_TRUE(dropped.ok);
        const auto dropped_output_hash = dropped.artifact.at("output_hash").get<std::uint64_t>();
        drop_different = dropped_output_hash != output_checksum;
    }
    EXPECT_TRUE(drop_different)
        << "Dropping one segment must fail or produce a deterministically different output artifact";

    auto reordered_paths = baseline_paths;
    std::swap(reordered_paths[0], reordered_paths[1]);
    const auto [reorder_config, reorder_output_dir] = write_run_config("reordered_segments", reordered_paths);
    const int reorder_exit = RunSarExample(reorder_config);

    bool reorder_different = (reorder_exit != 0);
    if (reorder_exit == 0) {
        const auto reordered = LoadArtifactSummary(reorder_output_dir);
        ASSERT_TRUE(reordered.ok);
        const auto reordered_output_hash = reordered.artifact.at("output_hash").get<std::uint64_t>();
        reorder_different = reordered_output_hash != output_checksum;
    }
    EXPECT_TRUE(reorder_different)
        << "Reordering segments must fail or produce a deterministically different output artifact";
}

} // namespace
