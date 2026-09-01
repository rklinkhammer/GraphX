/**
 * @file IMetricsCallback.hpp
 * @brief Imetrics Callback Graph runtime support.
 *
 * @details Provides metrics event and subscriber contracts for runtime observability. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#include "metrics/MetricsEvent.hpp"
#include "metrics/NodeMetricsSchema.hpp"
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>
#include <map>
#include <memory>
#include <mutex>


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
/**
 * @class IMetricsCallback
 * @brief Imetrics Callback type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class IMetricsCallback {
public:
    /**
     * @brief Releases resources owned by Imetrics Callback.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
/**
 * @brief Publish async.
 * @param event Parameter for publish async.
 * @return Result of the operation.
 */
    virtual bool PublishAsync(const app::metrics::MetricsEvent& event) noexcept = 0;
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
/**
 * @class IMetricsCallbackProvider
 * @brief Imetrics Callback Provider type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class IMetricsCallbackProvider {
public:
    /**
     * @brief Releases resources owned by Imetrics Callback Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
/**
 * @brief Set metrics callback.
 * @param callback Parameter for set metrics callback.
 * @return Result of the operation.
 */
    virtual bool SetMetricsCallback(IMetricsCallback* callback) noexcept = 0;

    bool SetMetricsCallbackShared(
        std::shared_ptr<IMetricsCallback> callback) noexcept {
        std::lock_guard<std::mutex> lock(shared_metrics_callback_mutex_);
        if (callback) {
            shared_metrics_callback_ = callback;
            return SetMetricsCallback(callback.get());
        }
        const bool result = SetMetricsCallback(nullptr);
        shared_metrics_callback_.reset();
        return result;
    }

    [[nodiscard]] std::shared_ptr<IMetricsCallback>
    AcquireMetricsCallback() const noexcept {
        std::lock_guard<std::mutex> lock(shared_metrics_callback_mutex_);
        return shared_metrics_callback_;
    }

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
/**
 * @brief Has metrics callback.
 * @return Result of the operation.
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
/**
 * @brief Get metrics callback.
 * @return Result of the operation.
 */
    virtual IMetricsCallback* GetMetricsCallback() const noexcept = 0;

/**
 * @brief Get node metrics schema.
 * @return Result of the operation.
 */
    virtual app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept = 0;

private:
    mutable std::mutex shared_metrics_callback_mutex_;
    std::shared_ptr<IMetricsCallback> shared_metrics_callback_;
};

}  // namespace graph
