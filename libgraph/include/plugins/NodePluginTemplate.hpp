// MIT License
/// @file NodePluginTemplate.hpp
/// @brief Plugin system support for NodePluginTemplate

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

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <algorithm>
#include <ranges>
#include <span>
#include <iostream>
#include "graph/Nodes.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/NodePluginInstance.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "graph/CompletionSignal.hpp"
#include "graph/IDataInjectionSource.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include <nlohmann/json.hpp>
#include <log4cxx/logger.h>

namespace graph {

// ============================================================================
// Helper Functions - PortMetadata conversion utilities
// ============================================================================

/// @brief Forward declarations
static log4cxx::LoggerPtr _metadata_logger = 
    log4cxx::Logger::getLogger("graph.plugins.PortMetadataHelper");

template <typename NodeType>
NodeDescriptor BuildNodeDescriptor(NodePluginInstance<NodeType>* inst) {
    NodeDescriptor descriptor;
    if (!inst || !inst->node) {
        return descriptor;
    }

    descriptor.name = inst->name;
    descriptor.type = inst->type;
    descriptor.lifecycle_state = inst->node->GetLifecycleState();
    descriptor.supports_configuration =
        dynamic_cast<graph::IConfigurable*>(inst->node.get()) != nullptr;
    if constexpr (requires { NodeType::Fields(); }) {
        for (const auto& field : NodeType::Fields()) {
            descriptor.config_fields.push_back(ConfigFieldMetadata{
                .name = std::string(field.name),
                .type = field.type,
                .required = field.required,
            });
        }
    }
    descriptor.input_ports = inst->node->GetInputPortMetadata();
    descriptor.output_ports = inst->node->GetOutputPortMetadata();
    return descriptor;
}

// ============================================================================
// Metadata Entry Copying
// ============================================================================

// ============================================================================
// Input Port Metadata Extraction
// ============================================================================

/// @brief Get input port metadata from a node instance
/// @tparam NodeType Type of node (must have GetInputPortMetadata() method)
/// @param handle Opaque node handle (cast to NodeType*)
/// @param out_count Output parameter: number of ports (set to 0 on error)
/// @return Heap-allocated array of PortMetadataC, or nullptr on error
///         Caller must free with FreePortMetadataImpl()
///
/// Template function for plugins to implement GetInputPortMetadata in NodeFacade.
/// Calls GetInputPortMetadata() on the node, converts to C format, and allocates
/// a heap array with owned string storage.
template <typename NodeType>
PortMetadataC* GetInputPortMetadataImpl(NodePluginInstance<NodeType>* inst, size_t* out_count)
{
    if (!inst || !out_count) {
        LOG4CXX_WARN(_metadata_logger, 
                     "GetInputPortMetadataImpl: invalid parameters (inst="
                     << (inst ? "OK" : "NULL") << ", out_count="
                     << (out_count ? "OK" : "NULL") << ")");
        if (out_count) *out_count = 0;
        return nullptr;
    }
    
    try {
        NodeType* node = inst->node.get();
        if (!node) {
            LOG4CXX_ERROR(_metadata_logger, "Failed to cast handle to node type");
            *out_count = 0;
            return nullptr;
        }
        
        auto descriptor = BuildNodeDescriptor(inst);
        *out_count = descriptor.input_ports.size();
        if (descriptor.input_ports.empty()) {
            LOG4CXX_TRACE(_metadata_logger, 
                         "GetInputPortMetadataImpl: no input ports");
            return nullptr;
        }
        
        // Allocate C array
        PortMetadataC* result = nullptr;
        try {
            result = new PortMetadataC[descriptor.input_ports.size()];
        } catch (const std::bad_alloc& e) {
            LOG4CXX_ERROR(_metadata_logger, 
                         "Failed to allocate PortMetadataC array: " << e.what());
            *out_count = 0;
            return nullptr;
        }
        
        // Convert descriptor metadata to stable C ABI representation
        for (size_t i = 0; i < descriptor.input_ports.size(); ++i) {
            result[i] = ToPortMetadataC(descriptor.input_ports[i]);
        }
        
        LOG4CXX_TRACE(_metadata_logger, 
                     "Successfully converted " << descriptor.input_ports.size() 
                     << " input port metadata entries");
        return result;
        
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(_metadata_logger, 
                     "Exception in GetInputPortMetadataImpl: " << e.what());
        *out_count = 0;
        return nullptr;
    }
}

// ============================================================================
// Output Port Metadata Extraction
// ============================================================================

/// @brief Get output port metadata from a node instance
/// @tparam NodeType Type of node (must have GetOutputPortMetadata() method)
/// @param handle Opaque node handle (cast to NodeType*)
/// @param out_count Output parameter: number of ports (set to 0 on error)
/// @return Heap-allocated array of PortMetadataC, or nullptr on error
///         Caller must free with FreePortMetadataImpl()
///
/// Template function for plugins to implement GetOutputPortMetadata in NodeFacade.
/// Calls GetOutputPortMetadata() on the node, converts to C format, and allocates
/// a heap array with owned string storage.
template <typename NodeType>
PortMetadataC* GetOutputPortMetadataImpl(NodePluginInstance<NodeType>* inst, size_t* out_count)
{
    if (!inst || !out_count) {
        LOG4CXX_WARN(_metadata_logger, 
                     "GetOutputPortMetadataImpl: invalid parameters (inst="
                     << (inst ? "OK" : "NULL") << ", out_count="
                     << (out_count ? "OK" : "NULL") << ")");
        if (out_count) *out_count = 0;
        return nullptr;
    }
    try {
        NodeType* node = inst->node.get();
        if (!node) {
            LOG4CXX_ERROR(_metadata_logger, "Failed to cast handle to node type");
            *out_count = 0;
            return nullptr;
        }
        
        auto descriptor = BuildNodeDescriptor(inst);
        *out_count = descriptor.output_ports.size();
        if (descriptor.output_ports.empty()) {
            LOG4CXX_TRACE(_metadata_logger, 
                         "GetOutputPortMetadataImpl: no output ports");
            return nullptr;
        }
        
        // Allocate C array
        PortMetadataC* result = nullptr;
        try {
            result = new PortMetadataC[descriptor.output_ports.size()];
        } catch (const std::bad_alloc& e) {
            LOG4CXX_ERROR(_metadata_logger, 
                         "Failed to allocate PortMetadataC array: " << e.what());
            *out_count = 0;
            return nullptr;
        }
        
        // Convert descriptor metadata to stable C ABI representation
        for (size_t i = 0; i < descriptor.output_ports.size(); ++i) {
            result[i] = ToPortMetadataC(descriptor.output_ports[i]);
        }
        
        LOG4CXX_TRACE(_metadata_logger, 
                     "Successfully converted " << descriptor.output_ports.size() 
                     << " output port metadata entries");
        return result;
        
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(_metadata_logger, 
                     "Exception in GetOutputPortMetadataImpl: " << e.what());
        *out_count = 0;
        return nullptr;
    }
}

// ============================================================================
// Memory Cleanup
// ============================================================================

/// @brief Free a PortMetadataC array
/// @param metadata Array to free (may be nullptr)
///
/// Common function for both GetInputPortMetadata and GetOutputPortMetadata
/// to use as their FreePortMetadata function pointer.
/// Safe to call with nullptr.
static inline void FreePortMetadataImpl(PortMetadataC* metadata)
{
    if (metadata) {
        delete[] metadata;
        LOG4CXX_TRACE(_metadata_logger, "Freed PortMetadataC array");
    }
}

// ============================================================================
// PluginPolicy - Customizable behavior for plugin node types
// ============================================================================

/// @brief Policy template for plugin node behavior
/// @tparam NodeT The concrete node type
/// 
/// @details
/// Can be specialized for specific node types to override default behavior.
/// 
/// **Extension Points** (methods you can override in specializations):
/// - `Execute(inst)` - Custom node execution logic
/// - `SetProperty(inst, key, value)` - Custom property setting
/// - `GetProperty(inst, key)` - Custom property retrieval
/// - `GetInputPortName(inst, id)` - Custom input port naming
/// - `GetOutputPortName(inst, id)` - Custom output port naming
/// - `GetInputPortMetadata(inst, out_count)` - Custom input port metadata
/// - `GetOutputPortMetadata(inst, out_count)` - Custom output port metadata
/// - `FreePortMetadata(metadata)` - Custom port metadata cleanup
/// - `GetConfigurationJSON(inst)` - Custom configuration retrieval (NEW)
/// - `SetConfigurationJSON(inst, json)` - Custom configuration validation (NEW)
/// - `GetDiagnosticsJSON(inst)` - Custom diagnostics with node-specific data (NEW)
/// - `SupportsDiagnostics(inst)` - Custom diagnostics capability check (NEW)
/// - `GetParametersJSON(inst)` - Custom parameter retrieval (NEW)
/// - `SupportsParameters(inst)` - Custom parameter capability check (NEW)
/// - `SetParameter(inst, name, value)` - Custom parameter setting (NEW)
///
/// **ABI Safety**:
/// All methods receive `NodePluginInstance<NodeT>*` which is a C++ wrapper.
/// The C/ABI boundary is in PluginGlue, which casts `void*` to `Instance*`
/// and delegates to Policy. This ensures type safety within the C++ layer
/// while maintaining ABI stability at the plugin boundary.
///
/// **Example Specialization**:
/// @code
/// template <>
/// struct PluginPolicy<MyNode> {
///     static const char* GetDiagnosticsJSON(NodePluginInstance<MyNode>* inst) {
///         // Return custom diagnostics with MyNode-specific data
///         static thread_local std::string result;
///         result = CreateCustomDiagnostics(inst->node.get()).dump(2);
///         return result.c_str();
///     }
/// };
/// @endcode
///
/// See POLICY_CUSTOMIZATION_EXAMPLE.md for detailed examples.
template <typename NodeT>
struct PluginPolicy {
    static constexpr const char* Description = "Graph node plugin";
    
