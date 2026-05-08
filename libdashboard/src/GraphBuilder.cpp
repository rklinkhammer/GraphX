// MIT License
//
// Copyright (c) 2025 graphlib contributors
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

#include "app/GraphBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphConfig.hpp"
#include "graph/EdgeRegistry.hpp"
#include "graph/JsonDynamicGraphLoader.hpp"
#include "graph/NodeFactory.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "plugins/NodePluginTemplate.hpp"
#include "sensor/DataInjectionAccelerometerNode.hpp"
#include "sensor/DataInjectionGyroscopeNode.hpp"
#include "sensor/DataInjectionMagnetometerNode.hpp"
#include "sensor/DataInjectionBarometricNode.hpp"
#include "sensor/DataInjectionGPSPositionNode.hpp"
#include "avionics/nodes/FlightFSMNode.hpp"
#include "avionics/nodes/AltitudeFusionNode.hpp"
#include "avionics/nodes/EstimationPipelineNode.hpp"
#include "avionics/nodes/FlightMonitorNode.hpp"
#include "avionics/nodes/FeedbackTestSinkNode.hpp"
#include "graph/CompletionAggregatorNode.hpp"
#include <log4cxx/logger.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace app {

// ============================================================================
// Static Initialization
// ============================================================================

log4cxx::LoggerPtr GraphBuilder::logger_ = log4cxx::Logger::getLogger("app.GraphBuilder");

// ============================================================================
// Constructor
// ============================================================================

GraphBuilder::GraphBuilder(const std::shared_ptr<app::capabilities::GraphCapability>& capability)
    : capability_(capability), node_count_(0), edge_count_(0) {
    
    if (!capability) {
        LOG4CXX_ERROR(logger_, "GraphBuilder constructed with null GraphCapability");
        throw std::invalid_argument("GraphCapability cannot be null");
    }
    
    if (!capability->GetNodeFactory()) {
        LOG4CXX_ERROR(logger_, "GraphCapability has null NodeFactory");
        throw std::invalid_argument("GraphCapability NodeFactory cannot be null");
    }
    
    if (capability->GetJsonConfigPath().empty()) {
        LOG4CXX_ERROR(logger_, "GraphCapability has empty JSON config path");
        throw std::invalid_argument("JSON config path cannot be empty");
    }
    
    LOG4CXX_TRACE(logger_, "GraphBuilder constructed successfully");
    LOG4CXX_TRACE(logger_, "JSON config path: " << capability->GetJsonConfigPath());
}

// ============================================================================
// Public Interface: Validate
// ============================================================================

bool GraphBuilder::Validate() {
    LOG4CXX_TRACE(logger_, "Starting validation");
    last_error_.clear();
    
    // Check JSON file exists
    const auto& json_path = capability_->GetJsonConfigPath();
    if (!fs::exists(json_path)) {
        last_error_ = "JSON configuration file does not exist: " + json_path;
        LOG4CXX_WARN(logger_, last_error_);
        return false;
    }
    
    if (!fs::is_regular_file(json_path)) {
        last_error_ = "JSON configuration path is not a regular file: " + json_path;
        LOG4CXX_WARN(logger_, last_error_);
        return false;
    }
    
    LOG4CXX_TRACE(logger_, "JSON config file exists: " << json_path);
    
    // Try to load graph configuration to validate JSON structure
    try {
        auto [nodes, edges] = graph::config::JsonDynamicGraphLoader::LoadGraph(
            json_path,
            capability_->GetNodeFactory());
        
        LOG4CXX_TRACE(logger_, "Validation successful: " << nodes.size() 
                             << " nodes, " << edges.size() << " edges");
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to load graph configuration: ") + e.what();
        LOG4CXX_WARN(logger_, last_error_);
        return false;
    }
}

// ============================================================================
// Public Interface: Build
// ============================================================================

