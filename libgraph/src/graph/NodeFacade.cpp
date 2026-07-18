/**
 * @file NodeFacade.cpp
 * @brief Node Facade Graph runtime support.
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

#include "graph/NodeFacade.hpp"
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <iostream>
#include <chrono>
#include <string>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <iomanip>
#include <cassert>
#include <nlohmann/json.hpp>

#include "graph/IConfigurable.hpp"
#include "graph/IPortFunction.hpp"
#include "graph/NodeFacadeInterop.hpp"

namespace graph {

namespace {

/**
 * @class DescriptorPortFunction
 * @brief Descriptor port function implementation for GraphX.
 */
class DescriptorPortFunction final : public IPortFunction {
public:
    explicit DescriptorPortFunction(RuntimePortDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    std::size_t GetPortId() const override {
        return descriptor_.id;
    }

    std::string_view GetTypeName() const override {
        return descriptor_.payload_type;
    }

    PortDirection GetDirection() const override {
        return descriptor_.direction;
    }

    std::string_view GetTransportTypeName() const override {
        return descriptor_.transport_type;
    }

    void SetCapacity(std::size_t capacity) override {
        capacity_ = capacity;
    }

    std::size_t GetQueueSize() const override {
        return 0;
    }

    bool Init() override {
        return true;
    }

    bool Start() override {
        return true;
    }

    void Stop() override {
    }

    void Join() override {
    }

    bool JoinWithTimeout(std::chrono::milliseconds) override {
        return true;
    }

    std::expected<void, RuntimePortConnectError>
    ConnectTo(IPortFunction& destination, std::size_t capacity) override {
        if (GetDirection() != PortDirection::Output ||
            destination.GetDirection() != PortDirection::Input) {
            return std::unexpected(RuntimePortConnectError::DirectionMismatch);
        }

        if (GetTypeName() != destination.GetTypeName()) {
            return std::unexpected(RuntimePortConnectError::PayloadTypeMismatch);
        }

        const std::string_view src_transport = GetTransportTypeName();
        const std::string_view dst_transport = destination.GetTransportTypeName();
        if (!src_transport.empty() && !dst_transport.empty() && src_transport != dst_transport) {
            return std::unexpected(RuntimePortConnectError::TransportTypeMismatch);
        }

        SetCapacity(capacity);
        destination.SetCapacity(capacity);
        return {};
    }

    std::expected<bool, RuntimePortConnectError>
    TransferTo(IPortFunction& destination) override {
        if (GetDirection() != PortDirection::Output ||
            destination.GetDirection() != PortDirection::Input) {
            return std::unexpected(RuntimePortConnectError::DirectionMismatch);
        }

        if (GetTypeName() != destination.GetTypeName()) {
            return std::unexpected(RuntimePortConnectError::PayloadTypeMismatch);
        }

        return false;
    }

    const void* GetQueueVoid() const override {
        return nullptr;
    }

private:
    const RuntimePortDescriptor descriptor_;
    std::size_t capacity_ = 0;
};

/**
 * @brief To facade port info.
 * @param metadata Parameter for to facade port info.
 */
INodeFacade::PortInfo ToFacadePortInfo(const PortMetadata& metadata) {
    return INodeFacade::PortInfo{
        .name = metadata.port_name,
        .type = metadata.payload_type,
        .direction = metadata.direction,
    };
}

std::expected<RuntimePortHandle, RuntimePortLookupError> LookupPortHandle(
    const std::vector<PortMetadataC>& metadata,
    std::string_view name_or_id,
    std::size_t node_index,
    PortDirection direction,
    NodeHandle handle,
    const NodeFacade* facade) {
    std::size_t port_id = 0;
    const bool parsed_as_id = [&]() {
        if (name_or_id.empty()) {
            return false;
        }

        const char* begin = name_or_id.data();
        const char* end = begin + name_or_id.size();
        auto [ptr, ec] = std::from_chars(begin, end, port_id);
        return ec == std::errc{} && ptr == end;
    }();

    const auto parse_port_alias_to_id = [&]() -> std::optional<std::size_t> {
        if (parsed_as_id) {
            return port_id;
        }

        if (name_or_id.empty()) {
            return std::nullopt;
        }

        if (name_or_id == "In" || name_or_id == "Input" ||
            name_or_id == "Out" || name_or_id == "Output" ||
            name_or_id == "Data" || name_or_id == "State") {
            return 0;
        }

        std::size_t parsed = 0;
        const auto pos = name_or_id.find_last_not_of("0123456789");
        if (pos == std::string_view::npos || pos + 1 >= name_or_id.size()) {
            return std::nullopt;
        }

        const std::string_view suffix = name_or_id.substr(pos + 1);
        auto [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
        if (ec == std::errc{} && ptr == suffix.data() + suffix.size()) {
            return parsed;
        }

        return std::nullopt;
    };

    const auto try_create_runtime_port = [&](std::size_t requested_port_id)
        -> std::shared_ptr<IPortFunction> {
        if (!facade || !handle || !facade->DestroyRuntimePort) {
            return nullptr;
        }

        void* runtime_port = nullptr;
        if (direction == PortDirection::Input && facade->CreateInputRuntimePort) {
            runtime_port = facade->CreateInputRuntimePort(handle, requested_port_id);
        } else if (direction == PortDirection::Output && facade->CreateOutputRuntimePort) {
            runtime_port = facade->CreateOutputRuntimePort(handle, requested_port_id);
        }

        if (!runtime_port) {
            return nullptr;
        }

        auto destroy_runtime_port = facade->DestroyRuntimePort;
        return std::shared_ptr<IPortFunction>(
            static_cast<IPortFunction*>(runtime_port),
            [destroy_runtime_port](IPortFunction* runtime_port_ptr) {
                destroy_runtime_port(static_cast<void*>(runtime_port_ptr));
            });
    };

    const auto build_handle_from_runtime_port = [&](std::size_t resolved_port_id,
                                                    std::shared_ptr<IPortFunction> owned_port)
        -> RuntimePortHandle {
        RuntimePortDescriptor descriptor{
            .id = resolved_port_id,
            .name = std::string(name_or_id),
            .direction = direction,
            .payload_type = std::string(owned_port ? owned_port->GetTypeName() : ""),
            .transport_type = std::string(owned_port ? owned_port->GetTransportTypeName() : "runtime.descriptor"),
        };

        if (descriptor.name.empty()) {
            descriptor.name = std::to_string(resolved_port_id);
        }

        return RuntimePortHandle{
            .node_index = node_index,
            .descriptor = std::move(descriptor),
            .owned_port = owned_port,
            .port = owned_port.get(),
        };
    };

    if (!metadata.empty()) {
        for (const auto& port : metadata) {
            const bool id_match = parsed_as_id && port.index == port_id;
            const bool name_match = !parsed_as_id && name_or_id == port.port_name;
            if (!id_match && !name_match) {
                continue;
            }

            RuntimePortDescriptor descriptor{
                .id = port.index,
                .name = port.port_name,
                .direction = direction,
                .payload_type = port.payload_type,
                .transport_type = "runtime.descriptor",
            };

            std::shared_ptr<IPortFunction> owned_port = try_create_runtime_port(port.index);
            if (owned_port) {
                descriptor.transport_type = std::string(owned_port->GetTransportTypeName());
            } else {
                owned_port = std::make_shared<DescriptorPortFunction>(descriptor);
            }

            return RuntimePortHandle{
                .node_index = node_index,
                .descriptor = std::move(descriptor),
                .owned_port = owned_port,
                .port = owned_port.get(),
            };
        }

        if (const auto alias_id = parse_port_alias_to_id()) {
            for (const auto& port : metadata) {
                if (port.index != *alias_id) {
                    continue;
                }

                RuntimePortDescriptor descriptor{
                    .id = port.index,
                    .name = port.port_name,
                    .direction = direction,
                    .payload_type = port.payload_type,
                    .transport_type = "runtime.descriptor",
                };

                std::shared_ptr<IPortFunction> owned_port = try_create_runtime_port(port.index);
                if (owned_port) {
                    descriptor.transport_type = std::string(owned_port->GetTransportTypeName());
                } else {
                    owned_port = std::make_shared<DescriptorPortFunction>(descriptor);
                }

                return RuntimePortHandle{
                    .node_index = node_index,
                    .descriptor = std::move(descriptor),
                    .owned_port = owned_port,
                    .port = owned_port.get(),
                };
            }
        }
    }

    if (const auto alias_id = parse_port_alias_to_id()) {
        if (auto owned_port = try_create_runtime_port(*alias_id)) {
            return build_handle_from_runtime_port(*alias_id, std::move(owned_port));
        }
    }

    if (metadata.empty()) {
        return std::unexpected(RuntimePortLookupError::MetadataUnavailable);
    }

    return std::unexpected(RuntimePortLookupError::PortNotFound);
}

}  // namespace

log4cxx::LoggerPtr NodeFacadeAdapter::logger_ =
    log4cxx::Logger::getLogger("graph.NodeFacadeAdapter");

NodeFacadeAdapter::NodeFacadeAdapter(
        NodeHandle handle,
        const NodeFacade* facade,
    const INodeMetadataService* metadata_service)
    : handle_(handle), facade_(facade), initialized_(false), started_(false),
            data_injection_node_config_ptr_(nullptr),
        metadata_service_(metadata_service ? metadata_service : &GetDefaultNodeMetadataService()) {
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

/**
 * @brief Extract interfaces.
 */
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
        metadata_service_(other.metadata_service_) {
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
    metadata_service_ = other.metadata_service_;
    
    // Invalidate the other object
    other.handle_ = nullptr;
    other.facade_ = nullptr;
    other.metadata_service_ = &GetDefaultNodeMetadataService();
    
    return *this;
}

/**
 * @brief Get lifecycle state.
 */
int NodeFacadeAdapter::GetLifecycleState() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetLifecycleState()");
    
    if (!facade_ || !facade_->GetLifecycleState || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or GetLifecycleState function not set");
        return -1;
    }
    
    return facade_->GetLifecycleState(handle_);
}

/**
 * @brief Init.
 */
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

/**
 * @brief Start.
 */
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

/**
 * @brief Stop.
 */
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
    // Keep started_ set until Join has joined the plugin-owned port threads.
    // Clearing it here made the normal GraphManager Stop -> Join sequence skip
    // Join for every dynamically loaded node.
    LOG4CXX_TRACE(logger_, "Node stop requested; join remains required");
}

