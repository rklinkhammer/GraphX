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

#include "graph/NodeFacade.hpp"
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <iostream>
#include <chrono>
#include <string>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <iomanip>
#include <cassert>
#include <nlohmann/json.hpp>

#include "graph/IConfigurable.hpp"
#include "graph/NodeFacadeInterop.hpp"

namespace graph {

namespace {

INodeFacade::PortInfo ToFacadePortInfo(const PortMetadata& metadata) {
    return INodeFacade::PortInfo{
        .name = metadata.port_name,
        .type = metadata.payload_type,
        .direction = metadata.direction,
    };
}

}  // namespace

log4cxx::LoggerPtr NodeFacadeAdapter::logger_ =
    log4cxx::Logger::getLogger("graph.NodeFacadeAdapter");

NodeFacadeAdapter::NodeFacadeAdapter(
        NodeHandle handle,
        const NodeFacade* facade,
        const INodeDescriptorProvider* descriptor_provider)
    : handle_(handle), facade_(facade), initialized_(false), started_(false),
            data_injection_node_config_ptr_(nullptr),
            descriptor_provider_(descriptor_provider ? descriptor_provider : &GetDefaultNodeDescriptorProvider()) {
    // Preconditions: handle and facade must be valid (non-null)
    // If violated, it indicates a programming error in the caller
    assert(handle != nullptr && "NodeFacadeAdapter constructor: handle must not be null");
    assert(facade != nullptr && "NodeFacadeAdapter constructor: facade must not be null");
    assert(facade->Init != nullptr && "NodeFacadeAdapter constructor: facade->Init must not be null");
    assert(facade->Start != nullptr && "NodeFacadeAdapter constructor: facade->Start must not be null");
    assert(facade->Stop != nullptr && "NodeFacadeAdapter constructor: facade->Stop must not be null");
    assert(facade->Destroy != nullptr && "NodeFacadeAdapter constructor: facade->Destroy must not be null");
    
    LOG4CXX_TRACE(logger_, "Created NodeFacadeAdapter for handle: " << handle);
    ExtractInterfaces();
}

NodeFacadeAdapter::~NodeFacadeAdapter() {
    LOG4CXX_TRACE(logger_, "Destroying NodeFacadeAdapter");
    
    // NOTE: Do NOT call Cleanup() or facade_->Destroy() here!
    // Cleanup() must be called explicitly BEFORE destruction via GraphManager
    // to avoid vptr corruption during ConcreteNode destructor chains.
    // See: Cleanup() method and GraphManager destructor for proper lifecycle.
}

void NodeFacadeAdapter::ExtractInterfaces() {
    // Preconditions established by constructor asserts:
    // - handle_ is valid (non-null)
    // - facade_ is valid (non-null)
    // Therefore, no need to check for null here
    
    const auto interfaces = ExtractNodeInterfaces(handle_, facade_);

    if (interfaces.data_injection_node_config) {
        // Store as shared_ptr with no-op deleter (we don't own the pointer)
        // The pointer is owned by the plugin node and valid for node's lifetime
        data_injection_node_config_ptr_ =
            std::shared_ptr<void>(interfaces.data_injection_node_config, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted DataInjectionNodeConfig interface from plugin");
    }

    if (interfaces.configurable) {
        configurable_ptr_ = std::shared_ptr<void>(interfaces.configurable, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted IConfigurable interface from plugin");
    }

    if (interfaces.diagnosable) {
        diagnosable_ptr_ = std::shared_ptr<void>(interfaces.diagnosable, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted IDiagnosable interface from plugin");
    }

    if (interfaces.parameterized) {
        parameterized_ptr_ = std::shared_ptr<void>(interfaces.parameterized, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted IParameterized interface from plugin");
    }

    if (interfaces.metrics_callback_provider) {
        metrics_callback_provider_ptr_ =
            std::shared_ptr<void>(interfaces.metrics_callback_provider, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted IMetricsCallbackProvider interface from plugin");
    }

    if (interfaces.completion_callback_provider) {
        completion_callback_provider_ptr_ =
            std::shared_ptr<void>(interfaces.completion_callback_provider, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted ICompletionCallback interface from plugin");
    }

    if (interfaces.gpu_capability_binding) {
        gpu_capability_binding_ptr_ =
            std::shared_ptr<void>(interfaces.gpu_capability_binding, [](void*) {});
        LOG4CXX_TRACE(logger_, "Extracted IGpuCapabilityBinding interface from plugin");
    }
}

NodeFacadeAdapter::NodeFacadeAdapter(NodeFacadeAdapter&& other) noexcept
    : handle_(other.handle_), facade_(other.facade_), 
      initialized_(other.initialized_), started_(other.started_),
      data_injection_node_config_ptr_(std::move(other.data_injection_node_config_ptr_)),
      configurable_ptr_(std::move(other.configurable_ptr_)),
      diagnosable_ptr_(std::move(other.diagnosable_ptr_)),
      parameterized_ptr_(std::move(other.parameterized_ptr_)),
      metrics_callback_provider_ptr_(std::move(other.metrics_callback_provider_ptr_)),
    completion_callback_provider_ptr_(std::move(other.completion_callback_provider_ptr_)),
        gpu_capability_binding_ptr_(std::move(other.gpu_capability_binding_ptr_)),
        descriptor_provider_(other.descriptor_provider_) {
    LOG4CXX_TRACE(logger_, "Move-constructing NodeFacadeAdapter");
    // Invalidate the other object so it doesn't call Destroy() in its destructor
    other.handle_ = nullptr;
    other.facade_ = nullptr;
}

NodeFacadeAdapter& NodeFacadeAdapter::operator=(NodeFacadeAdapter&& other) noexcept {
    LOG4CXX_TRACE(logger_, "Move-assigning NodeFacadeAdapter");
    
    // Clean up our current handle before taking ownership of other's
    if (started_) {
        Stop();
    }
    if (facade_ && facade_->Destroy && handle_) {
        facade_->Destroy(handle_);
    }
    
    // Take ownership
    handle_ = other.handle_;
    facade_ = other.facade_;
    initialized_ = other.initialized_;
    started_ = other.started_;
    data_injection_node_config_ptr_ = std::move(other.data_injection_node_config_ptr_);
    configurable_ptr_ = std::move(other.configurable_ptr_);
    diagnosable_ptr_ = std::move(other.diagnosable_ptr_);
    parameterized_ptr_ = std::move(other.parameterized_ptr_);
    metrics_callback_provider_ptr_ = std::move(other.metrics_callback_provider_ptr_);
    completion_callback_provider_ptr_ = std::move(other.completion_callback_provider_ptr_);
    gpu_capability_binding_ptr_ = std::move(other.gpu_capability_binding_ptr_);
    descriptor_provider_ = other.descriptor_provider_;
    
    // Invalidate the other object
    other.handle_ = nullptr;
    other.facade_ = nullptr;
    other.descriptor_provider_ = &GetDefaultNodeDescriptorProvider();
    
    return *this;
}

int NodeFacadeAdapter::GetLifecycleState() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetLifecycleState()");
    
    if (!facade_ || !facade_->GetLifecycleState || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or GetLifecycleState function not set");
        return -1;
    }
    
    return facade_->GetLifecycleState(handle_);
}

bool NodeFacadeAdapter::Init() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Init()");
    
    if (!facade_ || !facade_->Init || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Init function not set");
        return false;
    }
    
    initialized_ = facade_->Init(handle_);
    
    if (initialized_) {
        LOG4CXX_TRACE(logger_, "Node initialized successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node initialization failed");
    }
    
    return initialized_;
}

bool NodeFacadeAdapter::Start() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Start()");
    
    if (!initialized_) {
        LOG4CXX_WARN(logger_, "Node not initialized, cannot start");
        return false;
    }
    
    if (!facade_ || !facade_->Start || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Start function not set");
        return false;
    }
    
    started_ = facade_->Start(handle_);
    
    if (started_) {
        LOG4CXX_TRACE(logger_, "Node started successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node start failed");
    }
    
    return started_;
}

void NodeFacadeAdapter::Stop() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Stop()");
    
    if (!started_) {
        LOG4CXX_TRACE(logger_, "Node not started, skipping Stop()");
        return;
    }
    
    if (!facade_ || !facade_->Stop || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Stop function not set");
        return;
    }
    
    facade_->Stop(handle_);
    started_ = false;
    LOG4CXX_TRACE(logger_, "Node stopped");
}

void NodeFacadeAdapter::Cleanup() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Cleanup()");
    
    // Only call Destroy() once
    if (!handle_) {
        LOG4CXX_TRACE(logger_, "Node already cleaned up, skipping");
        return;
    }
    
    // Ensure node is stopped before destroying
    if (started_) {
        LOG4CXX_TRACE(logger_, "Node still started, stopping before cleanup...");
        try {
            Stop();
        } catch (const std::exception& e) {
            LOG4CXX_WARN(logger_, "Exception during Stop() in Cleanup: " << e.what());
        } catch (...) {
            LOG4CXX_WARN(logger_, "Unknown exception during Stop() in Cleanup");
        }
    }
    
    // IMPORTANT: Do NOT call Destroy() here!
    // Calling facade_->Destroy(handle_) during cleanup causes the plugin to delete
    // the NodePluginInstance, which releases shared_ptr<ConcreteNode>.
    // This triggers ConcreteNode's destructor while it's still being referenced
    // by the virtual function table, causing pure virtual errors.
    //
    // Instead, rely on the plugin to manage its own lifecycle. The handle will be
    // leaked if the plugin doesn't clean up automatically, but this is safer than
    // calling Destroy() during our destructor sequence.
    //
    // NOTE: This is a known limitation - plugins should implement RAII cleanup
    // via their own module initialization/cleanup handlers if needed.
    
    // Just mark as cleaned up
    handle_ = nullptr;
    LOG4CXX_TRACE(logger_, "Node marked as cleaned up (Destroy callback NOT called)");
}

bool NodeFacadeAdapter::Join() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Join()");
    
    if (!started_) {
        LOG4CXX_TRACE(logger_, "Node not started, skipping Join()");
        return true;
    }
    
    if (!facade_ || !facade_->Join || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Join function not set");
        return false;
    }
    
    bool result = facade_->Join(handle_);
    
    if (result) {
        LOG4CXX_TRACE(logger_, "Node joined successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node join failed");
    }
    
    return result;
}

bool NodeFacadeAdapter::JoinWithTimeout(std::chrono::milliseconds timeout) {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::JoinWithTimeout()");
    
    if (!started_) {
        LOG4CXX_TRACE(logger_, "Node not started, skipping JoinWithTimeout()");
        return true;
    }
    
    if (!facade_ || !facade_->JoinWithTimeout || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or JoinWithTimeout function not set");
        return false;
    }
    
    bool result = facade_->JoinWithTimeout(handle_, timeout);
    
    if (result) {
        LOG4CXX_TRACE(logger_, "Node joined successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node join failed");
    }
    
    return result;
}

void NodeFacadeAdapter::Execute() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Execute()");
    
