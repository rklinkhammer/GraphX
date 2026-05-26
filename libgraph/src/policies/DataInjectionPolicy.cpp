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


#include "graph/NodeFacade.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/CapabilityDiscovery.hpp"
#include "policies/DataInjectionPolicy.hpp"
#include "capabilities/DataInjectionCapability.hpp"
#include "capabilities/GraphCapability.hpp"
#include <chrono>
#include <thread>
#include <log4cxx/logger.h>

namespace policies {

void DataInjectionPolicy::InitDataInjectionSources(capabilities::GraphCapability& context) {
    LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy::InitDataInjectionSources() - scanning " 
                  << context.GetGraphManager()->GetNodes().size() << " nodes");
    if (!context.GetGraphManager()) {
        LOG4CXX_WARN(data_injection_logger_, "DataInjectionPolicy::InitDataInjectionSources() - no GraphManager");
        return ;
    }   
    const auto& nodes = context.GetGraphManager()->GetNodes();    
    for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
        if (!nodes[node_idx]) {
            continue;
        }
        LOG4CXX_TRACE(data_injection_logger_, "DataInjectionPolicy::InitDataInjectionSources() - checking Node[" 
                      << node_idx << "]");
        // Use unified capability discovery for IDataInjectionSource
        // Handles both built-in nodes (Phase 1: direct cast) and plugin nodes (Phase 2: TryGetInterface)
        auto injection_node_config = graph::DiscoverCapability<graph::datasources::IDataInjectionSource>(nodes[node_idx]);
        
        if (!injection_node_config) {
            // Not a CSV node, skip it
            LOG4CXX_TRACE(data_injection_logger_, "Injection Discovery: Node[" << node_idx << "]: Skipping - not a Injection node");
            continue;  
        }
        std::string node_name = GetNodeName(nodes[node_idx]);
        //std::string sensor_type_str = SensorClassificationTypeToString(injection_node_config->sensor_classification_type);
        capabilities::DataInjectionNodeConfig node_config;
        node_config.node_index = node_idx;
        node_config.node_name = node_name;
        node_config.node_type = GetTypeName(nodes[node_idx]);
        //node_config.sensor_classification_type = injection_node_config->sensor_classification_type;
        node_config.injection_queue = &injection_node_config->GetInjectionQueue();
        data_injection_capability_->RegisterDataInjectionNodeConfig(node_config);
        //LOG4CXX_TRACE(data_injection_logger_, "Injection Discovery: Node[" << node_idx << "]: Registered Injection node with classification: " << sensor_type_str);
    }
}

}  // namespace graph::policies