    static void Execute(NodePluginInstance<NodeT>*) {}

    static bool SetProperty(NodePluginInstance<NodeT>*, const char*, const char*) {
        return true;  // No-op: plugins don't support properties by default
    }

    static const char* GetProperty(NodePluginInstance<NodeT>*, const char*) {
        return "";   // No properties available by default
    }

    static std::size_t GetInputPortCount(NodePluginInstance<NodeT>* inst) {
        return inst->node->GetInputPortCount();
    }

    static std::size_t GetOutputPortCount(NodePluginInstance<NodeT>* inst) {
        return inst->node->GetOutputPortCount();
    }

    static const char* GetInputPortName(NodePluginInstance<NodeT>* inst,
                                        std::size_t id) {
        try {
            std::vector<graph::PortMetadata> md = inst->node->GetInputPortMetadata();
             if (id < md.size()) return strdup(md[id].port_name.c_str());
        } catch (...) {}
        return "unknown";
    }

    static const char* GetOutputPortName(NodePluginInstance<NodeT>* inst,
                                         std::size_t id) {
        try {
            auto md = inst->node->GetOutputPortMetadata();
            if (id < md.size()) return strdup(md[id].port_name.c_str());
        } catch (...) {}
        return "unknown";
    }

    static PortMetadataC* GetOutputPortMetadata(NodePluginInstance<NodeT>* inst, size_t* out_count) {
        return GetOutputPortMetadataImpl<NodeT>(inst, out_count);
    }

