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
 * @file DataInjectionProducerWithNotification.hpp
 * @brief Example CSV data producer node with notifications
 *
 * Provides a reusable SensorNode class that demonstrates:
 * - Data generator implementation
 * - Notification signal creation
 * - Named node pattern
 * - Metrics collection and reporting
 *
 * @author Robert Klinkhammer
 * @date 2025-12-31
 */

#pragma once


#include <chrono>
#include <memory>
#include <optional>
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"
#include "graph/CompletionSignal.hpp"
#include "sensor/SensorBasicTypes.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "graph/DataInjectionGeneratorBase.hpp"
#include "core/ActiveQueue.hpp"
#include "graph/DataProducerWithNotification.hpp"
#include "graph/IDataInjectionSource.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include <nlohmann/json.hpp>
#include <log4cxx/logger.h>

// Forward declarations handled by includes above


namespace graph {
    /**
     * @brief Example sensor node with metrics and notifications
     * 
     * Demonstrates a complete data source node that:
     * - Produces integer sensor samples at regular intervals
     * - Sends completion signals when exhausted
     * - Collects performance metrics (Phases 5-6)
     * - Uses the NamedType mixin for identification
     * 
     * Usage:
     * @code
     *   SensorNode sensor(std::chrono::microseconds(10000), 2, 100);
     *   sensor.EnableMetrics(true);
     *   sensor.Init();
     *   sensor.Start();
     *   // ... process data ...
     *   sensor.Stop();
     *   sensor.Join();
     *   
     *   // Query metrics
     *   auto* metrics = sensor.GetOutputPortThreadMetrics(0);
     *   if (metrics) {
     *       std::cout << MetricsReport::FormatThreadMetrics(0, *metrics);
     *   }
     * @endcode
     */
    
    template<typename SensorNodeType, typename DataType, typename PayloadType, 
        sensors::SensorClassificationType Classification>
    class DataInjectionProducerWithNotification : public DataProducerWithNotification<
        SensorNodeType, DataInjectionGeneratorBase<DataType, PayloadType>, 
        DataType, PayloadType, graph::message::CompletionSignal, Classification>,
        public graph::datasources::IDataInjectionSource,
        public graph::IConfigurable {
    public:
        using BaseType = DataProducerWithNotification<
            SensorNodeType, DataInjectionGeneratorBase<DataType, PayloadType>, 
            DataType, PayloadType, graph::message::CompletionSignal, Classification>;

        /**
         * @brief Construct a sensor node with default configuration
         * 
         * Default values:
         * - sample_interval: 10ms (100 Hz sampling)
         * - sample_ignore: 0 (process all samples)
         * 
         * These can be changed via Configure() method after construction.
         * All nodes must use default constructors (zero parameters).
         */
        DataInjectionProducerWithNotification()
            : DataProducerWithNotification<SensorNodeType, DataInjectionGeneratorBase<DataType, PayloadType>, 
                    DataType, PayloadType, graph::message::CompletionSignal, Classification>(
                std::make_unique<DataInjectionGeneratorBase<DataType, PayloadType>>(injection_queue_),
                std::chrono::milliseconds(10),  // Default: 10ms interval (100 Hz)
                0),                             // Default: process all samples
              graph::datasources::IDataInjectionSource(injection_queue_) {
            SetName("SensorNode");
        }
        
        // Delete parameterized constructor to enforce zero-parameter requirement
        DataInjectionProducerWithNotification(std::chrono::microseconds, std::size_t) = delete;
        
        /**
         * @brief Explicit virtual destructor for safe polymorphic destruction
         * 
         * Ensures proper cleanup order when destroyed polymorphically through
         * plugin handles. Critical for multiple inheritance safety.
         */
        virtual ~DataInjectionProducerWithNotification() {
            auto logger = log4cxx::Logger::getLogger("DataInjectionProducerWithNotification");
            LOG4CXX_TRACE(logger, "Destroying DataInjectionProducerWithNotification");
        }
        
        /**
         * @brief Create a completion notification signal
         * @return CompletionSignal with metadata about the generation process
         * 
         * Called when the data generator is exhausted to signal
         * downstream nodes that sampling has completed.
         */
        graph::message::CompletionSignal CreateNotification() const override {
            return graph::message::CompletionSignal(
                graph::message::CompletionSignal::Reason::CSV_DATA_EXHAUSTED,
                BaseType::GetName(),
                BaseType::GetLastGeneratorTimestamp(),
                BaseType::GetTotalSamplesGenerated(),
                "All samples produced"
            );
        }

        void SetName(const std::string& name) {
            BaseType::SetName(name);
        }

        std::string GetName() const {
            return BaseType::GetName();
        }

        /**
         * @brief Override virtual method for queue access via IDataInjectionSource
         * 
         * Returns a reference to the Injection queue owned by this node.
         * 
         * @return Reference to the Injection input queue
         */
        core::ActiveQueue<PayloadType>& GetInjectionQueue() override {
            return injection_queue_;
        }

        /**
         * @brief Get Injection node configuration
         * 
         * Returns the configuration that describes how Injection columns map to
         * sensor data fields for this node. Subclasses should override to
         * provide their specific column mappings.
         * 
         * @return IDataInjectionSource with column metadata
         */
        const graph::datasources::IDataInjectionSource& GetInjectionNodeConfig() const override {
            // Return *this since this class now inherits from IDataInjectionSource
            return *this;
        }

        /**
         * @brief Set the sample interval for data production
         * 
         * @param interval Time between samples (microseconds)
         */
        void SetSampleInterval(std::chrono::microseconds interval) {
            BaseType::sample_interval = interval;
        }

        /**
         * @brief Get the current sample interval
         * 
         * @return Time between samples (microseconds)
         */
        std::chrono::microseconds GetSampleInterval() const {
            return BaseType::sample_interval;
        }

        /**
         * @brief Set the number of initial samples to skip
         * 
         * @param count Number of samples to ignore before yielding data
         */
        void SetSampleIgnore(std::size_t count) {
            BaseType::sample_ignore_ = count;
        }

        /**
         * @brief Get the current sample ignore count
         * 
         * @return Number of initial samples being skipped
         */
        std::size_t GetSampleIgnore() const {
            return BaseType::sample_ignore_;
        }

        /**
         * @brief Configure this Injection node from JSON
         * 
         * Supported configuration keys:
         * - sample_interval_ms: Sample interval in milliseconds (integer, >= 1)
         * - sample_ignore: Number of initial samples to skip (integer, >= 0)
         * 
         * Example JSON:
         * @code
         * {
         *     "sample_interval_ms": 10,
         *     "sample_ignore": 5
         * }
         * @endcode
         * 
         * @param cfg JSON configuration view
         */
        void Configure(const graph::JsonView& cfg) override {
            try {
                // Parse sample_interval_ms if provided
                if (cfg.Contains("sample_interval_ms")) {
                    auto ms_value = cfg.GetInt("sample_interval_ms");
                    if (ms_value < 1) {
                        throw std::invalid_argument("sample_interval_ms must be >= 1");
                    }
                    SetSampleInterval(std::chrono::milliseconds(ms_value));
                }
                
                // Parse sample_ignore if provided
                if (cfg.Contains("sample_ignore")) {
                    SetSampleIgnore(cfg.GetInt("sample_ignore"));
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(BaseType::logger_, 
                    "Failed to configure Injection node: " << e.what());
                throw;
            }
        }

    private:
        core::ActiveQueue<PayloadType> injection_queue_;

    };

} // namespace graph

