// SPDX-License-Identifier: MIT

/**
 * @file test_graph_executor_builder_policies.cpp
 * @brief Test Graph Executor Builder Policies Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include "capabilities/CommandOutputCapability.hpp"
#include "capabilities/CommandCapability.hpp"
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
#include "test/TestMetricsSubscriber.hpp"
#include "test/TestGraphTopologies.hpp"

#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

/**
 * @brief Build topology.
 * @param type Parameter for build topology.
 */
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
        .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
        .WithCliMode(cli_mode)
        .WithExecutorTimeout(timeout)
        .Build();
}

graph::GraphConfigurationSnapshot MinimalSnapshot(
    const std::uint64_t revision) {
    std::ifstream input(
        std::string{GRAPHX_SOURCE_ROOT} +
        "/libgraph/test/config/topologies/minimal_graph.json");
    nlohmann::json document;
    input >> document;
    return graph::GraphConfigurationSnapshot(
        std::move(document), revision);
}

/**
 * @brief Assert initialization success.
 * @param result Parameter for assert initialization success.
 */
void AssertInitializationSuccess(const graph::InitializationResult& result) {
    ASSERT_TRUE(result.success) << "Init failed: " << result.message;
}

void AssertExecutionSuccess(const graph::ExecutionResult& result,
                            const char* operation) {
    ASSERT_TRUE(result.success) << operation << " failed: " << result.message;
}

template <typename CapabilityT>
/**
 * @brief Expect capability present.
 * @param executor Parameter for expect capability present.
 */
void ExpectCapabilityPresent(const graph::GraphExecutor& executor) {
    EXPECT_TRUE(executor.Has<CapabilityT>());
    EXPECT_NE(executor.GetCapability<CapabilityT>(), nullptr);
}

template <typename CapabilityT>
/**
 * @brief Expect capability missing.
 * @param executor Parameter for expect capability missing.
 */
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

/**
 * @class FakeDashboard
 * @brief Fake dashboard implementation for GraphX.
 */
class FakeDashboard {
public:
    explicit FakeDashboard(
        std::shared_ptr<capabilities::DashboardCapability> dashboard)
        : dashboard_(std::move(dashboard)) {}

/**
 * @brief Send command.
 * @param command Parameter for send command.
 */
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

/**
 * @brief Logs.
 */
    const std::vector<std::string>& Logs() const {
        return logs_;
    }

private:
/**
 * @brief Drain available logs.
 */
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
     BuilderRegistersStableControlCapabilitiesBeforeInit) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    ExpectCapabilityPresent<capabilities::GraphCapability>(*executor);
    ExpectCapabilityPresent<capabilities::MetricsCapability>(*executor);
    ExpectCapabilityPresent<capabilities::CommandCapability>(*executor);
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
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::CONFIGURED);
    ExpectCapabilityPresent<capabilities::MetricsCapability>(*executor);
    ExpectCapabilityPresent<capabilities::CommandCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DataInjectionCapability>(*executor);
    ExpectCapabilityMissing<capabilities::DashboardCapability>(*executor);
}

TEST(GraphExecutorBuilderPoliciesTest,
     InitRegistersDefaultPolicyCapabilities) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);

    const auto metrics_before =
        executor->GetCapability<capabilities::MetricsCapability>();
    const auto commands_before =
        executor->GetCapability<capabilities::CommandCapability>();
    AssertInitializationSuccess(executor->Init());

    ExpectCapabilityPresent<capabilities::GraphCapability>(*executor);
    ExpectCapabilityPresent<capabilities::MetricsCapability>(*executor);
    EXPECT_EQ(executor->GetCapability<capabilities::MetricsCapability>(),
              metrics_before);
    EXPECT_EQ(executor->GetCapability<capabilities::CommandCapability>(),
              commands_before);
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
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (executor->GetExecutionState() !=
               graph::ExecutionState::STOPPED &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPED);
    EXPECT_EQ(executor->GetStopSequenceCount(), 1);

    auto status_result = processor->ProcessCommand("status");
    EXPECT_TRUE(status_result.success) << status_result.message;
    EXPECT_NE(status_result.message.find("State: Stopped"),
              std::string::npos);
}

