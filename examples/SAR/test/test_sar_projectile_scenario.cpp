#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SyntheticApertureIqSourceNode.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

namespace {

std::shared_ptr<sar::SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SarDiagnosticsSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarDiagnosticsSinkNode>();
    }

    return nullptr;
}

std::shared_ptr<sar::SyntheticApertureIqSourceNode> ResolveSourceNode(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SyntheticApertureIqSourceNode") {
            continue;
        }
        return wrapper->GetNode<sar::SyntheticApertureIqSourceNode>();
    }

    return nullptr;
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

TEST(SarProjectileScenarioTest, ExecutesMovingTargetScenarioFromJsonConfig) {
    const std::filesystem::path config_path{"examples/SAR/config/sar_projectile_approach_pr1.json"};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto sink = ResolveDiagnosticsSink(graph_manager);
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(graph_manager->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 24u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);

    auto source = ResolveSourceNode(graph_manager);
    ASSERT_NE(source, nullptr);

    const auto& source_cfg = source->GetConfig();
    EXPECT_TRUE(source_cfg.moving_target_enabled);
    EXPECT_FLOAT_EQ(source_cfg.target_initial_range_m, 2200.0f);
    EXPECT_FLOAT_EQ(source_cfg.target_closing_velocity_mps, 320.0f);
    EXPECT_FLOAT_EQ(source_cfg.pulse_interval_s, 0.002f);
    EXPECT_FLOAT_EQ(source_cfg.target_reflectivity, 3.0f);

    source->Reset();
    auto first = source->Produce(std::integral_constant<std::size_t, 0>{});
    auto second = source->Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_FALSE(first->iq_samples.empty());
    ASSERT_FALSE(second->iq_samples.empty());

    const float first_magnitude = std::abs(first->iq_samples.front());
    const float second_magnitude = std::abs(second->iq_samples.front());
    EXPECT_GT(second_magnitude, first_magnitude);
}
