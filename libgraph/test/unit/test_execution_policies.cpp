// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/**
 * @file test_execution_policies.cpp
 * @brief Comprehensive unit tests for Execution Policies (Phase 5 Priority 1)
 *
 * Tests the execution policy system with:
 * - IExecutionPolicy interface contract
 * - ExecutionPolicyChain chaining and composition
 * - Individual policy lifecycle (OnInit, OnStart, OnRun, OnStop, OnJoin)
 * - Error handling and state management
 * - C++26 compliance
 *
 * @note Uses mock/stub policies to test the framework without requiring complex graph setup
 */

#include <gtest/gtest.h>
#include "graph/IExecutionPolicy.hpp"
#include "capabilities/GraphCapability.hpp"
#include "policies/CompletionPolicy.hpp"
#include <memory>
#include <vector>

namespace graph::test {

// ===================================================================================
// Mock/Test Policies
// ===================================================================================

class MockPolicy : public IExecutionPolicy {
public:
    MockPolicy() = default;
    
    std::vector<std::string> call_sequence;
    bool init_result = true;
    bool start_result = true;
    bool run_result = true;
    
    bool OnInit(capabilities::GraphCapability& context) override {
        (void)context;  // Suppress unused warning
        call_sequence.push_back("OnInit");
        return init_result;
    }
    
    bool OnStart(capabilities::GraphCapability& context) override {
        (void)context;
        call_sequence.push_back("OnStart");
        return start_result;
    }
    
    bool OnRun(capabilities::GraphCapability& context) override {
        (void)context;
        call_sequence.push_back("OnRun");
        return run_result;
    }
    
    void OnStop(capabilities::GraphCapability& context) override {
        (void)context;
        call_sequence.push_back("OnStop");
    }
    
    void OnJoin(capabilities::GraphCapability& context) override {
        (void)context;
        call_sequence.push_back("OnJoin");
    }
};

class FailingPolicy : public IExecutionPolicy {
public:
    bool OnInit(capabilities::GraphCapability& context) override {
        (void)context;
        return false;  // Always fail on init
    }
};

class NoOpPolicy : public IExecutionPolicy {
    // Uses default implementations (all return true/void)
};

// ===================================================================================
// Test Fixture
// ===================================================================================

class ExecutionPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a real GraphCapability instance for testing
        capability = std::make_shared<capabilities::GraphCapability>();
    }
    
    void TearDown() override {
        capability.reset();
    }
    
    std::shared_ptr<capabilities::GraphCapability> capability;
};

// ===================================================================================
// IExecutionPolicy Tests (4 tests)
// ===================================================================================

TEST_F(ExecutionPolicyTest, DefaultPolicyImplementation) {
    // Test that IExecutionPolicy provides default implementations
    auto policy = std::make_unique<NoOpPolicy>();
    
    // GraphCapability should be passed correctly
    EXPECT_TRUE(policy->OnInit(*capability));
    EXPECT_TRUE(policy->OnStart(*capability));
    EXPECT_TRUE(policy->OnRun(*capability));
    policy->OnStop(*capability);   // void return
    policy->OnJoin(*capability);   // void return
    
    SUCCEED();  // No exceptions thrown
}

TEST_F(ExecutionPolicyTest, MockPolicyCallSequence) {
    // Verify that policy lifecycle calls happen in correct order
    auto policy = std::make_unique<MockPolicy>();
    
    // Simulate graph execution lifecycle
    EXPECT_TRUE(policy->OnInit(*capability));
    EXPECT_TRUE(policy->OnStart(*capability));
    EXPECT_TRUE(policy->OnRun(*capability));
    policy->OnStop(*capability);
    policy->OnJoin(*capability);
    
    // Verify call sequence
    EXPECT_EQ(policy->call_sequence.size(), 5);
    EXPECT_EQ(policy->call_sequence[0], "OnInit");
    EXPECT_EQ(policy->call_sequence[1], "OnStart");
    EXPECT_EQ(policy->call_sequence[2], "OnRun");
    EXPECT_EQ(policy->call_sequence[3], "OnStop");
    EXPECT_EQ(policy->call_sequence[4], "OnJoin");
}

TEST_F(ExecutionPolicyTest, PolicyInitializationFailure) {
    // Test policy that fails during initialization
    auto policy = std::make_unique<FailingPolicy>();
    
    EXPECT_FALSE(policy->OnInit(*capability));
    // Remaining lifecycle methods should still be callable
    EXPECT_TRUE(policy->OnStart(*capability));
    EXPECT_TRUE(policy->OnRun(*capability));
}

