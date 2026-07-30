// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "capabilities/CommandCapability.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <chrono>
#include <atomic>
#include <array>
#include <barrier>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace {

struct CommandHarness {
    CommandHarness()
        : graph(test::TopologyBuilder::BuildTopology(
              test::TopologyType::MinimalGraph)),
          executor(graph::GraphExecutorBuilder()
                       .WithGraphManager(graph)
                       .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                       .Build()),
          commands(executor->GetCapability<
                   capabilities::CommandCapability>()) {}

    std::shared_ptr<graph::GraphManager> graph;
    std::shared_ptr<graph::GraphExecutor> executor;
    std::shared_ptr<capabilities::CommandCapability> commands;
};

capabilities::CommandOperationResult WaitForOperation(
    const std::shared_ptr<capabilities::CommandCapability>& commands,
    const std::string& operation_id) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        const auto result = commands->GetOperation(operation_id);
        if (result &&
            result->status != capabilities::OperationStatus::Accepted &&
            result->status != capabilities::OperationStatus::Running) {
            return *result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return capabilities::CommandOperationResult{
        .command = capabilities::CommandName::Run,
        .status = capabilities::OperationStatus::Failed,
        .message = "operation timed out"};
}

graph::GraphConfigurationSnapshot MinimalSnapshot(
    const std::uint64_t revision = 0) {
    std::ifstream input(
        std::string{GRAPHX_SOURCE_ROOT} +
        "/libgraph/test/config/topologies/minimal_graph.json");
    nlohmann::json document;
    input >> document;
    return graph::GraphConfigurationSnapshot(std::move(document), revision);
}

struct BlockingJoinState {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
};

class BlockingJoinPolicy final : public graph::IExecutionPolicy {
public:
    explicit BlockingJoinPolicy(std::shared_ptr<BlockingJoinState> state)
        : state_(std::move(state)) {}

    bool OnInit(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnStart(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnRun(capabilities::GraphCapability& context) override {
        context.SetStopped();
        return true;
    }

    void OnJoin(capabilities::GraphCapability&) override {
        std::unique_lock lock(state_->mutex);
        state_->entered = true;
        state_->condition.notify_all();
        state_->condition.wait(lock, [this] { return state_->released; });
    }

private:
    std::shared_ptr<BlockingJoinState> state_;
};

}  // namespace

TEST(CommandCapabilityPhase0Test, DiscoveryIsTypedAndComplete) {
    CommandHarness harness;
    const auto descriptors = harness.commands->DiscoverCommands();
    ASSERT_EQ(descriptors.size(), 6);
    EXPECT_EQ(descriptors[0].name, capabilities::CommandName::Configure);
    EXPECT_FALSE(descriptors[0].asynchronous);
    EXPECT_EQ(descriptors[3].name, capabilities::CommandName::Run);
    EXPECT_TRUE(descriptors[3].asynchronous);
}

TEST(CommandCapabilityPhase0Test, InvalidConfiguredTransitionsFailTruthfully) {
    CommandHarness harness;
    for (const auto command : {
             capabilities::CommandName::Start,
             capabilities::CommandName::Run,
             capabilities::CommandName::Stop,
             capabilities::CommandName::Join}) {
        const auto result = harness.commands->Submit({.name = command});
        EXPECT_FALSE(result.success) << capabilities::ToString(command);
        EXPECT_EQ(result.status, capabilities::OperationStatus::Failed);
        EXPECT_EQ(result.executor_state, graph::ExecutionState::CONFIGURED);
    }
}

TEST(CommandCapabilityPhase0Test,
     StopFromInitializedUsesOneWorkerAndDuplicateStopIsRejected) {
    CommandHarness harness;
    ASSERT_TRUE(harness.commands
                    ->Submit({.name = capabilities::CommandName::Init})
                    .success);

    const auto stop =
        harness.commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);
    EXPECT_EQ(stop.status, capabilities::OperationStatus::Accepted);
    const auto duplicate =
        harness.commands->Submit({.name = capabilities::CommandName::Stop});
    EXPECT_FALSE(duplicate.success);

