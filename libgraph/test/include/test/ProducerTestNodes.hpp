// SPDX-License-Identifier: MIT

/**
 * @file ProducerTestNodes.hpp
 * @brief Reusable test nodes for DataProducer and DataInjectionProducer testing
 *
 * Provides standard test implementations for producer-based graph testing:
 * - Generator implementations (counter-based, random, etc.)
 * - Producer node implementations with various configurations
 * - Sink node implementations for data validation
 * - Completion signal sink for testing completion semantics
 *
 * These nodes are designed to be reused across:
 * - Unit tests for DataProducer classes
 * - Integration tests with graph topologies
 * - Performance and stress testing
 *
 * @author Test Suite
 * @date May 29, 2026
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "graph/CompletionSignal.hpp"
#include "graph/DataGeneratorBase.hpp"
#include "graph/DataProducerWithNotification.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/Message.hpp"
#include "graph/NamedNodes.hpp"

namespace test {

// =============================================================================
// Node Classification Enum
// =============================================================================

enum class NodeClassification {
    Unclassified = 0,
    IntProducer = 1,
    DoubleProducer = 2,
    IntSink = 3,
    DoubleSink = 4,
    CompletionSink = 5
};

// =============================================================================
// Generator Implementations
// =============================================================================

/**
 * @class SimpleIntGenerator
 * @brief Counter-based integer generator (0, 1, 2, ..., max-1)
 *
 * Produces sequential integers from 0 to max-1, then becomes exhausted.
 * Useful for testing basic producer behavior with deterministic data.
 */
/**
 * @class SimpleIntGenerator
 * @brief Simple int generator implementation for GraphX.
 */
class SimpleIntGenerator : public graph::DataGeneratorBase<int> {
private:
    int counter_;
    int max_count_;

public:
    /**
     * @brief Construct with maximum count
     * @param max Number of samples to generate (0, 1, ..., max-1)
     */
    explicit SimpleIntGenerator(int max) : counter_(0), max_count_(max) {}

    std::optional<int> Produce(size_t) override {
        if (counter_ >= max_count_) {
            return std::nullopt;
        }
        return counter_++;
    }

    bool IsExhausted() const override { return counter_ >= max_count_; }

    std::chrono::nanoseconds GetLastTimestamp() const override {
        return std::chrono::nanoseconds{0};
    }
};

/**
 * @class SimpleDoubleGenerator
 * @brief Counter-based double generator (0.0, 1.0, 2.0, ..., max-1.0)
 *
 * Produces sequential doubles from 0.0 to max-1.0, then becomes exhausted.
 * Useful for testing with floating-point data.
 */
/**
 * @class SimpleDoubleGenerator
 * @brief Simple double generator implementation for GraphX.
 */
class SimpleDoubleGenerator : public graph::DataGeneratorBase<double> {
private:
    int counter_;
    int max_count_;

public:
    /**
     * @brief Construct with maximum count
     * @param max Number of samples to generate (0.0, 1.0, ..., max-1.0)
     */
    explicit SimpleDoubleGenerator(int max) : counter_(0), max_count_(max) {}

    std::optional<double> Produce(size_t) override {
        if (counter_ >= max_count_) {
            return std::nullopt;
        }
        return static_cast<double>(counter_++);
    }

    bool IsExhausted() const override { return counter_ >= max_count_; }

    std::chrono::nanoseconds GetLastTimestamp() const override {
        return std::chrono::nanoseconds{0};
    }
};

/**
 * @class RandomIntGenerator
 * @brief Random integer generator
 *
 * Produces random integers in range [min, max).
 * Useful for stress testing and random data scenarios.
 */
/**
 * @class RandomIntGenerator
 * @brief Random int generator implementation for GraphX.
 */
class RandomIntGenerator : public graph::DataGeneratorBase<int> {
private:
    int count_;
    int max_count_;
    int min_value_;
    int max_value_;
    std::mt19937 rng_;
    std::uniform_int_distribution<int> distribution_;

public:
    /**
     * @brief Construct with range and count
     * @param max_count Number of samples to generate
     * @param min_value Minimum random value (inclusive)
     * @param max_value Maximum random value (exclusive)
     */
    RandomIntGenerator(int max_count, int min_value = 0, int max_value = 100)
        : count_(0), max_count_(max_count), min_value_(min_value), max_value_(max_value),
          rng_(std::random_device{}()), distribution_(min_value_, max_value_ - 1) {}