TEST_F(ExecutionPolicyTest, PolicyWithCustomResults) {
    // Test policy with custom result values
    auto policy = std::make_unique<MockPolicy>();
    
    policy->init_result = true;
    policy->start_result = false;  // Simulate start failure
    policy->run_result = true;
    
    EXPECT_TRUE(policy->OnInit(*capability));
    EXPECT_FALSE(policy->OnStart(*capability));
    EXPECT_TRUE(policy->OnRun(*capability));
}

TEST_F(ExecutionPolicyTest, CompletionPolicyInitWithoutGraphManagerDoesNotThrow) {
    policies::CompletionPolicy policy;

    EXPECT_NO_THROW({
        EXPECT_TRUE(policy.OnInit(*capability));
    });
    EXPECT_FALSE(capability->IsCompletionSignaled());
}

// ===================================================================================
// ExecutionPolicyChain Tests (6 tests)
// ===================================================================================

TEST_F(ExecutionPolicyTest, ChainWithSinglePolicy) {
    // Create a chain with single policy
    auto policy = std::make_unique<MockPolicy>();
    auto mock_ptr = policy.get();
    
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(policy));
    
    // Execute chain lifecycle
    EXPECT_TRUE(chain->OnInit(*capability));
    EXPECT_TRUE(chain->OnStart(*capability));
    EXPECT_TRUE(chain->OnRun(*capability));
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    // Verify single policy was called
    EXPECT_EQ(mock_ptr->call_sequence.size(), 5);
}

TEST_F(ExecutionPolicyTest, ChainWithMultiplePolicies) {
    // Create a chain with multiple policies
    auto policy1 = std::make_unique<MockPolicy>();
    auto policy2 = std::make_unique<MockPolicy>();
    auto policy3 = std::make_unique<MockPolicy>();
    
    auto mock1 = policy1.get();
    auto mock2 = policy2.get();
    auto mock3 = policy3.get();
    
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(policy1));
    chain->AppendPolicy(std::move(policy2));
    chain->AppendPolicy(std::move(policy3));
    
    // Execute chain
    EXPECT_TRUE(chain->OnInit(*capability));
    EXPECT_TRUE(chain->OnStart(*capability));
    EXPECT_TRUE(chain->OnRun(*capability));
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    // Verify all policies were called
    EXPECT_EQ(mock1->call_sequence.size(), 5);
    EXPECT_EQ(mock2->call_sequence.size(), 5);
    EXPECT_EQ(mock3->call_sequence.size(), 5);
}

TEST_F(ExecutionPolicyTest, ChainStopsOnFirstFailure) {
    // Create a chain where second policy fails on Start (not Init)
    auto policy1 = std::make_unique<MockPolicy>();
    auto policy2 = std::make_unique<MockPolicy>();
    auto policy3 = std::make_unique<MockPolicy>();
    
    auto mock1 = policy1.get();
    auto mock2 = policy2.get();
    auto mock3 = policy3.get();
    
    // Set policy2 to fail on Start
    mock2->start_result = false;
    
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(policy1));
    chain->AppendPolicy(std::move(policy2));
    chain->AppendPolicy(std::move(policy3));
    
    // OnInit should succeed (all policies have init_result=true by default)
    EXPECT_TRUE(chain->OnInit(*capability));
    
    // OnStart should fail on second policy
    EXPECT_FALSE(chain->OnStart(*capability));
    
    // Verify which policies were called
    EXPECT_EQ(mock1->call_sequence.size(), 2);  // OnInit, OnStart
    EXPECT_EQ(mock2->call_sequence.size(), 2);  // OnInit, OnStart (failed)
    EXPECT_EQ(mock3->call_sequence.size(), 1);  // OnInit only (Start never reached)
}

