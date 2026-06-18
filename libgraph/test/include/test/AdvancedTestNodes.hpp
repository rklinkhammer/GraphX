// SPDX-License-Identifier: MIT

/**
 * @file AdvancedTestNodes.hpp
 * @brief Advanced Test Nodes Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <array>
#include <atomic>
#include <iostream>
#include <chrono>
#include "config/Config.hpp"
#include "config/DataTypes.hpp"
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "config/ConfigError.hpp"
#include "config/JsonView.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "metrics/MetricsEvent.hpp"
#include "metrics/NodeMetricsSchema.hpp"
#include "metrics/IMetricsSubscriber.hpp"
#include "test/TestMetricsSubscriber.hpp"
#include <log4cxx/logger.h>

namespace test {

    // Logger for tracing test node behavior
    static log4cxx::LoggerPtr test_logger = log4cxx::Logger::getLogger("test.nodes");

    // =========================================================================================
    // Module-level port name constants for template parameters
    // =========================================================================================
    inline constexpr char g_interior_input[] = "Input";
    inline constexpr char g_interior_output[] = "Output";
    inline constexpr char g_merge_input0[] = "In0";
    inline constexpr char g_merge_input1[] = "In1";
    inline constexpr char g_merge_output[] = "Out";
    inline constexpr char g_split_input[] = "In";
    inline constexpr char g_split_output0[] = "Out0";
    inline constexpr char g_split_output1[] = "Out1";

    /**

     * @enum TestNodeEvent

     * @brief Test Node Event values.

     *

     * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

     */

    enum class TestNodeEvent {
        None = 0,
        Init,
        Start,
        Stop,
        Join,
        Produce,
        Consume,
        Transfer,
        Merge,
        Split,
        MetricsCallbackSet,
        MetricsPublished,
        CompletionSignaled,
        ForcedFailure
    };

    inline std::atomic<uint64_t> g_test_node_event_sequence{0};

