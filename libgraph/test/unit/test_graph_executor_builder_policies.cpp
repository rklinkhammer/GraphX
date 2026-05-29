/**
 * @file test_graph_executor_builder_policies.cpp
 * @brief GraphExecutorBuilder policy and capability lifecycle tests.
 */

#include <gtest/gtest.h>

#include "capabilities/CommandOutputCapability.hpp"
#include "capabilities/CommandProcessorCapability.hpp"
#include "capabilities/CommandRegistryCapability.hpp"
#include "capabilities/DashboardCapability.hpp"
#include "capabilities/DataInjectionCapability.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "policies/CSVInjectionPolicy.hpp"
#include "policies/MetricsPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::shared_ptr<graph::GraphManager> BuildTopology(test::TopologyType type) {
    auto graph = test::TopologyBuilder::BuildTopology(type);
    EXPECT_NE(graph, nullptr);
    return graph;
}

std::shared_ptr<graph::GraphExecutor> BuildExecutor(
    std::shared_ptr<graph::GraphManager> graph,
    bool cli_mode = false,
    std::chrono::seconds timeout = std::chrono::seconds(30)) {
    return graph::GraphExecutorBuilder()
        .WithGraphManager(std::move(graph))
        .WithCliMode(cli_mode)
        .WithExecutorTimeout(timeout)
        .Build();
}

void AssertInitializationSuccess(const graph::InitializationResult& result) {
    ASSERT_TRUE(result.success) << "Init failed: " << result.message;
}

void AssertExecutionSuccess(const graph::ExecutionResult& result,
                            const char* operation) {
    ASSERT_TRUE(result.success) << operation << " failed: " << result.message;
}

template <typename CapabilityT>
void ExpectCapabilityPresent(const graph::GraphExecutor& executor) {
    EXPECT_TRUE(executor.Has<CapabilityT>());
    EXPECT_NE(executor.GetCapability<CapabilityT>(), nullptr);
}

template <typename CapabilityT>
void ExpectCapabilityMissing(const graph::GraphExecutor& executor) {
    EXPECT_FALSE(executor.Has<CapabilityT>());
    EXPECT_EQ(executor.GetCapability<CapabilityT>(), nullptr);
}

size_t CountMetricsCallbackProviders(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t provider_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto metrics_provider =
            graph::DiscoverCapability<graph::IMetricsCallbackProvider>(node);
        if (metrics_provider) {
            ++provider_count;
        }
    }

    return provider_count;
}

size_t CountInstalledMetricsCallbacks(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t installed_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto metrics_provider =
            graph::DiscoverCapability<graph::IMetricsCallbackProvider>(node);
        if (metrics_provider && metrics_provider->HasMetricsCallback()) {
            ++installed_count;
        }
    }

    return installed_count;
}

size_t CountCompletionCallbackProviders(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t provider_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto completion_provider =
            graph::DiscoverCapability<graph::CompletionCallbackProvider>(node);
        if (completion_provider) {
            ++provider_count;
        }
    }

    return provider_count;
}

size_t CountInstalledCompletionCallbacks(
    const std::shared_ptr<graph::GraphManager>& graph) {
    size_t installed_count = 0;

    for (const auto& node : graph->GetNodes()) {
        auto completion_provider =
            graph::DiscoverCapability<graph::CompletionCallbackProvider>(node);
        if (completion_provider && completion_provider->HasCallbackProvider()) {
            ++installed_count;
        }
    }

    return installed_count;
}

class FakeDashboard {
public:
    explicit FakeDashboard(
        std::shared_ptr<capabilities::DashboardCapability> dashboard)
        : dashboard_(std::move(dashboard)) {}

    bool SendCommand(const std::string& command) {
        return dashboard_->EnqueueCommand(command);
    }

    bool WaitForLogContaining(const std::string& text,
                              std::chrono::milliseconds timeout =
                                  std::chrono::milliseconds(1000)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            DrainAvailableLogs();
            for (const auto& log : logs_) {
                if (log.find(text) != std::string::npos) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);

        DrainAvailableLogs();
        return false;
    }

    const std::vector<std::string>& Logs() const {
        return logs_;
    }

private:
    void DrainAvailableLogs() {
        std::string line;
        while (dashboard_->DequeueLogQueueNonBlocking(line)) {
            logs_.push_back(line);
        }
    }

    std::shared_ptr<capabilities::DashboardCapability> dashboard_;
    std::vector<std::string> logs_;
};

}  // namespace

TEST(GraphExecutorBuilderPoliciesTest,
     BuilderRegistersOnlyGraphCapabilityBeforeInit) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    ExpectCapabilityPresent<capabilities::GraphCapability>(*executor);
    ExpectCapabilityMissing<capabilities::MetricsCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DataInjectionCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DashboardCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandRegistryCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandProcessorCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandOutputCapability>(*executor);
}

TEST(GraphExecutorBuilderPoliciesTest,
     StartBeforeInitFailsWithoutStartingPolicies) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    auto start_result = executor->Start();
    EXPECT_FALSE(start_result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    ExpectCapabilityMissing<capabilities::MetricsCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DataInjectionCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DashboardCapability>(*executor);
}