    if (!facade_ || !facade_->Execute || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Execute function not set");
        return;
    }
    
    facade_->Execute(handle_);
}

const std::string NodeFacadeAdapter::GetName() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetName()");
    
    if (!facade_ || !facade_->GetName || !handle_) {
        static const std::string empty;
        return empty;
    }
    
    const char* name = facade_->GetName(handle_);
    return name ? std::string(name) : "";
}

void NodeFacadeAdapter::SetName(const std::string& name) {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::SetName()");
    
    if (!facade_ || !facade_->SetName || !handle_) {
        return;
    }
    
    facade_->SetName(handle_, name.c_str());
}

const std::string NodeFacadeAdapter::GetType() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetType()");
    
    if (!facade_ || !facade_->GetType || !handle_) {
        static const std::string empty;
        return empty;
    }
    
    const char* type = facade_->GetType(handle_);
    return type ? std::string(type) : "";
}

size_t NodeFacadeAdapter::GetInputPortCount() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetInputPortCount()");
    
    if (!facade_ || !facade_->GetInputPortCount || !handle_) {
        return 0;
    }
    
    return facade_->GetInputPortCount(handle_);
}

size_t NodeFacadeAdapter::GetOutputPortCount() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetOutputPortCount()");
    
    if (!facade_ || !facade_->GetOutputPortCount || !handle_) {
        return 0;
    }
    
    return facade_->GetOutputPortCount(handle_);
}