/**
 * @class TestNodeInstrumentation
 * @brief Test node instrumentation implementation for GraphX.
 */
    /**
     * @class TestNodeInstrumentation
     * @brief Test Node Instrumentation graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class TestNodeInstrumentation {
    public:
        void Record(TestNodeEvent event) noexcept {
            last_event_.store(static_cast<int>(event), std::memory_order_relaxed);
            last_event_sequence_.store(
                g_test_node_event_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
                std::memory_order_relaxed);
        }

        void RecordInit() noexcept { init_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Init); }
        void RecordStart() noexcept { start_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Start); }
        void RecordStop() noexcept { stop_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Stop); }
        void RecordJoin() noexcept { join_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Join); }
        void RecordProduced() noexcept { produced_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Produce); }
        void RecordConsumed() noexcept { consumed_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Consume); }
        void RecordTransferred() noexcept { transferred_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Transfer); }
        void RecordMerged() noexcept { merged_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Merge); }
        void RecordSplit() noexcept { split_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::Split); }
        void RecordMetricsCallbackSet() noexcept { metrics_callback_set_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::MetricsCallbackSet); }
        void RecordMetricsPublished() noexcept { metrics_published_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::MetricsPublished); }
        void RecordCompletionSignaled() noexcept { completion_signal_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::CompletionSignaled); }
        void RecordForcedFailure() noexcept { forced_failure_count_.fetch_add(1, std::memory_order_relaxed); Record(TestNodeEvent::ForcedFailure); }

        size_t GetInitCount() const noexcept { return init_count_.load(std::memory_order_relaxed); }
        size_t GetStartCount() const noexcept { return start_count_.load(std::memory_order_relaxed); }
        size_t GetStopCount() const noexcept { return stop_count_.load(std::memory_order_relaxed); }
        size_t GetJoinCount() const noexcept { return join_count_.load(std::memory_order_relaxed); }
        size_t GetProducedCount() const noexcept { return produced_count_.load(std::memory_order_relaxed); }
        size_t GetConsumedCount() const noexcept { return consumed_count_.load(std::memory_order_relaxed); }
        size_t GetTransferredCount() const noexcept { return transferred_count_.load(std::memory_order_relaxed); }
        size_t GetMergedCount() const noexcept { return merged_count_.load(std::memory_order_relaxed); }
        size_t GetSplitCount() const noexcept { return split_count_.load(std::memory_order_relaxed); }
        size_t GetMetricsCallbackSetCount() const noexcept { return metrics_callback_set_count_.load(std::memory_order_relaxed); }
        size_t GetMetricsPublishedCount() const noexcept { return metrics_published_count_.load(std::memory_order_relaxed); }
        size_t GetCompletionSignalCount() const noexcept { return completion_signal_count_.load(std::memory_order_relaxed); }
        size_t GetForcedFailureCount() const noexcept { return forced_failure_count_.load(std::memory_order_relaxed); }
        TestNodeEvent GetLastEvent() const noexcept { return static_cast<TestNodeEvent>(last_event_.load(std::memory_order_relaxed)); }
        uint64_t GetLastEventSequence() const noexcept { return last_event_sequence_.load(std::memory_order_relaxed); }

        void SetFailInit(bool value) noexcept { fail_init_.store(value, std::memory_order_relaxed); }
        void SetFailStart(bool value) noexcept { fail_start_.store(value, std::memory_order_relaxed); }
        void SetFailProcess(bool value) noexcept { fail_process_.store(value, std::memory_order_relaxed); }
        void SetFailStop(bool value) noexcept { fail_stop_.store(value, std::memory_order_relaxed); }

        bool ShouldFailInit() const noexcept { return fail_init_.load(std::memory_order_relaxed); }
        bool ShouldFailStart() const noexcept { return fail_start_.load(std::memory_order_relaxed); }
        bool ShouldFailProcess() const noexcept { return fail_process_.load(std::memory_order_relaxed); }
        bool ShouldFailStop() const noexcept { return fail_stop_.load(std::memory_order_relaxed); }

    private:
        std::atomic<size_t> init_count_{0};
        std::atomic<size_t> start_count_{0};
        std::atomic<size_t> stop_count_{0};
        std::atomic<size_t> join_count_{0};
        std::atomic<size_t> produced_count_{0};
        std::atomic<size_t> consumed_count_{0};
        std::atomic<size_t> transferred_count_{0};
        std::atomic<size_t> merged_count_{0};
        std::atomic<size_t> split_count_{0};
        std::atomic<size_t> metrics_callback_set_count_{0};
        std::atomic<size_t> metrics_published_count_{0};
        std::atomic<size_t> completion_signal_count_{0};
        std::atomic<size_t> forced_failure_count_{0};
        std::atomic<int> last_event_{static_cast<int>(TestNodeEvent::None)};
        std::atomic<uint64_t> last_event_sequence_{0};
        std::atomic<bool> fail_init_{false};
        std::atomic<bool> fail_start_{false};
        std::atomic<bool> fail_process_{false};
        std::atomic<bool> fail_stop_{false};
    };

    // =========================================================================================
    // SourceTestNode - Data Producer
    // =========================================================================================
    
    /**
     * @class SourceTestNode
     * @brief Simple data source for testing with JSON configuration support
     */
    /**
     * @class SourceTestNode
     * @brief Source Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class SourceTestNode 
        : public graph::NamedSourceNode<
            SourceTestNode,
            graph::message::Message>,
          public graph::IConfigurable,
          public graph::IParameterized,
          public graph::IMetricsCallbackProvider {
    public:
        // Port definition - requires for template introspection
        static constexpr char kDataPort[] = "Data";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kDataPort,
                graph::PayloadList<int>>
            >;

        explicit SourceTestNode() = default;

        virtual ~SourceTestNode() = default;

        std::optional<T> Produce(std::integral_constant<std::size_t, port_id>) override{
            if (count_ < message_count_) {
                // Phase 5.5g: Track timing
                auto now = std::chrono::steady_clock::now();
                if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                    first_message_time_ = now;
                }
                last_message_time_ = now;
                
/**
 * @brief Msg.
 * @param count_ Parameter for msg.
 * @return Result of the operation.
 */
                graph::message::Message msg(count_);
                count_++;
                LOG4CXX_TRACE(test_logger, "SourceTestNode produced message: " << (count_ - 1) 
                    << " (total: " << count_ << "/" << message_count_ << ")");
                
                // Phase 2: Publish metrics event if callback is installed
                if (metrics_callback_) {
                    app::metrics::MetricsEvent event;
                    event.timestamp = std::chrono::system_clock::now();
                    event.source = "SourceTestNode";
                    event.event_type = "message_produced";
                    event.data["produced_messages"] = std::to_string(count_);
                    metrics_callback_->PublishAsync(event);
                }
                
                return msg;
            } else {
                LOG4CXX_DEBUG(test_logger, "SourceTestNode completed - produced " << count_ 
                    << " messages, limit was " << message_count_);
                return std::nullopt; // Signal completion after N messages
            }
        }

        /**
         * @brief Configure the source node from JSON
         * @param config_json JSON configuration containing optional "message_count" parameter
         * @throws ConfigError if configuration is invalid
         */
        void Configure(const graph::JsonView& config_json) override {
            try {
                if (config_json.Contains("message_count")) {
                    auto count_result = config_json.TryGetInt("message_count", -1);
                    if (!count_result) {
                        throw count_result.error();
                    }
                    int count = count_result.value();
                    if (count <= 0) {
                        throw graph::ConfigError("message_count must be > 0 (got " + 
                                               std::to_string(count) + ")");
                    }
                    SetMessageCount(static_cast<size_t>(count));
                    LOG4CXX_INFO(test_logger, "SourceTestNode configured with message_count=" << count);
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(test_logger, "SourceTestNode configuration error: " << e.what());
                throw graph::ConfigError(std::string("SourceTestNode configuration error: ") + e.what());
            }
        }
               
        /**
         * @brief Get the number of messages to produce
         * @return Number of messages configured
         */
        size_t GetMessageCount() const { 
            return message_count_; 
        }
        
        /**
         * @brief Get all configurable parameters and their current values
         * @return JsonView with parameter names and values
         */
        graph::JsonView GetParameters() const override {
            parameters_cache_ = nlohmann::json::object();
            parameters_cache_["message_count"] = message_count_;
            return graph::JsonView(parameters_cache_);
        }
        
        /**
         * @brief Get parameter metadata for a specific parameter
         * @param param_name Name of parameter to describe
         * @return JsonView with parameter metadata
         */
        graph::JsonView GetParameterDescription(const std::string& param_name) const override {
            parameter_description_cache_ = nlohmann::json::object();
            
            if (param_name == "message_count") {
                parameter_description_cache_["type"] = "integer";
                parameter_description_cache_["required"] = true;
                parameter_description_cache_["description"] = "Number of messages to produce before stopping";
                parameter_description_cache_["minimum"] = 1;
                parameter_description_cache_["default"] = 10;
                parameter_description_cache_["current"] = message_count_;
            }
            
            return graph::JsonView(parameter_description_cache_);
        }
        
        /**
         * @brief Get list of all available parameter names
         * @return Vector of parameter names
         */
        std::vector<std::string> GetParameterNames() const override {
            return {"message_count"};
        }

        static constexpr std::array<graph::JsonField, 1> Fields() {
            return {
                graph::JsonField{
                    .name = "message_count",
                    .type = graph::JsonType::Integer,
                    .required = true,
                    .min = 1.0,
                    .max = std::nullopt,
                    .default_value = "10",
                    .enum_values = std::nullopt,
                    .description = "Number of messages to produce before stopping"
                }
            };
        }
        
    private:
        /**
         * @brief Set the message count for this source (internal use via Configure)
         * @param count Number of messages to produce
         */
        void SetMessageCount(size_t count) {
            message_count_ = count;
        }
        
        size_t count_{0};
        size_t message_count_{10};
        mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
        mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
        
        // Phase 5.5g: Performance metrics
        std::chrono::steady_clock::time_point first_message_time_{};
        std::chrono::steady_clock::time_point last_message_time_{};
    
    public:
        /**
         * @brief Get total elapsed time for message production (nanoseconds)
         * @return Elapsed time in nanoseconds from first to last message
         */
        uint64_t GetElapsedNs() const {
            if (last_message_time_ == std::chrono::steady_clock::time_point{}) {
                return 0;
            }
            auto duration = last_message_time_ - first_message_time_;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        
        /**
         * @brief Get throughput in messages per second
         * @return Messages produced per second
         */
        double GetThroughputMps() const {
            uint64_t elapsed_ns = GetElapsedNs();
            if (elapsed_ns == 0 || count_ == 0) return 0.0;
            return static_cast<double>(count_) / (static_cast<double>(elapsed_ns) / 1e9);
        }
        
        /**
         * @brief Get average latency per message (nanoseconds)
         * @return Average nanoseconds per message
         */
        double GetAvgLatencyNs() const {
            if (count_ == 0) return 0.0;
            return static_cast<double>(GetElapsedNs()) / count_;
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "produced_messages"},
                {"type", "integer"},
                {"description", "Number of messages produced by this source"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "throughput_mps"},
                {"type", "number"},
                {"description", "Messages produced per second"},
                {"unit", "mps"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "SourceTestNode",
                .node_type = "source",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_produced"},
                .display_hints = nlohmann::json::object()
            };
        }
        
    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
    };
    
    // SinkTestNode - Alternative sink node
    // =========================================================================================
    
    /**
     * @class SinkTestNode
     * @brief Sink node for provider testing with completion callback and JSON configuration support
     * 
     * Implements ICompletionCallback to signal graph completion when a threshold
     * of messages has been consumed. This enables CompletionPolicy to automatically
     * detect when graph processing is complete.
     * 
     * Implements IConfigurable to support dynamic configuration via JSON for use with
     * NodeFacade and dynamic node loading.
     */
    /**
     * @class SinkTestNode
     * @brief Sink Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class SinkTestNode : public graph::NamedSinkNode<SinkTestNode, ::graph::message::Message>,
                        public graph::ICompletionCallback<::graph::message::CompletionSignal>,
                        public graph::IConfigurable,
                        public graph::IParameterized,
                        public graph::IMetricsCallbackProvider {
    public: 
        static constexpr char kStatePort[] = "State";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<int>>
            >;
            
        /**
         * @brief Construct sink node with optional expected message count
         * @param expected_messages Number of messages expected before signaling completion (0 = no auto-completion)
         */
        explicit SinkTestNode() = default;

        virtual ~SinkTestNode() = default;

        /**
         * @brief Consume message and track count for completion detection
         * @param msg The message to consume
         * @return true if message accepted, false if rejected
         */
        bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            // Phase 5.5g: Track timing
            bool ret = true;
            auto now = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = now;
            }
            last_message_time_ = now;
            
            (void)msg;
            message_count_++;
            LOG4CXX_TRACE(test_logger, "SinkTestNode consumed message (total: " << message_count_ 
                << "/" << expected_message_count_ << ")");
            
            // Phase 2: Publish metrics event if callback is installed
            // Protected by mutex: input edge thread calls this method
            // Lifetime fix: MetricsPolicy keeps callback alive in shared_ptr map until OnJoin()
            if (metrics_callback_) {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "SinkTestNode";
                event.event_type = "message_consumed";
                event.data["consumed_count"] = std::to_string(message_count_);
                ret = metrics_callback_->PublishAsync(event);
            }
      
            // Signal completion when expected message count reached
            if (expected_message_count_ > 0 && 
                message_count_ >= expected_message_count_) {
                LOG4CXX_INFO(test_logger, "SinkTestNode reached completion threshold: " 
                    << message_count_ << " >= " << expected_message_count_);
                SignalCompletion();
            }
            
            return ret;
        }

        /**
         * @brief Configure the sink node from JSON
         * @param config_json JSON configuration containing optional "expected_message_count" parameter
         * @throws ConfigError if configuration is invalid
         */
        void Configure(const graph::JsonView& config_json) override {
            try {
                if (config_json.Contains("expected_message_count")) {
                    auto count_result = config_json.TryGetInt("expected_message_count", -1);
                    if (!count_result) {
                        throw count_result.error();
                    }
                    int count = count_result.value();
                    if (count <= 0) {
                        throw graph::ConfigError("expected_message_count must be > 0 (got " + 
                                               std::to_string(count) + ")");
                    }
                    SetExpectedMessageCount(static_cast<size_t>(count));
                    LOG4CXX_INFO(test_logger, "SinkTestNode configured with expected_message_count=" << count);
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(test_logger, "SinkTestNode configuration error: " << e.what());
                throw graph::ConfigError(std::string("SinkTestNode configuration error: ") + e.what());
            }
        }
        
        /**
         * @brief Get the number of messages consumed
         * @return Number of messages processed via Consume()
         */
        size_t GetMessageCount() const { 
            return message_count_; 
        }
        
        // Phase 5.5g: Performance metrics
        /**
         * @brief Get total elapsed time for message consumption (nanoseconds)
         * @return Elapsed time in nanoseconds from first to last message
         */
        uint64_t GetElapsedNs() const {
            if (last_message_time_ == std::chrono::steady_clock::time_point{}) {
                return 0;
            }
            auto duration = last_message_time_ - first_message_time_;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        
        /**
         * @brief Get throughput in messages per second
         * @return Messages consumed per second
         */
        double GetThroughputMps() const {
            uint64_t elapsed_ns = GetElapsedNs();
            if (elapsed_ns == 0 || message_count_ == 0) return 0.0;
            return static_cast<double>(message_count_) / (static_cast<double>(elapsed_ns) / 1e9);
        }
        
        /**
         * @brief Get average latency per message (nanoseconds)
         * @return Average nanoseconds per message
         */
        double GetAvgLatencyNs() const {
            if (message_count_ == 0) return 0.0;
            return static_cast<double>(GetElapsedNs()) / message_count_;
        }
        
        /**
         * @brief Get all configurable parameters and their current values
         * @return JsonView with parameter names and values
         */
        graph::JsonView GetParameters() const override {
            parameters_cache_ = nlohmann::json::object();
            parameters_cache_["expected_message_count"] = expected_message_count_;
            return graph::JsonView(parameters_cache_);
        }
        
        /**
         * @brief Get parameter metadata for a specific parameter
         * @param param_name Name of parameter to describe
         * @return JsonView with parameter metadata
         */
        graph::JsonView GetParameterDescription(const std::string& param_name) const override {
            parameter_description_cache_ = nlohmann::json::object();
            
            if (param_name == "expected_message_count") {
                parameter_description_cache_["type"] = "integer";
                parameter_description_cache_["required"] = true;
                parameter_description_cache_["description"] = "Number of messages to consume before signaling completion";
                parameter_description_cache_["minimum"] = 1;
                parameter_description_cache_["default"] = 10;
                parameter_description_cache_["current"] = expected_message_count_;
            }
            
            return graph::JsonView(parameter_description_cache_);
        }
        
        /**
         * @brief Get list of all available parameter names
         * @return Vector of parameter names
         */
        std::vector<std::string> GetParameterNames() const override {
            return {"expected_message_count"};
        }

        static constexpr std::array<graph::JsonField, 1> Fields() {
            return {
                graph::JsonField{
                    .name = "expected_message_count",
                    .type = graph::JsonType::Integer,
                    .required = true,
                    .min = 1.0,
                    .max = std::nullopt,
                    .default_value = "10",
                    .enum_values = std::nullopt,
                    .description = "Number of messages to consume before signaling completion"
                }
            };
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "consumed_messages"},
                {"type", "integer"},
                {"description", "Number of messages consumed by this sink"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "throughput_mps"},
                {"type", "number"},
                {"description", "Messages consumed per second"},
                {"unit", "mps"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "SinkTestNode",
                .node_type = "sink",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_consumed"},
                .display_hints = nlohmann::json::object()
            };
        }
        
    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
        std::mutex metrics_mutex_;  // Protect concurrent metrics publishing from multiple sinks
        /**
         * @brief Set the expected message count for completion detection (internal use via Configure)
         * @param count Number of messages to expect before completion signal
         */
        void SetExpectedMessageCount(size_t count) {
            expected_message_count_ = count;
        }
        /**
         * @brief Signal completion to the CompletionPolicy
         * 
         * Invokes the installed callback provider's OnComplete() method,
         * which notifies the CompletionPolicy that this sink is finished processing.
         */
        void SignalCompletion() {
            LOG4CXX_DEBUG(test_logger, "SinkTestNode::SignalCompletion() called - checking callback provider");
            
            if (this->HasCallbackProvider()) {
                LOG4CXX_DEBUG(test_logger, "SinkTestNode has callback provider - calling OnComplete()");
                auto provider = dynamic_cast<CompletionNodeCallback*>(this->GetCallbackProvider());
                assert(provider != nullptr);   // conservative check 
                provider->OnComplete();
            } else {
                LOG4CXX_WARN(test_logger, "SinkTestNode has NO callback provider - completion signal cannot be fired");
            }
        }
        
        std::atomic<size_t> message_count_{0};
        size_t expected_message_count_{10};
        mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
        mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
        
        // Phase 5.5g: Performance metrics
        std::chrono::steady_clock::time_point first_message_time_{};
        std::chrono::steady_clock::time_point last_message_time_{};
    };

    // =========================================================================================
    // OptionalConfigTestNode - Configurable node with no required config fields
    // =========================================================================================