TEST_F(ExecutionPolicyTest, ChainVoidMethodsAlwaysExecute) {
    // Test that void methods (OnStop, OnJoin) always execute for all policies
    auto policy1 = std::make_unique<MockPolicy>();
    auto policy2 = std::make_unique<MockPolicy>();
    auto policy3 = std::make_unique<MockPolicy>();
    
    auto mock1 = policy1.get();
    auto mock2 = policy2.get();
    auto mock3 = policy3.get();
    
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(policy1));
    chain->AppendPolicy(std::move(policy2));
    chain->AppendPolicy(std::move(policy3));
    
    // Execute OnStop and OnJoin
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    // Verify all policies received the calls
    EXPECT_EQ(mock1->call_sequence.size(), 2);
    EXPECT_EQ(mock2->call_sequence.size(), 2);
    EXPECT_EQ(mock3->call_sequence.size(), 2);
    
    EXPECT_EQ(mock1->call_sequence[0], "OnStop");
    EXPECT_EQ(mock1->call_sequence[1], "OnJoin");
    EXPECT_EQ(mock2->call_sequence[0], "OnStop");
    EXPECT_EQ(mock2->call_sequence[1], "OnJoin");
    EXPECT_EQ(mock3->call_sequence[0], "OnStop");
    EXPECT_EQ(mock3->call_sequence[1], "OnJoin");
}

TEST_F(ExecutionPolicyTest, AppendNextChain) {
    // Test AppendNext for combining policy chains
    auto policy1 = std::make_unique<MockPolicy>();
    auto policy2 = std::make_unique<MockPolicy>();
    auto policy3 = std::make_unique<MockPolicy>();
    
    auto mock1 = policy1.get();
    auto mock2 = policy2.get();
    auto mock3 = policy3.get();
    
    auto chain1 = std::make_unique<ExecutionPolicyChain>(std::move(policy1));
    chain1->AppendPolicy(std::move(policy2));
    
    auto chain2 = std::make_unique<ExecutionPolicyChain>(std::move(policy3));
    
    chain1->AppendNext(std::move(chain2));
    
    // Execute combined chain
    EXPECT_TRUE(chain1->OnInit(*capability));
    EXPECT_TRUE(chain1->OnStart(*capability));
    
    // Verify all policies were called
    EXPECT_EQ(mock1->call_sequence.size(), 2);
    EXPECT_EQ(mock2->call_sequence.size(), 2);
    EXPECT_EQ(mock3->call_sequence.size(), 2);
}

// ===================================================================================
// Integration Tests (3 tests)
// ===================================================================================

TEST_F(ExecutionPolicyTest, ComplexChainComposition) {
    // Test a realistic chain composition
    auto chain = std::make_unique<ExecutionPolicyChain>(
        std::make_unique<MockPolicy>()
    );
    
    for (int i = 0; i < 4; ++i) {
        chain->AppendPolicy(std::make_unique<MockPolicy>());
    }
    
    // Execute full lifecycle
    EXPECT_TRUE(chain->OnInit(*capability));
    EXPECT_TRUE(chain->OnStart(*capability));
    EXPECT_TRUE(chain->OnRun(*capability));
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    SUCCEED();  // Chain executed without exceptions
}

TEST_F(ExecutionPolicyTest, PolicyChainErrorRecovery) {
    // Test chain behavior with intermittent failures
    auto policy1 = std::make_unique<MockPolicy>();
    auto policy2 = std::make_unique<MockPolicy>();
    auto policy3 = std::make_unique<MockPolicy>();
    
    auto mock1 = policy1.get();
    auto mock2 = policy2.get();
    auto mock3 = policy3.get();
    
    // Set policy2 to fail on Run
    mock2->run_result = false;
    
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(policy1));
    chain->AppendPolicy(std::move(policy2));
    chain->AppendPolicy(std::move(policy3));
    
    // Init and Start should succeed
    EXPECT_TRUE(chain->OnInit(*capability));
    EXPECT_TRUE(chain->OnStart(*capability));
    
    // Run should fail on policy2
    EXPECT_FALSE(chain->OnRun(*capability));
    
    // Stop and Join should execute all policies regardless
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    EXPECT_EQ(mock1->call_sequence.size(), 5);
    EXPECT_EQ(mock2->call_sequence.size(), 5);
    EXPECT_EQ(mock3->call_sequence.size(), 4);  // OnInit, OnStart, OnStop, OnJoin (4 total)
}

TEST_F(ExecutionPolicyTest, EmptyChainBehavior) {
    // Test behavior of chain with no policies (edge case)
    auto empty_policy = std::make_unique<NoOpPolicy>();
    auto chain = std::make_unique<ExecutionPolicyChain>(std::move(empty_policy));
    
    // All operations should succeed without issue
    EXPECT_TRUE(chain->OnInit(*capability));
    EXPECT_TRUE(chain->OnStart(*capability));
    EXPECT_TRUE(chain->OnRun(*capability));
    chain->OnStop(*capability);
    chain->OnJoin(*capability);
    
    SUCCEED();
}

} // namespace graph::test
