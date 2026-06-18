/**
 * @file ICallbackProvider.hpp
 * @brief Icallback Provider Graph runtime support.
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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <log4cxx/logger.h>


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
/**
 * @class ICallbackProvider
 * @brief Icallback Provider type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class ICallbackProvider {
public:
    /**
     * @brief Releases resources owned by Icallback Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
/**
 * @class ISourceCallbackProvider
 * @brief Isource Callback Provider type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class ISourceCallbackProvider : public ICallbackProvider {
public:
    /**
     * @brief Releases resources owned by Isource Callback Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
/**
 * @class IProcessingCallbackProvider
 * @brief Iprocessing Callback Provider type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class IProcessingCallbackProvider : public ICallbackProvider {
public:
    /**
     * @brief Releases resources owned by Iprocessing Callback Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
/**
 * @class ISinkCallbackProvider
 * @brief Isink Callback Provider type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
class ISinkCallbackProvider : public ICallbackProvider {
public:
    /**
     * @brief Releases resources owned by Isink Callback Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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