TEST(GraphExecutorBuilderPoliciesTest,
     InitRegistersDefaultPolicyCapabilities) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    ExpectCapabilityPresent<capabilities::GraphCapability>(*executor);
    ExpectCapabilityPresent<capabilities::MetricsCapability>(*executor);
    ExpectCapabilityPresent<capabilities::DataInjectionCapability>(*executor);
    ExpectCapabilityPresent<capabilities::DashboardCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandRegistryCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandProcessorCapability>(*executor);
    ExpectCapabilityMissing<capabilities::CommandOutputCapability>(*executor);
}

TEST(GraphExecutorBuilderPoliciesTest,
     RunBeforeStartFailsWithoutBlocking) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    auto run_result = executor->Run();
    EXPECT_FALSE(run_result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::INITIALIZED);
}

TEST(GraphExecutorBuilderPoliciesTest,
     CliModeInitRegistersCommandCapabilitiesAndBuiltinCommands) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph, true);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    ExpectCapabilityPresent<capabilities::DashboardCapability>(*executor);
    ExpectCapabilityPresent<capabilities::CommandRegistryCapability>(*executor);
    ExpectCapabilityPresent<capabilities::CommandProcessorCapability>(*executor);
    ExpectCapabilityPresent<capabilities::CommandOutputCapability>(*executor);

    auto registry =
        executor->GetCapability<capabilities::CommandRegistryCapability>();
    ASSERT_NE(registry, nullptr);
    EXPECT_TRUE(registry->HasCommand("help"));
    EXPECT_TRUE(registry->HasCommand("pause"));
    EXPECT_TRUE(registry->HasCommand("resume"));
    EXPECT_TRUE(registry->HasCommand("stop"));
    EXPECT_TRUE(registry->HasCommand("status"));

    auto processor =
        executor->GetCapability<capabilities::CommandProcessorCapability>();
    ASSERT_NE(processor, nullptr);
    EXPECT_TRUE(processor->IsReady());

    auto status = processor->ProcessCommand("status");
    EXPECT_TRUE(status.success) << status.message;
}

TEST(GraphExecutorBuilderPoliciesTest,
     StopCommandSignalsGraphCapability) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph, true);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    auto graph_capability =
        executor->GetCapability<capabilities::GraphCapability>();
    ASSERT_NE(graph_capability, nullptr);
    ASSERT_FALSE(graph_capability->IsStopped());

    auto processor =
        executor->GetCapability<capabilities::CommandProcessorCapability>();
    ASSERT_NE(processor, nullptr);

    auto stop_result = processor->ProcessCommand("stop");
    EXPECT_TRUE(stop_result.success) << stop_result.message;
    EXPECT_TRUE(graph_capability->IsStopped());

    auto status_result = processor->ProcessCommand("status");
    EXPECT_TRUE(status_result.success) << status_result.message;
    EXPECT_NE(status_result.message.find("State: Stopped"),
              std::string::npos);
}

TEST(GraphExecutorBuilderPoliciesTest,
     FakeDashboardDrivesCliCommandQueueAndLogsResults) {
    auto graph = BuildTopology(test::TopologyType::SourceOnly);
    auto executor = BuildExecutor(graph, true);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());
    AssertExecutionSuccess(executor->Start(), "Start");

    auto dashboard =
        executor->GetCapability<capabilities::DashboardCapability>();
    ASSERT_NE(dashboard, nullptr);
    FakeDashboard fake_dashboard(dashboard);

    EXPECT_TRUE(fake_dashboard.SendCommand("status"));
    EXPECT_TRUE(fake_dashboard.WaitForLogContaining("[OK] Graph Status"));

    EXPECT_TRUE(fake_dashboard.SendCommand("does_not_exist"));
    EXPECT_TRUE(fake_dashboard.WaitForLogContaining(
        "[ERROR] Unknown command: does_not_exist"));

    auto graph_capability =
        executor->GetCapability<capabilities::GraphCapability>();
    ASSERT_NE(graph_capability, nullptr);
    ASSERT_FALSE(graph_capability->IsStopped());

    EXPECT_TRUE(fake_dashboard.SendCommand("stop"));
    EXPECT_TRUE(fake_dashboard.WaitForLogContaining(
        "[OK] Graph execution stopped"));
    EXPECT_TRUE(graph_capability->IsStopped());

    AssertExecutionSuccess(executor->Stop(), "Stop");
    EXPECT_FALSE(fake_dashboard.SendCommand("status"));
    AssertExecutionSuccess(executor->Join(), "Join");
}

