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

#include "app/metrics/MetricsEvent.hpp"
#include "app/metrics/NodeMetricsSchema.hpp"
#include "app/metrics/IMetricsSubscriber.hpp"
#include "graph/CapabilityBus.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

/**
 * @file MetricsCapability.hpp
 * @brief Capability interface for metrics discovery and subscription
 *
 * Provides methods for discovering node metrics schemas and registering
 * callbacks to receive metrics events published during graph execution.
 */

namespace app::capabilities {

/**
 * @class MetricsCapability
 * @brief Capability for metrics discovery and real-time event subscription
 *
 * MetricsCapability is the interface through which the dashboard discovers
 * available metrics from graph nodes and subscribes to real-time updates.
 *
 * Key responsibilities:
 * 1. **Discovery**: Provide schemas describing node metrics (names, units, types)
 * 2. **Subscription**: Register callbacks to receive metric events
 * 3. **Publishing**: Notify subscribers when metrics are updated
 *
 * Execution Flow:
 * 1. Dashboard calls GetNodeMetricsSchemas() during initialization
 * 2. Creates MetricsTiles from schemas for UI rendering
 * 3. Registers Dashboard as a subscriber via RegisterMetricsCallback()
 * 4. During execution, graph nodes publish metrics through this capability
 * 5. Published metrics trigger OnMetricsEvent() on all registered subscribers
 * 6. Dashboard updates display with new values
 *
 * Thread Safety:
 * - Callback registration/unregistration use mutex protection
 * - Publishing should be done from executor thread
 * - Subscriber callbacks are invoked from publishing thread
 * - Dashboard must handle thread-safe updates in OnMetricsEvent()
 *
 * Example Use:
 * ```cpp
 * auto metrics_cap = bus->GetCapability<MetricsCapability>();
 * 
 * // Discover available metrics
 * auto schemas = metrics_cap->GetNodeMetricsSchemas();
 * for (const auto& schema : schemas) {
 *     std::cout << "Node: " << schema.node_name << "\n";
 * }
 * 
 * // Register for updates
 * metrics_cap->RegisterMetricsCallback(subscriber);
 * 
 * // During execution, MetricsPolicy publishes events:
 * app::metrics::MetricsEvent event;
 * event.node_name = "Sensor";
 * event.metric_id = "temperature";
 * event.data = 42.5;
 * metrics_cap->PublishMetricsEvent(event);
 * 
 * // All registered subscribers are notified
 * ```
 *
 * @see IMetricsSubscriber, MetricsEvent, NodeMetricsSchema
 */
class MetricsCapability {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~MetricsCapability() = default;
    
    /**
     * @brief Register a callback to receive metrics events
     *
     * Called during dashboard initialization to subscribe to metric updates.
     * The callback will be invoked for each metric event during execution.
     *
     * @param callback The subscriber to register
     *
     * @see UnregisterMetricsCallback, IMetricsSubscriber
     */
    virtual void RegisterMetricsCallback(
        app::metrics::IMetricsSubscriber* callback) {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        subscribers_.push_back(callback);
    }

    /**
     * @brief Unregister a callback from metrics events
     *
     * Removes the subscriber from the notification list.
     *
     * @param callback The subscriber to unregister
     */
    virtual void UnregisterMetricsCallback(
        app::metrics::IMetricsSubscriber* callback) {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), callback),
            subscribers_.end());
    }   

    /**
     * @brief Get the number of registered metric callbacks
     *
     * @return Count of currently registered subscribers
     */
    virtual size_t GetCallbackCount() {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        return subscribers_.size();
    }   

    /**
     * @brief Set the metrics schemas discovered from the graph
     *
     * Called by the graph builder to populate available metrics descriptions.
     * Should be called before RegisterMetricsCallback().
     *
     * @param schemas Vector of metric schemas for all nodes
     *
     * @see GetNodeMetricsSchemas, NodeMetricsSchema
     */
    virtual void SetNodeMetricsSchemas(std::vector<app::metrics::NodeMetricsSchema> schemas) {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        schemas_ = schemas;
    }

    /**
     * @brief Get all available node metrics schemas
     *
     * Returns the metric schema for each node in the graph.
     * Used by dashboard to discover what metrics to display.
     *
     * @return Vector of NodeMetricsSchema describing all nodes
     *
     * @see NodeMetricsSchema
     */
    std::vector<app::metrics::NodeMetricsSchema> GetNodeMetricsSchemas() {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        return schemas_;
    }
    
    /**
     * @brief Invoke all registered subscribers with a metrics event
     *
     * Notifies all registered subscribers that a metric has been updated.
     * Typically called by MetricsPolicy during graph execution.
     * Invocation happens on the publishing thread; subscribers must be thread-safe.
     *
     * @param event The metrics event to publish (node name, metric, value, timestamp)
     *
     * @see MetricsEvent, IMetricsSubscriber, MetricsPolicy
     */
    virtual void InvokeSubscribers(const app::metrics::MetricsEvent& event) {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        for (auto& subscriber : subscribers_) {
            if (subscriber) {
                subscriber->OnMetricsEvent(event);
            }
        }
    }
    
protected:
    /// @brief Mutex protecting subscriber list and schemas
    std::mutex callbacks_lock_;
    
    /// @brief List of registered metrics subscribers
    std::vector<app::metrics::IMetricsSubscriber*> subscribers_;   
    
    /// @brief Cached metric schemas discovered from graph nodes
    std::vector<app::metrics::NodeMetricsSchema> schemas_;
};

}  // namespace app::capabilities
