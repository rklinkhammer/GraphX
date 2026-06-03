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


#include "graph/GraphManager.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/CompletionSignal.hpp"
#include "policies/CompletionPolicy.hpp"

#include <chrono>
#include <thread>
#include <log4cxx/logger.h>

namespace policies {


bool CompletionPolicy::InitCompletionCallbacks(capabilities::GraphCapability& context) {
    const auto graph_manager = context.GetGraphManager();
    if (!graph_manager) {
        LOG4CXX_WARN(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - no GraphManager");
        return false;
    }

    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - scanning " 
                  << graph_manager->GetNodes().size() << " nodes");
    
    using CompletionProvider = graph::CompletionCallbackProvider;
    
    // Clear any previously installed callbacks
    completion_callbacks_.clear();
    
    size_t callbacks_installed = 0;
    const auto& nodes = graph_manager->GetNodes();    
    for (const auto& node : nodes) {
        if (!node) {
            continue;
        }
        std::shared_ptr<CompletionProvider> completion_provider;
        std::string node_name;

        auto facade_adapter = GetAsNodeFacadeAdapter(node);
        if (facade_adapter) {
            node_name = facade_adapter->GetName();
            completion_provider = std::static_pointer_cast<CompletionProvider>(
                facade_adapter->GetCompletionCallbackProviderPtr());
        } else {
            // Support direct graph nodes that implement CompletionCallbackProvider.
            node_name = "<direct-node>";
            completion_provider = std::dynamic_pointer_cast<CompletionProvider>(node);
        }

        LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - checking node: "
                      << node_name);

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
        completion_provider->SetCallbackProvider(callback.get());
        LOG4CXX_TRACE(completion_logger, "CompletionPolicy - callback installed on node");
    }
    
    LOG4CXX_TRACE(completion_logger, "CompletionPolicy::InitCompletionCallbacks() - "
                  << callbacks_installed << " callbacks installed");
    return true;
}


}  // namespace graph
