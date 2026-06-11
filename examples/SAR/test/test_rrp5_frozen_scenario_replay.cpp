#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#ifndef SAR_RRP5_FROZEN_SCENARIO_REPLAY_GUIDE_PATH
#define SAR_RRP5_FROZEN_SCENARIO_REPLAY_GUIDE_PATH "examples/SAR/tools/rrp5_frozen_scenario_replay.md"
#endif

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open guide file: " << path;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(Rrp5FrozenScenarioReplayTest, GuideDescribesExactLocalSetupAndArtifactLayout) {
    const auto guide_path = std::filesystem::path{SAR_RRP5_FROZEN_SCENARIO_REPLAY_GUIDE_PATH};
    ASSERT_TRUE(std::filesystem::exists(guide_path));

    const auto guide = ReadText(guide_path);

    EXPECT_NE(guide.find("# RRP5 Frozen Scenario Replay Guide"), std::string::npos);
    EXPECT_NE(guide.find("export GOTCHA_DIR=/path/to/unpacked/GOTCHA"), std::string::npos);
    EXPECT_NE(guide.find("export GOTCHA_BACK_BIN=/path/to/gotcha-back/sarbp"), std::string::npos);
    EXPECT_NE(guide.find("python3 examples/SAR/tools/rrp1_local_runner.py"), std::string::npos);
    EXPECT_NE(guide.find("/tmp/graphx_rrp1_scenario_001/graphx/run_graphx.sh"), std::string::npos);
    EXPECT_NE(guide.find("/tmp/graphx_rrp1_scenario_001/reference/run_gotcha_back.sh"), std::string::npos);
    EXPECT_NE(guide.find("examples/SAR/tools/rrp3_gotcha_back_adapter.py"), std::string::npos);
    EXPECT_NE(guide.find("examples/SAR/tools/rrp4_image_comparator.py"), std::string::npos);
    EXPECT_NE(guide.find("<output-dir>/\n  manifest/\n    scenario_001.json"), std::string::npos);
    EXPECT_NE(guide.find("reports/image_comparison_report.json"), std::string::npos);
}

TEST(Rrp5FrozenScenarioReplayTest, GuideStatesReplayExpectationsAndScopeBoundaries) {
    const auto guide_path = std::filesystem::path{SAR_RRP5_FROZEN_SCENARIO_REPLAY_GUIDE_PATH};
    ASSERT_TRUE(std::filesystem::exists(guide_path));

    const auto guide = ReadText(guide_path);

    EXPECT_NE(guide.find("scenario_001.json is immutable"), std::string::npos);
    EXPECT_NE(guide.find("matching artifacts produce `pass`; mismatched pixels produce `fail`"), std::string::npos);
    EXPECT_NE(guide.find("does not download external data"), std::string::npos);
    EXPECT_NE(guide.find("does not clone gotcha-back"), std::string::npos);
    EXPECT_NE(guide.find("does not change SAR math"), std::string::npos);
    EXPECT_NE(guide.find("does not alter accel-token architecture"), std::string::npos);
    EXPECT_NE(guide.find("does not introduce a CI dependency on GOTCHA data"), std::string::npos);
}