    const auto completed =
        WaitForOperation(harness.commands, stop.operation_id);
    EXPECT_TRUE(completed.success) << completed.message;
    EXPECT_EQ(completed.executor_state, graph::ExecutionState::STOPPED);
    EXPECT_EQ(harness.executor->GetStopSequenceCount(), 1);
    const auto joined =
        harness.commands->Submit({.name = capabilities::CommandName::Join});
    EXPECT_TRUE(joined.success);
    EXPECT_EQ(joined.status, capabilities::OperationStatus::Completed);
    const auto joined_again =
        harness.commands->Submit({.name = capabilities::CommandName::Join});
    EXPECT_TRUE(joined_again.success);
    EXPECT_EQ(joined_again.status,
              capabilities::OperationStatus::Completed);
}

TEST(CommandCapabilityPhase0Test,
     ConcurrentStopsLinearizeToOneAcceptedOperation) {
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto graph_capability =
        std::make_shared<capabilities::GraphCapability>();
    graph_capability->SetGraphManager(graph_manager);
    auto metrics =
        std::make_shared<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::MetricsCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::CommandCapability>(commands);
    auto blocking_state = std::make_shared<BlockingJoinState>();
    auto policies = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<BlockingJoinPolicy>(blocking_state), nullptr);
    auto executor = std::make_shared<graph::GraphExecutor>(
        std::move(policies), graph_capability);
    commands->BindExecutor(executor);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);

    std::array<capabilities::CommandOperationResult, 2> results;
    std::barrier start{3};
    std::array<std::thread, 2> submitters;
    for (std::size_t index = 0; index < submitters.size(); ++index) {
        submitters[index] = std::thread([&, index] {
            start.arrive_and_wait();
            results[index] = commands->Submit(
                {.name = capabilities::CommandName::Stop});
        });
    }
    start.arrive_and_wait();
    for (auto& submitter : submitters) {
        submitter.join();
    }

    const auto accepted_count =
        static_cast<int>(results[0].success) +
        static_cast<int>(results[1].success);
    EXPECT_EQ(accepted_count, 1);
    const auto& accepted =
        results[0].success ? results[0] : results[1];
    const auto& rejected =
        results[0].success ? results[1] : results[0];
    EXPECT_EQ(accepted.status,
              capabilities::OperationStatus::Accepted);
    EXPECT_EQ(rejected.status,
              capabilities::OperationStatus::Failed);

    {
        std::scoped_lock lock(blocking_state->mutex);
        blocking_state->released = true;
    }
    blocking_state->condition.notify_all();
    EXPECT_TRUE(
        WaitForOperation(commands, accepted.operation_id).success);
    EXPECT_EQ(executor->GetStopSequenceCount(), 1);
}

TEST(CommandCapabilityPhase0Test,
     StopCancelsRunAndRunWorkerPerformsSoleTeardown) {
    CommandHarness harness;
    ASSERT_TRUE(harness.commands
                    ->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(harness.commands
                    ->Submit({.name = capabilities::CommandName::Start})
                    .success);

    const auto run =
        harness.commands->Submit({.name = capabilities::CommandName::Run});
    ASSERT_TRUE(run.success);
    const auto stop =
        harness.commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);

    const auto run_completed =
        WaitForOperation(harness.commands, run.operation_id);
    const auto stop_completed =
        WaitForOperation(harness.commands, stop.operation_id);
    EXPECT_EQ(run_completed.status,
              capabilities::OperationStatus::Cancelled);
    EXPECT_TRUE(stop_completed.success);
    EXPECT_EQ(harness.executor->GetStopSequenceCount(), 1);
    EXPECT_EQ(harness.executor->GetExecutionState(),
              graph::ExecutionState::STOPPED);
}

