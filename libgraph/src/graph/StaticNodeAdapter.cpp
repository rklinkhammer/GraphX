/**
 * @file StaticNodeAdapter.cpp
 * @brief Static Node Adapter Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

#include "graph/StaticNodeAdapter.hpp"

#include <stdexcept>
#include <sstream>
#include <cstring>
#include <map>

#include "log4cxx/logger.h"

namespace graph::config {

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("graph.config.StaticNodeAdapter"));

// Global registry to hold adapter instances alive
static std::map<void*, std::shared_ptr<StaticNodeAdapter::StaticNodeInstance>> g_adapter_instances;

NodeFacadeAdapter StaticNodeAdapter::Adapt(
    std::shared_ptr<graph::INode> node,
    const std::string& type_name) {
    
    if (!node) {
        LOG4CXX_ERROR(logger, "Cannot adapt null INode");
        throw std::invalid_argument("Node cannot be null");
    }
    
    if (type_name.empty()) {
        LOG4CXX_ERROR(logger, "Cannot adapt node with empty type name");
        throw std::invalid_argument("Type name cannot be empty");
    }
    
    LOG4CXX_TRACE(logger, "Adapting static node of type: " << type_name);
    
    // Create a new StaticNodeInstance and store it in global registry
    auto instance = std::make_shared<StaticNodeInstance>(node, type_name);
    
    // Store instance so it stays alive
    void* handle = instance.get();
    g_adapter_instances[handle] = instance;
    
    // Create NodeFacadeAdapter from the instance
    NodeFacadeAdapter adapter(handle, instance->GetFacade());
    
    LOG4CXX_TRACE(logger, "Successfully adapted static node of type: " << type_name);
    
    return adapter;
}

StaticNodeAdapter::StaticNodeInstance::StaticNodeInstance(
    std::shared_ptr<graph::INode> node,
    const std::string& type_name)
    : node_(std::move(node)), type_name_(type_name) {
    
    LOG4CXX_TRACE(logger, "Creating StaticNodeInstance for type: " << type_name_);
    BuildFacade();
}

/**
 * @brief Build facade.
 */