/**
 * @brief Cleanup.
 */
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
            (void)Join();
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

/**
 * @brief Join.
 */
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
        started_ = false;
        LOG4CXX_TRACE(logger_, "Node joined successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node join failed");
    }
    
    return result;
}

/**
 * @brief Join with timeout.
 * @param timeout Parameter for join with timeout.
 */
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
        started_ = false;
        LOG4CXX_TRACE(logger_, "Node joined successfully");
    } else {
        LOG4CXX_WARN(logger_, "Node join failed");
    }
    
    return result;
}

/**
 * @brief Execute.
 */
void NodeFacadeAdapter::Execute() {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::Execute()");
    
    if (!facade_ || !facade_->Execute || !handle_) {
        LOG4CXX_WARN(logger_, "Facade or Execute function not set");
        return;
    }
    
    facade_->Execute(handle_);
}

/**
 * @brief Get name.
 */
const std::string NodeFacadeAdapter::GetName() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetName()");
    
    if (!facade_ || !facade_->GetName || !handle_) {
        static const std::string empty;
        return empty;
    }
    
    const char* name = facade_->GetName(handle_);
    return name ? std::string(name) : "";
}

/**
 * @brief Set name.
 * @param name Parameter for set name.
 */
void NodeFacadeAdapter::SetName(const std::string& name) {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::SetName()");
    
    if (!facade_ || !facade_->SetName || !handle_) {
        return;
    }
    
    facade_->SetName(handle_, name.c_str());
}

