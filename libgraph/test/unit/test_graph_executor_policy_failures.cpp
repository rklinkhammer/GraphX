/**
 * @file test_graph_executor_policy_failures.cpp
 * @brief GraphExecutor policy-chain failure and cleanup tests.
 */

#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

struct RecordingState {
    std::vector<std::string> calls;
};

class RecordingPolicy : public graph::IExecutionPolicy {
public:
    RecordingPolicy(std::string name,
                    std::shared_ptr<RecordingState> state,
                    bool init_result = true,
                    bool start_result = true)
        : name_(std::move(name)),
          state_(std::move(state)),
          init_result_(init_result),
          start_result_(start_result) {}

    bool OnInit(capabilities::GraphCapability&) override {
        state_->calls.push_back(name_ + ".init");
        return init_result_;
    }

    bool OnStart(capabilities::GraphCapability&) override {
        state_->calls.push_back(name_ + ".start");
        return start_result_;
    }

    bool OnRun(capabilities::GraphCapability&) override {
        state_->calls.push_back(name_ + ".run");
        return true;
    }

    void OnStop(capabilities::GraphCapability&) override {
        state_->calls.push_back(name_ + ".stop");
    }

    void OnJoin(capabilities::GraphCapability&) override {
        state_->calls.push_back(name_ + ".join");
    }

private:
    std::string name_;
    std::shared_ptr<RecordingState> state_;
    bool init_result_;
    bool start_result_;
};

std::unique_ptr<graph::ExecutionPolicyChain> MakePolicyChain(
    std::shared_ptr<RecordingState> state,
    bool first_init = true,
    bool second_init = true,
    bool first_start = true,
    bool second_start = true) {
    auto second = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<RecordingPolicy>("second", state, second_init,
                                          second_start),
        nullptr);
    return std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<RecordingPolicy>("first", state, first_init,
                                          first_start),
        std::move(second));
}

std::unique_ptr<graph::GraphExecutor> MakeExecutor(
    std::unique_ptr<graph::ExecutionPolicyChain> policy_chain) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);
    auto capability = std::make_shared<capabilities::GraphCapability>();
    capability->SetGraphManager(graph);
    return std::make_unique<graph::GraphExecutor>(
        std::move(policy_chain), capability);
}

}  // namespace

TEST(GraphExecutorPolicyFailuresTest,
     InitFailureStopsAtFailingPolicyAndLeavesStateStopped) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(MakePolicyChain(state, false));

    auto result = executor->Init();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_EQ(state->calls, std::vector<std::string>({"first.init"}));

    EXPECT_NO_THROW({
        executor->Stop();
        executor->Join();
    });
}

TEST(GraphExecutorPolicyFailuresTest,
     InitFailureInSecondPolicyDoesNotCallLaterInit) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(MakePolicyChain(state, true, false));

    auto result = executor->Init();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_EQ(state->calls,
              std::vector<std::string>({"first.init", "second.init"}));
}

TEST(GraphExecutorPolicyFailuresTest,
     StartFailureStopsAtFailingPolicyAndLeavesStateError) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(
        MakePolicyChain(state, true, true, false, true));

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message;
    state->calls.clear();

    auto start_result = executor->Start();

    EXPECT_FALSE(start_result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::ERROR);
    EXPECT_EQ(state->calls, std::vector<std::string>({"first.start"}));

    EXPECT_NO_THROW({
        executor->Stop();
        executor->Join();
    });
}

TEST(GraphExecutorPolicyFailuresTest,
     StartFailureInSecondPolicyDoesNotStartLaterPolicies) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(
        MakePolicyChain(state, true, true, true, false));

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message;
    state->calls.clear();

    auto start_result = executor->Start();

    EXPECT_FALSE(start_result.success);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::ERROR);
    EXPECT_EQ(state->calls,
              std::vector<std::string>({"first.start", "second.start"}));
}

TEST(GraphExecutorPolicyFailuresTest,
     StopAndJoinVisitAllPoliciesAfterPartialInitFailure) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(MakePolicyChain(state, false));

    auto init_result = executor->Init();
    ASSERT_FALSE(init_result.success);
    state->calls.clear();

    auto stop_result = executor->Stop();
    auto join_result = executor->Join();

    EXPECT_TRUE(stop_result.success);
    EXPECT_TRUE(join_result.success);
    EXPECT_EQ(state->calls,
              std::vector<std::string>({"first.stop", "second.stop",
                                        "first.join", "second.join"}));
}