BuildResult GraphBuilder::Build() {
    LOG4CXX_TRACE(logger_, "Starting 6-step graph build process");
    last_error_.clear();
    node_count_ = 0;
    edge_count_ = 0;
    
    try {
        // Step 1: Validate configuration
        LOG4CXX_TRACE(logger_, "Step 1: Validating configuration");
        if (!Validate()) {
            BuildResult result;
            result.success = false;
            result.error_message = last_error_;
            result.graph = nullptr;
            result.node_count = 0;
            result.edge_count = 0;
            LOG4CXX_ERROR(logger_, "Build failed at validation step");
            return result;
        }
        
        // Step 2: Load graph configuration
        LOG4CXX_TRACE(logger_, "Step 2: Loading graph configuration");
        const auto& json_path = capability_->GetJsonConfigPath();
        auto [nodes, edge_configs] = graph::config::JsonDynamicGraphLoader::LoadGraph(
            json_path,
            capability_->GetNodeFactory());
        
        LOG4CXX_TRACE(logger_, "Loaded " << nodes.size() << " nodes and " 
                             << edge_configs.size() << " edges from JSON");
        
        // CRITICAL: Save nodes_ to preserve shared_ptr ownership
        // This prevents nodes from being destroyed when the local 'nodes' vector
        // goes out of scope. The NodeFacadeAdapterWrapper instances stored in GraphManager
        // hold references to these shared_ptrs.
        nodes_ = nodes;
        LOG4CXX_TRACE(logger_, "Preserved " << nodes_.size() << " node shared_ptrs in nodes_ member");
        
        // Step 3: Create GraphManager
        LOG4CXX_TRACE(logger_, "Step 3: Creating GraphManager");
        graph_ = std::make_shared<graph::GraphManager>();
        
        // Step 3.5: Initialize EdgeRegistry
        LOG4CXX_TRACE(logger_, "Step 3.5: Initializing EdgeRegistry");
        InitializeEdgeRegistry();
        
        // Step 4: Register nodes with GraphManager
        LOG4CXX_TRACE(logger_, "Step 4: Registering nodes with GraphManager");
        
        // Save node type information before RegisterNodes
        std::map<std::string, std::pair<size_t, std::string>> node_type_map;
        for (size_t i = 0; i < nodes.size(); ++i) {
            std::string node_name = nodes[i]->GetName();
            std::string node_type = nodes[i]->GetType();
            node_type_map[node_name] = {i, node_type};
            LOG4CXX_TRACE(logger_, "Node " << i << ": " << node_name << " (type: " << node_type << ")");
        }
        
        RegisterNodes(nodes);
        
        LOG4CXX_TRACE(logger_, "Registered " << nodes.size() << " nodes");
        
        // Step 5: Wire edges
        LOG4CXX_TRACE(logger_, "Step 5: Wiring edges between nodes");
        WireEdges(edge_configs, node_type_map);
        
        LOG4CXX_TRACE(logger_, "Wired " << edge_configs.size() << " edges");
        
        // Step 6: Validate topology (optional)
        LOG4CXX_TRACE(logger_, "Step 6: Validating graph topology");
        if (!ValidateTopology()) {
            BuildResult result;
            result.success = false;
            result.error_message = "Graph topology validation failed: " + last_error_;
            result.graph = nullptr;
            result.node_count = nodes.size();
            result.edge_count = edge_configs.size();
            LOG4CXX_ERROR(logger_, "Build failed at topology validation");
            return result;
        }
        
        // Build successful
        node_count_ = nodes.size();
        edge_count_ = edge_configs.size();
        
        LOG4CXX_TRACE(logger_, "Graph build completed successfully: " 
                             << node_count_ << " nodes, " << edge_count_ << " edges");
        
        BuildResult result;
        result.success = true;
        result.error_message = "";
        result.graph = graph_;
        result.node_count = node_count_;
        result.edge_count = edge_count_;
        
        // Populate node and edge names for debugging
        for (const auto& node : nodes) {
            result.node_names.push_back(node->GetName());
        }
        
        for (const auto& edge : edge_configs) {
            std::string description = edge.source_node_id + ":" + 
                                     std::to_string(edge.source_port) + " → " +
                                     edge.target_node_id + ":" +
                                     std::to_string(edge.target_port);
            result.edge_descriptions.push_back(description);
        }
        
        return result;
    } catch (const std::exception& e) {
        last_error_ = std::string("Build process failed: ") + e.what();
        LOG4CXX_ERROR(logger_, last_error_);
        
        BuildResult result;
        result.success = false;
        result.error_message = last_error_;
        result.graph = nullptr;
        result.node_count = 0;
        result.edge_count = 0;
        return result;
    }
}

// ============================================================================
// Private Implementation: Step 3 - RegisterNodes
// ============================================================================

