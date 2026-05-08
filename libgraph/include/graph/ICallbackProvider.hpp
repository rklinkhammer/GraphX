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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <log4cxx/logger.h>

/**
 * @file ICallbackProvider.hpp
 * @brief Multi-strategy callback provider interfaces for different node types
 *
 * This file defines the callback provider strategies for monitoring different types of nodes
 * in the active graph execution pipeline. Each strategy corresponds to a node's role in the
 * data flow and provides appropriate hooks for tracking that node's behavior.
 *
 * @section multi_strategy_architecture Multi-Strategy Callback Architecture
 *
 * The callback system is built on the principle that different node types require different
 * monitoring hooks based on their role in the data processing pipeline:
 *
 * \code
 * [CSV Sensor] --data--> [Filter] --data--> [Aggregator]
 *     |                       |                    |
 *     v                       v                    v
 * ISourceCallback      IProcessingCallback   ISinkCallback
 * (data produced)     (received & produced)  (data consumed)
 * \endcode
 *
 * This multi-strategy approach provides several advantages:
 * 1. **Type-Specific Monitoring**: Each interface defines exactly the hooks needed for its node type
 * 2. **Clear Semantics**: Implementers know what role their node plays in the pipeline
 * 3. **Minimal Overhead**: Only override hooks relevant to your node's functionality
 * 4. **Extensible**: New callback strategies can be added without affecting existing ones
 * 5. **Safe Runtime Detection**: Graph executor uses dynamic_cast to find supported strategies
 *
 * @section implementation_patterns Implementation Patterns
 *
 * For each callback provider type, implementations follow this general pattern:
 *
 * \code
 * class MyNode : public INode, public ISourceCallbackProvider {
 * private:
 *     NodeCallback* callback_provider_{nullptr};
 *
 * public:
 *     // Required: Set the callback handler
 *     bool SetCallbackProvider(NodeCallback* provider) noexcept override {
 *         callback_provider_ = provider;
 *         return callback_provider_ != nullptr;
 *     }
 *
 *     // Required: Check if callbacks are enabled
 *     bool HasCallbackProvider() const noexcept override {
 *         return callback_provider_ != nullptr;
 *     }
 *
 * protected:
 *     // Optional: Override hooks to invoke callbacks at key points
 *     void OnSampleProduced() noexcept override {
 *         if (callback_provider_) {
 *             callback_provider_->RecordSampleProduced();
 *         }
 *     }
 * };
 * \endcode
 *
 * @section runtime_detection Runtime Detection
 *
 * The graph executor discovers and enables callbacks using safe dynamic_cast:
 *
 * \code
 * auto node = graph_->FindNode(node_id);
 *
 * // Try source callbacks
 * if (auto source = dynamic_cast<ISourceCallbackProvider*>(node.get())) {
 *     source->SetCallbackProvider(wrapper.get());
 * }
 *
 * // Try processing callbacks
 * if (auto proc = dynamic_cast<IProcessingCallbackProvider*>(node.get())) {
 *     proc->SetCallbackProvider(wrapper.get());
 * }
 *
 * // Try sink callbacks
 * if (auto sink = dynamic_cast<ISinkCallbackProvider*>(node.get())) {
 *     sink->SetCallbackProvider(wrapper.get());
 * }
 * \endcode
 *
 * This approach is **100% exception-safe**: dynamic_cast returns nullptr if the type
 * doesn't match, never throws an exception.
 */

namespace graph {


// ============================================================================

/**
 * @brief Base interface for callback-capable nodes
 *
 * This interface provides the common contract for all callback provider types.
 * It allows the graph executor to detect at runtime whether a node supports callbacks
 * without needing to know the concrete node type.
 *
 * @note This interface is primarily used for runtime type detection and safe feature
 *       enablement. Most of the actual callback logic is in the specific provider types
 *       (ISourceCallbackProvider, IProcessingCallbackProvider, ISinkCallbackProvider).
 *
 * @note Exception Safety: All methods are noexcept. Implementations must never throw.
 */
class ICallbackProvider {
public:
    virtual ~ICallbackProvider() = default;

    /**
     * @brief Set the callback provider for this node
     * @param provider Pointer to the callback handler (may be nullptr)
     * @return true if provider was successfully set
     */
    virtual bool SetCallbackProvider(NodeCallback* provider) noexcept {
        callback_provider_ = provider;
        return callback_provider_ != nullptr;
    }

    /**
     * @brief Check if a callback provider is installed
     * @return true if a callback provider is currently set
     */
    virtual bool HasCallbackProvider() const noexcept {
        return callback_provider_ != nullptr;
    }

    /**
     * @brief Get the currently installed callback provider
     * @return Pointer to callback provider, or nullptr if none installed
     */
    virtual NodeCallback* GetCallbackProvider() const noexcept {
        return callback_provider_;
    }

protected:
    NodeCallback* callback_provider_{nullptr};
};

// ============================================================================

/**
 * @brief Source node callback provider interface
 *
 * Nodes that produce data (e.g., CSV sensors) implement this interface to enable
 * callbacks when data is produced or becomes exhausted.
 *
 * @tparam DataType Type of data produced by the node
 */
template<typename DataType>
class ISourceCallbackProvider : public ICallbackProvider {
public:
    virtual ~ISourceCallbackProvider() = default;

    /**
     * @brief Invoked when the source produces data
     */
    virtual void OnDataProduced(const DataType& msg) noexcept {
        (void)msg;
    }

    /**
     * @brief Invoked when the source has no more data to produce
     */
    virtual void OnDataExhausted() noexcept {}
};

// ============================================================================

/**
 * @brief Processing node callback provider interface
 *
 * Nodes that both consume and produce data implement this interface to enable
 * callbacks when data is consumed and/or produced.
 *
 * @tparam DataType Type of data consumed and produced
 */
template<typename DataType>
class IProcessingCallbackProvider : public ICallbackProvider {
public:
    virtual ~IProcessingCallbackProvider() = default;

    /**
     * @brief Invoked when data is consumed on a specific port
     * @param port Input port index
     * @param msg Data consumed
     */
    virtual void OnDataConsumed(std::size_t port, const DataType& msg) noexcept {
        (void)port;
        (void)msg;
    }

    /**
     * @brief Invoked when data is produced on a specific port
     * @param port Output port index
     * @param msg Data produced
     */
    virtual void OnDataProduced(std::size_t port, const DataType& msg) noexcept {
        (void)port;
        (void)msg;
    }
};

// ============================================================================

/**
 * @brief Sink node callback provider interface
 *
 * Nodes that consume data (e.g., aggregators) implement this interface to enable
 * callbacks when data is consumed.
 *
 * @tparam DataType Type of data consumed
 */
template<typename DataType>
class ISinkCallbackProvider : public ICallbackProvider {
public:
    virtual ~ISinkCallbackProvider() = default;

    /**
     * @brief Invoked when data is consumed on a specific port
     * @param port Input port index
     * @param msg Data consumed
     */
    virtual void OnDataConsumed(std::size_t port, const DataType& msg) noexcept {
        (void)port;
        (void)msg;
    }
};

}  // namespace graph