void StaticNodeAdapter::StaticNodeInstance::BuildFacade() {
    // Initialize facade with zero/null values
    facade_ = NodeFacade();
    
    // Bind Init() method
    facade_.Init = [](NodeHandle handle) -> bool {
        if (!handle) {
            LOG4CXX_WARN(logger, "Init called with null handle");
            return false;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            bool result = instance->node_->Init();
            LOG4CXX_TRACE(logger, "INode::Init() returned: " << result);
            return result;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "Exception in INode::Init(): " << e.what());
            return false;
        } catch (...) {
            LOG4CXX_ERROR(logger, "Unknown exception in INode::Init()");
            return false;
        }
    };
    
    // Bind Start() method
    facade_.Start = [](NodeHandle handle) -> bool {
        if (!handle) {
            LOG4CXX_WARN(logger, "Start called with null handle");
            return false;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            bool result = instance->node_->Start();
            LOG4CXX_TRACE(logger, "INode::Start() returned: " << result);
            return result;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "Exception in INode::Start(): " << e.what());
            return false;
        } catch (...) {
            LOG4CXX_ERROR(logger, "Unknown exception in INode::Start()");
            return false;
        }
    };
    
    // Bind Stop() method
    facade_.Stop = [](NodeHandle handle) {
        if (!handle) {
            LOG4CXX_WARN(logger, "Stop called with null handle");
            return;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            instance->node_->Stop();
            LOG4CXX_TRACE(logger, "INode::Stop() completed");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "Exception in INode::Stop(): " << e.what());
        } catch (...) {
            LOG4CXX_ERROR(logger, "Unknown exception in INode::Stop()");
        }
    };
    
    // Bind Join() method
    facade_.Join = [](NodeHandle handle) -> bool {
        if (!handle) {
            LOG4CXX_WARN(logger, "Join called with null handle");
            return false;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            instance->node_->Join();
            LOG4CXX_TRACE(logger, "INode::Join() completed");
            return true;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "Exception in INode::Join(): " << e.what());
            return false;
        } catch (...) {
            LOG4CXX_ERROR(logger, "Unknown exception in INode::Join()");
            return false;
        }
    };
    
    // Bind JoinWithTimeout() method
    facade_.JoinWithTimeout = [](NodeHandle handle, std::chrono::milliseconds timeout) -> bool {
        if (!handle) {
            LOG4CXX_WARN(logger, "JoinWithTimeout called with null handle");
            return false;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            bool result = instance->node_->JoinWithTimeout(timeout);
            LOG4CXX_TRACE(logger, "INode::JoinWithTimeout() returned: " << result);
            return result;
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger, "Exception in INode::JoinWithTimeout(): " << e.what());
            return false;
        } catch (...) {
            LOG4CXX_ERROR(logger, "Unknown exception in INode::JoinWithTimeout()");
            return false;
        }
    };
    
    // Bind Execute() method - may not be implemented in INode
    facade_.Execute = [](NodeHandle) {
        // INode doesn't have Execute(), so this is a no-op
        // (Execution happens in node's internal threads)
    };
    
    // Bind GetInputPortMetadata() method
    facade_.GetInputPortMetadata = [](NodeHandle handle, size_t* out_count) -> PortMetadataC* {
        if (!handle || !out_count) {
            return nullptr;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            auto ports = instance->node_->InputPorts();
            if (ports.empty()) {
                *out_count = 0;
                return nullptr;
            }
            
            // Allocate array for metadata
            auto* metadata = new PortMetadataC[ports.size()];
            
            // Fill in metadata
            for (size_t i = 0; i < ports.size(); ++i) {
                metadata[i] = MakePortMetadataC(
                    i,
                    ports[i].type_name,
                    ports[i].type_name,
                    "input");
            }
            
            *out_count = ports.size();
            return metadata;
        } catch (...) {
            return nullptr;
        }
    };
    
    // Bind GetOutputPortMetadata() method
    facade_.GetOutputPortMetadata = [](NodeHandle handle, size_t* out_count) -> PortMetadataC* {
        if (!handle || !out_count) {
            return nullptr;
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        try {
            auto ports = instance->node_->OutputPorts();
            if (ports.empty()) {
                *out_count = 0;
                return nullptr;
            }
            
            // Allocate array for metadata
            auto* metadata = new PortMetadataC[ports.size()];
            
            // Fill in metadata
            for (size_t i = 0; i < ports.size(); ++i) {
                metadata[i] = MakePortMetadataC(
                    i,
                    ports[i].type_name,
                    ports[i].type_name,
                    "output");
            }
            
            *out_count = ports.size();
            return metadata;
        } catch (...) {
            return nullptr;
        }
    };
    
    // Bind FreePortMetadata() for cleanup
    facade_.FreePortMetadata = [](PortMetadataC* metadata) {
        if (metadata) {
            delete[] metadata;
        }
    };
    
    // Bind GetName() method
    facade_.GetName = [](NodeHandle handle) -> const char* {
        if (!handle) {
            return "";
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        return instance->type_name_.c_str();
    };
    
    // Bind GetType() method
    facade_.GetType = [](NodeHandle handle) -> const char* {
        if (!handle) {
            return "";
        }
        auto* instance = static_cast<StaticNodeInstance*>(handle);
        return instance->type_name_.c_str();
    };
    
    // Set remaining function pointers to nullptr (not implemented for static nodes)
    facade_.GetInputPortCount = nullptr;
    facade_.GetOutputPortCount = nullptr;
    facade_.GetInputPortName = nullptr;
    facade_.GetOutputPortName = nullptr;
    facade_.GetThreadMetrics = nullptr;
    facade_.GetThreadUtilizationPercent = nullptr;
    facade_.Destroy = nullptr;
}

}  // namespace graph::config