TEST(GraphExecutorBuilderPoliciesTest,
     PauseAndResumeCommandsReportUnsupportedStatus) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph, true);
    ASSERT_NE(executor, nullptr);

    AssertInitializationSuccess(executor->Init());

    auto processor =
        executor->GetCapability<capabilities::CommandProcessorCapability>();
    ASSERT_NE(processor, nullptr);

    auto pause_result = processor->ProcessCommand("pause");
    EXPECT_FALSE(pause_result.success);
    EXPECT_NE(pause_result.message.find("Unsupported command: pause"),
              std::string::npos);

    auto resume_result = processor->ProcessCommand("resume");
    EXPECT_FALSE(resume_result.success);
    EXPECT_NE(resume_result.message.find("Unsupported command: resume"),
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
        "[OK] Stop accepted"));
    EXPECT_TRUE(graph_capability->IsStopped());
    const auto stopped_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (executor->GetExecutionState() !=
               graph::ExecutionState::STOPPED &&
           std::chrono::steady_clock::now() < stopped_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPED);
    EXPECT_EQ(executor->GetStopSequenceCount(), 1);
    EXPECT_FALSE(fake_dashboard.SendCommand("status"));
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
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPING);
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

    auto events = subscriber.GetCapturedEvents();
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

TEST(GraphExecutorBuilderPoliciesTest,
     MetricsCapabilityAndSubscriberRemainStableAcrossTwoGenerations) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);
    auto executor = BuildExecutor(graph);
    ASSERT_NE(executor, nullptr);
    auto metrics =
        executor->GetCapability<capabilities::MetricsCapability>();
    auto commands =
        executor->GetCapability<capabilities::CommandCapability>();
    ASSERT_NE(metrics, nullptr);
    ASSERT_NE(commands, nullptr);
    test::TestMetricsSubscriber subscriber;
    metrics->RegisterMetricsCallback(&subscriber);

    AssertInitializationSuccess(executor->Init());
    const auto first_schemas = metrics->GetNodeMetricsSchemas();
    ASSERT_FALSE(first_schemas.empty());
    AssertExecutionSuccess(executor->Start(), "first Start");
    AssertExecutionSuccess(executor->Run(), "first Run");
    AssertExecutionSuccess(executor->Stop(), "first Stop");
    AssertExecutionSuccess(executor->Join(), "first Join");
    const auto first_events = subscriber.GetCapturedEvents().size();
    ASSERT_GT(first_events, 0u);

    const auto configured = commands->Submit(
        {.name = capabilities::CommandName::Configure,
         .configuration = MinimalSnapshot(1)});
    ASSERT_TRUE(configured.success) << configured.message;
    EXPECT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::CONFIGURED);
    EXPECT_EQ(executor->GetConfiguredRevision(), 1u);
    EXPECT_FALSE(executor->GetActiveRevision());
    EXPECT_TRUE(metrics->GetNodeMetricsSchemas().empty());
    EXPECT_EQ(executor->GetCapability<capabilities::MetricsCapability>(),
              metrics);

    AssertInitializationSuccess(executor->Init());
    const auto second_schemas = metrics->GetNodeMetricsSchemas();
    ASSERT_FALSE(second_schemas.empty());
    EXPECT_EQ(second_schemas.size(), first_schemas.size());
    EXPECT_EQ(executor->GetActiveRevision(), 1u);
    AssertExecutionSuccess(executor->Start(), "second Start");
    AssertExecutionSuccess(executor->Run(), "second Run");
    AssertExecutionSuccess(executor->Stop(), "second Stop");
    AssertExecutionSuccess(executor->Join(), "second Join");

    EXPECT_EQ(executor->GetCapability<capabilities::MetricsCapability>(),
              metrics);
    EXPECT_EQ(metrics->GetCallbackCount(), 1u);
    EXPECT_GT(subscriber.GetCapturedEvents().size(), first_events);
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

TEST(GraphExecutorBuilderPoliciesTest,
     BuildExpectedReportsMissingConfigWithoutThrowing) {
    graph::GraphExecutorBuilder builder;

    auto result = builder.BuildExpected();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              app::error::GraphExecutionError::ConfigurationInvalid);
    EXPECT_NE(result.error().message.find("WithJsonConfig() is required"),
              std::string::npos);
}

TEST(GraphExecutorBuilderPoliciesTest,
     BuildExpectedConsumesBuilderOnSuccessAndReportsSecondUse) {
    auto graph = BuildTopology(test::TopologyType::MinimalGraph);

    graph::GraphExecutorBuilder builder;
    auto first_result = builder.WithGraphManager(graph).BuildExpected();
    ASSERT_TRUE(first_result);
    ASSERT_NE(*first_result, nullptr);

    auto second_result = builder.BuildExpected();
    ASSERT_FALSE(second_result);
    EXPECT_EQ(second_result.error().code,
              app::error::GraphExecutionError::InvalidState);
    EXPECT_NE(second_result.error().message.find("Build() already called"),
              std::string::npos);
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