std::string NodeFacadeAdapter::GetInputPortName(size_t port) const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetInputPortName(" << port << ")");
    
    if (!facade_ || !facade_->GetInputPortName || !handle_) {
        return "";
    }
    
    const char* name = facade_->GetInputPortName(handle_, port);
    return name ? std::string(name) : "";
}

std::string NodeFacadeAdapter::GetOutputPortName(size_t port) const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetOutputPortName(" << port << ")");
    
    if (!facade_ || !facade_->GetOutputPortName || !handle_) {
        return "";
    }
    
    const char* name = facade_->GetOutputPortName(handle_, port);
    return name ? std::string(name) : "";
}

std::vector<PortMetadataC> NodeFacadeAdapter::GetInputPortMetadata() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetInputPortMetadata()");
    
    std::vector<PortMetadataC> result;
    
    if (!facade_ || !facade_->GetInputPortMetadata || !handle_) {
        LOG4CXX_TRACE(logger_, "GetInputPortMetadata not implemented in facade");
        return result;
    }
    
    size_t count = 0;
    PortMetadataC* metadata = facade_->GetInputPortMetadata(handle_, &count);
    
    if (!metadata || count == 0) {
        LOG4CXX_TRACE(logger_, "No input port metadata available");
        if (metadata && facade_ && facade_->FreePortMetadata) {
            facade_->FreePortMetadata(metadata);
        }
        return result;
    }
    
    // Copy metadata from C array to C++ vector
    try {
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(metadata[i]);
        }
        LOG4CXX_TRACE(logger_, "Retrieved " << count << " input port metadata entries");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Exception while copying input metadata: " << e.what());
        result.clear();
    }
    
    // Free the C array
    if (facade_ && facade_->FreePortMetadata) {
        facade_->FreePortMetadata(metadata);
    }
    
    return result;
}