    static PortMetadataC* GetInputPortMetadata(NodePluginInstance<NodeT>* inst, size_t* out_count) { 
        return GetInputPortMetadataImpl<NodeT>(inst, out_count);
    }

    static void FreePortMetadata(PortMetadataC* metadata) {
        FreePortMetadataImpl(metadata);
    }

    // ========== Diagnostic & Configuration Methods ==========
    // CRITICAL: These methods MUST be customizable via Policy specialization.
    //
    // Design Pattern:
    // 1. PluginGlue receives void* from C interface (ABI boundary)
    // 2. PluginGlue casts to Instance* and calls Policy::Method(instance)
    // 3. Policy method has full C++ type information and can call dynamic_cast
    // 4. Specializations override these methods for custom node behavior
    //
    // Example specialization:
    //   template <>
    //   struct PluginPolicy<MyCustomNode> {
    //       static const char* GetDiagnosticsJSON(NodePluginInstance<MyCustomNode>* inst) {
    //           // Custom diagnostics for MyCustomNode
    //       }
    //   };
    //
    // These methods provide customization points for diagnostic and configuration behavior.
    // By default, they interact with node interfaces via dynamic_cast.
    // Specializations can override these methods to customize behavior.
    
    /// Get node configuration as JSON
    /// Calls node's IParameterized interface if available
    static const char* GetConfigurationJSON(NodePluginInstance<NodeT>* inst) {
        if (!inst || !inst->node) return nullptr;
        
        // Try to get parameters via IParameterized
        if (auto* params = dynamic_cast<graph::IParameterized*>(inst->node.get())) {
            static thread_local std::string result;
            try {
                auto params_view = params->GetParameters();
                result = params_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }
    
    /// Set node configuration from JSON
    /// Calls node's IConfigurable interface if available
    static bool SetConfigurationJSON(NodePluginInstance<NodeT>* inst, const char* json_str) {
        if (!inst || !inst->node || !json_str) return false;
        
        if (auto* configurable = dynamic_cast<graph::IConfigurable*>(inst->node.get())) {
            try {
                auto json_obj = nlohmann::json::parse(json_str);
                JsonView view(json_obj);
                configurable->Configure(view);
                return true;
            } catch (...) {
                return false;
            }
        }
        return false;
    }
    
    /// Check if node supports diagnostics
    /// Returns true if node implements IDiagnosable
    static bool SupportsDiagnostics(NodePluginInstance<NodeT>* inst) {
        if (!inst || !inst->node) return false;
        return dynamic_cast<graph::IDiagnosable*>(inst->node.get()) != nullptr;
    }
    
    /// Get diagnostics as JSON
    /// Calls node's IDiagnosable interface if available
    static const char* GetDiagnosticsJSON(NodePluginInstance<NodeT>* inst) {
        if (!inst || !inst->node) return nullptr;
        
        if (auto* diagnosable = dynamic_cast<graph::IDiagnosable*>(inst->node.get())) {
            static thread_local std::string result;
            try {
                auto diag_view = diagnosable->GetDiagnostics();
                result = diag_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }
    
    /// Check if node supports parameters
    /// Returns true if node implements IParameterized
    static bool SupportsParameters(NodePluginInstance<NodeT>* inst) {
        if (!inst || !inst->node) return false;
        return dynamic_cast<graph::IParameterized*>(inst->node.get()) != nullptr;
    }
    
    /// Get parameters as JSON
    /// Calls node's IParameterized interface if available
    static const char* GetParametersJSON(NodePluginInstance<NodeT>* inst) {
        if (!inst || !inst->node) return nullptr;
        
        if (auto* params = dynamic_cast<graph::IParameterized*>(inst->node.get())) {
            static thread_local std::string result;
            try {
                auto params_view = params->GetParameters();
                result = params_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }
    
    /// Set individual parameter
    /// Placeholder for future implementation
    /// IParameterized currently doesn't have SetParameter, but this provides
    /// a customization point for specializations
    static bool SetParameter(NodePluginInstance<NodeT>* inst, const char* param_name, const char* json_value) {
        if (!inst || !inst->node || !param_name || !json_value) return false;
        // Future implementation when IParameterized adds SetParameter method
        return false;
    }

    static void *GetAsDataInjectionNodeConfig(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* csv_node = dynamic_cast<graph::datasources::IDataInjectionSource*>(instance->node.get())) {
            return static_cast<void*>(csv_node);
        }

        return nullptr;
    }
    
    /// Get IConfigurable interface from node
    static void *GetAsIConfigurable(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* configurable = dynamic_cast<graph::IConfigurable*>(instance->node.get())) {
            return static_cast<void*>(configurable);
        }

        return nullptr;
    }
    
    /// Get IDiagnosable interface from node
    static void *GetAsIDiagnosable(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* diagnosable = dynamic_cast<graph::IDiagnosable*>(instance->node.get())) {
            return static_cast<void*>(diagnosable);
        }

        return nullptr;
    }
    
    /// Get IParameterized interface from node
    static void *GetAsIParameterized(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* parameterized = dynamic_cast<graph::IParameterized*>(instance->node.get())) {
            return static_cast<void*>(parameterized);
        }

        return nullptr;
    }
    
    /// Get IMetricsCallbackProvider interface from node
    static void *GetAsIMetricsCallbackProvider(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* metrics_provider = dynamic_cast<graph::IMetricsCallbackProvider*>(instance->node.get())) {
            return static_cast<void*>(metrics_provider);
        }

        return nullptr;
    }
    
    /// Get ICompletionCallback interface from node
    static void *GetAsICompletionCallback(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* completion_provider = dynamic_cast<graph::ICompletionCallback<graph::message::CompletionSignal>*>(instance->node.get())) {
            return static_cast<void*>(completion_provider);
        }

        return nullptr;
    }

