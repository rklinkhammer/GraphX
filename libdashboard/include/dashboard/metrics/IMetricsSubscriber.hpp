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

/**
 * @file IMetricsSubscriber.hpp
 * @brief Interface for components that receive metrics from nodes
 */
namespace app::metrics  {

/**
 * @brief Interface for receiving metrics events and polled snapshots
 *
 * Classes implementing this interface can receive two types of metrics updates:
 * 1. Async events: Published immediately when a metric event occurs
 * 2. Polled snapshots: Periodic updates with current metric values
 *
 * Thread-safe implementations should use proper synchronization.
 */
class IMetricsSubscriber {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~IMetricsSubscriber() = default;

    /**
     * @brief Called when a metrics event is published
     *
     * Async events from nodes are delivered here immediately.
     * Implementations should be fast and avoid blocking operations.
     *
     * @param event Metrics event from node
     */
    virtual void OnMetricsEvent(const app::metrics::MetricsEvent& event) = 0;
};

}  // namespace app::metrics
    