std::vector<PortMetadataC> NodeFacadeAdapter::GetOutputPortMetadata() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetOutputPortMetadata()");
    
    std::vector<PortMetadataC> result;
    
    if (!facade_ || !facade_->GetOutputPortMetadata || !handle_) {
        LOG4CXX_TRACE(logger_, "GetOutputPortMetadata not implemented in facade");
        return result;
    }
    size_t count = 0;
    PortMetadataC* metadata = facade_->GetOutputPortMetadata(handle_, &count);

    if (!metadata || count == 0) {
        LOG4CXX_TRACE(logger_, "No output port metadata available");
        if (metadata && facade_ && facade_->FreePortMetadata) {
            facade_->FreePortMetadata(metadata);
        }
        return result;
    }

    // Copy metadata from C array to C++ vector
    try {
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(metadata[i]);
        }
        LOG4CXX_TRACE(logger_, "Retrieved " << count << " output port metadata entries");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Exception while copying output metadata: " << e.what());
        result.clear();
    }
    
    // Free the C array
    if (facade_ && facade_->FreePortMetadata) {
        facade_->FreePortMetadata(metadata);
    }
    
    return result;
}

std::string NodeFacadeAdapter::GetStatusString() const {
    // Not yet implemented
    return "";
}

static void PrintPortMetadata(const std::vector<PortMetadataC>& metadata) {
    if (metadata.empty()) {
        std::cout << "  No port metadata available\n";
        return;
    }

    // Print header
    std::cout << "┌─────────┬──────────────────────────┬──────────────────────────┬───────────┐\n";
    std::cout << "│ Index   │ Port Name                │ Payload Type             │ Direction │\n";
    std::cout << "├─────────┼──────────────────────────┼──────────────────────────┼───────────┤\n";

    // Print each metadata entry
    for (const auto& port : metadata) {
        std::cout << "│ " << std::setw(5) << port.index << "   │ "
                  << std::setw(24) << port.port_name << " │ "
                  << std::setw(24) << port.payload_type << " │ "
                  << std::setw(9) << port.direction << " │\n";
    }

    std::cout << "└─────────┴──────────────────────────┴──────────────────────────┴───────────┘\n";
}

void DisplayNodeFacadeAdapter(std::shared_ptr<NodeFacadeAdapter> adapter) {
    if (!adapter) {
        std::cout << "NodeFacadeAdapter is null\n";
        return;
    }
    std::cout << "Node Name: " << adapter->GetName() << "\n";
    std::cout << "Node Type: " << adapter->GetType() << "\n";
    std::cout << "Input Ports: " << adapter->GetInputPortCount() << "\n";
    for (size_t i = 0; i < adapter->GetInputPortCount(); ++i) {
        std::cout << "  Input Port " << i << ": " << adapter->GetInputPortName(i) << "\n";
    }
    std::cout << "Output Ports: " << adapter->GetOutputPortCount() << "\n";
    for (size_t i = 0; i < adapter->GetOutputPortCount(); ++i) {
        std::cout << "  Output Port " << i << ": " << adapter->GetOutputPortName(i) << "\n";
    }
    std::cout << "Input Port Metadata:\n";
    PrintPortMetadata(adapter->GetInputPortMetadata());
    std::cout << "Output Port Metadata:\n";
    PrintPortMetadata(adapter->GetOutputPortMetadata());
}

