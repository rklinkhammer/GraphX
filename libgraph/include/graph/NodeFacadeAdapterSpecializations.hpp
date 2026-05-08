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

/**
 * @file NodeFacadeAdapterSpecializations.hpp
 * @brief Template specializations for NodeFacadeAdapter::TryGetInterface
 *
 * This header provides template specializations for TryGetInterface to work
 * with app-layer types like IDataInjectionSource. It must be included in files
 * that use TryGetInterface with these types.
 *
 * This separation is necessary because NodeFacade.hpp (framework layer) cannot
 * depend on app layer headers. The specializations are defined here where
 * app layer types are available.
 *
 * Include this header in any .cpp file that calls:
 *   adapter->TryGetInterface<graph::datasources::IDataInjectionSource>()
 *
 * @author GitHub Copilot
 * @date 2026-01-09
 */

#pragma once

#include "graph/Nodes.hpp"
#include "graph/NodeFacade.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IMetricsCallback.hpp"
#include "graph/IDataInjectionSource.hpp"

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

}  // namespace graph

