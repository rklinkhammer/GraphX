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
#include "graph/NodeCallback.hpp"
#include "graph/CompletionSignal.hpp"

namespace graph {

/**
 * @brief Completion callback provider interface
 *
 * Nodes that signal completion implement this interface to enable callbacks
 * when processing is complete.
 *
 * @tparam DataType Type of completion signal (typically CompletionSignal)
 */
template<typename DataType>
class ICompletionCallback  {
public:

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

    class CompletionNodeCallback : public NodeCallback {
    public:
        virtual ~CompletionNodeCallback() = default;
          /**
            * @brief Set callback to invoke when all sensors complete
            * @param callback Function to call when completion detected
            * 
            * Callback should be quick (non-blocking).
            * Typical usage: set completion event or signal condition variable.
            */
        void SetOnComplete(std::function<void()> callback)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            on_complete_callback_ = std::move(callback);
        }

        void OnComplete() noexcept {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (on_complete_callback_) {
                on_complete_callback_();
            }
        }

    private:
        mutable std::mutex state_mutex_;
        std::function<void()> on_complete_callback_;
    };

    virtual ~ICompletionCallback() = default;

    /**
     * @brief Invoked when processing completes
     */
    virtual void OnProcessingComplete() noexcept {}

protected:
    NodeCallback* callback_provider_{nullptr};

};

/**
 * @brief Type alias for standard completion callback provider
 * 
 * Used when nodes need to notify the framework that processing is complete.
 * This is the standard specialization with CompletionSignal as the data type.
 * 
 * @note If different signal types are needed in the future, create additional
 *       specializations (ICompletionCallback<DifferentSignal>) rather
 *       than using this alias.
 */
using CompletionCallbackProvider = ICompletionCallback<graph::message::CompletionSignal>;

}  // namespace graph

