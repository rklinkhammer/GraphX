/**
 * @file test_graph_executor_policy_failures.cpp
 * @brief GraphExecutor policy-chain failure and cleanup tests.
 */

#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphManager.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "test/TestGraphTopologies.hpp"

#include <memory>
#include <stdexcept>
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

class ThrowingInitPolicy : public graph::IExecutionPolicy {
public:
    bool OnInit(capabilities::GraphCapability&) override {
        throw std::runtime_error("init failed by exception");
    }

    bool OnStart(capabilities::GraphCapability&) override {
        return true;
    }

    bool OnRun(capabilities::GraphCapability&) override {
        return true;
    }
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

TEST(GraphManagerExpectedTest, StartExpectedBeforeInitReturnsInvalidState) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);

    auto result = graph->StartExpected();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, app::error::GraphExecutionError::InvalidState);
    EXPECT_EQ(result.error().message,
              "GraphManager::Start() requires successful Init()");
}

TEST(GraphManagerExpectedTest, InitExpectedRejectsDoubleInit) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::MinimalGraph);

    auto first_result = graph->InitExpected();
    ASSERT_TRUE(first_result);

    auto second_result = graph->InitExpected();

    ASSERT_FALSE(second_result);
    EXPECT_EQ(second_result.error().code,
              app::error::GraphExecutionError::InvalidState);
    EXPECT_EQ(second_result.error().message,
              "GraphManager::Init() called after graph is already initialized");

    graph->Stop();
    graph->Join();
}

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
     InitExpectedReturnsPolicyFailedForPolicyInitFailure) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(MakePolicyChain(state, false));

    auto result = executor->InitExpected();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, app::error::GraphExecutionError::PolicyFailed);
    EXPECT_EQ(result.error().message, "ExecutionPolicyChain::OnInit() failed");
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_EQ(state->calls, std::vector<std::string>({"first.init"}));
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
     StartExpectedReturnsInvalidStateWhenNotInitialized) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(MakePolicyChain(state));

    auto result = executor->StartExpected();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, app::error::GraphExecutionError::InvalidState);
    EXPECT_EQ(result.error().message,
              "GraphExecutor::Start() requires INITIALIZED state");
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
    EXPECT_TRUE(state->calls.empty());
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
     StartExpectedReturnsPolicyFailedForPolicyStartFailure) {
    auto state = std::make_shared<RecordingState>();
    auto executor = MakeExecutor(
        MakePolicyChain(state, true, true, false, true));

    auto init_result = executor->InitExpected();
    ASSERT_TRUE(init_result);
    state->calls.clear();

    auto start_result = executor->StartExpected();

    ASSERT_FALSE(start_result);
    EXPECT_EQ(start_result.error().code,
              app::error::GraphExecutionError::PolicyFailed);
    EXPECT_EQ(start_result.error().message,
              "ExecutionPolicyChain::OnStart() failed");
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::ERROR);
    EXPECT_EQ(state->calls, std::vector<std::string>({"first.start"}));
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
     InitExpectedCatchesPolicyExceptionAsUnknown) {
    auto policy_chain = std::make_unique<graph::ExecutionPolicyChain>(
        std::make_unique<ThrowingInitPolicy>(), nullptr);
    auto executor = MakeExecutor(std::move(policy_chain));

    auto result = executor->InitExpected();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, app::error::GraphExecutionError::Unknown);
    EXPECT_NE(result.error().message.find("GraphExecutor::Init() threw"),
              std::string::npos);
    EXPECT_EQ(executor->GetExecutionState(), graph::ExecutionState::STOPPED);
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
