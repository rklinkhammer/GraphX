#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_EXTERNAL_BASELINE_POLICY_PATH
#define SAR_EXTERNAL_BASELINE_POLICY_PATH "plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md"
#endif

#ifndef SAR_BASELINE_PACKAGE_REGISTRY_PATH
#define SAR_BASELINE_PACKAGE_REGISTRY_PATH "plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json"
#endif

nlohmann::json LoadRegistry() {
    std::ifstream in(SAR_BASELINE_PACKAGE_REGISTRY_PATH);
    EXPECT_TRUE(in.good()) << "unable to open registry: " << SAR_BASELINE_PACKAGE_REGISTRY_PATH;

    nlohmann::json registry;
    in >> registry;
    return registry;
}

std::string LoadPolicyText() {
    std::ifstream in(SAR_EXTERNAL_BASELINE_POLICY_PATH);
    EXPECT_TRUE(in.good()) << "unable to open policy: " << SAR_EXTERNAL_BASELINE_POLICY_PATH;

    std::string text;
    std::string line;
    while (std::getline(in, line)) {
        text.append(line);
        text.push_back('\n');
    }
    return text;
}

std::unordered_map<std::string, nlohmann::json> PackageMap(const nlohmann::json& registry) {
    std::unordered_map<std::string, nlohmann::json> out;
    for (const auto& pkg : registry.at("packages")) {
        out.emplace(pkg.at("name").get<std::string>(), pkg);
    }
    return out;
}

} // namespace

TEST(ExternalBaselinePolicyRegistryTest, DeclaresComparatorOnlyPackageRoles) {
    const auto registry = LoadRegistry();

    ASSERT_TRUE(registry.contains("comparator_only"));
    EXPECT_TRUE(registry.at("comparator_only").get<bool>());

    ASSERT_TRUE(registry.contains("packages"));
    ASSERT_TRUE(registry.at("packages").is_array());

    const auto packages = PackageMap(registry);
    ASSERT_TRUE(packages.contains("SarPy"));
    ASSERT_TRUE(packages.contains("ISCE3"));
    ASSERT_TRUE(packages.contains("gotcha-back"));

    EXPECT_EQ(packages.at("SarPy").at("role").get<std::string>(), "primary");
    EXPECT_EQ(packages.at("ISCE3").at("role").get<std::string>(), "secondary");
    EXPECT_EQ(packages.at("gotcha-back").at("role").get<std::string>(), "secondary");

    EXPECT_TRUE(packages.at("SarPy").at("comparator_only").get<bool>());
    EXPECT_TRUE(packages.at("ISCE3").at("comparator_only").get<bool>());
    EXPECT_TRUE(packages.at("gotcha-back").at("comparator_only").get<bool>());
}

TEST(ExternalBaselinePolicyRegistryTest, EncodesLicensingAndArchitectureBoundaries) {
    const auto registry = LoadRegistry();

    ASSERT_TRUE(registry.contains("architecture_protection"));
    const auto& architecture = registry.at("architecture_protection");
    EXPECT_EQ(architecture.at("canonical_runtime_contract").get<std::string>(),
              "AccelControlToken<SarSidecar>");
    EXPECT_TRUE(architecture.at("metal_first_backend").get<bool>());
    EXPECT_TRUE(architecture.at("preserve_dynamic_loading").get<bool>());
    EXPECT_TRUE(architecture.at("preserve_resolver_behavior").get<bool>());
    EXPECT_TRUE(architecture.at("no_graphx_core_api_mimicry").get<bool>());
    EXPECT_TRUE(architecture.at("no_graphx_core_contract_changes_for_external_packages").get<bool>());

    ASSERT_TRUE(registry.contains("licensing_boundaries"));
    const auto& licensing = registry.at("licensing_boundaries");
    EXPECT_TRUE(licensing.at("registry_mandatory_license_field").get<bool>());
    EXPECT_TRUE(licensing.at("license_sensitive_integrations_outside_core").get<bool>());
    EXPECT_TRUE(licensing.at("ci_safe_requires_deterministic_fixture").get<bool>());
    EXPECT_TRUE(licensing.at("ci_safe_disallow_large_dataset_fetch").get<bool>());

    for (const auto& pkg : registry.at("packages")) {
        EXPECT_TRUE(pkg.contains("license"));
        EXPECT_FALSE(pkg.at("license").get<std::string>().empty());
    }
}

TEST(ExternalBaselinePolicyRegistryTest, PolicyDeclaresComparatorOnlyBoundaries) {
    const auto policy = LoadPolicyText();

    EXPECT_NE(policy.find("comparators only"), std::string::npos);
    EXPECT_NE(policy.find("SarPy"), std::string::npos);
    EXPECT_NE(policy.find("ISCE3"), std::string::npos);
    EXPECT_NE(policy.find("gotcha-back"), std::string::npos);
    EXPECT_NE(policy.find("Do not modify `libgraph` or `libgpu` contracts"), std::string::npos);
    EXPECT_NE(policy.find("AccelControlToken<SarSidecar>"), std::string::npos);
}
