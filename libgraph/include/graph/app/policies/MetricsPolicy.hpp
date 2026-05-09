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

#include <memory>
#include <chrono>
#include <thread>
#include <log4cxx/logger.h>
#include "core/ActiveQueue.hpp"
#include "app/metrics/MetricsEvent.hpp"
#include "graph/IMetricsCallback.hpp"
#include "graph/IExecutionPolicy.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/MetricsCapability.hpp"



namespace app::policies {

static auto metrics_logger = log4cxx::Logger::getLogger("app.policies.MetricsPolicy");

/**
 * @struct MetricsCapabilityCallback
 * @brief Concrete callback implementation for publishing metrics to capability bus
 *
 * Implements IMetricsCallback interface and bridges metrics events from graph
 * nodes to the MetricsCapability via PublishAsync().
 *
 * @see IMetricsCallback, MetricsPolicy, MetricsCapability
 */
struct MetricsCapabilityCallback : public graph::IMetricsCallback {
    /**
     * @brief Publish a metrics event asynchronously
     *
     * Forwards the event to the registered on_publish_async_ handler,
     * which routes it to MetricsCapability for subscriber notification.
     *
     * @param event The metrics event to publish
     */
    void PublishAsync(const app::metrics::MetricsEvent& event) noexcept override{
        if (on_publish_async_) {
            on_publish_async_(event);
        }
    }
    
    /// @brief Callback function for publishing metrics events
    std::function<void(const app::metrics::MetricsEvent&)> on_publish_async_;
};


/**
 * @class MetricsPolicy
 * @brief Execution policy that manages metrics publishing infrastructure
 *
 * MetricsPolicy implements the IExecutionPolicy interface to set up and manage
 * the metrics publishing system during graph execution.
 *
 * Key responsibilities:
 * 1. **Initialization**: Create MetricsCapability and register in CapabilityBus
 * 2. **Metrics Source Setup**: Discover and initialize metrics from all nodes
 * 3. **Event Queue Management**: Async queue for metrics events from graph execution thread
 * 4. **Subscriber Publishing**: Dequeue events and forward to all registered subscribers
 * 5. **Thread Management**: Spawn background thread for metrics event distribution
 *
 * Lifecycle:
 * - OnInit(): Create MetricsCapability, discover metrics sources, register callback providers
 * - OnStart(): Spawn async publishing thread that dequeues and distributes events
 * - OnStop(): Signal thread shutdown, collect final metrics
 * - OnDone(): Clean up resources and thread
 *
 * Thread Model:
 * - Graph nodes run on executor thread, publish to metrics_event_queue_
 * - MetricsPolicy spawns separate thread for dequeuing and distributing
 * - Dashboard runs on main thread, receives events from MetricsCapability
 * - No blocking operations between threads (lockfree queue for thread safety)
 *
 * Example integration (from GraphExecutor initialization):
 * ```cpp
 * auto metrics_policy = std::make_shared<MetricsPolicy>();
 * ExecutionPolicyChain policies;
 * policies.Push(metrics_policy);
 * 
 * GraphExecutor executor;
 * executor.SetPolicies(policies);
 * executor.Init(config);  // MetricsPolicy::OnInit() called
 * executor.Start();       // MetricsPolicy::OnStart() called
 * 
 * // During execution, graph nodes publish metrics
 * // MetricsPolicy routes them to subscribed dashboard
 * 
 * executor.Stop();        // MetricsPolicy::OnStop() called
 * ```
 *
 * @see IExecutionPolicy, MetricsCapability, MetricsEvent
 */
class MetricsPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Construct a metrics policy with logging
     */
    MetricsPolicy() {
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy initialized");
    }   

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~MetricsPolicy() = default;

    /**
     * @brief Initialize metrics infrastructure during graph setup
     *
     * Called by GraphExecutor during Init() phase. Creates MetricsCapability,
     * registers it with the CapabilityBus, and discovers all metrics sources.
     *
     * @param context GraphExecutorContext with capability bus and graph reference
     * @return True if initialization succeeded, false on error
     *
     * @see OnStart, MetricsCapability
     */
    bool OnInit(app::capabilities::GraphCapability& context) override {
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy OnInit called");
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::OnInit() - creating MetricsCapability");
        metrics_capability_ = std::make_shared<app::capabilities::MetricsCapability>();
        context.GetCapabilityBus().Register<app::capabilities::MetricsCapability>(metrics_capability_);
        InitMetricsSources(context);
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::OnInit() - MetricsCapability registered in CapabilityBus");
        return true;
    }

    /**
     * @brief Start the metrics publishing thread
     *
     * Called by GraphExecutor during Start() phase. Spawns a background thread
     * that dequeues metrics events and forwards them to all subscribers.
     *
     * @param context GraphExecutorContext for accessing shared resources
     * @return True if thread startup succeeded, false on error
     *
     * @see OnStop, OnDone
     */
    bool OnStart(app::capabilities::GraphCapability& context) override
    {
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::OnStart()");
        bool ret = true;
        auto fn = [this, &context]()
        {
            while (!context.IsStopped())
            {
                app::metrics::MetricsEvent event;
                if (metrics_event_queue_.Dequeue(event))
                {
                    LOG4CXX_TRACE(metrics_logger, "MetricsPolicy: Publishing metrics event from Node '" 
                        << event.source << "' "
                        << "of type '" << event.event_type << "' with " << event.data.size() << " fields:");
                    for (const auto &[field, value] : event.data)
                    {
                        LOG4CXX_TRACE(metrics_logger, "  Field: " << field << " = " << value);
                    }
                    metrics_capability_->InvokeSubscribers(event);
                }
                else
                {
                    context.SetStopped();
                }
            }
        };
        metrics_thread_ = std::thread(fn);
        return ret;
    }

    void OnStop(app::capabilities::GraphCapability &) override
    {
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy OnStop called");
        metrics_event_queue_.Disable();
        // Stop metrics collection and cleanup here if needed
    }

    void OnJoin(app::capabilities::GraphCapability &) override {
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy OnJoin called");
        if (metrics_thread_.joinable()) {
            metrics_thread_.join();
        }
    }   

    std::unordered_map<std::string, app::metrics::NodeMetricsSchema>& GetNodeMetricsSchemas() {
        return node_metrics_schemas_;
    }

    void AddNodeMetrics( const std::string& node_name,
                        std::shared_ptr<MetricsCapabilityCallback> callback,
                        const app::metrics::NodeMetricsSchema& schema) {
        metrics_node_callbacks_[node_name] = callback;
        node_metrics_schemas_[node_name] = schema;
    }


private:
    void InitMetricsSources(app::capabilities::GraphCapability& context);

    std::thread metrics_thread_;
    core::ActiveQueue<app::metrics::MetricsEvent> metrics_event_queue_;
    std::shared_ptr<app::capabilities::MetricsCapability> metrics_capability_;
    std::unordered_map<std::string, std::shared_ptr<MetricsCapabilityCallback>> metrics_node_callbacks_;
    std::unordered_map<std::string, app::metrics::NodeMetricsSchema> node_metrics_schemas_;

}; // class MetricsPolicy

    
}// namespace app::policies