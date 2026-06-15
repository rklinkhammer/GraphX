#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"
#include "sar/SarScenario001GraphxRunner.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_LOCAL_RUNNER_PATH
#define SAR_LOCAL_RUNNER_PATH "examples/SAR/tools/sar_local_runner.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_SCENARIO_001_FIXTURE_DATA_PATH
#define SAR_SCENARIO_001_FIXTURE_DATA_PATH "examples/SAR/fixtures/scenario_001/deterministic_iq_phase_history_fixture_v1.json"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

class ScopedExternalDataEnvGuard {
public:
    explicit ScopedExternalDataEnvGuard(const std::string& value) {
        const char* current = std::getenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
        had_original_ = current != nullptr;
        if (had_original_) {
            original_ = current;
        }
#ifdef _WIN32
        _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str());
#else
        setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str(), 1);
#endif
    }

    ~ScopedExternalDataEnvGuard() {
        if (had_original_) {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str());
#else
            setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", "");
#else
            unsetenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
#endif
        }
    }

private:
    bool had_original_{false};
    std::string original_{};
};

std::shared_ptr<sar::SarMaterializedImageSinkNode> ResolveMaterializedSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper || wrapper->GetType() != "SarMaterializedImageSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarMaterializedImageSinkNode>();
    }
    return nullptr;
}

struct GraphxRunCapture {
    std::vector<float> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string hash{};
    std::filesystem::path artifact_bin_path{};
    std::filesystem::path artifact_contract_path{};
};

GraphxRunCapture RunScenario001Graphx(const std::filesystem::path& output_dir) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto phase_fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};
    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};

    if (!std::filesystem::exists(scenario_path)) {
        throw std::invalid_argument("missing scenario file");
    }
    if (!std::filesystem::exists(phase_fixture_path)) {
        throw std::invalid_argument("missing phase-history fixture file");
    }
    if (!std::filesystem::exists(plugin_dir)) {
        throw std::invalid_argument("missing plugin directory");
    }

    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string scaffold_command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";
    if (std::system(scaffold_command.c_str()) != 0) {
        throw std::invalid_argument("sar_local_runner failed to scaffold output layout");
    }

    const auto phase_fixture = sar::scenario001::graphx::LoadJsonFile(phase_fixture_path);
    const auto gotcha_fixture = sar::scenario001::graphx::ConvertToGotchaReplayFixture(phase_fixture);

    const auto converted_fixture_path = output_dir / "graphx" / "scenario_001_graphx_fixture.json";
    sar::scenario001::graphx::WriteJson(converted_fixture_path, gotcha_fixture);

    auto config = sar::scenario001::graphx::LoadJsonFile(output_dir / "graphx" / "graphx_config.json");
    for (auto& node : config["nodes"]) {
        if (node.at("id").get<std::string>() == "src") {
            node["node_config"]["fixture_path"] = converted_fixture_path.string();
            node["node_config"]["allow_external_fixture"] = true;
        }
    }

    const auto runtime_config_path = output_dir / "graphx" / "graphx_runtime_config.json";
    sar::scenario001::graphx::WriteJson(runtime_config_path, config);

    ScopedExternalDataEnvGuard external_data_opt_in("1");

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(runtime_config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
                        .Build();
    if (!executor || !executor->GetGraphManager()) {
        throw std::invalid_argument("unable to construct graph executor");
    }

    const auto run_result = executor->Execute();
    if (!run_result.success || !executor->IsCompletionSignaled()) {
        throw std::invalid_argument("graph execution failed");
    }

    auto sink = ResolveMaterializedSink(executor->GetGraphManager());
    if (!sink || !sink->has_materialized_image()) {
        throw std::invalid_argument("materialized image sink did not capture output");
    }

    const auto metadata = sink->last_capture_metadata();
    if (metadata.element_count == 0u) {
        throw std::invalid_argument("materialized image metadata has zero element_count");
    }

    GraphxRunCapture out{};
    out.pixels = sink->last_materialized_image();
    out.width = static_cast<std::uint32_t>(metadata.element_count);
    out.height = 1u;
    out.hash = sar::scenario001::graphx::Fnv1a64Hex(out.pixels);

    const auto scenario_id = gotcha_fixture.at("derived_from_scenario").get<std::string>();
    const auto fixture_id = gotcha_fixture.at("source_fixture_id").get<std::string>();

    out.artifact_bin_path = output_dir / "graphx" / "graphx_output_runtime.bin";
    out.artifact_contract_path = output_dir / "graphx" / "graphx_output_contract_runtime.json";

    sar::scenario001::graphx::WriteFloat32Raster(out.artifact_bin_path, out.pixels);
    sar::scenario001::graphx::WriteJson(
        out.artifact_contract_path,
        sar::scenario001::graphx::BuildGraphxArtifactContract(
            scenario_id,
            fixture_id,
            out.width,
            out.height,
            out.artifact_bin_path,
            out.hash));

    return out;
}

} // namespace