void GraphBuilder::RegisterNodes(std::vector<std::shared_ptr<graph::NodeFacadeAdapter>>& nodes) {
    LOG4CXX_TRACE(logger_, "Registering " << nodes.size() << " nodes");
    
    for (size_t i = 0; i < nodes.size(); ++i) {
        try {
            // Wrap the NodeFacadeAdapter in NodeFacadeAdapterWrapper to make it compatible with INode
            auto wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(nodes[i]);
            
            // Add the wrapped node to GraphManager
            graph_->AddNode(wrapper);
            
            LOG4CXX_TRACE(logger_, "Registered node " << i << ": " 
                                  << wrapper->GetAdapter()->GetName());
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to register node " << i 
                                  << ": " << e.what());
            throw std::runtime_error(
                std::string("Failed to register node: ") + e.what());
        }
    }
    
    LOG4CXX_TRACE(logger_, "Successfully registered " << nodes.size() << " nodes");
}

// ============================================================================
// Private Implementation: Step 3.5 - InitializeEdgeRegistry
// ============================================================================

void GraphBuilder::InitializeEdgeRegistry() {
    using namespace graph::config;  // For EdgeRegistry
    using namespace graph;
    using namespace sensors;
    using namespace graph;
    using namespace avionics;
    using namespace avionics::estimators;
    
    // If edges are already registered, skip initialization
    if (EdgeRegistry::GetRegisteredCount() > 0) {
        LOG4CXX_TRACE(logger_, "EdgeRegistry already initialized with " 
                               << EdgeRegistry::GetRegisteredCount() << " edges");
        return;
    }
    
    LOG4CXX_TRACE(logger_, "Registering all 14 edge types with EdgeRegistry");
    
    // ========================================================================
    // Main Pipeline Edges (9 edges): Sensor inputs → FSM → Fusion → Sink
    // ========================================================================
    
    // Edge 1: DataInjectionAccelerometerNode[0] → FlightFSMNode[0]
    EdgeRegistry::Register<DataInjectionAccelerometerNode, 0, FlightFSMNode, 0>(
        "DataInjectionAccelerometerNode", "FlightFSMNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) {
                LOG4CXX_WARN(logger_, "Edge 1: Failed to cast nodes to NodeFacadeAdapterWrapper");
                return false;
            }
            
            // Verify types match what we expect using string type names
            auto src_type = src_wrapper->GetType();
            auto dst_type = dst_wrapper->GetType();
            if (src_type != "DataInjectionAccelerometerNode" || dst_type != "FlightFSMNode") {
                LOG4CXX_ERROR(logger_, "Edge 1: Type mismatch! Expected DataInjectionAccelerometerNode→FlightFSMNode, "
                                       "got " << src_type << "→" << dst_type);
                return false;
            }
            
            // Now attempt typed extraction - should succeed since types match
            auto src = src_wrapper->GetNode<sensors::DataInjectionAccelerometerNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightFSMNode>();
            if (!src) {
                LOG4CXX_ERROR(logger_, "Edge 1: Failed to extract DataInjectionAccelerometerNode from adapter "
                                       "(type name matched but handle cast failed - plugin instance mismatch?)");
                return false;
            }
            if (!dst) {
                LOG4CXX_ERROR(logger_, "Edge 1: Failed to extract FlightFSMNode from adapter");
                return false;
            }
            g.AddEdge<DataInjectionAccelerometerNode, 0, FlightFSMNode, 0>(src, dst, buffer_size);
            LOG4CXX_TRACE(logger_, "Edge 1: Successfully created DataInjectionAccelerometerNode[0]→FlightFSMNode[0]");
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionAccelerometerNode[0] → FlightFSMNode[0]");
    
    // Edge 2: DataInjectionGyroscopeNode[0] → FlightFSMNode[1]
    EdgeRegistry::Register<DataInjectionGyroscopeNode, 0, FlightFSMNode, 1>(
        "DataInjectionGyroscopeNode", "FlightFSMNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) {
                LOG4CXX_WARN(logger_, "Edge 2: Type verification - wrapper cast failed");
                return false;
            }
            if (src_wrapper->GetType() != "DataInjectionGyroscopeNode" || dst_wrapper->GetType() != "FlightFSMNode") {
                LOG4CXX_ERROR(logger_, "Edge 2: Type mismatch - expected DataInjectionGyroscopeNode→FlightFSMNode");
                return false;
            }
            auto src = src_wrapper->GetNode<sensors::DataInjectionGyroscopeNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightFSMNode>();
            if (!src || !dst) {
                LOG4CXX_WARN(logger_, "Edge 2: Typed node extraction failed");
                return false;
            }
            g.AddEdge<DataInjectionGyroscopeNode, 0, FlightFSMNode, 1>(src, dst, buffer_size);
            LOG4CXX_TRACE(logger_, "Edge 2: Created");
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionGyroscopeNode[0] → FlightFSMNode[1]");
    
    // Edge 3: DataInjectionMagnetometerNode[0] → FlightFSMNode[2]
    EdgeRegistry::Register<DataInjectionMagnetometerNode, 0, FlightFSMNode, 2>(
        "DataInjectionMagnetometerNode", "FlightFSMNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionMagnetometerNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightFSMNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionMagnetometerNode, 0, FlightFSMNode, 2>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionMagnetometerNode[0] → FlightFSMNode[2]");
    
    // Edge 4: DataInjectionBarometricNode[0] → FlightFSMNode[3]
    EdgeRegistry::Register<DataInjectionBarometricNode, 0, FlightFSMNode, 3>(
        "DataInjectionBarometricNode", "FlightFSMNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionBarometricNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightFSMNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionBarometricNode, 0, FlightFSMNode, 3>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionBarometricNode[0] → FlightFSMNode[3]");
    
    // Edge 5: DataInjectionGPSPositionNode[0] → FlightFSMNode[4]
    EdgeRegistry::Register<DataInjectionGPSPositionNode, 0, FlightFSMNode, 4>(
        "DataInjectionGPSPositionNode", "FlightFSMNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionGPSPositionNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightFSMNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionGPSPositionNode, 0, FlightFSMNode, 4>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionGPSPositionNode[0] → FlightFSMNode[4]");
    
    // Edge 6: FlightFSMNode[0] → AltitudeFusionNode[0]
    EdgeRegistry::Register<avionics::FlightFSMNode, 0, AltitudeFusionNode, 0>(
        "FlightFSMNode", "AltitudeFusionNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<avionics::FlightFSMNode>();
            auto dst = dst_wrapper->GetNode<avionics::AltitudeFusionNode>();
            if (!src || !dst) return false;
            g.AddEdge<avionics::FlightFSMNode, 0, AltitudeFusionNode, 0>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: FlightFSMNode[0] → AltitudeFusionNode[0]");
    
    // Edge 7: AltitudeFusionNode[0] → EstimationPipelineNode[0]
    EdgeRegistry::Register<AltitudeFusionNode, 0, EstimationPipelineNode, 0>(
        "AltitudeFusionNode", "EstimationPipelineNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<avionics::AltitudeFusionNode>();
            auto dst = dst_wrapper->GetNode<avionics::estimators::EstimationPipelineNode>();
            if (!src || !dst) return false;
            g.AddEdge<AltitudeFusionNode, 0, EstimationPipelineNode, 0>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: AltitudeFusionNode[0] → EstimationPipelineNode[0]");
    
    // Edge 8: EstimationPipelineNode[0] → FlightMonitorNode[0]
    EdgeRegistry::Register<EstimationPipelineNode, 0, FlightMonitorNode, 0>(
        "EstimationPipelineNode", "FlightMonitorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<avionics::estimators::EstimationPipelineNode>();
            auto dst = dst_wrapper->GetNode<avionics::FlightMonitorNode>();
            if (!src || !dst) return false;
            g.AddEdge<EstimationPipelineNode, 0, FlightMonitorNode, 0>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: EstimationPipelineNode[0] → FlightMonitorNode[0]");
    
    // Edge 9: FlightMonitorNode[0] → FeedbackTestSinkNode[0]
    EdgeRegistry::Register<FlightMonitorNode, 0, FeedbackTestSinkNode, 0>(
        "FlightMonitorNode", "FeedbackTestSinkNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<avionics::FlightMonitorNode>();
            auto dst = dst_wrapper->GetNode<avionics::FeedbackTestSinkNode>();
            if (!src || !dst) return false;
            g.AddEdge<FlightMonitorNode, 0, FeedbackTestSinkNode, 0>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: FlightMonitorNode[0] → FeedbackTestSinkNode[0]");
    
    // ========================================================================
    // Completion Path Edges (5 edges): Sensor notifications → Aggregator
    // ========================================================================
    
    // Edge 10: DataInjectionAccelerometerNode[1] → CompletionAggregatorNode[0]
    EdgeRegistry::Register<DataInjectionAccelerometerNode, 1, CompletionAggregatorNode, 0>(
        "DataInjectionAccelerometerNode", "CompletionAggregatorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionAccelerometerNode>();
            auto dst = dst_wrapper->GetNode<graph::CompletionAggregatorNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionAccelerometerNode, 1, CompletionAggregatorNode, 0>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionAccelerometerNode[1] → CompletionAggregatorNode[0]");
    
    // Edge 11: DataInjectionGyroscopeNode[1] → CompletionAggregatorNode[1]
    EdgeRegistry::Register<DataInjectionGyroscopeNode, 1, CompletionAggregatorNode, 1>(
        "DataInjectionGyroscopeNode", "CompletionAggregatorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionGyroscopeNode>();
            auto dst = dst_wrapper->GetNode<graph::CompletionAggregatorNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionGyroscopeNode, 1, CompletionAggregatorNode, 1>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionGyroscopeNode[1] → CompletionAggregatorNode[1]");
    
    // Edge 12: DataInjectionMagnetometerNode[1] → CompletionAggregatorNode[2]
    EdgeRegistry::Register<DataInjectionMagnetometerNode, 1, CompletionAggregatorNode, 2>(
        "DataInjectionMagnetometerNode", "CompletionAggregatorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionMagnetometerNode>();
            auto dst = dst_wrapper->GetNode<graph::CompletionAggregatorNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionMagnetometerNode, 1, CompletionAggregatorNode, 2>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionMagnetometerNode[1] → CompletionAggregatorNode[2]");
    
    // Edge 13: DataInjectionBarometricNode[1] → CompletionAggregatorNode[3]
    EdgeRegistry::Register<DataInjectionBarometricNode, 1, CompletionAggregatorNode, 3>(
        "DataInjectionBarometricNode", "CompletionAggregatorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionBarometricNode>();
            auto dst = dst_wrapper->GetNode<graph::CompletionAggregatorNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionBarometricNode, 1, CompletionAggregatorNode, 3>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionBarometricNode[1] → CompletionAggregatorNode[3]");
    
    // Edge 14: DataInjectionGPSPositionNode[1] → CompletionAggregatorNode[4]
    EdgeRegistry::Register<DataInjectionGPSPositionNode, 1, CompletionAggregatorNode, 4>(
        "DataInjectionGPSPositionNode", "CompletionAggregatorNode",
        [](graph::GraphManager& g, size_t src_idx, size_t dst_idx, size_t buffer_size) {
            auto src_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[src_idx]);
            auto dst_wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(g.GetNodes()[dst_idx]);
            if (!src_wrapper || !dst_wrapper) return false;
            auto src = src_wrapper->GetNode<sensors::DataInjectionGPSPositionNode>();
            auto dst = dst_wrapper->GetNode<graph::CompletionAggregatorNode>();
            if (!src || !dst) return false;
            g.AddEdge<DataInjectionGPSPositionNode, 1, CompletionAggregatorNode, 4>(src, dst, buffer_size);
            return true;
        });
    LOG4CXX_TRACE(logger_, "Registered edge: DataInjectionGPSPositionNode[1] → CompletionAggregatorNode[4]");
    
    LOG4CXX_TRACE(logger_, "Successfully registered all 14 edge types with EdgeRegistry");
}

// ============================================================================
// Private Implementation: Step 5 - WireEdges
// ============================================================================

void GraphBuilder::WireEdges(
    const std::vector<graph::EdgeConfig>& edge_configs,
    const std::map<std::string, std::pair<size_t, std::string>>& node_type_map) {
    
    LOG4CXX_TRACE(logger_, "Wiring " << edge_configs.size() << " edges");
    LOG4CXX_TRACE(logger_, "Available nodes in map:");
    for (const auto& [node_name, pair] : node_type_map) {
        auto [idx, type] = pair;
        LOG4CXX_TRACE(logger_, "  - " << node_name << " (index: " << idx << ", type: " << type << ")");
    }
    
    if (!graph_) {
        throw std::runtime_error("GraphManager is null - nodes must be registered first");
    }
    
    for (size_t i = 0; i < edge_configs.size(); ++i) {
        const auto& edge = edge_configs[i];
        
        try {
            LOG4CXX_TRACE(logger_, "Wiring edge " << i << ": " 
                                  << edge.source_node_id << ":" << edge.source_port
                                  << " → " << edge.target_node_id << ":" << edge.target_port);
            
            // Validate edge configuration
            if (!edge.Validate()) {
                throw std::runtime_error("Edge configuration invalid");
            }
            
            // Look up source node
            auto src_it = node_type_map.find(edge.source_node_id);
            if (src_it == node_type_map.end()) {
                std::string msg = "Source node not found: " + edge.source_node_id + "\n"
                    "Available nodes: ";
                for (const auto& [name, _] : node_type_map) {
                    msg += name + ", ";
                }
                throw std::runtime_error(msg);
            }
            auto [src_idx, src_type] = src_it->second;
            
            // Look up destination node
            auto dst_it = node_type_map.find(edge.target_node_id);
            if (dst_it == node_type_map.end()) {
                std::string msg = "Destination node not found: " + edge.target_node_id + "\n"
                    "Available nodes: ";
                for (const auto& [name, _] : node_type_map) {
                    msg += name + ", ";
                }
                throw std::runtime_error(msg);
            }
            auto [dst_idx, dst_type] = dst_it->second;
            
            LOG4CXX_TRACE(logger_, "Edge " << i << " connecting actual types: " 
                                  << src_type << ":" << edge.source_port 
                                  << " to " << dst_type << ":" << edge.target_port);
            
            // Use EdgeRegistry to create the type-aware edge
            bool created = graph::config::EdgeRegistry::CreateEdge(
                *graph_,
                src_type,
                edge.source_port,
                dst_type,
                edge.target_port,
                src_idx,
                dst_idx,
                edge.buffer_size);
            
            if (!created) {
                throw std::runtime_error(
                    std::string("EdgeRegistry::CreateEdge FAILED for ") + 
                    src_type + ":" + std::to_string(edge.source_port) + " → " +
                    dst_type + ":" + std::to_string(edge.target_port) +
                    " (Likely cause: GetNode<T>() returned nullptr - adapter handle mismatch or wrong type)");
            }
            
            LOG4CXX_TRACE(logger_, "Edge " << i << " created successfully: "
                                 << src_type << ":" << edge.source_port << " → "
                                 << dst_type << ":" << edge.target_port);
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to wire edge " << i << ": " << e.what());
            throw std::runtime_error(
                std::string("Failed to wire edge between ") + 
                edge.source_node_id + " and " + edge.target_node_id + ": " + e.what());
        }
    }
    
    LOG4CXX_TRACE(logger_, "Successfully wired " << edge_configs.size() << " edges");
}

// ============================================================================
// Private Implementation: Step 6 - ValidateTopology
// ============================================================================

bool GraphBuilder::ValidateTopology() {
    LOG4CXX_TRACE(logger_, "Validating graph topology");
    last_error_.clear();
    
    if (!graph_) {
        last_error_ = "GraphManager is null";
        LOG4CXX_ERROR(logger_, last_error_);
        return false;
    }
    
    // Basic topology validation:
    // - Graph must have at least one node
    // - Graph must not have disconnected components (if required)
    
    try {
        // Verify graph has nodes
        if (node_count_ == 0) {
            last_error_ = "Graph has no nodes";
            LOG4CXX_WARN(logger_, last_error_);
            // Note: Empty graph is technically valid, just warn
        }
        
        // // If cycles are not allowed and backpressure is disabled,
        // // could do cycle detection here
        // if (!capability_->graph_impl.backpressure_enabled) {
        //     LOG4CXX_TRACE(logger_, "Graph requires cycle-free topology (backpressure disabled)");
        //     // Cycle detection would go here
        // }
        
        LOG4CXX_TRACE(logger_, "Topology validation passed");
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Topology validation failed: ") + e.what();
        LOG4CXX_ERROR(logger_, last_error_);
        return false;
    }
}

}  // namespace app
