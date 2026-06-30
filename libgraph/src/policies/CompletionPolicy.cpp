/**
 * @file CompletionPolicy.cpp
 * @brief Completion Policy Graph runtime support.
 *
 * @details Provides executor policy integration for commands, metrics, completion, and data injection. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include "graph/GraphManagerCore.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/CapabilityContext.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/CompletionSignal.hpp"
#include "policies/CompletionPolicy.hpp"

#include <chrono>
#include <thread>
#include <log4cxx/logger.h>

namespace policies {


/**
 * @brief Init completion callbacks.
 * @param context Parameter for init completion callbacks.
 */
bool CompletionPolicy::InitCompletionCallbacks(capabilities::GraphCapability& context) {
    graph::CapabilityContext capability_context{context};
    auto nodes_result = capability_context.Nodes();
    if (!nodes_result) {
        LOG4CXX_WARN(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - no GraphManager");
        return false;
    }

    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - scanning " 
                  << nodes_result->size() << " nodes");
    
    using CompletionProvider = graph::CompletionCallbackProvider;
    
    // Clear any previously installed callbacks
    completion_callbacks_.clear();
    
    size_t callbacks_installed = 0;
    auto nodes = *nodes_result;
    for (const auto& node : nodes) {
        if (!node) {
            continue;
        }
        auto descriptor = capability_context.DescribeNode(node);
        auto completion_provider = capability_context.NodeCapability<CompletionProvider>(node);

        LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - checking node: "
                      << descriptor.name);

        if(!completion_provider) {
            LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - node does not support CompletionProvider");
            continue;
        }

        auto callback = std::make_shared<CompletionProvider::CompletionNodeCallback>();
        callback->SetOnComplete([this]() {
            {
                std::lock_guard<std::mutex> lock(completion_mutex_);
                completion_signaled_ = true;
                LOG4CXX_TRACE(completion_logger, "CompletionPolicy - completion signal received");
            }
            completion_cv_.notify_one();
        });
        completion_callbacks_.push_back(callback);
        ++callbacks_installed;
        (*completion_provider)->SetCallbackProvider(callback.get());
        LOG4CXX_TRACE(completion_logger, "CompletionPolicy - callback installed on node");
    }
    
    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - "
                  << callbacks_installed << " callbacks installed");
    return true;
}


}  // namespace graph