TEST(GraphExecutorBuilderPoliciesTest,
     CliModeStartStopJoinManagesCommandThread) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph, true);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());
    AssertExecutionSuccess(executor->Start(), "Start");
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::RUNNING);

    auto dashboard =
        executor->GetCapability<capabilities::DashboardCapability>();
    ASSERT_NE(dashboard, nullptr);
    EXPECT_TRUE(dashboard->EnqueueCommand("status"));

    AssertExecutionSuccess(executor->Stop(), "Stop");
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    auto graph_capability =
        executor->GetCapability<capabilities::GraphCapability>();
    ASSERT_NE(graph_capability, nullptr);
    EXPECT_TRUE(graph_capability->IsStopped());
    AssertExecutionSuccess(executor->Join(), "Join");
}

TEST(GraphExecutorBuilderPoliciesTest,
     InitInstallsMetricsAndCompletionCallbacksBeforeStart) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    EXPECT_EQ(CountInstalledMetricsCallbacks(graph), 0u);
    EXPECT_EQ(CountInstalledCompletionCallbacks(graph), 0u);

    AssertInitializationSuccess(executor->Init());

    EXPECT_EQ(CountMetricsCallbackProviders(graph), 2u);
    EXPECT_EQ(CountInstalledMetricsCallbacks(graph), 2u);
    EXPECT_EQ(CountCompletionCallbackProviders(graph), 1u);
    EXPECT_EQ(CountInstalledCompletionCallbacks(graph), 1u);
}

TEST(GraphExecutorBuilderPoliciesTest,
     MetricsSubscriberRegisteredAfterInitReceivesEvents) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    auto metrics_cap = executor->GetCapability<capabilities::MetricsCapability>();
    ASSERT_NE(metrics_cap, nullptr);

    test::TestMetricsSubscriber subscriber;
    metrics_cap->RegisterMetricsCallback(&subscriber);

    AssertExecutionSuccess(executor->Start(), "Start");
    AssertExecutionSuccess(executor->Run(), "Run");
    AssertExecutionSuccess(executor->Stop(), "Stop");
    AssertExecutionSuccess(executor->Join(), "Join");

    auto events = subscriber.GetEvents();
    EXPECT_FALSE(events.empty());

    bool saw_produced = false;
    bool saw_consumed = false;
    for (const auto& event : events) {
        saw_produced = saw_produced || event.event_type == "message_produced";
        saw_consumed = saw_consumed || event.event_type == "message_consumed";
    }

    EXPECT_TRUE(saw_produced);
    EXPECT_TRUE(saw_consumed);
}

TEST(GraphExecutorBuilderPoliciesTest, BuilderRejectsInvalidOptions) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);

    EXPECT_THROW(graph::GraphExecutorBuilder().WithGraphManager(nullptr),
                 std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithJsonConfig(""),
                 std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithPluginDirectory(""),
                 std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithCSVInput(""),
                 std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithCSVInputs({{"", "node"}}),
                 std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithCSVInjectionRate(0),
                 std::invalid_argument);
    EXPECT_THROW(
        graph::GraphExecutorBuilder().WithExecutorTimeout(std::chrono::seconds(0)),
        std::invalid_argument);
    EXPECT_THROW(graph::GraphExecutorBuilder().WithGraphThreads(0),
                 std::invalid_argument);
}

TEST(GraphExecutorBuilderPoliciesTest, BuilderIsSingleUse) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);

    graph::GraphExecutorBuilder builder;
    auto executor = builder.WithGraphManager(graph).Build();
    ASSERT_NE(executor, nullptr);

    EXPECT_THROW(builder.Build(), std::logic_error);
}

TEST(GraphExecutorBuilderPoliciesTest, FailedBuildDoesNotConsumeBuilder) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);

    graph::GraphExecutorBuilder builder;
    EXPECT_THROW(builder.Build(), std::invalid_argument);

    auto executor = builder.WithGraphManager(graph).Build();
    ASSERT_NE(executor, nullptr);

    EXPECT_THROW(builder.Build(), std::logic_error);
}

TEST(GraphExecutorBuilderPoliciesTest,
     CSVInjectionPolicyStopJoinBeforeInitIsNoop) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    capabilities::GraphCapability context;
    context.SetGraphManager(graph);

    policies::CSVInjectionPolicy policy;

    EXPECT_NO_THROW(policy.OnStop(context));
    EXPECT_NO_THROW(policy.OnJoin(context));
}

TEST(GraphExecutorBuilderPoliciesTest,
     CSVInjectionPolicyStartBeforeInitFailsCleanly) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    capabilities::GraphCapability context;
    context.SetGraphManager(graph);

    policies::CSVInjectionPolicy policy;

    EXPECT_FALSE(policy.OnStart(context));
    EXPECT_NO_THROW(policy.OnStop(context));
    EXPECT_NO_THROW(policy.OnJoin(context));
}

TEST(GraphExecutorBuilderPoliciesTest,
     MetricsPolicyStartBeforeInitFailsCleanly) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    capabilities::GraphCapability context;
    context.SetGraphManager(graph);

    policies::MetricsPolicy policy;

    EXPECT_FALSE(policy.OnStart(context));
    EXPECT_NO_THROW(policy.OnStop(context));
    EXPECT_NO_THROW(policy.OnJoin(context));
}
