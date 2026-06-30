// SPDX-License-Identifier: MIT

/**
 * @file test_resolving_node_provider.cpp
 * @brief Test Resolving Node Provider Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include "graph/ResolvingNodeProvider.hpp"

#include <set>
#include <string>
#include <vector>

namespace {

/**
 * @class FakeNodeProvider
 * @brief Fake node provider implementation for GraphX.
 */
class FakeNodeProvider final : public graph::INodeProvider {
public:
    explicit FakeNodeProvider(std::set<std::string> available)
        : available_(std::move(available)) {}

    [[nodiscard]] std::expected<graph::NodeFacadeAdapter, graph::NodeCreationError>
    CreateNodeExpected(const std::string&) noexcept override {
        return std::unexpected(graph::NodeCreationError::CreationFailed);
    }

    [[nodiscard]] bool IsNodeTypeAvailable(const std::string& node_type_name) const override {
        return available_.contains(node_type_name);
    }

    [[nodiscard]] std::vector<std::string> GetAvailableNodeTypes() const override {
        return {available_.begin(), available_.end()};
    }

private:
    std::set<std::string> available_;
};

graph::GraphConfig::ResolverConfig ResolverConfig(
    graph::ResolverBackend execution_backend,
    graph::ResolverFallbackPolicy fallback_policy = graph::ResolverFallbackPolicy::Strict) {
    graph::GraphConfig::ResolverConfig config{};
    config.execution_backend = execution_backend;
    config.backend_fallback_policy = fallback_policy;
    config.resolver_diagnostics = true;
    config.edge_contract = "accel-token";
    return config;
}

} // namespace

TEST(ResolvingNodeProviderTest, ResolvesMetalH2DVariantWhenAvailable) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode", "H2DAsyncNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Metal));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNodeMetal");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_FALSE(resolved->fallback_used);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, AutoBackendPrefersMetalWhenAvailable) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode", "H2DAsyncNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Auto));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNodeMetal");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_FALSE(resolved->fallback_used);
    EXPECT_EQ(resolved->fallback_reason, graph::ResolverFallbackReason::None);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, FallsBackToGenericStubWhenRequestedVariantUnavailable) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal, graph::ResolverFallbackPolicy::AllowFallback));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Stub);
    EXPECT_TRUE(resolved->fallback_used);
    EXPECT_EQ(
        resolved->fallback_reason,
        graph::ResolverFallbackReason::RequestedBackendUnavailable);
}

TEST(ResolvingNodeProviderTest, CreateNodeExpectedRecordsResolutionDiagnostics) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode", "H2DAsyncNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Auto));

    const auto created = provider.CreateNodeExpected("H2DAsyncNode");
    EXPECT_FALSE(created.has_value());

    ASSERT_EQ(provider.diagnostics().size(), 1u);
    const auto& diagnostic = provider.diagnostics().front();
    EXPECT_EQ(diagnostic.intent_type, "H2DAsyncNode");
    EXPECT_EQ(diagnostic.concrete_type, "H2DAsyncNodeMetal");
    EXPECT_EQ(diagnostic.selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(diagnostic.input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(diagnostic.output_token_type, "DeviceBufferView");
    EXPECT_FALSE(diagnostic.fallback_used);
}

TEST(ResolvingNodeProviderTest, CreateNodeExpectedFallbackDiagnosticsPreserveTokenContractMetadata) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal, graph::ResolverFallbackPolicy::AllowFallback));

    const auto created = provider.CreateNodeExpected("H2DAsyncNode");
    EXPECT_FALSE(created.has_value());

    ASSERT_EQ(provider.diagnostics().size(), 1u);
    const auto& diagnostic = provider.diagnostics().front();
    EXPECT_EQ(diagnostic.intent_type, "H2DAsyncNode");
    EXPECT_EQ(diagnostic.concrete_type, "H2DAsyncNode");
    EXPECT_EQ(diagnostic.selected_backend, graph::ResolverBackend::Stub);
    EXPECT_TRUE(diagnostic.fallback_used);
    EXPECT_EQ(
        diagnostic.fallback_reason,
        graph::ResolverFallbackReason::RequestedBackendUnavailable);
    EXPECT_EQ(diagnostic.input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(diagnostic.output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, StrictRequestedBackendRejectsFallbackOnlyIntent) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal, graph::ResolverFallbackPolicy::Strict));

    EXPECT_FALSE(provider.ResolveNodeType("H2DAsyncNode").has_value());
    EXPECT_FALSE(provider.IsNodeTypeAvailable("H2DAsyncNode"));
}

