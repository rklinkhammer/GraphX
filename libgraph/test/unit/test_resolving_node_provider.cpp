#include <gtest/gtest.h>

#include "graph/ResolvingNodeProvider.hpp"

#include <set>
#include <string>
#include <vector>

namespace {

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
    std::string execution_backend,
    std::string fallback_policy = "strict") {
    graph::GraphConfig::ResolverConfig config{};
    config.execution_backend = std::move(execution_backend);
    config.backend_fallback_policy = std::move(fallback_policy);
    config.resolver_diagnostics = true;
    config.edge_contract = "accel-token";
    return config;
}

} // namespace

TEST(ResolvingNodeProviderTest, ResolvesMetalH2DVariantWhenAvailable) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode", "H2DAsyncNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal"));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNodeMetal");
    EXPECT_EQ(resolved->selected_backend, "metal");
    EXPECT_FALSE(resolved->fallback_used);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, FallsBackToGenericStubWhenRequestedVariantUnavailable) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal", "allow_fallback"));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->selected_backend, "stub");
    EXPECT_TRUE(resolved->fallback_used);
    EXPECT_EQ(resolved->fallback_reason, "requested-backend-unavailable");
}

TEST(ResolvingNodeProviderTest, StrictRequestedBackendRejectsFallbackOnlyIntent) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal", "strict"));

    EXPECT_FALSE(provider.ResolveNodeType("H2DAsyncNode").has_value());
    EXPECT_FALSE(provider.IsNodeTypeAvailable("H2DAsyncNode"));
}

TEST(ResolvingNodeProviderTest, PassesThroughNonIntentTypesDirectly) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"SyntheticApertureIqSourceNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal", "strict"));

    const auto resolved = provider.ResolveNodeType("SyntheticApertureIqSourceNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->intent_type, "SyntheticApertureIqSourceNode");
    EXPECT_EQ(resolved->concrete_type, "SyntheticApertureIqSourceNode");
    EXPECT_EQ(resolved->selected_backend, "direct");
    EXPECT_FALSE(resolved->fallback_used);
}

TEST(ResolvingNodeProviderTest, ResolvesGenericDeviceTransformToMetalVariant) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"DeviceTransformNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal"));

    const auto resolved = provider.ResolveNodeType("DeviceTransformNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "DeviceTransformNodeMetal");
    EXPECT_EQ(resolved->selected_backend, "metal");
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesGenericDeviceKernelToMetalVariant) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"DeviceKernelNodeMetal"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("metal"));

    const auto resolved = provider.ResolveNodeType("DeviceKernelNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "DeviceKernelNodeMetal");
    EXPECT_EQ(resolved->selected_backend, "metal");
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesSyclD2HVariantWhenRequested) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"D2HAsyncNodeSycl"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("sycl"));

    const auto resolved = provider.ResolveNodeType("D2HAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "D2HAsyncNodeSycl");
    EXPECT_EQ(resolved->selected_backend, "sycl");
    EXPECT_EQ(resolved->input_token_type, "DeviceBufferView");
    EXPECT_EQ(resolved->output_token_type, "HostPinnedBufferView");
}

TEST(ResolvingNodeProviderTest, ResolvesCudaIntentToGenericCudaLaneWhenRequested) {
    auto inner = std::make_shared<FakeNodeProvider>(
        std::set<std::string>{"H2DAsyncNode"});
    graph::ResolvingNodeProvider provider(inner, ResolverConfig("cuda"));

    const auto resolved = provider.ResolveNodeType("H2DAsyncNode");

    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved->concrete_type, "H2DAsyncNode");
    EXPECT_EQ(resolved->selected_backend, "cuda");
    EXPECT_FALSE(resolved->fallback_used);
    EXPECT_EQ(resolved->input_token_type, "HostPinnedBufferView");
    EXPECT_EQ(resolved->output_token_type, "DeviceBufferView");
}
