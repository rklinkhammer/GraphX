// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include "graph/NodeFacadeInterop.hpp"

namespace graph {

namespace {

template <typename CallbackT>
void* ExtractOptionalInterface(NodeHandle handle, CallbackT callback) noexcept {
    if (!callback) {
        return nullptr;
    }
    return callback(handle);
}

}  // namespace

ExtractedNodeInterfaces ExtractNodeInterfaces(
    NodeHandle handle,
    const NodeFacade* facade) noexcept {
    ExtractedNodeInterfaces interfaces{};
    if (!handle || !facade) {
        return interfaces;
    }

    interfaces.data_injection_node_config =
        ExtractOptionalInterface(handle, facade->GetAsDataInjectionNodeConfig);
    interfaces.configurable =
        ExtractOptionalInterface(handle, facade->GetAsIConfigurable);
    interfaces.diagnosable =
        ExtractOptionalInterface(handle, facade->GetAsIDiagnosable);
    interfaces.parameterized =
        ExtractOptionalInterface(handle, facade->GetAsIParameterized);
    interfaces.metrics_callback_provider =
        ExtractOptionalInterface(handle, facade->GetAsIMetricsCallbackProvider);
    interfaces.completion_callback_provider =
        ExtractOptionalInterface(handle, facade->GetAsICompletionCallback);
    interfaces.gpu_capability_binding =
        ExtractOptionalInterface(handle, facade->GetAsIGpuCapabilityBinding);

    return interfaces;
}

}  // namespace graph