// ============================================================================
// Phase 2: Unified INodeFacade Implementation
// ============================================================================

// Get node description (not available in C interface, return empty string)
std::string NodeFacadeAdapter::GetDescription() const {
    return "";
}

INodeFacade::NodeMetadata NodeFacadeAdapter::GetMetadata() const {
    auto descriptor = GetDescriptor();

    INodeFacade::NodeMetadata metadata;

    metadata.name = descriptor.name;
    metadata.type = descriptor.type;
    metadata.description = descriptor.description;
    metadata.input_port_count = descriptor.input_ports.size();
    metadata.output_port_count = descriptor.output_ports.size();

    metadata.input_ports.reserve(descriptor.input_ports.size());
    for (const auto& port : descriptor.input_ports) {
        metadata.input_ports.push_back(ToFacadePortInfo(port));
    }

    metadata.output_ports.reserve(descriptor.output_ports.size());
    for (const auto& port : descriptor.output_ports) {
        metadata.output_ports.push_back(ToFacadePortInfo(port));
    }

    return metadata;
}

NodeDescriptor NodeFacadeAdapter::GetDescriptor() const {
    std::vector<PortMetadata> input_ports;
    std::vector<PortMetadata> output_ports;

    const auto input_metadata = GetInputPortMetadata();
    input_ports.reserve(input_metadata.size());
    for (const auto& input_port : input_metadata) {
        input_ports.push_back(ToPortMetadata(input_port));
    }

    const auto output_metadata = GetOutputPortMetadata();
    output_ports.reserve(output_metadata.size());
    for (const auto& output_port : output_metadata) {
        output_ports.push_back(ToPortMetadata(output_port));
    }

    auto* parameterized = static_cast<IParameterized*>(GetParameterizedPtr().get());
    return descriptor_provider_->BuildRuntimeDescriptor(RuntimeNodeDescriptorRequest{
        .seed = NodeDescriptorSeed{
            .name = GetName(),
            .type = GetType(),
            .description = GetDescription(),
            .lifecycle_state = static_cast<LifecycleState>(GetLifecycleState()),
            .supports_configuration = static_cast<bool>(GetConfigurablePtr()),
        },
        .parameterized = parameterized,
        .input_ports = std::move(input_ports),
        .output_ports = std::move(output_ports),
    });
}

std::shared_ptr<void> NodeFacadeAdapter::GetInterface(const std::string& name) const {
    if (name == "csv_injection") {
        return GetDataInjectionNodeConfigPtr();
    } else if (name == "metrics_callback") {
        return GetMetricsCallbackProviderPtr();
    } else if (name == "completion_callback") {
        return GetCompletionCallbackProviderPtr();
    } else if (name == "gpu_capability_binding") {
        return GetGpuCapabilityBindingPtr();
    } else if (name == "configurable") {
        return GetConfigurablePtr();
    } else if (name == "diagnosable") {
        return GetDiagnosablePtr();
    } else if (name == "parameterized") {
        return GetParameterizedPtr();
    }
    return nullptr;
}

std::shared_ptr<void> NodeFacadeAdapter::GetCSVInterface() const {
    // Return through generic interface system
    return GetInterface("csv_injection");
}

// ============================================================================
// Phase 3: Memory Management - Convenience Methods
// ============================================================================

std::vector<std::string> NodeFacadeAdapter::GetInputPortNames() const {
    std::vector<std::string> names;
    
    size_t count = GetInputPortCount();
    names.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        names.push_back(GetInputPortName(i));
    }
    
    return names;
}

std::vector<std::string> NodeFacadeAdapter::GetOutputPortNames() const {
    std::vector<std::string> names;
    
    size_t count = GetOutputPortCount();
    names.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        names.push_back(GetOutputPortName(i));
    }
    
    return names;
}

}  // namespace graph