/**
 * @brief Get type.
 */
const std::string NodeFacadeAdapter::GetType() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetType()");
    
    if (!facade_ || !facade_->GetType || !handle_) {
        static const std::string empty;
        return empty;
    }
    
    const char* type = facade_->GetType(handle_);
    return type ? std::string(type) : "";
}

/**
 * @brief Get input port count.
 */
size_t NodeFacadeAdapter::GetInputPortCount() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetInputPortCount()");
    
    if (!facade_ || !facade_->GetInputPortCount || !handle_) {
        return 0;
    }
    
    return facade_->GetInputPortCount(handle_);
}

/**
 * @brief Get output port count.
 */
size_t NodeFacadeAdapter::GetOutputPortCount() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetOutputPortCount()");
    
    if (!facade_ || !facade_->GetOutputPortCount || !handle_) {
        return 0;
    }
    
    return facade_->GetOutputPortCount(handle_);
}

/**
 * @brief Get input port name.
 * @param port Parameter for get input port name.
 */
std::string NodeFacadeAdapter::GetInputPortName(size_t port) const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetInputPortName(" << port << ")");
    
    if (!facade_ || !facade_->GetInputPortName || !handle_) {
        return "";
    }
    
    const char* name = facade_->GetInputPortName(handle_, port);
    return name ? std::string(name) : "";
}

/**
 * @brief Get output port name.
 * @param port Parameter for get output port name.
 */