TEST(GraphxScenarioRunnerContractTest, ExecutesScenario001AndEmitsGraphxArtifactContract) {
    const auto out_dir = std::filesystem::temp_directory_path() / "graphx_scenario_runner_contract";
    const auto capture = RunScenario001Graphx(out_dir);

    ASSERT_TRUE(std::filesystem::exists(capture.artifact_bin_path));
    ASSERT_TRUE(std::filesystem::exists(capture.artifact_contract_path));

    const auto contract = sar::scenario001::graphx::LoadJsonFile(capture.artifact_contract_path);
    EXPECT_EQ(contract.at("source_tool").get<std::string>(), "graphx");
    EXPECT_EQ(contract.at("provenance_class").get<std::string>(), "graphx_runtime");
    EXPECT_EQ(contract.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(contract.at("fixture_id").get<std::string>(), "deterministic_iq_phase_history_fixture_v1");
    EXPECT_EQ(contract.at("algorithm").get<std::string>(), "graphx_sar_pipeline");
    EXPECT_EQ(contract.at("format").get<std::string>(), "float32_raster");
    EXPECT_EQ(contract.at("layout").get<std::string>(), "row_major");
    EXPECT_EQ(contract.at("dtype").get<std::string>(), "float32");
    EXPECT_EQ(contract.at("artifact_kind").get<std::string>(), "materialized_image");
    EXPECT_EQ(contract.at("width").get<std::uint32_t>(), capture.width);
    EXPECT_EQ(contract.at("height").get<std::uint32_t>(), capture.height);
    EXPECT_EQ(contract.at("deterministic_hash").get<std::string>(), capture.hash);
}

TEST(GraphxScenarioRunnerContractTest, OutputIsFiniteNonZeroAndDeterministicAcrossRuns) {
    const auto out_dir_1 = std::filesystem::temp_directory_path() / "graphx_scenario_runner_contract_1";
    const auto out_dir_2 = std::filesystem::temp_directory_path() / "graphx_scenario_runner_contract_2";

    const auto first = RunScenario001Graphx(out_dir_1);
    const auto second = RunScenario001Graphx(out_dir_2);

    ASSERT_EQ(first.pixels.size(), second.pixels.size());
    EXPECT_EQ(first.width, second.width);
    EXPECT_EQ(first.height, second.height);
    EXPECT_EQ(first.hash, second.hash);

    bool has_nonzero = false;
    std::size_t first_peak = 0u;
    std::size_t second_peak = 0u;

    for (std::size_t i = 0; i < first.pixels.size(); ++i) {
        EXPECT_TRUE(std::isfinite(first.pixels[i]));
        EXPECT_TRUE(std::isfinite(second.pixels[i]));
        EXPECT_FLOAT_EQ(first.pixels[i], second.pixels[i]);
        has_nonzero = has_nonzero || (std::abs(first.pixels[i]) > 0.0f);

        if (first.pixels[i] > first.pixels[first_peak]) {
            first_peak = i;
        }
        if (second.pixels[i] > second.pixels[second_peak]) {
            second_peak = i;
        }
    }

    EXPECT_TRUE(has_nonzero);
    EXPECT_EQ(first_peak, second_peak);
}

TEST(GraphxScenarioRunnerContractTest, MissingRequiredFixtureFieldReportsExactFieldName) {
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    auto broken = sar::scenario001::graphx::LoadJsonFile(fixture_path);
    broken["geometry"].erase("carrier_hz");

    try {
        (void)sar::scenario001::graphx::ConvertToGotchaReplayFixture(broken);
        FAIL() << "expected missing-field failure";
    } catch (const std::invalid_argument& ex) {
        EXPECT_NE(std::string(ex.what()).find("fixture.geometry.carrier_hz"), std::string::npos);
    }
}