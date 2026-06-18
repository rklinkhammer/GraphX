/**
 * @file NodeFacadeAdapterSpecializations.hpp
 * @brief Node Facade Adapter Specializations Graph runtime support.
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


#pragma once

#include "graph/NodeFacade.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "metrics/IMetricsCallback.hpp"
#include "graph/IDataInjectionSource.hpp"
#include "graph/IGpuCapabilityBinding.hpp"

namespace graph {

/**
 * Template specialization: TryGetInterface for graph::datasources::IDataInjectionSource
 *
 * Allows plugin-loaded CSV nodes to expose their IDataInjectionSource interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::datasources::IDataInjectionSource> NodeFacadeAdapter::TryGetInterface<graph::datasources::IDataInjectionSource>() const {
    if (data_injection_node_config_ptr_) {
        // The void pointer points to a IDataInjectionSource instance provided by the plugin's callback
        // It's safe to cast because the plugin's GetAsDataInjectionNodeConfig callback performed the cast
        return std::static_pointer_cast<graph::datasources::IDataInjectionSource>(data_injection_node_config_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::IConfigurable
 *
 * Allows plugin-loaded nodes to expose their IConfigurable interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::IConfigurable> NodeFacadeAdapter::TryGetInterface<graph::IConfigurable>() const {
    if (configurable_ptr_) {
        return std::static_pointer_cast<graph::IConfigurable>(configurable_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::IDiagnosable
 *
 * Allows plugin-loaded nodes to expose their IDiagnosable interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::IDiagnosable> NodeFacadeAdapter::TryGetInterface<graph::IDiagnosable>() const {
    if (diagnosable_ptr_) {
        return std::static_pointer_cast<graph::IDiagnosable>(diagnosable_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::IParameterized
 *
 * Allows plugin-loaded nodes to expose their IParameterized interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::IParameterized> NodeFacadeAdapter::TryGetInterface<graph::IParameterized>() const {
    if (parameterized_ptr_) {
        return std::static_pointer_cast<graph::IParameterized>(parameterized_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::IMetricsCallbackProvider
 *
 * Allows plugin-loaded nodes to expose their IMetricsCallbackProvider interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::IMetricsCallbackProvider> NodeFacadeAdapter::TryGetInterface<graph::IMetricsCallbackProvider>() const {
    if (metrics_callback_provider_ptr_) {
        return std::static_pointer_cast<graph::IMetricsCallbackProvider>(metrics_callback_provider_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::CompletionCallbackProvider
 *
 * Allows plugin-loaded nodes to expose their CompletionCallbackProvider interface
 * (ICompletionCallback<CompletionSignal>) through the NodeFacadeAdapter
 * without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::CompletionCallbackProvider> NodeFacadeAdapter::TryGetInterface<graph::CompletionCallbackProvider>() const {
    if (completion_callback_provider_ptr_) {
        return std::static_pointer_cast<graph::CompletionCallbackProvider>(completion_callback_provider_ptr_);
    }
    return nullptr;
}

/**
 * Template specialization: TryGetInterface for graph::IGpuCapabilityBinding
 *
 * Allows plugin-loaded nodes to expose their IGpuCapabilityBinding interface
 * through the NodeFacadeAdapter without requiring RTTI through wrapper layers.
 */
template <>
inline std::shared_ptr<graph::IGpuCapabilityBinding> NodeFacadeAdapter::TryGetInterface<graph::IGpuCapabilityBinding>() const {
    if (gpu_capability_binding_ptr_) {
        return std::static_pointer_cast<graph::IGpuCapabilityBinding>(gpu_capability_binding_ptr_);
    }
    return nullptr;
}

}  // namespace graph