std::string NodeFacadeAdapter::GetOutputPortName(size_t port) const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetOutputPortName(" << port << ")");
    
    if (!facade_ || !facade_->GetOutputPortName || !handle_) {
        return "";
    }
    
    const char* name = facade_->GetOutputPortName(handle_, port);
    return name ? std::string(name) : "";
}

/**
 * @brief Get input port metadata.
 */
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

/**
 * @brief Get output port metadata.
 */
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

std::vector<ConfigFieldMetadataC> NodeFacadeAdapter::GetConfigFieldMetadata() const {
    LOG4CXX_TRACE(logger_, "NodeFacadeAdapter::GetConfigFieldMetadata()");

    std::vector<ConfigFieldMetadataC> result;

    if (!facade_ || !facade_->GetConfigFieldMetadata || !handle_) {
        LOG4CXX_TRACE(logger_, "GetConfigFieldMetadata not implemented in facade");
        return result;
    }

    size_t count = 0;
    ConfigFieldMetadataC* metadata = facade_->GetConfigFieldMetadata(handle_, &count);

    if (!metadata || count == 0) {
        if (metadata && facade_->FreeConfigFieldMetadata) {
            facade_->FreeConfigFieldMetadata(metadata);
        }
        return result;
    }

    try {
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(metadata[i]);
        }
        LOG4CXX_TRACE(logger_, "Retrieved " << count << " config field metadata entries");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Exception while copying config field metadata: " << e.what());
        result.clear();
    }

    if (facade_->FreeConfigFieldMetadata) {
        facade_->FreeConfigFieldMetadata(metadata);
    }

    return result;
}

std::expected<RuntimePortHandle, RuntimePortLookupError>
NodeFacadeAdapter::GetInputPortHandle(std::string_view name_or_id, std::size_t node_index) const {
    return LookupPortHandle(
        GetInputPortMetadata(),
        name_or_id,
        node_index,
        PortDirection::Input,
        handle_,
        facade_);
}

std::expected<RuntimePortHandle, RuntimePortLookupError>
NodeFacadeAdapter::GetOutputPortHandle(std::string_view name_or_id, std::size_t node_index) const {
    return LookupPortHandle(
        GetOutputPortMetadata(),
        name_or_id,
        node_index,
        PortDirection::Output,
        handle_,
        facade_);
}

/**
 * @brief Get status string.
 */
std::string NodeFacadeAdapter::GetStatusString() const {
    // Not yet implemented
    return "";
}

/**
 * @brief Print port metadata.
 * @param metadata Parameter for print port metadata.
 */
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

/**
 * @brief Display node facade adapter.
 * @param adapter Parameter for display node facade adapter.
 */
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
/**
 * @brief Get description.
 */
std::string NodeFacadeAdapter::GetDescription() const {
    return "";
}

/**
 * @brief Get metadata.
 */
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

/**
 * @brief Get descriptor.
 */
NodeDescriptor NodeFacadeAdapter::GetDescriptor() const {
    std::vector<PortMetadata> input_ports;
    std::vector<PortMetadata> output_ports;
    std::vector<ConfigFieldMetadata> config_fields;

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

    const auto config_metadata = GetConfigFieldMetadata();
    config_fields.reserve(config_metadata.size());
    for (const auto& config_field : config_metadata) {
        config_fields.push_back(ToConfigFieldMetadata(config_field));
    }

    const INodeDescriptorProvider& descriptor_provider = metadata_service_->DescriptorProvider();

    auto* parameterized = static_cast<IParameterized*>(GetParameterizedPtr().get());
    auto descriptor = descriptor_provider.BuildRuntimeDescriptor(RuntimeNodeDescriptorRequest{
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

    if (descriptor.config_fields.empty() && !config_fields.empty()) {
        descriptor.config_fields = std::move(config_fields);
    }

    return descriptor;
}

/**
 * @brief Get interface.
 * @param name Parameter for get interface.
 */
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

/**
 * @brief Get csv interface.
 */
std::shared_ptr<void> NodeFacadeAdapter::GetCSVInterface() const {
    // Return through generic interface system
    return GetInterface("csv_injection");
}

// ============================================================================
// Phase 3: Memory Management - Convenience Methods
// ============================================================================

/**
 * @brief Get input port names.
 */
std::vector<std::string> NodeFacadeAdapter::GetInputPortNames() const {
    std::vector<std::string> names;
    
    size_t count = GetInputPortCount();
    names.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        names.push_back(GetInputPortName(i));
    }
    
    return names;
}

/**
 * @brief Get output port names.
 */
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