TEST(CommandCapabilityPhase0Test, RetentionIsBounded) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph)
                        .Build();
    auto metrics =
        executor->GetCapability<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics, 2);
    commands->BindExecutor(executor);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Start})
                    .success);

    const auto run =
        commands->Submit({.name = capabilities::CommandName::Run});
    const auto stop =
        commands->Submit({.name = capabilities::CommandName::Stop});
    const auto join =
        commands->Submit({.name = capabilities::CommandName::Join});
    ASSERT_TRUE(run.success);
    ASSERT_TRUE(stop.success);
    ASSERT_TRUE(join.success);
    static_cast<void>(WaitForOperation(commands, stop.operation_id));
    EXPECT_FALSE(commands->GetOperation(run.operation_id));
    EXPECT_TRUE(commands->GetOperation(stop.operation_id));
    EXPECT_TRUE(commands->GetOperation(join.operation_id));
}

TEST(CommandCapabilityPhase0Test, DestructionJoinsActiveWorker) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph)
                        .Build();
    auto metrics =
        executor->GetCapability<capabilities::MetricsCapability>();
    {
        auto commands =
            std::make_shared<capabilities::CommandCapability>(metrics);
        commands->BindExecutor(executor);
        ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                        .success);
        ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Start})
                        .success);
        ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Run})
                        .success);
    }
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_EQ(executor->GetStopSequenceCount(), 1);
}

TEST(CommandCapabilityPhase0Test,
     InitializedAndRunningRejectEveryNonTableTransition) {
    CommandHarness harness;
    ASSERT_TRUE(harness.commands
                    ->Submit({.name = capabilities::CommandName::Init})
                    .success);

    for (const auto command : {
             capabilities::CommandName::Init,
             capabilities::CommandName::Run,
             capabilities::CommandName::Join}) {
        EXPECT_FALSE(harness.commands->Submit({.name = command}).success)
            << capabilities::ToString(command);
    }
    EXPECT_FALSE(harness.commands
                     ->Submit({.name = capabilities::CommandName::Configure,
                               .configuration = MinimalSnapshot()})
                     .success);

    ASSERT_TRUE(harness.commands
                    ->Submit({.name = capabilities::CommandName::Start})
                    .success);
    for (const auto command : {
             capabilities::CommandName::Init,
             capabilities::CommandName::Start,
             capabilities::CommandName::Join}) {
        EXPECT_FALSE(harness.commands->Submit({.name = command}).success)
            << capabilities::ToString(command);
    }
    EXPECT_FALSE(harness.commands
                     ->Submit({.name = capabilities::CommandName::Configure,
                               .configuration = MinimalSnapshot()})
                     .success);

    const auto run =
        harness.commands->Submit({.name = capabilities::CommandName::Run});
    ASSERT_TRUE(run.success);
    EXPECT_FALSE(harness.commands
                     ->Submit({.name = capabilities::CommandName::Run})
                     .success);
    const auto stop =
        harness.commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);
    EXPECT_TRUE(
        WaitForOperation(harness.commands, stop.operation_id).success);
}

TEST(CommandCapabilityPhase0Test,
     DirtyRunningExecutorRejectsRunWithoutAnActiveWorker) {
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphSnapshot(MinimalSnapshot())
                        .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                        .Build();
    auto commands =
        executor->GetCapability<capabilities::CommandCapability>();
    ASSERT_NE(commands, nullptr);
    ASSERT_TRUE(commands
                    ->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(commands
                    ->Submit({.name = capabilities::CommandName::Start})
                    .success);
    executor->ObserveCoordinatorRevision(1);
    ASSERT_TRUE(executor->IsConfigurationDirty());

    const auto run =
        commands->Submit({.name = capabilities::CommandName::Run});
    EXPECT_FALSE(run.success);
    EXPECT_EQ(run.status, capabilities::OperationStatus::Failed);
    EXPECT_EQ(run.executor_state, graph::ExecutionState::RUNNING);

    const auto stop =
        commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);
    EXPECT_TRUE(
        WaitForOperation(commands, stop.operation_id).success);
}

