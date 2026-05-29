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
#include "capabilities/GraphCapability.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/CompletionSignal.hpp"
#include "policies/MetricsPolicy.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "metrics/IMetricsCallback.hpp"

#include <chrono>
#include <thread>
#include <log4cxx/logger.h>

namespace policies {


void MetricsPolicy::InitMetricsSources(capabilities::GraphCapability& context) {
     if (!context.GetGraphManager()) {
        LOG4CXX_WARN(metrics_logger, "MetricsPolicy::InitMetricsSources() - no GraphManager");
        return ;
    }   
    std::vector<app::metrics::NodeMetricsSchema> schemas;
    
    const auto& nodes = context.GetGraphManager()->GetNodes();    
    for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
        if (!nodes[node_idx]) {
            continue;
        }
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - checking Node[" 
                      << node_idx << "]");

        auto metrics_node = graph::DiscoverCapability<graph::IMetricsCallbackProvider>(nodes[node_idx]);
        if (!metrics_node) {
            LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - Node[" 
                          << node_idx << "] does not implement IMetricsCallbackProvider");
            continue;
        }
        std::string node_name = GetNodeName(nodes[node_idx]);
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - Node[" 
                     << node_idx << "] '" << node_name << "' supports IMetricsCallbackProvider");
    
        auto metrics_callback = std::make_shared<MetricsCapabilityCallback>();
        metrics_callback->on_publish_async_ = [this, node_name](const app::metrics::MetricsEvent& event) -> bool{
            return metrics_event_queue_.Enqueue(event);
        };
        metrics_node->SetMetricsCallback(metrics_callback.get());
        AddNodeMetrics(node_name, metrics_callback, metrics_node->GetNodeMetricsSchema());
        schemas.push_back(metrics_node->GetNodeMetricsSchema());
    }
    metrics_capability_->SetNodeMetricsSchemas(schemas);
}

}  // namespace policies
