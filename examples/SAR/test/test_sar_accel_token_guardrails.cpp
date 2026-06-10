#include <gtest/gtest.h>

#include "graph/GraphConfigParser.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

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
    // PR4 note: these legacy-name literals are intentional negative-validation
    // artifacts, not runtime contracts. Expanded coverage aligns with PR6 guardrails.
    const std::array<const char*, 7> legacy_payload_contracts = {
        "SarPulseBlockMessage",
        "SarRangeTileMessage",
        "SarImageTileMessage",
        "SarDeviceLeaseMessage",
        "SarTransferTicketMessage",
        "  SarRangeTileMessage  ",
        "sardeviceleasemessage",
    };

    for (std::size_t i = 0; i < legacy_payload_contracts.size(); ++i) {
        SCOPED_TRACE(legacy_payload_contracts[i]);

        config["edges"][0]["payload_contract"] = legacy_payload_contracts[i];

        const auto temp_path = std::filesystem::temp_directory_path() /
                               (std::string("sar_pr6_legacy_payload_contract_") +
                                std::to_string(i) + ".json");
        {
            std::ofstream out(temp_path, std::ios::trunc);
            ASSERT_TRUE(out.good());
            out << config.dump(2) << '\n';
        }

        const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(temp_path.string());
        ASSERT_FALSE(parsed);
        EXPECT_EQ(parsed.error(), app::error::ConfigError::ValidationFailed);
    }
}

} // namespace