TEST(CommandCapabilityPhase0Test,
     TerminalAndUnsupportedStatesRejectEveryLifecycleCommand) {
    for (const auto state : {
             graph::ExecutionState::PAUSED,
             graph::ExecutionState::STEPPING,
             graph::ExecutionState::ERROR,
             graph::ExecutionState::ANY}) {
        CommandHarness harness;
        harness.executor->SetExecutionState(state);
        for (const auto command : {
                 capabilities::CommandName::Configure,
                 capabilities::CommandName::Init,
                 capabilities::CommandName::Start,
                 capabilities::CommandName::Run,
                 capabilities::CommandName::Stop,
                 capabilities::CommandName::Join}) {
            capabilities::CommandRequest request{.name = command};
            if (command == capabilities::CommandName::Configure) {
                request.configuration = MinimalSnapshot(1);
            }
            const auto result = harness.commands->Submit(request);
            EXPECT_FALSE(result.success)
                << graph::GetExecutionStateName(state) << " "
                << capabilities::ToString(command);
            EXPECT_EQ(result.status,
                      capabilities::OperationStatus::Failed);
        }
        const auto inspection = harness.commands->GetState();
        EXPECT_TRUE(inspection.success);
        EXPECT_EQ(inspection.executor_state, state);
    }
}

TEST(CommandCapabilityPhase0Test,
     StopFromInitializedWorksAcrossConfiguredGenerations) {
    CommandHarness harness;
    for (std::uint64_t revision = 0; revision < 2; ++revision) {
        if (revision != 0) {
            const auto configured = harness.commands->Submit(
                {.name = capabilities::CommandName::Configure,
                 .configuration = MinimalSnapshot(revision)});
            ASSERT_TRUE(configured.success) << configured.message;
        }
        ASSERT_TRUE(harness.commands
                        ->Submit({.name = capabilities::CommandName::Init})
                        .success);
        const auto stop = harness.commands->Submit(
            {.name = capabilities::CommandName::Stop});
        ASSERT_TRUE(stop.success) << stop.message;
        EXPECT_TRUE(
            WaitForOperation(harness.commands, stop.operation_id).success);
        EXPECT_EQ(harness.executor->GetExecutionState(),
                  graph::ExecutionState::STOPPED);
    }
}

TEST(CommandCapabilityPhase0Test,
     InvalidConfigureIsAtomicAndPreservesGenerationState) {
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphSnapshot(MinimalSnapshot(1))
                        .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                        .Build();
    auto commands =
        executor->GetCapability<capabilities::CommandCapability>();
    auto metrics =
        executor->GetCapability<capabilities::MetricsCapability>();
    ASSERT_NE(commands, nullptr);
    ASSERT_NE(metrics, nullptr);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Start})
                    .success);
    const auto stop =
        commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);
    ASSERT_TRUE(
        WaitForOperation(commands, stop.operation_id).success);

    const auto manager = executor->GetGraphManager();
    const auto state = executor->GetExecutionState();
    const auto configured_revision = executor->GetConfiguredRevision();
    const auto active_revision = executor->GetActiveRevision();
    const auto generation = executor->GetGraphGeneration();
    const auto schemas = metrics->GetNodeMetricsSchemas();
    ASSERT_TRUE(commands->GetOperation(stop.operation_id));

    const graph::GraphConfigurationSnapshot invalid_snapshot(
        nlohmann::json{{"nodes", "not-an-array"},
                       {"edges", nlohmann::json::array()}},
        2);
    const auto rejected = commands->Submit(
        {.name = capabilities::CommandName::Configure,
         .configuration = invalid_snapshot});
    EXPECT_FALSE(rejected.success);
    EXPECT_EQ(rejected.status,
              capabilities::OperationStatus::Failed);
    EXPECT_EQ(executor->GetExecutionState(), state);
    EXPECT_EQ(executor->GetConfiguredRevision(), configured_revision);
    EXPECT_EQ(executor->GetActiveRevision(), active_revision);
    EXPECT_EQ(executor->GetGraphGeneration(), generation);
    EXPECT_EQ(executor->GetGraphManager(), manager);
    EXPECT_EQ(metrics->GetNodeMetricsSchemas().size(),
              schemas.size());
    EXPECT_TRUE(commands->GetOperation(stop.operation_id));
}