    /// Get IGpuCapabilityBinding interface from node
    static void *GetAsIGpuCapabilityBinding(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;

        if (auto* gpu_binding = dynamic_cast<graph::IGpuCapabilityBinding*>(instance->node.get())) {
            return static_cast<void*>(gpu_binding);
        }

        return nullptr;
    }

};

// ============================================================================
// PluginGlue - C ABI interface adapter for plugins
// ============================================================================

/// @brief Adapts plugin node interface to C ABI for dynamic loading
/// @tparam NodeT The concrete node type
/// @tparam Policy Customization policy for the node type (defaults to PluginPolicy<NodeT>)};

// ============================================================================
// Helper Namespace - Metrics conversion utilities
// ============================================================================

namespace detail {
    /// Helper function for converting ThreadMetrics to ThreadMetricsC
    template <typename MetricsT>
    inline void ConvertThreadMetrics(const MetricsT& src, ThreadMetricsC& dst) {
        dst.total_iterations = src.total_iterations.load();
        dst.produce_calls = src.produce_calls.load();
        dst.consume_calls = src.consume_calls.load();
        dst.transfer_calls = src.transfer_calls.load();
        dst.total_produce_time_ns = src.total_produce_time_ns.load();
        dst.total_consume_time_ns = src.total_consume_time_ns.load();
        dst.total_transfer_time_ns = src.total_transfer_time_ns.load();
        dst.total_idle_time_ns = src.total_idle_time_ns.load();
        dst.thread_active = src.thread_active.load();
    }
}

// ============================================================================
// Template-based NodeFacade implementation helper
// ============================================================================

/// Provides implementations of metrics/state queries using SFINAE for optional methods
/// @tparam NodeT The concrete node type
template <typename NodeT>
class NodeFacadeImpl {
public:
    /// Get thread metrics if node provides GetThreadMetrics()
    static const ThreadMetricsC* GetThreadMetrics(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;
        
        // SFINAE: Check if NodeT has GetThreadMetrics() method
        if constexpr (requires(NodeT* n) { n->GetThreadMetrics(); }) {
            const auto& metrics = instance->node->GetThreadMetrics();
            static thread_local ThreadMetricsC metrics_c{};
            detail::ConvertThreadMetrics(metrics, metrics_c);
            return &metrics_c;
        }
        return nullptr;
    }
    