    std::optional<int> Produce(size_t) override {
        if (count_ >= max_count_) {
            return std::nullopt;
        }
        count_++;
        return distribution_(rng_);
    }

    bool IsExhausted() const override { return count_ >= max_count_; }

    std::chrono::nanoseconds GetLastTimestamp() const override {
        return std::chrono::nanoseconds{0};
    }
};

// =============================================================================
// Producer Node Implementations
// =============================================================================

/**
 * @class TestIntProducer
 * @brief Producer node for integer data with configurable timing
 *
 * Template Parameters:
 * - Generator: SimpleIntGenerator by default
 * - Interval: 100 microseconds by default
 * - SampleIgnore: 1 sample skip by default
 *
 * Outputs:
 * - Port 0: int values
 * - Port 1: CompletionSignal
 */
/**
 * @class TestIntProducer
 * @brief Test int producer implementation for GraphX.
 */
class TestIntProducer : public graph::DataProducerWithNotification<TestIntProducer, SimpleIntGenerator, int, int,
                                                                    graph::message::CompletionSignal, NodeClassification,
                                                                    NodeClassification::IntProducer> {
public:
    /**
     * @brief Construct with default configuration
     *
     * Generates 5 samples (0-4), skips first sample, uses 100μs intervals
     */
    TestIntProducer()
        : DataProducerWithNotification(std::make_unique<SimpleIntGenerator>(5), std::chrono::microseconds(100), 1) {
        SetName("TestIntProducer");
    }

    virtual ~TestIntProducer() = default;

    graph::message::CompletionSignal CreateNotification() const noexcept override {
        return graph::message::CompletionSignal();
    }

    void OnDataProduced(const int&) noexcept override {
        // Hook for metrics/logging (optional)
    }

    void OnDataExhausted() noexcept override {
        // Hook for cleanup (optional)
    }
};

/**
 * @class TestDoubleProducer
 * @brief Producer node for double data with configurable timing
 *
 * Template Parameters:
 * - Generator: SimpleDoubleGenerator by default
 * - Interval: 100 microseconds by default
 * - SampleIgnore: 0 (no skip) by default
 *
 * Outputs:
 * - Port 0: double values
 * - Port 1: CompletionSignal
 */
/**
 * @class TestDoubleProducer
 * @brief Test double producer implementation for GraphX.
 */
class TestDoubleProducer
    : public graph::DataProducerWithNotification<TestDoubleProducer, SimpleDoubleGenerator, double, double,
                                                  graph::message::CompletionSignal, NodeClassification,
                                                  NodeClassification::DoubleProducer> {
public:
    /**
     * @brief Construct with default configuration
     *
     * Generates 10 samples (0.0-9.0), no skipping, uses 100μs intervals
     */
    TestDoubleProducer()
        : DataProducerWithNotification(std::make_unique<SimpleDoubleGenerator>(10), std::chrono::microseconds(100),
                                       0) {
        SetName("TestDoubleProducer");
    }

    virtual ~TestDoubleProducer() = default;

    graph::message::CompletionSignal CreateNotification() const noexcept override {
        return graph::message::CompletionSignal();
    }

    void OnDataProduced(const double&) noexcept override {}
    void OnDataExhausted() noexcept override {}
};

/**
 * @class FailingProducerNode
 * @brief Producer node for error injection and failure scenario testing
 *
 * Supports failure modes for testing error handling:
 * - NoFailure: Normal operation
 * - ThrowException: Simulate exceptions
 * - ReturnInvalidData: Produce invalid data
 * - ProduceOutOfOrder: Produce data out of sequence
 *
 * **Usage**:
 * ```cpp
 * auto producer = std::make_shared<test::FailingProducerNode>(
 *     test::FailingProducerNode::FailureMode::ThrowException, 2);
 * producer->Init();
 * producer->Start();
 * EXPECT_TRUE(producer->HasErrorOccurred());
 * ```
 */