/**
 * @class OptionalConfigTestNode
 * @brief Optional config test node implementation for GraphX.
 */
    /**
     * @class OptionalConfigTestNode
     * @brief Optional Config Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class OptionalConfigTestNode
        : public graph::NamedSinkNode<OptionalConfigTestNode, ::graph::message::Message>,
          public graph::IConfigurable,
          public graph::IParameterized {
    public:
        static constexpr char kStatePort[] = "State";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<int>>
            >;

        explicit OptionalConfigTestNode() = default;
        virtual ~OptionalConfigTestNode() = default;

        bool Consume(const ::graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            (void)msg;
            return true;
        }

        void Configure(const graph::JsonView& config_json) override {
            (void)config_json;
        }

        graph::JsonView GetParameters() const override {
            parameters_cache_ = nlohmann::json::object();
            return graph::JsonView(parameters_cache_);
        }

        graph::JsonView GetParameterDescription(const std::string& param_name) const override {
            (void)param_name;
            parameter_description_cache_ = nlohmann::json::object();
            return graph::JsonView(parameter_description_cache_);
        }

        std::vector<std::string> GetParameterNames() const override {
            return {};
        }

    private:
        mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
        mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
    };
    
    // =========================================================================================
    // FailingTestNode - Tests error handling
    // =========================================================================================
    
    /**
     * @class FailingTestNode
     * @brief Node that can be configured to report failures
     */
    /**
     * @class FailingTestNode
     * @brief Failing Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class FailingTestNode : public graph::NamedSinkNode<FailingTestNode, graph::message::Message>,
                           public graph::IMetricsCallbackProvider {
    public: 
        static constexpr char kStatePort[] = "State";
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort,
                graph::PayloadList<int>>
            >;
            
        explicit FailingTestNode() = default;

        virtual ~FailingTestNode() = default;

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            (void)msg;
            
            // Phase 2: Publish metrics event if callback is installed
            if (metrics_callback_) {
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "FailingTestNode";
                event.event_type = "message_consumed";
                event.data["consumed"] = "true";
                metrics_callback_->PublishAsync(event);
            }
            
            return true;
        }
        
        /// Control whether Init() should fail
        void SetFailInit(bool fail) { should_fail_init_ = fail; }
        
        /// Returns false if SetFailInit(true) was called
        bool Init() override {
            if (should_fail_init_) {
                return false;
            }
            return graph::NamedSinkNode<FailingTestNode, ::graph::message::Message>::Init();
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "consumed"},
                {"type", "boolean"},
                {"description", "Whether a message was consumed"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "FailingTestNode",
                .node_type = "sink",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_consumed"},
                .display_hints = nlohmann::json::object()
            };
        }
        
    private:
        std::atomic<bool> should_fail_init_{false};
        graph::IMetricsCallback* metrics_callback_{nullptr};
    };

/**
 * @class NSinkTestNode
 * @brief N sink test node implementation for GraphX.
 */
    /**
     * @class NSinkTestNode
     * @brief Nsink Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class NSinkTestNode : public graph::NamedSinkNode<NSinkTestNode,
                                graph::message::Message, 
                                graph::message::Message, 
                                graph::message::Message, 
                                graph::message::Message, 
                                graph::message::Message>,
                         public graph::IMetricsCallbackProvider {
    public:
         
        static constexpr char kStatePort0[] = "State0";
        static constexpr char kStatePort1[] = "State1";
        static constexpr char kStatePort2[] = "State2";
        static constexpr char kStatePort3[] = "State3";
        static constexpr char kStatePort4[] = "State4";

        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kStatePort0,
                graph::PayloadList<int>>,
            graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Input, kStatePort1,
                graph::PayloadList<int>>,
            graph::PortSpec<2, ::graph::message::Message, graph::PortDirection::Input, kStatePort2,
                graph::PayloadList<int>>,
            graph::PortSpec<3, ::graph::message::Message, graph::PortDirection::Input, kStatePort3,
                graph::PayloadList<int>>,
            graph::PortSpec<4, ::graph::message::Message, graph::PortDirection::Input, kStatePort4,
                graph::PayloadList<int>>
            >;

        explicit NSinkTestNode() = default;

        /**
         * @brief Construct aggregator
         */
        virtual ~NSinkTestNode() = default;

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 0>) override {
            (void)msg;
            PublishMetrics();
            return true;
        }

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 1>) override {
            (void)msg;
            PublishMetrics();
            return true;
        }

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 2>) override {
            (void)msg;
            PublishMetrics();
            return true;
        }

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 3>) override {
            (void)msg;
            PublishMetrics();
            return true;
        }

        bool Consume(const graph::message::Message& msg, std::integral_constant<std::size_t, 4>) override {
            (void)msg;
            PublishMetrics();
            return true;
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "consumed_count"},
                {"type", "integer"},
                {"description", "Number of messages consumed across all input ports"},
                {"unit", "count"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "NSinkTestNode",
                .node_type = "sink",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_consumed"},
                .display_hints = nlohmann::json::object()
            };
        }
    
    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
        std::atomic<size_t> consume_count_{0};
        mutable std::mutex metrics_mutex_;  ///< Protects metrics publishing from concurrent edge threads
        
        void PublishMetrics() noexcept {
            consume_count_++;
            // Phase 2: Publish metrics event if callback is installed
            // Protected by mutex: each of 5 input edges may call this concurrently
            if (metrics_callback_) {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "NSinkTestNode";
                event.event_type = "message_consumed";
                event.data["consumed_count"] = std::to_string(consume_count_);
                metrics_callback_->PublishAsync(event);
            }
        }

    };


    // =========================================================================================
    // InteriorTestNode - Message processor with metrics tracking
    // =========================================================================================
    // InteriorNodeBase requires PortSpec in template parameters with string literals,
    // which C++ doesn't allow. A future refactor could use a different approach.
    //
    // This node implements IMetricsCallbackProvider to track message transfers and publish
    // metrics events, enabling testing of the MetricsCapability framework.
    