    /// Get thread utilization percentage (0-100)
    static double GetThreadUtilizationPercent(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return 0.0;
        
        if constexpr (requires(NodeT* n) { n->GetThreadMetrics(); }) {
            const auto& metrics = instance->node->GetThreadMetrics();
            // Calculate total elapsed time for utilization calculation
            // Use idle_time + busy_time as total
            uint64_t busy_ns = metrics.total_produce_time_ns.load(std::memory_order_relaxed)
                             + metrics.total_consume_time_ns.load(std::memory_order_relaxed)
                             + metrics.total_transfer_time_ns.load(std::memory_order_relaxed);
            uint64_t total_ns = busy_ns
                              + metrics.total_idle_time_ns.load(std::memory_order_relaxed)
                              + metrics.total_queue_wait_ns.load(std::memory_order_relaxed);
            return metrics.GetThreadUtilizationPercent(total_ns);
        }
        return 0.0;
    }
    
    /// Get input port queue metrics
    static bool GetInputQueueMetrics(NodeHandle handle, size_t port_index,
                                     PortQueueMetrics* out_metrics) {
        if (!out_metrics) return false;
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return false;
        
        if constexpr (requires(NodeT* n) { 
            n->GetInputPortCount(); 
            n->GetInputPortName(size_t{}); 
        }) {
            if (port_index >= instance->node->GetInputPortCount()) {
                return false;
            }
            strncpy(out_metrics->port_name,
                   instance->node->GetInputPortName(port_index),
                   sizeof(out_metrics->port_name) - 1);
            out_metrics->port_name[sizeof(out_metrics->port_name) - 1] = '\0';
            out_metrics->port_index = port_index;
            return true;
        }
        return false;
    }
    
    /// Get output port queue metrics
    static bool GetOutputQueueMetrics(NodeHandle handle, size_t port_index,
                                      PortQueueMetrics* out_metrics) {
        if (!out_metrics) return false;
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return false;
        
        if constexpr (requires(NodeT* n) { 
            n->GetOutputPortCount(); 
            n->GetOutputPortName(size_t{}); 
        }) {
            if (port_index >= instance->node->GetOutputPortCount()) {
                return false;
            }
            strncpy(out_metrics->port_name,
                   instance->node->GetOutputPortName(port_index),
                   sizeof(out_metrics->port_name) - 1);
            out_metrics->port_name[sizeof(out_metrics->port_name) - 1] = '\0';
            out_metrics->port_index = port_index;
            return true;
        }
        return false;
    }
    
