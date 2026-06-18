// SPDX-License-Identifier: MIT

/**
 * @file test_graph_executor_lifecycle.cpp
 * @brief Test Graph Executor Lifecycle Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphExecutor.hpp"
#include "test/TestGraphTopologies.hpp"

#include <memory>
#include <string>

namespace {

struct ExecutorContext {
    std::shared_ptr<graph::GraphManager> graph;
    std::shared_ptr<capabilities::GraphCapability> capability;
    std::unique_ptr<graph::GraphExecutor> executor;
};

/**
 * @brief Topology name.
 * @param type Parameter for topology name.
 */
std::string TopologyName(test::TopologyType type) {
    return test::TopologyBuilder::GetTopologyMetadata(type).name;
}

/**
 * @brief Create executor context.
 * @param type Parameter for create executor context.
 */
ExecutorContext CreateExecutorContext(test::TopologyType type) {
    ExecutorContext context;
    context.graph = test::TopologyBuilder::BuildTopology(type);
    context.capability = std::make_shared<capabilities::GraphCapability>();
    context.capability->SetGraphManager(context.graph);
    context.executor =
        std::make_unique<graph::GraphExecutor>(nullptr, context.capability);
    return context;
}

void AssertExecutionSuccess(const graph::ExecutionResult& result,
                            const char* operation) {
    ASSERT_TRUE(result.success) << operation << " failed: " << result.message;
}

}  // namespace

TEST(GraphExecutorLifecycleTest, ConstructAndDestructWithEveryTopology) {
    for (const auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
        SCOPED_TRACE(TopologyName(type));

        EXPECT_NO_THROW({
            auto graph = test::TopologyBuilder::BuildTopology(type);
            auto capability =
                std::make_shared<capabilities::GraphCapability>();
            capability->SetGraphManager(graph);

            graph::GraphExecutor executor(nullptr, capability);
        });
    }
}

TEST(GraphExecutorLifecycleTest, InitStopJoinWithEveryTopology) {
    for (const auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
        SCOPED_TRACE(TopologyName(type));

        auto context = CreateExecutorContext(type);
        ASSERT_NE(context.graph, nullptr);
        ASSERT_NE(context.executor, nullptr);

        const auto init_result = context.executor->Init();
        ASSERT_TRUE(init_result.success)
            << "Init failed: " << init_result.message;

        AssertExecutionSuccess(context.executor->Stop(), "Stop");
        AssertExecutionSuccess(context.executor->Join(), "Join");
    }
}

TEST(GraphExecutorLifecycleTest, InitStartStopJoinWithEveryTopologyWithoutRun) {
    for (const auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
        SCOPED_TRACE(TopologyName(type));

        auto context = CreateExecutorContext(type);
        ASSERT_NE(context.graph, nullptr);
        ASSERT_NE(context.executor, nullptr);

        const auto init_result = context.executor->Init();
        ASSERT_TRUE(init_result.success)
            << "Init failed: " << init_result.message;

        AssertExecutionSuccess(context.executor->Start(), "Start");
        AssertExecutionSuccess(context.executor->Stop(), "Stop");
        AssertExecutionSuccess(context.executor->Join(), "Join");
    }
}

TEST(GraphExecutorLifecycleTest, DestructorAfterInitDoesNotRequireExplicitStopJoin) {
    for (const auto type : test::TopologyBuilder::GetAllTopologyTypes()) {
        SCOPED_TRACE(TopologyName(type));

        auto context = CreateExecutorContext(type);
        ASSERT_NE(context.graph, nullptr);
        ASSERT_NE(context.executor, nullptr);

        const auto init_result = context.executor->Init();
        ASSERT_TRUE(init_result.success)
            << "Init failed: " << init_result.message;

        EXPECT_NO_THROW(context.executor.reset());
    }
}