/**
 * @class FailingProducerNode
 * @brief Failing producer node implementation for GraphX.
 */
class FailingProducerNode : public graph::DataProducerWithNotification<FailingProducerNode, SimpleIntGenerator, int,
                                                                        int, graph::message::CompletionSignal,
                                                                        NodeClassification,
                                                                        NodeClassification::IntProducer> {
public:
    enum class FailureMode { NoFailure, ThrowException, ReturnInvalidData, ProduceOutOfOrder };

    /**
     * @brief Construct with optional failure configuration
     * @param mode Failure mode to simulate
     * @param fail_at_iteration At which iteration to trigger failure (1-indexed)
     */
    FailingProducerNode(FailureMode mode = FailureMode::NoFailure, int fail_at_iteration = 2)
        : DataProducerWithNotification(std::make_unique<SimpleIntGenerator>(5), std::chrono::microseconds(100), 1),
          failure_mode_(mode),
          fail_at_iteration_(fail_at_iteration),
          iteration_count_(0) {
        SetName("FailingProducerNode");
    }

    virtual ~FailingProducerNode() = default;

    graph::message::CompletionSignal CreateNotification() const noexcept override {
        return graph::message::CompletionSignal();
    }

    void OnDataProduced(const int&) noexcept override {
        iteration_count_++;

        // Implement failure mode if configured
        if (failure_mode_ == FailureMode::ThrowException && iteration_count_ == fail_at_iteration_) {
            // Note: Would throw in real implementation
            error_occurred_ = true;
        }
    }

    void OnDataExhausted() noexcept override {}

    // Test helpers
    void SetFailureMode(FailureMode mode) { failure_mode_ = mode; }
    void SetFailAtIteration(int iteration) { fail_at_iteration_ = iteration; }
    bool HasErrorOccurred() const { return error_occurred_; }
    int GetIterationCount() const { return iteration_count_; }

private:
    FailureMode failure_mode_;
    int fail_at_iteration_;
    std::atomic<int> iteration_count_{0};
    std::atomic<bool> error_occurred_{false};
};

// =============================================================================
// Sink Node Implementations
// =============================================================================

/**
 * @class TestIntSinkNode
 * @brief Sink node for validating integer data flow
 *
 * Features:
 * - Receives Message (type-erased int)
 * - Unpacks Message to retrieve int value
 * - Thread-safe data collection
 * - Data loss detection (gaps in sequence)
 * - Duplicate detection
 * - Timing information tracking
 */
/**
 * @class TestIntSinkNode
 * @brief Test int sink node implementation for GraphX.
 */
class TestIntSinkNode : public graph::NamedSinkNode<TestIntSinkNode, ::graph::message::Message> {
public:
    TestIntSinkNode() = default;
    virtual ~TestIntSinkNode() = default;

    bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        try {
            const int& value = msg.get<int>();
            received_values_.push_back(value);
            last_message_time_ = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = last_message_time_;
            }
            return true; // Keep consuming
        } catch (const std::bad_cast&) {
            // Type mismatch - message doesn't contain int
            return false;
        }
    }

    // Test helpers
    std::vector<int> GetReceivedValues() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return received_values_;
    }

    size_t GetReceivedCount() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return received_values_.size();
    }

    std::chrono::steady_clock::time_point GetFirstMessageTime() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return first_message_time_;
    }

    std::chrono::steady_clock::time_point GetLastMessageTime() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_message_time_;
    }

    bool HasDataLoss() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (size_t i = 1; i < received_values_.size(); ++i) {
            if (received_values_[i] != received_values_[i - 1] + 1) {
                return true;
            }
        }
        return false;
    }

    bool HasDuplicates() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
/**
 * @brief Unique values.
 * @param received_values_.begin() Parameter for unique values.
 * @param received_values_.end() Parameter for unique values.
 * @return Result of the operation.
 */
        std::set<int> unique_values(received_values_.begin(), received_values_.end());
        return unique_values.size() != received_values_.size();
    }

private:
    mutable std::mutex state_mutex_;
    std::vector<int> received_values_;
    std::chrono::steady_clock::time_point first_message_time_;
    std::chrono::steady_clock::time_point last_message_time_;
};

