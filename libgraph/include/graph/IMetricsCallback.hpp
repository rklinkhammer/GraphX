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
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>
#include <map>

/**
 * @file IMetricsCallback.hpp
 * @brief Interfaces for async metrics callbacks from nodes to MetricsPublisher
 *
 * This file defines the callback provider pattern for nodes that publish
 * async metrics events (key state changes, phase transitions, etc.).
 *
 * @section callback_provider_pattern Callback Provider Pattern
 *
 * Follows the same pattern as ICallbackProvider from the codebase:
 *
 * \code
 * class MyNode : public INode, public IMetricsCallbackProvider {
 * private:
 *     IMetricsCallback* metrics_callback_{nullptr};
 *
 * public:
 *     bool SetMetricsCallback(IMetricsCallback* callback) noexcept override {
 *         metrics_callback_ = callback;
 *         return callback != nullptr;
 *     }
 *
 *     bool HasMetricsCallback() const noexcept override {
 *         return metrics_callback_ != nullptr;
 *     }
 *
 * protected:
 *     void OnImportantStateChange() noexcept {
 *         if (metrics_callback_) {
 *             MetricsEvent event{
 *                 .timestamp = std::chrono::system_clock::now(),
 *                 .source = "MyNode",
 *                 .event_type = "state_change",
 *                 .data = {{"key", "value"}}
 *             };
 *             metrics_callback_->PublishAsync(event);
 *         }
 *     }
 * };
 * \endcode
 *
 * @section node_discovery Node Discovery
 *
 * The MetricsPublisher discovers metrics-capable nodes using dynamic_cast:
 *
 * \code
 * for (const auto& node : graph.GetNodes()) {
 *     auto* metrics_node = 
 *         dynamic_cast<IMetricsCallbackProvider*>(node.get());
 *     if (metrics_node) {
 *         // Wire the callback
 *         metrics_node->SetMetricsCallback(adapter.get());
 *     }
 * }
 * \endcode
 *
 * This is 100% exception-safe: dynamic_cast returns nullptr if type doesn't
 * match, never throws an exception.
 *
 * @section exception_safety Exception Safety
 *
 * All methods are marked noexcept. Implementations must never throw.
 * Callback handlers are expected to be thread-safe.
 */

namespace graph {

/**
 * @brief Interface for receiving async metrics events from nodes
 *
 * Implemented by MetricsPublisherAdapter.
 * Nodes call PublishAsync() when important state changes occur.
 *
 * This interface enables nodes to push metrics to the pub/sub system
 * without knowing about MetricsPublisher internals.
 *
 * @note All methods are noexcept - implementations must never throw.
 * @note Callback handlers are expected to be thread-safe.
 */
class IMetricsCallback {
public:
    virtual ~IMetricsCallback() = default;

    /**
     * @brief Publish an async metrics event
     *
     * Called by nodes when important state changes occur:
     * - Flight phase transitions (FlightMonitorNode)
     * - Completion signals (CompletionAggregationNode)
     * - Custom thresholds or state changes
     *
     * Implementation routes to MetricsPublisher for distribution to subscribers.
     *
     * @param event The metrics event with timestamp, source, type, and data
     *
     * @note noexcept: Implementation must never throw
     * @note Thread-safe: May be called from node's Process() thread
     */
    virtual void PublishAsync(const app::metrics::MetricsEvent& event) noexcept = 0;
};

/**
 * @brief Base interface for nodes that provide metrics callbacks
 *
 * Nodes inherit from this interface to indicate they support async metrics publishing.
 * The MetricsPublisher discovers these nodes via dynamic_cast and wires callbacks.
 *
 * Implementation pattern (same as ICallbackProvider):
 *
 * \code
 * class MyNode : public INode, public IMetricsCallbackProvider {
 * private:
 *     IMetricsCallback* metrics_callback_{nullptr};
 *
 * public:
 *     bool SetMetricsCallback(IMetricsCallback* callback) noexcept override {
 *         metrics_callback_ = callback;
 *         return callback != nullptr;
 *     }
 *
 *     bool HasMetricsCallback() const noexcept override {
 *         return metrics_callback_ != nullptr;
 *     }
 * };
 * \endcode
 *
 * @note Exception Safety: All methods are noexcept
 * @note Thread-safe: dynamic_cast is thread-safe
 */
class IMetricsCallbackProvider {
public:
    virtual ~IMetricsCallbackProvider() = default;

    /**
     * @brief Set the metrics callback provider for this node
     *
     * Called by MetricsPublisher during node discovery phase.
     * Idempotent: calling again replaces previous callback.
     *
     * @param callback Pointer to the callback handler (may be nullptr)
     * @return true if callback was successfully set, false if callback is nullptr
     *
     * @note noexcept: Implementation must never throw
     */
    virtual bool SetMetricsCallback(IMetricsCallback* callback) noexcept = 0;

    /**
     * @brief Check if a metrics callback is installed
     *
     * Used by nodes to check if they should publish metrics.
     *
     * @return true if a callback provider is currently set
     *
     * @note noexcept: Implementation must never throw
     * @note Safe to call from Process()
     */
    virtual bool HasMetricsCallback() const noexcept = 0;

    /**
     * @brief Get the currently installed callback provider
     *
     * @return Pointer to callback provider, or nullptr if none installed
     *
     * @note noexcept: Implementation must never throw
     * @note Safe to call from Process()
     */
    virtual IMetricsCallback* GetMetricsCallback() const noexcept = 0;

    virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept = 0;

};

}  // namespace graph

