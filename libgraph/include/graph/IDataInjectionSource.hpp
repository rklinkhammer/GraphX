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

#include <string>
#include <vector>
#include <cstddef>
#include <memory>

#include "sensor/SensorClassificationType.hpp"
#include "sensor/SensorDataTypes.hpp"

// Forward declarations
namespace core {
template <typename T>
class ActiveQueue;
}

namespace graph::datasources {

/**
 * @brief Configuration metadata for a CSV data source node
 *
 * IDataInjectionSource serves dual purposes:
 * 1. Data struct holding column mappings and metadata
 * 2. Virtual base class for discovering Injection-capable nodes at runtime
 *
 *
 * Virtual methods provide type-erased access to the node's Data Injection queue.
 *
 */
class IDataInjectionSource {
public:
    /**
     * @brief Construct Injection data source configuration
     *
     * Configuration is just data - no queue reference needed.
     * Derived classes that inherit from this will manage the queue.
     */
    //IDataInjectionSource() = default;

    explicit IDataInjectionSource(
        core::ActiveQueue<sensors::SensorPayload>& queue)
        : queue_(queue) {
    }

    /**
     * @brief Virtual destructor - does not access the queue reference
     * 
     * CRITICAL: This destructor must NOT call Disable() or Clear() on queue_
     * because in derived classes like DataInjectionProducerWithNotification, the queue
     * is a member of the derived class that will be destroyed BEFORE this base
     * class destructor finishes (due to member destruction order).
     * 
     * The queue cleanup must happen in the derived class destructor, not here.
     */
    virtual ~IDataInjectionSource() = default;

    // ========== Configuration Data Members ==========

    /// Sensor classification type enum
    /// Possible values: ACCELEROMETER, GYROSCOPE, MAGNETOMETER, BAROMETRIC, GPS_POSITION, UNKNOWN
    /// Use SensorClassificationTypeToString() to convert to "accel", "gyro", etc.
    sensors::SensorClassificationType sensor_classification_type = sensors::SensorClassificationType::UNKNOWN;

    
    // ========== Virtual Methods for Node Discovery ==========

    /**
     * @brief Get reference to the Injection input queue
     *
     * Returns the queue passed in during construction.
     *
     * @return Reference to ActiveQueue<SensorPayload>
     */
    
    virtual core::ActiveQueue<sensors::SensorPayload>& GetInjectionQueue() {
        return queue_;
    }

    /**
     * @brief Get a const reference to this configuration
     *
     * Default implementation returns this.
     *
     * @return Const reference to IDataInjectionSource
     */
    virtual const IDataInjectionSource& GetInjectionNodeConfig() const {
        return *this;
    }

protected:
    /// Pointer to the Injection queue owned by DataInjectionProducerWithNotification
    core::ActiveQueue<sensors::SensorPayload>& queue_;
};

}  // namespace graph::datasources