TEST(ResolvingNodeProviderTest,
     StrictRequestedBackendUnavailableCreateNodeReturnsTypeNotFoundWithoutFallbackDiagnostic) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal, graph::ResolverFallbackPolicy::Strict));

    const auto created = provider.CreateNodeExpected("H2DAsyncNode");
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error(), graph::NodeCreationError::TypeNotFound);

    // Strict mode must fail resolution rather than recording a fallback path.
    EXPECT_TRUE(provider.diagnostics().empty());
}

TEST(ResolvingNodeProviderTest, PassesThroughNonIntentTypesDirectly) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"SyntheticApertureIqSourceNode"});
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal, graph::ResolverFallbackPolicy::Strict));

    const auto resolved = provider.ResolveNodeType("SyntheticApertureIqSourceNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "SyntheticApertureIqSourceNode");
    EXPECT_EQ(resolved->concrete_type, "SyntheticApertureIqSourceNode");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Direct);
    EXPECT_FALSE(resolved->fallback_used);
}

TEST(ResolvingNodeProviderTest, ResolvesGenericDeviceTransformToMetalVariant) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"DeviceTransformNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Metal));

    const auto resolved = provider.ResolveNodeType("DeviceTransformNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "DeviceTransformNodeMetal");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesGenericDeviceKernelToMetalVariant) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"DeviceKernelNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Metal));

    const auto resolved = provider.ResolveNodeType("DeviceKernelNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "DeviceKernelNodeMetal");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesSarBackprojectionToAdapterWithoutLocalMetalDuplicate) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"SarBackprojectionTransformNode"});
    auto registry = graph::NodeResolutionRegistry::CreateDefault();
    registry.AddContract(graph::NodeResolutionContract{
        .intent_type = "SarBackprojectionTransformNode",
        .input_token_type = "DeviceBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {graph::ResolverBackend::Metal, "SarBackprojectionTransformNode"},
            {graph::ResolverBackend::Stub, "SarBackprojectionTransformNode"},
        },
    });
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal),
        std::move(registry));

    const auto resolved = provider.ResolveNodeType("SarBackprojectionTransformNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "SarBackprojectionTransformNode");
    EXPECT_EQ(resolved->concrete_type, "SarBackprojectionTransformNode");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, UnregisteredDomainIntentPassesThroughDirectly) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"SarBackprojectionTransformNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Metal));

    const auto resolved = provider.ResolveNodeType("SarBackprojectionTransformNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "SarBackprojectionTransformNode");
    EXPECT_EQ(resolved->concrete_type, "SarBackprojectionTransformNode");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Direct);
    EXPECT_EQ(resolved->input_token_type, "");
    EXPECT_EQ(resolved->output_token_type, "");
}

TEST(ResolvingNodeProviderTest, DynamicMappingCanSelectDomainLocalMetalNode) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"SarRangeCompressionNodeMetal", "RangeCompressionNode"});
    graph::NodeResolutionRegistry registry;
    registry.AddContract(graph::NodeResolutionContract{
        .intent_type = "RangeCompressionNode",
        .input_token_type = "HostPinnedBufferView",
        .output_token_type = "DeviceBufferView",
        .variants = {
            {graph::ResolverBackend::Metal, "SarRangeCompressionNodeMetal"},
            {graph::ResolverBackend::Stub, "RangeCompressionNode"},
        },
    });
    graph::ResolvingNodeProvider provider(
        inner,
        ResolverConfig(graph::ResolverBackend::Metal),
        std::move(registry));

    const auto resolved = provider.ResolveNodeType("RangeCompressionNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "RangeCompressionNode");
    EXPECT_EQ(resolved->concrete_type, "SarRangeCompressionNodeMetal");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesSyclD2HVariantWhenRequested) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"D2HAsyncNodeSycl"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Sycl));

    const auto resolved = provider.ResolveNodeType("D2HAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "D2HAsyncNodeSycl");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Sycl);
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "HostPinnedBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesCudaIntentToGenericCudaLaneWhenRequested) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig(graph::ResolverBackend::Cuda));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->selected_backend, graph::ResolverBackend::Cuda);
    EXPECT_FALSE(resolved->fallback_used);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}
