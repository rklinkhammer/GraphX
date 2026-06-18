/**
 * @file CompletionAggregatorNode.hpp
 * @brief Completion Aggregator Node Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#pragma once

#include "graph/Nodes.hpp"
#include "core/ActiveQueue.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>
#include <log4cxx/logger.h>
#include "graph/IConfigurable.hpp"
#include "config/ConfigError.hpp"
#include "config/JsonView.hpp"
#include "graph/CompletionSignal.hpp"
#include "graph/ICompletionCallback.hpp"

namespace graph {

/**
 * @class CompletionAggregatorNode
 * @brief Completion Aggregator Node graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class CompletionAggregatorNode : public NamedSinkNode<
    CompletionAggregatorNode,
    graph::message::CompletionSignal, 
    graph::message::CompletionSignal, 
    graph::message::CompletionSignal, 
    graph::message::CompletionSignal, 
    graph::message::CompletionSignal>,
    public graph::IConfigurable,
    public graph::ICompletionCallback<graph::message::CompletionSignal> {
public:
 
     /**
     * @brief Construct aggregator
     */
    explicit CompletionAggregatorNode() = default;

    /**
     * @brief Destructor
     */
    ~CompletionAggregatorNode() override = default;

    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param msg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume (const graph::message::CompletionSignal& msg,std::integral_constant<std::size_t, 0>) override  {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_signals_[0] = msg;
        received_completions_++;
        return CheckForAllCompletions();
    }

    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param msg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume (const graph::message::CompletionSignal& msg,std::integral_constant<std::size_t, 1>) override  {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_signals_[1] = msg;
        received_completions_++;
        return CheckForAllCompletions();
    }
   
    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param msg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume (const graph::message::CompletionSignal& msg,std::integral_constant<std::size_t, 2>) override  {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_signals_[2] = msg;
        received_completions_++;
        return CheckForAllCompletions();
    }
   
    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param msg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume (const graph::message::CompletionSignal& msg,std::integral_constant<std::size_t, 3>) override  {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_signals_[3] = msg;
        received_completions_++;
        return CheckForAllCompletions();
    }
    
    /**
     * @brief Processes data through the Consume operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param msg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Consume (const graph::message::CompletionSignal& msg,std::integral_constant<std::size_t, 4>) override  {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        completion_signals_[4] = msg;
        received_completions_++;
        return CheckForAllCompletions();
    }
    
    /**
     * @brief Applies configuration to this object.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param config_json Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Configure(const graph::JsonView& config_json) override {
        using namespace graph;
        try {
            /**
             * @brief Executes the Log4 Cxx Trace operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param completion_logger_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_TRACE(completion_logger_, "Configuring CompletionAggregatorNode from JSON");
            // Configure expected_sensors parameter if present
            if (config_json.Contains("expected_sensors")) {
                auto expected_count_result = config_json.TryGetInt("expected_sensors", -1);
                if (!expected_count_result) {
                    throw expected_count_result.error();
                }
                int expected_count = expected_count_result.value();
                if (expected_count <= 0) {
                    throw ConfigError("expected_sensors must be > 0 (got " + 
                                     std::to_string(expected_count) + ")");
                }
                /**
                 * @brief Updates the Expected Sensors.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                SetExpectedSensors(static_cast<size_t>(expected_count));
                /**
                 * @brief Executes the Log4 Cxx Trace operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param completion_logger_ Input or configuration value consumed by the method.
                 * @param expected_count Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                LOG4CXX_TRACE(completion_logger_, "Set expected_sensors to " << expected_count);
            }
        } catch (const std::exception& e) {
/**
 * @brief Config error.
 * @param e.what() Parameter for config error.
 * @return Result of the operation.
 */
            throw ConfigError(std::string("CompletionAggregatorNode configuration error: ") + e.what());
        }
    }
        
    /**
     * @brief Initialize the aggregator node
     * @return true on success
     */
    bool Init() override
    {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param completion_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(completion_logger_, "CompletionAggregatorNode initializing");
        return SinkNode::Init();
    }

    /**
     * @brief Start the aggregator node
     * @return true on success
     */
    bool Start() override
    {
        LOG4CXX_TRACE(completion_logger_, "CompletionAggregatorNode starting (expecting "
                     << expected_sensor_count_ << " sensors)");
        return SinkNode::Start();
    }

    /**
     * @brief Stop the aggregator node
     */
    void Stop() override
    {
        {
            /**
             * @brief Executes the Lock operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param state_mutex_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            std::lock_guard<std::mutex> lock(state_mutex_);
            LOG4CXX_TRACE(
                completion_logger_,
                "CompletionAggregatorNode stopping (received "
                << received_completions_ << " completions)"
            );
        }
        SinkNode::Stop();
    }

    /**
     * @brief Join the aggregator node
     */
    void Join() override
    {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param completion_logger_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(completion_logger_, "CompletionAggregatorNode joining");
        SinkNode::Join();
    }

    // ====================================================================
    // Configuration API
    // ====================================================================

    /**
     * @brief Set the expected number of sensors to wait for
     * @param count Number of sensors that will emit completion signals
     * 
     * Aggregator will signal completion when this many sensors have reported.
     */
    void SetExpectedSensors(size_t count)
    {
        if (count == 0) {
            LOG4CXX_WARN(completion_logger_, 
                "SetExpectedSensors: count must be > 0 (got 0, callback would trigger immediately)");
            return;
        }
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param state_mutex_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(state_mutex_);
        expected_sensor_count_ = count;
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param completion_logger_ Input or configuration value consumed by the method.
         * @param count Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(completion_logger_, "Expected sensors set to " << count);
    }

    bool CheckForAllCompletions()
    {
        LOG4CXX_TRACE(completion_logger_,
                      "Received completions: " << received_completions_ << "/" << expected_sensor_count_);
            //std::lock_guard<std::mutex> lock(state_mutex_);
            if (expected_sensor_count_ > 0 &&
                received_completions_ >= expected_sensor_count_) {
                LOG4CXX_TRACE(
                    completion_logger_,
                    "CompletionAggregatorNode received all expected completions ("
                    << received_completions_ << "/" << expected_sensor_count_ << ")"
                );
                // Invoke callback if set
                if (this->HasCallbackProvider()) {
                    auto provider = dynamic_cast<CompletionNodeCallback*>(  
                        this->GetCallbackProvider());
                    assert(provider != nullptr);   // conservative check 
                    provider->OnComplete();
                }
           }
        return true;
    }   

private:
    static inline log4cxx::LoggerPtr completion_logger_ =
        log4cxx::Logger::getLogger("avionics.CompletionAggregator");

    /// Expected number of sensors to wait for
    size_t expected_sensor_count_ = 0;

    /// Current number of received completions
    std::atomic<size_t> received_completions_ = 0;

 
    /// Mutex protecting shared state
    mutable std::mutex state_mutex_;

    std::map<int, graph::message::CompletionSignal> completion_signals_;

};

}  // namespace graph