TEST(CommandCapabilityPhase0Test,
     InitializedPublicationAlwaysIncludesActiveRevision) {
    for (int iteration = 0; iteration < 64; ++iteration) {
        auto graph = test::TopologyBuilder::BuildTopology(
            test::TopologyType::MinimalGraph);
        auto executor = graph::GraphExecutorBuilder()
                            .WithGraphManager(graph)
                            .Build();
        std::atomic<bool> finished{false};
        std::atomic<bool> initialized{false};
        std::thread initializer([&] {
            initialized.store(executor->Init().success,
                              std::memory_order_release);
            finished.store(true, std::memory_order_release);
        });
        while (!finished.load(std::memory_order_acquire)) {
            if (executor->GetExecutionState() ==
                graph::ExecutionState::INITIALIZED) {
                EXPECT_EQ(executor->GetActiveRevision(), 0u);
            }
            std::this_thread::yield();
        }
        initializer.join();
        ASSERT_TRUE(initialized.load(std::memory_order_acquire));
        EXPECT_EQ(executor->GetExecutionState(),
                  graph::ExecutionState::INITIALIZED);
        EXPECT_EQ(executor->GetActiveRevision(), 0u);
    }
}

TEST(CommandCapabilityPhase0Test,
     StoppingAllowsOnlyJoinAndJoinedStoppedAllowsJoinOrConfigure) {
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto graph_capability =
        std::make_shared<capabilities::GraphCapability>();
    graph_capability->SetGraphManager(graph_manager);
    auto metrics =
        std::make_shared<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::MetricsCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::CommandCapability>(commands);
    auto blocking_state = std::make_shared<BlockingJoinState>();
    auto policies = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<BlockingJoinPolicy>(blocking_state), nullptr);
    auto executor = std::make_shared<graph::GraphExecutor>(
        std::move(policies), graph_capability);
    commands->BindExecutor(executor);

    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);
    const auto stop =
        commands->Submit({.name = capabilities::CommandName::Stop});
    ASSERT_TRUE(stop.success);
    {
        std::unique_lock lock(blocking_state->mutex);
        ASSERT_TRUE(blocking_state->condition.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return blocking_state->entered; }));
    }
    ASSERT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPING);

    for (const auto command : {
             capabilities::CommandName::Init,
             capabilities::CommandName::Start,
             capabilities::CommandName::Run,
             capabilities::CommandName::Stop}) {
        EXPECT_FALSE(commands->Submit({.name = command}).success)
            << capabilities::ToString(command);
    }
    EXPECT_FALSE(
        commands
            ->Submit({.name = capabilities::CommandName::Configure,
                      .configuration = MinimalSnapshot(1)})
            .success);

    const auto join =
        commands->Submit({.name = capabilities::CommandName::Join});
    ASSERT_TRUE(join.success);
    EXPECT_EQ(join.status, capabilities::OperationStatus::Accepted);
    {
        std::scoped_lock lock(blocking_state->mutex);
        blocking_state->released = true;
    }
    blocking_state->condition.notify_all();
    EXPECT_TRUE(WaitForOperation(commands, join.operation_id).success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);

    for (const auto command : {
             capabilities::CommandName::Init,
             capabilities::CommandName::Start,
             capabilities::CommandName::Run,
             capabilities::CommandName::Stop}) {
        EXPECT_FALSE(commands->Submit({.name = command}).success)
            << capabilities::ToString(command);
    }
    EXPECT_TRUE(commands->Submit({.name = capabilities::CommandName::Join})
                    .success);

    const auto generation = executor->GetGraphGeneration();
    ASSERT_TRUE(
        commands
            ->Submit({.name = capabilities::CommandName::Configure,
                      .configuration = MinimalSnapshot(1)})
            .success);
    EXPECT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::CONFIGURED);
    EXPECT_EQ(executor->GetGraphGeneration(), generation + 1);
    EXPECT_FALSE(commands->GetOperation(stop.operation_id));
    EXPECT_FALSE(commands->GetOperation(join.operation_id));
}

