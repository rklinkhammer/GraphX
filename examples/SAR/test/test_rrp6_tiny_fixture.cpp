#include <gtest/gtest.h>

#include "sar/GotchaReplaySourceNode.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_RRP6_TINY_GOTCHA_FIXTURE_PATH
#define SAR_RRP6_TINY_GOTCHA_FIXTURE_PATH "examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json"
#endif

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;

    nlohmann::json value;
    input >> value;
    return value;
}

} // namespace

TEST(Rrp6TinyFixtureTest, TinyFixtureIsTraceableToScenario001AndCiSafe) {
    const auto fixture_path = std::filesystem::path{SAR_RRP6_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    const auto fixture = LoadJson(fixture_path);
    ASSERT_EQ(fixture.at("schema").get<std::string>(), "graphx.sar.gotcha.normalized.v1");
    EXPECT_EQ(fixture.at("derived_from_scenario").get<std::string>(), "scenario_001");
    EXPECT_TRUE(fixture.at("ci_safe").get<bool>());
    ASSERT_TRUE(fixture.contains("records"));
    ASSERT_TRUE(fixture.at("records").is_array());
    ASSERT_EQ(fixture.at("records").size(), 1u);

    sar::GotchaOfflineConverter converter;
    const auto records = converter.LoadFromFile(fixture_path);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records.front().frame_id, 0u);
    EXPECT_EQ(records.front().pass_id, 17u);
    EXPECT_EQ(records.front().pulse_block_id, 9000u);
}

TEST(Rrp6TinyFixtureTest, GotchaReplaySourceAcceptsCiSafeTinyFixtureWithoutExternalOptIn) {
    const auto fixture_path = std::filesystem::path{SAR_RRP6_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    sar::GotchaReplaySourceNode node;
    const nlohmann::json cfg_json{
        {"fixture_path", fixture_path.string()},
        {"emit_watermark", false},
    };

    EXPECT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->sidecar.marker, sar::SarFrameMarker::Data);

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);
}