/**
 * @class InteriorTestNode
 * @brief Interior test node implementation for GraphX.
 */
    /**
     * @class InteriorTestNode
     * @brief Interior Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class InteriorTestNode
        : public graph::NamedInteriorNode<
              graph::TypeList<graph::message::Message>,
              graph::TypeList<graph::message::Message>,
              InteriorTestNode>,
          public graph::IMetricsCallbackProvider {
    public:
        
        static constexpr char kInput[] = "Input";
        static constexpr char kOutput[] = "Output";  

        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kInput,
                graph::PayloadList<int>>,
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutput,
                graph::PayloadList<int>>
        >;

        InteriorTestNode() {
            SetName("InteriorTestNode");
        }
        
        virtual ~InteriorTestNode() = default;

        /**
         * @brief Process message and track metrics
         * @param input The input message to transfer
         * @return The transferred message, with metrics event published
         */
        std::optional<::graph::message::Message> Transfer(
            const ::graph::message::Message& input,
            std::integral_constant<std::size_t, 0>,
            std::integral_constant<std::size_t, 0>) override {
            
            // Phase 5.5g: Track timing
            auto now = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = now;
            }
            last_message_time_ = now;
            
            // Increment transfer counter
            message_count_++;
            
            // Phase 2: Publish metrics event if callback is installed
            // Protected by mutex: input edge thread calls this method
            if (metrics_callback_) {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "InteriorTestNode";
                event.event_type = "message_transfer";
                event.data["transferred_messages"] = std::to_string(message_count_);
                metrics_callback_->PublishAsync(event);
            }
            
            return input;
        }

        std::optional<::graph::message::Message> Process(
            const ::graph::message::Message& input,
            std::integral_constant<std::size_t, 0> port) {
            return Transfer(input, port, std::integral_constant<std::size_t, 0>{});
        }

        // /**
        //  * @brief Set the metrics callback provider
        //  * @param callback Pointer to the callback handler (may be nullptr)
        //  * @return true if callback was successfully set
        //  */
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }

        // /**
        //  * @brief Check if a metrics callback is installed
        //  * @return true if a callback provider is currently set
        //  */
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }

        // /**
        //  * @brief Get the currently installed callback provider
        //  * @return Pointer to callback provider, or nullptr if none installed
        //  */
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }

        // /**
        //  * @brief Get the node's metrics schema for discovery
        //  * @return Schema describing available metrics
        //  */
        app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "transferred_messages"},
                {"type", "integer"},
                {"description", "Number of messages transferred through this node"},
                {"unit", "count"}
            }));

            return app::metrics::NodeMetricsSchema{
                .node_name = "InteriorTestNode",
                .node_type = "processor",
                .metrics_schema = metrics_json,
                .event_types = {"message_transfer"},
                .display_hints = nlohmann::json::object()
            };
        }

        /**
         * @brief Get the number of messages transferred
         * @return Number of messages processed via Transfer()
         */
        size_t GetMessageCount() const {
            return message_count_;
        }
        
        // Phase 5.5g: Performance metrics
        /**
         * @brief Get total elapsed time for message processing (nanoseconds)
         * @return Elapsed time in nanoseconds from first to last message
         */
        uint64_t GetElapsedNs() const {
            if (last_message_time_ == std::chrono::steady_clock::time_point{}) {
                return 0;
            }
            auto duration = last_message_time_ - first_message_time_;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        
        /**
         * @brief Get throughput in messages per second
         * @return Messages processed per second
         */
        double GetThroughputMps() const {
            uint64_t elapsed_ns = GetElapsedNs();
            if (elapsed_ns == 0 || message_count_ == 0) return 0.0;
            return static_cast<double>(message_count_) / (static_cast<double>(elapsed_ns) / 1e9);
        }
        
        /**
         * @brief Get average latency per message (nanoseconds)
         * @return Average nanoseconds per message
         */
        double GetAvgLatencyNs() const {
            if (message_count_ == 0) return 0.0;
            return static_cast<double>(GetElapsedNs()) / message_count_;
        }

    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
        std::atomic<size_t> message_count_{0};
        mutable std::mutex metrics_mutex_;  ///< Protects metrics publishing from edge thread
        
        // Phase 5.5g: Performance metrics
        std::chrono::steady_clock::time_point first_message_time_{};
        std::chrono::steady_clock::time_point last_message_time_{};
    };

    // =========================================================================================
    // MergeTestNode - Multi-Input Merge Node (2 inputs -> 1 output)
    // =========================================================================================
    
    /**
     * @class MergeTestNode
     * @brief Merge node combining two input streams into one output
     *
     * Tracks message counts from each input port for advanced metrics validation.
     */
    /**
     * @class MergeTestNode
     * @brief Merge Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class MergeTestNode 
        : public graph::MergeNode<2, ::graph::message::Message, ::graph::message::Message, MergeTestNode>,
          public graph::IMetricsCallbackProvider {
    public:
        static constexpr char kInput0[] = "In0";
        static constexpr char kInput1[] = "In1";
        static constexpr char kOutput[] = "Out";
        
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kInput0,
                graph::PayloadList<int>>,
            graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Input, kInput1,
                graph::PayloadList<int>>,
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutput,
                graph::PayloadList<int>>
        >;
        
        explicit MergeTestNode() = default;
        
        virtual ~MergeTestNode() = default;
        
        /// Process method: pass through merged messages
        std::optional<::graph::message::Message> Process(
            const ::graph::message::Message& input,
            std::integral_constant<std::size_t, 0>) override {
            // Phase 5.5g: Track timing
            auto now = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = now;
            }
            last_message_time_ = now;
            
            // Track which input port the message came from
            // This is a simplified counter - base class handles actual merging
            merged_message_count_++;
            LOG4CXX_TRACE(test_logger, "MergeTestNode processed message, count=" << merged_message_count_);
            
            // Phase 2: Publish metrics event if callback is installed
            // Protected by mutex: 2 input edges may call this concurrently
            if (metrics_callback_) {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "MergeTestNode";
                event.event_type = "message_merged";
                event.data["merged_messages"] = std::to_string(merged_message_count_);
                event.data["throughput_mps"] = std::to_string(GetThroughputMps());
                metrics_callback_->PublishAsync(event);
            }
            
            return input;
        }
        
        /// Get total messages merged from both inputs
        size_t GetMergedMessageCount() const {
            return merged_message_count_;
        }
        
        /// Get message count from specific input port
        size_t GetInputMessageCount(size_t port_id) const {
            if (port_id == 0) return input0_count_;
            if (port_id == 1) return input1_count_;
            return 0;
        }
        
        // Phase 5.5g: Performance metrics for MergeTestNode
        /**
         * @brief Get total elapsed time for message merging (nanoseconds)
         * @return Elapsed time in nanoseconds from first to last message
         */
        uint64_t GetElapsedNs() const {
            if (last_message_time_ == std::chrono::steady_clock::time_point{}) {
                return 0;
            }
            auto duration = last_message_time_ - first_message_time_;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        
        /**
         * @brief Get throughput in messages per second
         * @return Messages merged per second
         */
        double GetThroughputMps() const {
            uint64_t elapsed_ns = GetElapsedNs();
            if (elapsed_ns == 0 || merged_message_count_ == 0) return 0.0;
            return static_cast<double>(merged_message_count_) / (static_cast<double>(elapsed_ns) / 1e9);
        }
        
        /**
         * @brief Get average latency per message (nanoseconds)
         * @return Average nanoseconds per message
         */
        double GetAvgLatencyNs() const {
            if (merged_message_count_ == 0) return 0.0;
            return static_cast<double>(GetElapsedNs()) / merged_message_count_;
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "merged_messages"},
                {"type", "integer"},
                {"description", "Number of messages merged from both inputs"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "throughput_mps"},
                {"type", "number"},
                {"description", "Messages merged per second"},
                {"unit", "mps"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "MergeTestNode",
                .node_type = "merge",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_merged"},
                .display_hints = nlohmann::json::object()
            };
        }
    
    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
        std::atomic<size_t> merged_message_count_{0};   // Total messages processed
        std::atomic<size_t> input0_count_{0};            // Messages from input 0
        std::atomic<size_t> input1_count_{0};            // Messages from input 1
        mutable std::mutex metrics_mutex_;               ///< Protects metrics publishing from concurrent edge threads
        std::chrono::steady_clock::time_point first_message_time_{};
        std::chrono::steady_clock::time_point last_message_time_{};
    };

    // =========================================================================================
    // SplitTestNode - Single Input to Multiple Outputs (1 input -> 2 outputs)
    // =========================================================================================
    
    /**
     * @class SplitTestNode
     * @brief Split node that replicates input to multiple output streams
     *
     * Tracks message counts through each output port for advanced metrics validation.
     */
    /**
     * @class SplitTestNode
     * @brief Split Test Node graph node.
     *
     * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
     */
    class SplitTestNode
        : public graph::SplitNode2<::graph::message::Message>,
          public graph::IMetricsCallbackProvider {
    public:
        static constexpr char kInput[] = "In";
        static constexpr char kOutput0[] = "Out0";
        static constexpr char kOutput1[] = "Out1";
        
        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kInput,
                graph::PayloadList<int>>,
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutput0,
                graph::PayloadList<int>>,
            graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Output, kOutput1,
                graph::PayloadList<int>>
        >;
        
        explicit SplitTestNode() = default;

        std::string GetNodeTypeName() const {
            return "SplitTestNode";
        }
        
        virtual ~SplitTestNode() = default;
        
        /// Consume method: replicate input to both output queues
        bool Consume(const ::graph::message::Message& msg, 
                     std::integral_constant<std::size_t, 0>) override {
            // Phase 5.5g: Track timing
            auto now = std::chrono::steady_clock::now();
            if (first_message_time_ == std::chrono::steady_clock::time_point{}) {
                first_message_time_ = now;
            }
            last_message_time_ = now;
            
            input_message_count_++;
            bool success = true;
            success &= input_queue_[0].Enqueue(msg);
            if (success) output0_count_++;
            success &= input_queue_[1].Enqueue(msg);
            if (success) output1_count_++;
            LOG4CXX_TRACE(test_logger, "SplitTestNode replicated message, inputs=" 
                << input_message_count_ << " out0=" << output0_count_ 
                << " out1=" << output1_count_);
            
            // Phase 2: Publish metrics event if callback is installed
            // Protected by mutex: input edge thread calls this method
            if (metrics_callback_) {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                app::metrics::MetricsEvent event;
                event.timestamp = std::chrono::system_clock::now();
                event.source = "SplitTestNode";
                event.event_type = "message_split";
                event.data["input_count"] = std::to_string(input_message_count_);
                event.data["output0_count"] = std::to_string(output0_count_);
                event.data["output1_count"] = std::to_string(output1_count_);
                event.data["throughput_mps"] = std::to_string(GetThroughputMps());
                metrics_callback_->PublishAsync(event);
            }
            
            return success;
        }
        
        /// Get total input messages received
        size_t GetInputMessageCount() const {
            return input_message_count_;
        }
        
        /// Get total replications to specific output port
        size_t GetOutputMessageCount(size_t port_id) const {
            if (port_id == 0) return output0_count_;
            if (port_id == 1) return output1_count_;
            return 0;
        }
        
        /// Get total messages sent (sum of all outputs)
        size_t GetTotalOutputCount() const {
            return output0_count_ + output1_count_;
        }
        
        // Phase 5.5g: Performance metrics for SplitTestNode
        /**
         * @brief Get total elapsed time for message splitting (nanoseconds)
         * @return Elapsed time in nanoseconds from first to last message
         */
        uint64_t GetElapsedNs() const {
            if (last_message_time_ == std::chrono::steady_clock::time_point{}) {
                return 0;
            }
            auto duration = last_message_time_ - first_message_time_;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        
        /**
         * @brief Get throughput in messages per second
         * @return Messages split per second
         */
        double GetThroughputMps() const {
            uint64_t elapsed_ns = GetElapsedNs();
            if (elapsed_ns == 0 || input_message_count_ == 0) return 0.0;
            return static_cast<double>(input_message_count_) / (static_cast<double>(elapsed_ns) / 1e9);
        }
        
        /**
         * @brief Get average latency per message (nanoseconds)
         * @return Average nanoseconds per message
         */
        double GetAvgLatencyNs() const {
            if (input_message_count_ == 0) return 0.0;
            return static_cast<double>(GetElapsedNs()) / input_message_count_;
        }
        
        // IMetricsCallbackProvider implementation
        virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
            metrics_callback_ = callback;
            return callback != nullptr;
        }
        
        virtual bool HasMetricsCallback() const noexcept override {
            return metrics_callback_ != nullptr;
        }
        
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
            return metrics_callback_;
        }
        
        virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            nlohmann::json metrics_json = nlohmann::json::object();
            metrics_json["fields"] = nlohmann::json::array();
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "input_count"},
                {"type", "integer"},
                {"description", "Number of input messages received"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "output0_count"},
                {"type", "integer"},
                {"description", "Number of replications to output 0"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "output1_count"},
                {"type", "integer"},
                {"description", "Number of replications to output 1"},
                {"unit", "count"}
            }));
            metrics_json["fields"].push_back(nlohmann::json::object({
                {"name", "throughput_mps"},
                {"type", "number"},
                {"description", "Messages split per second"},
                {"unit", "mps"}
            }));
            return app::metrics::NodeMetricsSchema{
                .node_name = "SplitTestNode",
                .node_type = "split",
                .metrics_schema = metrics_json,
                .event_types = std::vector<std::string>{"message_split"},
                .display_hints = nlohmann::json::object()
            };
        }
    
    private:
        graph::IMetricsCallback* metrics_callback_{nullptr};
        std::atomic<size_t> input_message_count_{0};     // Total input messages
        std::atomic<size_t> output0_count_{0};           // Replications to output 0
        std::atomic<size_t> output1_count_{0};           // Replications to output 1
        mutable std::mutex metrics_mutex_;               ///< Protects metrics publishing from edge thread
        std::chrono::steady_clock::time_point first_message_time_{};
        std::chrono::steady_clock::time_point last_message_time_{};
    };

} // namespace test