TEST(CommandCapabilityPhase0Test,
     NaturalRunStoppingAcceptsJoinObserversAndBoundsRetention) {
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto graph_capability =
        std::make_shared<capabilities::GraphCapability>();
    graph_capability->SetGraphManager(graph_manager);
    auto metrics =
        std::make_shared<capabilities::MetricsCapability>();
    auto commands =
        std::make_shared<capabilities::CommandCapability>(metrics, 2);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::MetricsCapability>(metrics);
    graph_capability->GetCapabilityBus()
        .Register<capabilities::CommandCapability>(commands);
    auto blocking_state = std::make_shared<BlockingJoinState>();
    auto policies = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<BlockingJoinPolicy>(blocking_state), nullptr);
    auto executor = std::make_shared<graph::GraphExecutor>(
        std::move(policies), graph_capability);
    commands->BindExecutor(executor);

    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Start})
                    .success);
    const auto run =
        commands->Submit({.name = capabilities::CommandName::Run});
    ASSERT_TRUE(run.success);
    {
        std::unique_lock lock(blocking_state->mutex);
        ASSERT_TRUE(blocking_state->condition.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return blocking_state->entered; }));
    }
    ASSERT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPING);

    std::vector<capabilities::CommandOperationResult> joins;
    for (int index = 0; index < 4; ++index) {
        joins.push_back(
            commands->Submit({.name = capabilities::CommandName::Join}));
        EXPECT_TRUE(joins.back().success);
        EXPECT_EQ(joins.back().status,
                  capabilities::OperationStatus::Accepted);
    }
    EXPECT_FALSE(commands->GetOperation(run.operation_id));
    EXPECT_FALSE(commands->GetOperation(joins[0].operation_id));
    EXPECT_FALSE(commands->GetOperation(joins[1].operation_id));
    EXPECT_TRUE(commands->GetOperation(joins[2].operation_id));
    EXPECT_TRUE(commands->GetOperation(joins[3].operation_id));

    {
        std::scoped_lock lock(blocking_state->mutex);
        blocking_state->released = true;
    }
    blocking_state->condition.notify_all();
    EXPECT_TRUE(
        WaitForOperation(commands, joins[2].operation_id).success);
    EXPECT_TRUE(
        WaitForOperation(commands, joins[3].operation_id).success);
    EXPECT_EQ(executor->GetExecutionState(),
              graph::ExecutionState::STOPPED);
}

TEST(CommandCapabilityPhase0Test,
     BuilderRegisteredCapabilityMayReleaseAllHandlesDuringActiveRun) {
    auto graph_manager = test::TopologyBuilder::BuildTopology(
        test::TopologyType::SourceOnly);
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph_manager)
                        .Build();
    auto commands =
        executor->GetCapability<capabilities::CommandCapability>();
    ASSERT_NE(commands, nullptr);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Init})
                    .success);
    ASSERT_TRUE(commands->Submit({.name = capabilities::CommandName::Start})
                    .success);
    const auto run =
        commands->Submit({.name = capabilities::CommandName::Run});
    ASSERT_TRUE(run.success);
    EXPECT_EQ(run.status, capabilities::OperationStatus::Accepted);

    std::weak_ptr<graph::GraphExecutor> weak_executor;
    std::weak_ptr<capabilities::CommandCapability> weak_commands;
    weak_executor = executor;
    weak_commands = commands;
    commands.reset();
    executor.reset();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!weak_executor.expired() || !weak_commands.expired()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(weak_executor.expired());
    EXPECT_TRUE(weak_commands.expired());
    for (const auto& node : graph_manager->GetNodes()) {
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLifecycleState(),
                  graph::LifecycleState::Joined);
    }
}
