// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "accelgraph/fhss/FHSSAccelConfig.hpp"
#include "accelgraph/fhss/FHSSAccelTypes.hpp"
#include "config/JsonView.hpp"

namespace {

std::vector<std::string> FieldNames(const std::array<graph::JsonField, 6>& fields) {
    std::vector<std::string> names;
    names.reserve(fields.size());
    for (const auto& field : fields) {
        names.emplace_back(field.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

TEST(AccelGraphFhssContractTest, TypeBridgeReusesExistingFhssTokenAliases) {
    static_assert(std::is_same_v<accelgraph::fhss::FHSSSyntheticIqToken,
                                 dsp::fhss::FHSSSyntheticIqToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSDownconvertedIqToken,
                                 dsp::fhss::FHSSDownconvertedIqToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSChannelizedIqToken,
                                 dsp::fhss::FHSSChannelizedIqToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSPerChannelPulseEvidenceToken,
                                 dsp::fhss::FHSSPerChannelPulseEvidenceToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSCpsmBranchMetricToken,
                                 dsp::fhss::FHSSCpsmBranchMetricToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSCpsmSymbolDecisionToken,
                                 dsp::fhss::FHSSCpsmSymbolDecisionToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSDecodedPulseWordToken,
                                 dsp::fhss::FHSSDecodedPulseWordToken>);
    static_assert(std::is_same_v<accelgraph::fhss::FHSSAssembledMessageToken,
                                 dsp::fhss::FHSSAssembledMessageToken>);
    SUCCEED();
}

TEST(AccelGraphFhssContractTest, ParseDefaultsUsesCpuStrictAndDefaultIds) {
    const nlohmann::json json = nlohmann::json::object();
    const graph::JsonView view{json};
    const auto config = accelgraph::fhss::ParseFHSSAccelConfig(view);

    EXPECT_EQ(config.backend, accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(config.fallback_policy, accelgraph::fhss::FHSSFallbackPolicy::Strict);
    EXPECT_TRUE(config.strict_fallback);
    EXPECT_EQ(config.provider_id, "cpu.default");
    EXPECT_EQ(config.session_key, "graph.default");
    EXPECT_EQ(config.cuda_device_ordinal, 0);
}

TEST(AccelGraphFhssContractTest, ParseStrictFallbackForMetal) {
    const nlohmann::json json = {
        {"backend", "metal"},
        {"strict_fallback", true},
    };

    const graph::JsonView view{json};
    const auto config = accelgraph::fhss::ParseFHSSAccelConfig(view);
    EXPECT_EQ(config.backend, accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(config.fallback_policy, accelgraph::fhss::FHSSFallbackPolicy::Strict);
    EXPECT_TRUE(config.strict_fallback);
    EXPECT_EQ(config.provider_id, "metal.default");
}

TEST(AccelGraphFhssContractTest, ParseAllowFallbackForCuda) {
    const nlohmann::json json = {
        {"backend", "cuda"},
        {"fallback_policy", "allow"},
        {"cuda_device_ordinal", 3},
    };

    const graph::JsonView view{json};
    const auto config = accelgraph::fhss::ParseFHSSAccelConfig(view);
    EXPECT_EQ(config.backend, accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(config.fallback_policy, accelgraph::fhss::FHSSFallbackPolicy::Allow);
    EXPECT_FALSE(config.strict_fallback);
    EXPECT_EQ(config.provider_id, "cuda.default");
    EXPECT_EQ(config.cuda_device_ordinal, 3);
}

TEST(AccelGraphFhssContractTest, InvalidBackendStringIsRejected) {
    const nlohmann::json json = {
        {"backend", "invalid"},
    };

    EXPECT_THROW(
        {
            const graph::JsonView view{json};
            (void)accelgraph::fhss::ParseFHSSAccelConfig(view);
        },
        graph::ConfigError);
}

TEST(AccelGraphFhssContractTest, InvalidCudaDeviceOrdinalIsRejected) {
    const nlohmann::json json = {
        {"backend", "cuda"},
        {"cuda_device_ordinal", -1},
    };

    EXPECT_THROW(
        {
            const graph::JsonView view{json};
            (void)accelgraph::fhss::ParseFHSSAccelConfig(view);
        },
        graph::ConfigError);
}

TEST(AccelGraphFhssContractTest, ProviderBackendMismatchIsRejected) {
    const nlohmann::json json = {
        {"backend", "metal"},
        {"provider_id", "cpu.default"},
    };

    EXPECT_THROW(
        {
            const graph::JsonView view{json};
            (void)accelgraph::fhss::ParseFHSSAccelConfig(view);
        },
        graph::ConfigError);
}

TEST(AccelGraphFhssContractTest, DescriptorFieldsContainAllSharedConfigKeys) {
    const auto names = FieldNames(accelgraph::fhss::FHSSAccelConfigFields());

    const std::vector<std::string> expected = {
        "backend",
        "cuda_device_ordinal",
        "fallback_policy",
        "provider_id",
        "session_key",
        "strict_fallback",
    };

    EXPECT_EQ(names, expected);
}
