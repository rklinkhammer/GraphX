#include <gtest/gtest.h>

#include "graph/GraphConfigParser.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH
#define SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr6_matched_filter.json"
#endif

TEST(SarAccelTokenGuardrailsTest, RejectsLegacyPayloadContractUnderAccelTokenMode) {
    const std::filesystem::path input_path{SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(input_path));

    std::ifstream in(input_path);
    ASSERT_TRUE(in.good());

    nlohmann::json config;
    in >> config;
    config["edges"][0]["payload_contract"] = "SarRangeTileMessage";

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_pr6_legacy_payload_contract.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << config.dump(2) << '\n';
    }

    const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(temp_path.string());
    ASSERT_TRUE(parsed);

    const auto validation = graph::config::GraphConfigParser::Validate(parsed.value());
    EXPECT_FALSE(validation.valid);
    ASSERT_FALSE(validation.errors.empty());
    EXPECT_NE(
        validation.errors.front().find("Legacy SAR payload contract is not allowed on accel-token edge"),
        std::string::npos);
}

} // namespace