    /// Get configuration as JSON (if node supports IConfigurable)
    static const char* GetConfigurationJSON(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;
        
        // Try to get parameters via IParameterized (which provides JSON)
        if (auto* params = dynamic_cast<graph::IParameterized*>(instance->node.get())) {
            static thread_local std::string result;
            try {
                auto params_view = params->GetParameters();
                result = params_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        
        return nullptr;
    }
    
    /// Set configuration from JSON (if node supports IConfigurable)
    static bool SetConfigurationJSON(NodeHandle handle, const char* json_str) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node || !json_str) return false;
        
        if (auto* configurable = dynamic_cast<graph::IConfigurable*>(instance->node.get())) {
            try {
                auto json_obj = nlohmann::json::parse(json_str);
                JsonView view(json_obj);
                configurable->Configure(view);
                return true;
            } catch (...) {
                return false;
            }
        }
        
        return false;
    }
    
    /// Check if node supports diagnostics (implements IDiagnosable)
    static bool SupportsDiagnostics(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return false;
        
        return dynamic_cast<graph::IDiagnosable*>(instance->node.get()) != nullptr;
    }
    
    /// Get diagnostics as JSON (if node supports IDiagnosable)
    static const char* GetDiagnosticsJSON(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;
        
        if (auto* diagnosable = dynamic_cast<graph::IDiagnosable*>(instance->node.get())) {
            static thread_local std::string result;
            try {
                auto diag_view = diagnosable->GetDiagnostics();
                result = diag_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        
        return nullptr;
    }
    
    /// Check if node supports parameters (implements IParameterized)
    static bool SupportsParameters(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return false;
        
        return dynamic_cast<graph::IParameterized*>(instance->node.get()) != nullptr;
    }
    
    /// Get parameters as JSON (if node supports IParameterized)
    static const char* GetParametersJSON(NodeHandle handle) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node) return nullptr;
        
        if (auto* params = dynamic_cast<graph::IParameterized*>(instance->node.get())) {
            static thread_local std::string result;
            try {
                auto params_view = params->GetParameters();
                result = params_view.Raw().dump(2);
                return result.c_str();
            } catch (...) {
                return nullptr;
            }
        }
        
        return nullptr;
    }
    
    /// Set individual parameter (if node supports IParameterized)
    static bool SetParameter(NodeHandle handle, const char* param_name, const char* json_value) {
        auto* instance = static_cast<NodePluginInstance<NodeT>*>(handle);
        if (!instance || !instance->node || !param_name || !json_value) return false;
        
        // IParameterized in the current implementation doesn't have SetParameter
        // This function is a placeholder for future use
        // For now, ConfigureJSON is the way to set parameters
        return false;
    }
};

// ============================================================================
// C ABI Glue (shared by all plugins)
// ============================================================================

template <typename NodeT, typename Policy = PluginPolicy<NodeT>>
struct PluginGlue {

    using Instance = NodePluginInstance<NodeT>;

    static int GetLifecycleState(void* h) {
        return static_cast<int>(static_cast<Instance*>(h)->node->GetLifecycleState());
    }

    static bool Init(void* h) {
        return static_cast<Instance*>(h)->node->Init();
    }

    static bool Start(void* h) {
        return static_cast<Instance*>(h)->node->Start();
    }

    static void Stop(void* h) {
        static_cast<Instance*>(h)->node->Stop();
    }

    static bool Join(void* h) {
        static_cast<Instance*>(h)->node->Join();
        return true;
    }

    static bool JoinWithTimeout(void* h, std::chrono::milliseconds timeout) {
        return static_cast<Instance*>(h)->node->JoinWithTimeout(timeout);
    }

    static void Execute(void* h) {
        Policy::Execute(static_cast<Instance*>(h));
    }

    static const char* GetName(void* h) {
        return static_cast<Instance*>(h)->name.c_str();
    }

    static void SetName(void* h, const char* name) {
        static_cast<Instance*>(h)->name = name ? name : "";
    }

    static const char* GetType(void* h) {
        return static_cast<Instance*>(h)->type.c_str();
    }

    static std::size_t GetInputPortCount(void* h) {
        return Policy::GetInputPortCount(static_cast<Instance*>(h));
    }

    static std::size_t GetOutputPortCount(void* h) {
        return Policy::GetOutputPortCount(static_cast<Instance*>(h));
    }

    static const char* GetInputPortName(void* h, std::size_t id) {
        return Policy::GetInputPortName(static_cast<Instance*>(h), id);
    }

    static const char* GetOutputPortName(void* h, std::size_t id) {
        return Policy::GetOutputPortName(static_cast<Instance*>(h), id);
    }

    static PortMetadataC* GetOutputPortMetadata(void *h, size_t* out_count) {
        return Policy::GetOutputPortMetadata(static_cast<Instance*>(h), out_count);
    }

