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
 * @file SensorDataRouter.hpp
 * @brief Type-based routing for polymorphic sensor data (Phase 4 Generalization)
 *
 * Specializes the generic graph::VariantRouter<Variant> template for sensor data,
 * providing backward-compatible access to the generalized routing infrastructure.
 *
 * **Phase 4 Generalization**: Refactored to use graph::VariantRouter<SensorPayload>,
 * enabling the same routing logic for arbitrary variant types while maintaining
 * backward compatibility with existing sensor-specific code.
 *
 * The Type Router pattern enables:
 * - Type-safe handler registration for each sensor type
 * - Polymorphic dispatch of SensorPayload variants
 * - Hardware device independence (one router per port)
 * - Clear separation of routing logic from FSM
 * - Zero overhead: generic dispatcher expanded at compile-time
 *
 * @see core/VariantRouter.hpp for generic implementation
 * @see config/DataTypes.hpp for SensorPayload variant definition
 */

#pragma once

#include "sensor/SensorClassificationType.hpp"
#include "config/DataTypes.hpp"
#include "core/VariantRouter.hpp"
#include <functional>
#include <memory>
#include <string>

namespace sensors {

    // ===================================================================================
    // SensorDataRouter - Specialization of Generic VariantRouter for SensorPayload
    // -----------------------------------------------------------------------------------
    // Provides backward-compatible routing for sensor data variants.
    // Underlying implementation uses graph::VariantRouter<SensorPayload> for zero-overhead
    // polymorphic dispatch via C++17 fold expressions.
    // ===================================================================================

    /**
     * @brief Type-based dispatcher for SensorPayload variants (Backward Compatible)
     *
     * Specializes graph::VariantRouter<SensorPayload> to provide familiar API
     * for existing sensor-based code while leveraging generic infrastructure.
     *
     * **Type-Safe Handler Registration**:
     * @code
     *   router.RegisterHandler<AccelerometerData>([this](const auto& data) {
     *       fsm_.OnAcceleration(data.acceleration, data.timestamp);
     *   });
     * @endcode
     *
     * **Polymorphic Dispatch**:
     * @code
     *   sensors::SensorPayload payload(...);
     *   router.Dispatch(payload);  // Routes to appropriate type handler
     * @endcode
     *
     * **Phase 4 Implementation**:
     * Inherits from graph::VariantRouter<SensorPayload> to provide all
     * generic routing functionality. All handler registration and dispatch
     * is delegated to the templated base class.
     *
     * **Benefits of Generalization**:
     * - Decoupled from sensor types: Same router works with other variant types
     * - Extensible without modification: No hardcoded type checks
     * - Zero overhead: Generic dispatcher expands at compile-time
     * - Header-only: Easy integration across projects
     */
    class SensorDataRouter : public graph::VariantRouter<SensorPayload> {
    public:
        // ====================================================================
        // Type Definitions
        // ====================================================================

        /**
         * @brief Handler function type for sensor data
         * @tparam T The sensor data type (e.g., AccelerometerData)
         *
         * Handler takes a const reference to typed sensor data.
         * Handlers are called when data of matching type is dispatched.
         */
        template<typename T>
        using Handler = std::function<void(const T&)>;

        // ====================================================================
        // Construction / Destruction / Lifecycle
        // ====================================================================

        /// Default constructor
        SensorDataRouter() = default;

        /// Destructor
        ~SensorDataRouter() = default;

        /// Delete copy to avoid handler registration duplication
        SensorDataRouter(const SensorDataRouter&) = delete;
        SensorDataRouter& operator=(const SensorDataRouter&) = delete;

        /// Allow move semantics for flexible container usage
        SensorDataRouter(SensorDataRouter&&) = default;
        SensorDataRouter& operator=(SensorDataRouter&&) = default;

        // ====================================================================
        // Public API (Inherited from graph::VariantRouter<SensorPayload>)
        // ====================================================================

        /**
         * @brief Register a handler for a specific sensor type
         * @tparam T The sensor data type (e.g., AccelerometerData)
         * @param handler Function to call when data of type T is dispatched
         *
         * Example:
         * @code
         *   router.RegisterHandler<AccelerometerData>([this](const auto& data) {
         *       fusion_.UpdateAccelerometer(data.acceleration, data.timestamp);
         *   });
         * @endcode
         *
         * This method is inherited from graph::VariantRouter<SensorPayload>.
         * Refer to parent class documentation for details.
         *
         * @see graph::VariantRouter<SensorPayload>::RegisterHandler
         */
        using graph::VariantRouter<SensorPayload>::RegisterHandler;

        /**
         * @brief Dispatch a sensor payload to registered handler
         * @param payload The sensor data variant to dispatch
         *
         * Looks up handler for the payload's actual type and calls it.
         * Silently ignores payloads with no registered handler (OK if intentional).
         *
         * Example:
         * @code
         *   sensors::SensorPayload payload(std::in_place_type<AccelerometerData>, ...);
         *   router.Dispatch(payload);  // Routes to AccelerometerData handler
         * @endcode
         *
         * This method is inherited from graph::VariantRouter<SensorPayload>.
         * Uses C++17 fold expressions for zero-overhead dispatch.
         *
         * @see graph::VariantRouter<SensorPayload>::Dispatch
         */
        using graph::VariantRouter<SensorPayload>::Dispatch;

        /**
         * @brief Clear all registered handlers
         *
         * Useful for resetting router state or cleaning up before destruction.
         *
         * @see graph::VariantRouter<SensorPayload>::ClearHandlers
         */
        using graph::VariantRouter<SensorPayload>::ClearHandlers;

        /**
         * @brief Get count of registered handlers
         * @return Number of distinct sensor type handlers registered
         *
         * Useful for debugging and validation of router configuration.
         *
         * @see graph::VariantRouter<SensorPayload>::GetHandlerCount
         */
        using graph::VariantRouter<SensorPayload>::GetHandlerCount;

        /**
         * @brief Check if handler is registered for a specific type
         * @tparam T The sensor data type to query
         * @return true if handler registered for type T, false otherwise
         *
         * @see graph::VariantRouter<SensorPayload>::HasHandler
         */
        using graph::VariantRouter<SensorPayload>::HasHandler;
    };

}  // namespace sensors

