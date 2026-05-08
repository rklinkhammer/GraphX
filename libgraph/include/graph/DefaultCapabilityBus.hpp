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

#pragma once


#include <unordered_map>
#include <typeindex>
#include <memory>
#include "graph/CapabilityBus.hpp"

namespace graph {

/**
 * @class DefaultCapabilityBus
 * @brief Default implementation of the capability discovery and retrieval service
 *
 * DefaultCapabilityBus provides type-safe storage and retrieval of capabilities
 * (services) that graph components require. Each capability is registered by
 * its C++ type and can be retrieved using template methods.
 *
 * Capabilities provided:
 * - `MetricsCapability`: Metrics discovery and publishing
 * - `GraphCapability`: Graph state queries (optional, for future use)
 * - Additional domain-specific capabilities as needed
 *
 * Design Pattern:
 * - Type-indexed storage using std::type_index
 * - Type-safe Get/Register methods via C++ templates
 * - Dynamic casting handled internally with error checking
 * - Nullptr returned if capability not found (safe failure)
 *
 * Thread Safety:
 * - Not inherently thread-safe (called during initialization, before execution)
 * - Registrations happen during GraphExecutor::Init()
 * - Queries happen during Dashboard::Initialize() and execution phases
 * - No concurrent modifications expected
 *
 * Example usage:
 * ```cpp
 * // Create and configure the bus
 * auto bus = std::make_shared<DefaultCapabilityBus>();
 * auto metrics_cap = std::make_shared<DefaultMetricsCapability>();
 * bus->Register(metrics_cap);
 *
 * // Later, retrieve the capability
 * auto retrieved = bus->Get<MetricsCapability>();
 * if (retrieved) {
 *     retrieved->RegisterMetricsCallback(dashboard);
 * }
 *
 * // Check if capability exists
 * if (bus->Has<MetricsCapability>()) {
 *     // Capability is registered
 * }
 * ```
 *
 * @see CapabilityBus, MetricsCapability, GraphExecutor
 */
class DefaultCapabilityBus : public CapabilityBus {
public:
    /**
     * @brief Register a capability implementation
     *
     * Stores the capability by its type for later retrieval.
     * Replaces any existing capability of the same type.
     *
     * @tparam CapabilityT The capability interface type (e.g., MetricsCapability)
     * @param capability Shared pointer to the capability implementation
     *
     * @see Get(), Has()
     */
    template<typename CapabilityT>
    void Register(std::shared_ptr<CapabilityT> capability) {
        static_assert(std::is_class_v<CapabilityT>);
        capabilities_[typeid(CapabilityT)] = std::move(capability);
    }

    /**
     * @brief Retrieve a registered capability by type
     *
     * Returns a shared pointer to the capability implementation,
     * or nullptr if the capability is not registered.
     *
     * @tparam CapabilityT The capability interface type (e.g., MetricsCapability)
     * @return Shared pointer to the capability, or nullptr if not found
     *
     * @see Register(), Has()
     */
    template<typename CapabilityT>
    std::shared_ptr<CapabilityT> Get() const {
        auto it = capabilities_.find(typeid(CapabilityT));
        if (it == capabilities_.end()) {
            return nullptr;
        }
        return std::static_pointer_cast<CapabilityT>(it->second);
    }

    /**
     * @brief Check if a capability is registered
     *
     * Performs a quick lookup without creating a shared pointer.
     * Useful for conditional capability usage.
     *
     * @tparam CapabilityT The capability interface type
     * @return True if the capability is registered, false otherwise
     *
     * @see Get(), Register()
     */
    template<typename CapabilityT>
    bool Has() const {
        return capabilities_.count(typeid(CapabilityT)) != 0;
    }

    /**
     * @brief Clear all registered capabilities
     *
     * Removes all capabilities from the bus. Called during shutdown
     * or when resetting the graph execution environment.
     *
     * @see Register(), Get()
     */
    void Clear() {
        capabilities_.clear();
    }

private:
    /// @brief Type-indexed map of capabilities
    std::unordered_map<std::type_index, std::shared_ptr<void>> capabilities_;
};

}