    static PortMetadataC* GetInputPortMetadata(void *h, size_t* out_count) { 
        return Policy::GetInputPortMetadata(static_cast<Instance*>(h), out_count);
    /// @brief Construct a NodeFacade with all function pointers for this plugin type
    /// @return Fully initialized NodeFacade instance
    }

    static void FreePortMetadata(PortMetadataC* metadata) {
        return Policy::FreePortMetadata(metadata);
    }
    
    // ====== NEW: Configuration & Diagnostics Wrapper Functions ======
    
    /// Wrapper for GetConfigurationJSON to match function pointer signature
    static const char* GetConfigurationJSONImpl(void* h) {
        return Policy::GetConfigurationJSON(static_cast<Instance*>(h));
    }
    
    /// Wrapper for SetConfigurationJSON to match function pointer signature
    static bool SetConfigurationJSONImpl(void* h, const char* json_str) {
        return Policy::SetConfigurationJSON(static_cast<Instance*>(h), json_str);
    }
    
    /// Wrapper for SupportsDiagnostics to match function pointer signature
    static bool SupportsDiagnosticsImpl(void* h) {
        return Policy::SupportsDiagnostics(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetDiagnosticsJSON to match function pointer signature
    static const char* GetDiagnosticsJSONImpl(void* h) {
        return Policy::GetDiagnosticsJSON(static_cast<Instance*>(h));
    }
    
    /// Wrapper for SupportsParameters to match function pointer signature
    static bool SupportsParametersImpl(void* h) {
        return Policy::SupportsParameters(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetParametersJSON to match function pointer signature
    static const char* GetParametersJSONImpl(void* h) {
        return Policy::GetParametersJSON(static_cast<Instance*>(h));
    }
    
    /// Wrapper for SetParameter to match function pointer signature
    static bool SetParameterImpl(void* h, const char* param_name, const char* json_value) {
        return Policy::SetParameter(static_cast<Instance*>(h), param_name, json_value);
    }

    static void* GetAsDataInjectionNodeConfigImpl(void* h) {
        return Policy::GetAsDataInjectionNodeConfig(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetAsIConfigurable to match function pointer signature
    static void* GetAsIConfigurableImpl(void* h) {
        return Policy::GetAsIConfigurable(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetAsIDiagnosable to match function pointer signature
    static void* GetAsIDiagnosableImpl(void* h) {
        return Policy::GetAsIDiagnosable(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetAsIParameterized to match function pointer signature
    static void* GetAsIParameterizedImpl(void* h) {
        return Policy::GetAsIParameterized(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetAsIMetricsCallbackProvider to match function pointer signature
    static void* GetAsIMetricsCallbackProviderImpl(void* h) {
        return Policy::GetAsIMetricsCallbackProvider(static_cast<Instance*>(h));
    }
    
    /// Wrapper for GetAsICompletionCallback to match function pointer signature
    static void* GetAsICompletionCallbackImpl(void* h) {
        return Policy::GetAsICompletionCallback(static_cast<Instance*>(h));
    }

    /// Wrapper for GetAsIGpuCapabilityBinding to match function pointer signature
    static void* GetAsIGpuCapabilityBindingImpl(void* h) {
        return Policy::GetAsIGpuCapabilityBinding(static_cast<Instance*>(h));
    }
        
    static void Destroy(void* h) {
        delete static_cast<Instance*>(h);
    }

    static NodeFacade MakeFacade() {
        NodeFacade facade{};
        
        // ====== Lifecycle Management ======
        facade.GetLifecycleState = GetLifecycleState;
        facade.Init = Init;
        facade.Start = Start;
        facade.Stop = Stop;
        facade.Join = Join;
        facade.JoinWithTimeout = JoinWithTimeout;
        
        // ====== Execution ======
        facade.Execute = Execute;
        
        // ====== Metadata Queries ======
        facade.GetName = GetName;
        facade.SetName = SetName;
        facade.GetType = GetType;
        
        // ====== Configuration ======
        
        // ====== Port Information ======
        facade.GetInputPortCount =           GetInputPortCount;
        facade.GetOutputPortCount = GetOutputPortCount;
        facade.GetInputPortName = GetInputPortName;
        facade.GetOutputPortName = GetOutputPortName;
        
        // ====== Port Metadata Queries ======
        facade.GetInputPortMetadata = GetInputPortMetadata;
        facade.GetOutputPortMetadata = GetOutputPortMetadata;
        facade.FreePortMetadata = FreePortMetadata;
        
        // ====== Thread Metrics Queries ======
        // Note: These implementations use SFINAE (requires) in NodeFacadeImpl
        // They return nullptr or 0 if the node doesn't support the capability
        facade.GetThreadMetrics = GetThreadMetricsImpl;
        facade.GetThreadUtilizationPercent = GetThreadUtilizationPercentImpl;
        
        // ====== Port Queue Metrics Queries ======
        
        // ====== NEW: Configuration & Diagnostics ======
        
        // ====== NEW: Framework Interface Queries ======
        facade.GetAsDataInjectionNodeConfig = GetAsDataInjectionNodeConfigImpl;
        facade.GetAsIConfigurable = GetAsIConfigurableImpl;
        facade.GetAsIDiagnosable = GetAsIDiagnosableImpl;
        facade.GetAsIParameterized = GetAsIParameterizedImpl;
        facade.GetAsIMetricsCallbackProvider = GetAsIMetricsCallbackProviderImpl;
        facade.GetAsICompletionCallback = GetAsICompletionCallbackImpl;
        facade.GetAsIGpuCapabilityBinding = GetAsIGpuCapabilityBindingImpl;

        // ====== Cleanup ======
        facade.Destroy = Destroy;
        
        return facade;
    }

    static NodeFacade MakeFacade2() {
        NodeFacade facade{};
        
        // ====== Lifecycle Management ======
        // facade.GetLifecycleState = GetLifecycleState;
        // facade.Init = Init;
        // facade.Start = Start;
        // facade.Stop = Stop;
        // facade.Join = Join;
        // facade.JoinWithTimeout = JoinWithTimeout;
        
        // // ====== Execution ======
        // facade.Execute = Execute;
        
        // // ====== Metadata Queries ======
        // facade.GetName = GetName;
        // facade.GetType = GetType;
        
        // // ====== Configuration ======
        
        // // ====== Port Information ======
        // facade.GetInputPortCount = GetInputPortCount;
        // facade.GetOutputPortCount = GetOutputPortCount;
        // facade.GetInputPortName = GetInputPortName;
        // facade.GetOutputPortName = GetOutputPortName;
        
        // // ====== Port Metadata Queries ======
        // // facade.GetInputPortMetadata = GetInputPortMetadata;
        // // facade.GetOutputPortMetadata = GetOutputPortMetadata;
        // // facade.FreePortMetadata = FreePortMetadata;
        
        // // ====== Thread Metrics Queries ======
        // // Note: These implementations use SFINAE (requires) in NodeFacadeImpl
        // // They return nullptr or 0 if the node doesn't support the capability
        // facade.GetThreadMetrics = GetThreadMetricsImpl;
        // facade.GetThreadUtilizationPercent = GetThreadUtilizationPercentImpl;
        
        // // ====== Port Queue Metrics Queries ======
        
        // // ====== NEW: Configuration & Diagnostics ======
        
        // // ====== NEW: Framework Interface Queries ======
        // facade.GetAsDataInjectionNodeConfig = GetAsDataInjectionNodeConfigImpl;
        // facade.GetAsIMetricsCallbackProvider = GetAsIMetricsCallbackProviderImpl;
        // facade.GetAsICompletionCallback = GetAsICompletionCallbackImpl;

        // // ====== Cleanup ======
        // facade.Destroy = Destroy;
        
        return facade;
    }

private:
    // ====== Thread Metrics Implementation Helpers ======
    
    /// Wrapper for GetThreadMetrics to match function pointer signature
    static const ThreadMetricsC* GetThreadMetricsImpl(NodeHandle h) {
        return NodeFacadeImpl<NodeT>::GetThreadMetrics(h);
    }
    
    /// Wrapper for GetThreadUtilizationPercent to match function pointer signature
    static double GetThreadUtilizationPercentImpl(NodeHandle h) {
        return NodeFacadeImpl<NodeT>::GetThreadUtilizationPercent(h);
    }
    
    /// Wrapper for GetInputQueueMetrics to match function pointer signature
    static bool GetInputQueueMetricsImpl(NodeHandle h, size_t port_idx, 
                                       PortQueueMetrics* out_metrics) {
        return NodeFacadeImpl<NodeT>::GetInputQueueMetrics(h, port_idx, out_metrics);
    }
    
    /// Wrapper for GetOutputQueueMetrics to match function pointer signature
    static bool GetOutputQueueMetricsImpl(NodeHandle h, size_t port_idx, 
                                        PortQueueMetrics* out_metrics) {
        return NodeFacadeImpl<NodeT>::GetOutputQueueMetrics(h, port_idx, out_metrics);
    }
};


}  // namespace graph