/**
 * @class TestDoubleSinkNode
 * @brief Sink node for validating double data flow
 *
 * Features:
 * - Receives Message (type-erased double)
 * - Unpacks Message to retrieve double value
 * - Thread-safe data collection
 * - Timing information tracking
 * - Statistical analysis helpers
 */
/**
 * @class TestDoubleSinkNode
 * @brief Test double sink node implementation for GraphX.
 */
class TestDoubleSinkNode : public graph::NamedSinkNode<TestDoubleSinkNode, ::graph::message::Message> {
public:
    TestDoubleSinkNode() = default;
    virtual ~TestDoubleSinkNode() = default;

    bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        try {
            const double& value = msg.get<double>();
            received_values_.push_back(value);
            last_message_time_ = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = last_message_time_;
            }
            return true; // Keep consuming
        } catch (const std::bad_cast&) {
            // Type mismatch - message doesn't contain double
            return false;
        }
    }

    // Test helpers
    std::vector<double> GetReceivedValues() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return received_values_;
    }

    size_t GetReceivedCount() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return received_values_.size();
    }

    std::chrono::steady_clock::time_point GetFirstMessageTime() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return first_message_time_;
    }

    std::chrono::steady_clock::time_point GetLastMessageTime() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_message_time_;
    }

private:
    mutable std::mutex state_mutex_;
    std::vector<double> received_values_;
    std::chrono::steady_clock::time_point first_message_time_;
    std::chrono::steady_clock::time_point last_message_time_;
};

// =============================================================================
// Completion Signal Sink
// =============================================================================

/**
 * @class CompletionNode
 * @brief Sink node for receiving and validating completion signals
 *
 * Features:
 * - Tracks all completion signals received
 * - Records signal timing
 * - Provides completion callback support
 * - Stops after first signal (allows single-producer completion)
 */
/**
 * @class CompletionNode
 * @brief Completion node implementation for GraphX.
 */
class CompletionNode : public graph::NamedSinkNode<CompletionNode, graph::message::CompletionSignal>,
                       public graph::CompletionCallbackProvider {
public:
    CompletionNode() = default;
    ~CompletionNode() override = default;

    bool Consume(const graph::message::CompletionSignal& msg, std::integral_constant<std::size_t, 0>) override {
        std::function<bool()> completion_gate;
        std::chrono::milliseconds completion_gate_timeout;
        {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
            std::lock_guard<std::mutex> lock(state_mutex_);
            completion_signals_[signal_count_] = msg;
            signal_count_++;
            signal_time_ = std::chrono::steady_clock::now();
            completion_gate = completion_gate_;
            completion_gate_timeout = completion_gate_timeout_;
        }

        if (completion_gate) {
            const auto deadline = std::chrono::steady_clock::now() + completion_gate_timeout;
            while (!completion_gate() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
            std::lock_guard<std::mutex> lock(state_mutex_);
            completion_gate_satisfied_ = completion_gate();
        }

        if (this->HasCallbackProvider()) {
            auto provider = dynamic_cast<CompletionNodeCallback*>(this->GetCallbackProvider());
            if (provider) {
                provider->OnComplete();
            }
        }
        return false; // Stop after first signal
    }

    // Test helpers
    size_t GetSignalCount() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return signal_count_;
    }

    bool HasReceivedCompletion() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return signal_count_ > 0;
    }

    std::chrono::steady_clock::time_point GetSignalTime() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return signal_time_;
    }

    void SetCompletionGate(std::function<bool()> completion_gate,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_gate_ = std::move(completion_gate);
        completion_gate_timeout_ = timeout;
        completion_gate_satisfied_ = false;
    }

    bool WasCompletionGateSatisfied() const {
/**
 * @brief Lock.
 * @param state_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
        std::lock_guard<std::mutex> lock(state_mutex_);
        return completion_gate_satisfied_;
    }

private:
    mutable std::mutex state_mutex_;
    std::map<size_t, graph::message::CompletionSignal> completion_signals_;
    size_t signal_count_{0};
    std::chrono::steady_clock::time_point signal_time_;
    std::function<bool()> completion_gate_;
    std::chrono::milliseconds completion_gate_timeout_{2000};
    bool completion_gate_satisfied_{true};
};

} // namespace test
