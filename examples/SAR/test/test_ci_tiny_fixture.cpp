#include <gtest/gtest.h>

#include "sar/GotchaReplaySourceNode.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_CI_TINY_GOTCHA_FIXTURE_PATH
#define SAR_CI_TINY_GOTCHA_FIXTURE_PATH "examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json"
#endif

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;

    nlohmann::json value;
    input >> value;
    return value;
}

} // namespace

TEST(CiTinyFixtureTest, TinyFixtureIsTraceableToScenario001AndCiSafe) {
    const auto fixture_path = std::filesystem::path{SAR_CI_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    const auto fixture = LoadJson(fixture_path);
    ASSERT_EQ(fixture.at("schema").get<std::string>(), "graphx.sar.gotcha.normalized.v1");
    EXPECT_EQ(fixture.at("derived_from_scenario").get<std::string>(), "scenario_001");
    EXPECT_TRUE(fixture.at("ci_safe").get<bool>());
    ASSERT_TRUE(fixture.contains("records"));
    ASSERT_TRUE(fixture.at("records").is_array());
    ASSERT_EQ(fixture.at("records").size(), 4u);

    std::uint64_t expected_ordering_key = 0u;
    std::uint64_t expected_range_bin_start = 0u;
    for (const auto& record : fixture.at("records")) {
        ASSERT_TRUE(record.contains("ordering_key"));
        ASSERT_TRUE(record.contains("range_bin_start"));
        ASSERT_TRUE(record.contains("range_bin_count"));
        ASSERT_TRUE(record.contains("iq_samples"));
        ASSERT_TRUE(record.at("iq_samples").is_array());
        ASSERT_FALSE(record.at("iq_samples").empty());

        EXPECT_EQ(record.at("ordering_key").get<std::uint64_t>(), expected_ordering_key++);
        EXPECT_EQ(record.at("range_bin_start").get<std::uint64_t>(), expected_range_bin_start);

        const auto range_bin_count = record.at("range_bin_count").get<std::uint64_t>();
        EXPECT_EQ(record.at("iq_samples").size(), range_bin_count);
        expected_range_bin_start += range_bin_count;

        for (const auto& sample : record.at("iq_samples")) {
            ASSERT_TRUE(sample.contains("real"));
            ASSERT_TRUE(sample.contains("imag"));
            EXPECT_TRUE(std::isfinite(sample.at("real").get<double>()));
            EXPECT_TRUE(std::isfinite(sample.at("imag").get<double>()));
        }
    }

    sar::GotchaOfflineConverter converter;
    const auto records = converter.LoadFromFile(fixture_path);
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records.front().frame_id, 0u);
    EXPECT_EQ(records.front().pass_id, 17u);
    EXPECT_EQ(records.front().pulse_block_id, 9000u);
}

TEST(CiTinyFixtureTest, GotchaReplaySourceAcceptsCiSafeTinyFixtureWithoutExternalOptIn) {
    const auto fixture_path = std::filesystem::path{SAR_CI_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    sar::GotchaReplaySourceNode node;
    const nlohmann::json cfg_json{
        {"fixture_path", fixture_path.string()},
        {"emit_watermark", false},
    };

    EXPECT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));

    auto consume_all = [](sar::GotchaReplaySourceNode& src) {
        std::vector<sar::SarAccelControlToken> tokens;
        while (true) {
            auto token = src.Produce(std::integral_constant<std::size_t, 0>{});
            if (!token.has_value()) {
                break;
            }
            tokens.push_back(*token);
            if (token->sidecar.marker == sar::SarFrameMarker::EndOfStream) {
                break;
            }
        }
        return tokens;
    };

    const auto first_run = consume_all(node);
    ASSERT_EQ(first_run.size(), 5u);

    for (std::size_t i = 0; i < 4; ++i) {
        const auto& token = first_run[i];
        EXPECT_EQ(token.sidecar.marker, sar::SarFrameMarker::Data);
        EXPECT_TRUE(token.has_host_view);
        EXPECT_EQ(token.sidecar.sequence_id, i);
        EXPECT_EQ(token.sidecar.stream_id, 5u);
        EXPECT_EQ(token.sidecar.backend_id, 0u);
        EXPECT_GT(token.sidecar.payload_byte_count, 0u);
        EXPECT_EQ(token.host_view.bytes, token.sidecar.payload_byte_count);
        EXPECT_EQ(token.sidecar.pulse_range_count, 8u);
    }

    const auto& eos = first_run.back();
    EXPECT_EQ(eos.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos.sidecar.sequence_id, 4u);
    EXPECT_TRUE(eos.has_host_view);
    EXPECT_EQ(eos.sidecar.payload_byte_count, 0u);

    sar::GotchaReplaySourceNode rerun_node;
    EXPECT_NO_THROW(rerun_node.Configure(graph::JsonView(cfg_json)));
    const auto second_run = consume_all(rerun_node);
    ASSERT_EQ(second_run.size(), first_run.size());

    for (std::size_t i = 0; i < first_run.size(); ++i) {
        EXPECT_EQ(first_run[i].sidecar.marker, second_run[i].sidecar.marker);
        EXPECT_EQ(first_run[i].sidecar.sequence_id, second_run[i].sidecar.sequence_id);
        EXPECT_EQ(first_run[i].sidecar.pulse_range_start, second_run[i].sidecar.pulse_range_start);
        EXPECT_EQ(first_run[i].sidecar.pulse_range_count, second_run[i].sidecar.pulse_range_count);
        EXPECT_EQ(first_run[i].sidecar.payload_byte_count, second_run[i].sidecar.payload_byte_count);
    }
}