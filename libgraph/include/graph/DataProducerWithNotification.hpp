// MIT License
//
// Copyright (c) 2025 GraphX Contributors
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

/*!
 * @file DataGeneratorBase.hpp
 * @brief Base interface for data generators (producers of templated data types)
 * @author GraphX Contributors
 * @date 2025
 * @license MIT
 */

#pragma once

#include <optional>
#include <chrono>
#include <memory>
#include <atomic>
#include "graph/NamedNodes.hpp"
#include "graph/DataGeneratorBase.hpp"
#include "graph/Message.hpp"
#include "graph/CompletionSignal.hpp"
#include "sensor/SensorClassificationType.hpp"
#include "core/ActiveQueue.hpp"
#include "graph/NodeCallback.hpp"
#include "graph/ICallbackProvider.hpp"
#include <log4cxx/logger.h>

namespace graph {

/**
 * @brief Data producer node with typed notifications and completion signals
 *
 * A complete, production-ready data source node that:
 * - Generates typed samples at configurable intervals
 * - Skips an initial number of samples (ignore count)
 * - Emits completion signals when the generator is exhausted
 * - Integrates with GraphX metrics (Phase 5-6) for performance monitoring
 * - Uses two-port architecture: Port 0 for data, Port 1 for completion
 *
 * ## Architecture
 *
 * ```
 * DataProducerWithNotification
 *   ├─ NamedSourceNode (with 2 output ports)
 *   │  ├─ SourceNode (lifecycle + port management)
 *   │  └─ NamedType (runtime identification)
 *   └─ Metrics (Phase 5-6)
 *
 * Port 0: Message(DataType) - sampled data
 * Port 1: Message(NotificationType) - completion signal
 * ```
 *
 * ## Performance Characteristics
 *
 * - **Timing Precision**: Uses std::chrono::steady_clock with sleep_until()
 *   for microsecond-precision sampling intervals
 * - **Metrics Overhead**: Negligible with memory_order_relaxed atomics
 * - **Memory**: Small fixed overhead (one generator, one notification queue)
 * - **Queue Strategy**: Port 0 uses FastQueue (no notification tracking),
 *   Port 1 uses ActiveQueue (exactly-once delivery)
 *
 * ## Phase 5-6 Metrics Integration
 *
 * Automatically collects:
 * - Thread metrics: iteration counts, operation timings, thread state
 * - Queue metrics: enqueue/dequeue rates, peak sizes, timing averages
 *
 * Access via:
 * @code
 *   EnableMetrics(true);  // After Init()
 *   auto* qm = GetOutputQueueMetrics(0);
 *   auto* tm = GetOutputPortThreadMetrics(0);
 * @endcode
 *
 * Export via MetricsFormatter:
 * @code
 *   std::string text = MetricsReport::FormatQueueMetrics(0, *qm);
 *   std::string json = MetricsReport::FormatQueueMetricsJSON(0, *qm);
 *   std::string csv = MetricsReport::FormatQueueMetricsCSV(0, *qm);
 * @endcode
 *
 * @tparam NodeType The derived class (for NamedType CRTP and identification)
 * @tparam DataGenerator The data source type (must extend DataGeneratorBase<DataType>)
 * @tparam DataType The type of data produced (int, double, custom struct, etc.)
 * @tparam NotificationType The completion signal type (CompletionSignal, etc.)
 *
 * @note The constructor calls std::move() on the generator, taking ownership
 * @note DataProducerWithNotification is NOT copy-constructible (generator is unique_ptr)
 * @note All generator calls happen on the producer thread (thread-safe by design)
 * @note Call Init() before Start() to set up listener ports
 * @note Call Stop() then Join() to cleanly shutdown and wait for threads
 *
 * @see DataGeneratorBase for implementing custom data sources
 * @see MetricsFormatter for exporting metrics
 * @see CompletionSignal for standard notification type
 * @see NamedType for GetName(), SetName(), GetNodeTypeName()
 *
 * Thread Safety Model:
 * - generator_ and notification_queue_ accessed only from producer thread
 * - total_samples_generated_ is read-only from other threads (atomic safety)
 * - Metrics are collected via atomics with memory_order_relaxed
 * - Port operations are thread-safe (managed by SourceNode base class)
 */
template <typename NodeType, typename DataGenerator, typename DataType, typename PayloadType, typename NotificationType, 
    sensors::SensorClassificationType Classification = sensors::SensorClassificationType::UNKNOWN>
class DataProducerWithNotification : 
    public NamedSourceNode<NodeType, graph::message::Message, NotificationType>,
    public graph::ISourceCallbackProvider<DataType> {
public:
    /**
     * @brief Constructor for typed data producer with notifications
     *
     * Initializes the producer with timing and sampling parameters. The generator
     * is moved into ownership. Call Init() to set up port listeners, then Start()
     * to begin sampling.
     *
     * @param generator Unique pointer to the data source (taken by move semantics)
     * @param sample_interval Time between sample productions (microseconds)
     * @param sample_ignore Number of initial samples to skip before yielding data
     *
     * @note Constructor is NOT thread-safe; call from single thread
     * @note Does NOT start worker threads (call Start() after Init())
     *
     * Example:
     * @code
     *   auto producer = std::make_unique<SensorNode>(
     *       std::make_unique<SensorDataGenerator>(1000),  // max 1000 samples
     *       std::chrono::microseconds(10000),             // 10ms interval
     *       10);                                          // skip first 10 samples
     *   producer->Init();
     *   producer->Start();
     * @endcode
     */
    DataProducerWithNotification(std::unique_ptr<DataGenerator> generator, 
        std::chrono::microseconds sample_interval, std::size_t sample_ignore) 
        : sample_interval(sample_interval),
          sample_ignore_(sample_ignore),
          generator_(std::move(generator)),
          start_time_(std::chrono::steady_clock::now()),
          next_time_(start_time_ + sample_interval),
          total_samples_generated_(0) {

    }
    
    /**
     * @brief Virtual destructor for safe polymorphism
     * Cleans up generator and notification queue
     */
    virtual ~DataProducerWithNotification() {
        auto logger = log4cxx::Logger::getLogger("DataProducerWithNotification");
        LOG4CXX_TRACE(logger, "Destroying DataProducerWithNotification");
    }
   
    /**
     * @brief Produce data samples on Port 0 (interval-based timed generation)
     *
     * Core data generation loop. Called continuously by the GraphX framework
     * to fetch samples at the configured sample_interval. Implements precise
     * microsecond timing using std::this_thread::sleep_until().
     *
     * ## Execution Flow
     *
     * 1. **Sleep until next interval**: Maintains steady timing
     * 2. **Call generator->Produce()**: Fetch next data item
     * 3. **Handle success**: Increment counters, return Message if not ignored
     * 4. **Handle exhaustion**: Queue completion signal, return nullopt
     *
     * ## Timing Semantics
     *
     * The sampling happens at precise intervals:
     * - start_time = time of first Produce() call
     * - next_time starts at start_time + sample_interval
     * - Each call advances next_time by sample_interval
     * - Sleep blocks until next_time is reached
     *
     * This ensures samples are produced at consistent T, 2T, 3T, ... timing.
     *
     * ## Sample Ignore Semantics
     *
     * The first sample_ignore samples are produced and counted, but NOT
     * returned (returns nullopt). This allows the generator to "warm up"
     * its internal state before counting begins.
     *
     * Example with sample_ignore=2:
     * ```
     * Call 1: returns nullopt (sample #1 ignored)
     * Call 2: returns nullopt (sample #2 ignored)
     * Call 3: returns Message(sample) (sample #3 is data)
     * ...
     * ```
     *
     * ## Metrics Collection (Phase 5)
     *
     * When metrics are enabled, this method contributes to:
     * - Iteration count (incremented per call)
     * - Produce timing (time to call generator->Produce())
     * - Thread state tracking (active/blocked)
     *
     * @return Message wrapping the data sample, or nullopt if:
     *   - Sample is within ignore count (early returns)
     *   - Generator exhausted (completion signal queued on Port 1)
     *
     * @note **Thread Model**: Called exclusively from the producer thread
     * @note **Real-time**: Uses steady_clock for precision timing (not affected by system clock adjustments)
     * @note **Blocking**: Sleeps until next interval (blocks producer thread)
     *
     * @see GetNextSampleInterval() for customizing intervals
     * @see CreateNotification() for customizing completion signal
     * @see sample_ignore_ member for configured ignore count
     * @see generator_ member for the data source
     *
     * Internal State Modified:
     * - next_time_: Advanced by sample_interval (or custom interval)
     * - total_samples_generated_: Incremented on each generator->Produce() success
     * - notification_queue_: Single message enqueued on exhaustion
     */
    std::optional<::graph::message::Message> Produce(std::integral_constant<std::size_t, 0>) override {
        next_time_ += GetNextSampleInterval();
        std::this_thread::sleep_until(next_time_);
        std::optional<DataType> sample = generator_->Produce(0);
        if (sample.has_value()) {
            this->OnDataProduced(sample.value());
            total_samples_generated_++;
            if (total_samples_generated_ > sample_ignore_) {
                PayloadType payload(sample.value());
                return ::graph::message::Message(payload);
            }
        } else {
            // Generator exhausted - don't emit data, completion signal will be on port 1
            this->OnDataExhausted();
            NotificationType notification = CreateNotification();
            // C++26: [[nodiscard]] return value intentionally unused (signal will be consumed by port 1)
            static_cast<void>(notification_queue_.Enqueue(notification));
            LOG4CXX_TRACE(logger_, "Data exhausted in DataProducerWithNotification: " << producer_name_
                                     << " (samples=" << total_samples_generated_ << ")");
        }
        return std::nullopt;
    }

    /**
     * @brief Produce completion signal on Port 1 (exactly-once delivery)
     *
     * Called repeatedly by the GraphX framework to fetch the completion signal.
     * Returns the signal exactly once when the generator is exhausted, then
     * returns nullopt forever after. This enables downstream nodes to recognize
     * end-of-stream and coordinate graph-wide shutdown.
     *
     * ## Two-Phase Notification Pattern
     *
     * **Phase 1** (Port 0 Produce() exhausted): Completion signal enqueued
     * - Port 0 call: generator->Produce() returns nullopt (exhausted)
     * - Port 0 response: Queues signal in notification_queue_, returns nullopt
     * - Logging: "Data exhausted in DataProducerWithNotification"
     *
     * **Phase 2** (Port 1 dequeues signal): Completion signal emitted
     * - Port 1 call: Dequeue from notification_queue_ and return Message
     * - Returns: Message(CompletionSignal) with exhaustion metadata
     * - Repeat: Returns nullopt (queue empty)
     *
     * ## Semantics
     *
     * The notification queue is a simple FIFO with exactly-once delivery:
     * ```
     * First dequeue call:  -> Message(notification) [contains metadata]
     * Subsequent calls:    -> nullopt               [queue stays empty]
     * ```
     *
     * This ensures:
     * - Downstream nodes see completion signal exactly once
     * - No loss of notification (enqueued then dequeued)
     * - Clear end-of-stream semantics for graph coordination
     *
     * ## Notification Content
     *
     * The NotificationType (typically CompletionSignal) contains:
     * - Reason enum (CSV_DATA_EXHAUSTED, PROCESSING_ERROR, etc.)
     * - Node name and timestamp
     * - Total sample count and custom message
     * - Used by listeners to decide shutdown or restart logic
     *
     * See CompletionSignal for standard notification structure.
     *
     * ## Metrics Integration (Phase 5)
     *
     * This method contributes to:
     * - Port 1 queue metrics (dequeue count = 0 or 1)
     * - Thread metrics (iteration count)
     * - Port 1 thread metrics (called during shutdown sequence)
     *
     * @return Message wrapping the CompletionSignal on first call after exhaustion,
     *         nullopt on all subsequent calls or if generator not yet exhausted
     *
     * @note **Called only after exhaustion**: Returns nullopt until Port 0 is exhausted
     * @note **Single emission**: Dequeues exactly once, then queue stays empty
     * @note **No sleep**: Polling method, no blocking (unlike Port 0)
     * @note **Thread model**: Called from producer thread (same as Port 0)
     *
     * @see CreateNotification() for customizing the notification message
     * @see notification_queue_ for the underlying queue implementation
     * @see CompletionSignal for notification structure
     *
     * Typical Usage Pattern:
     * @code
     *   // In downstream listener:
     *   listener.Connect(producer, 1);  // Port 1 = completion signals
     *   
     *   // Later, when signal received:
     *   Message signal_msg = receive();
     *   if (auto* signal = std::get_if<CompletionSignal>(&signal_msg.data())) {
     *       std::cout << "Reason: " << signal->GetReason() << std::endl;
     *       std::cout << "Samples: " << signal->GetSampleCount() << std::endl;
     *       // Coordinate shutdown or restart
     *   }
     * @endcode
     */
    std::optional<NotificationType> Produce(std::integral_constant<std::size_t, 1>) override {
        NotificationType notification;
        if(!notification_queue_.Dequeue(notification)) {
            return std::nullopt;
        }
        return notification;
    }

    /**
     * @brief Check if the data generator has been exhausted
     *
     * Query method for checking the generator's state at any time,
     * even while sampling is in progress.
     *
     * @return true if the generator has returned nullopt (end of stream),
     *         false if more samples may be available
     *
     * @note Thread-safe read: May be called from other threads
     * @note Non-blocking: No lock contention, just delegates to generator
     *
     * @see generator_->IsExhausted() for the underlying call
     */
    bool IsGeneratorExhausted() const {
        return generator_->IsExhausted();
    }

    /**
     * @brief Get the timestamp of the last produced sample
     *
     * Retrieves timing information from the generator for diagnostics
     * and metrics. Useful for synchronization with external events or
     * calculating production rates.
     *
     * @return Nanosecond-resolution timestamp from generator->GetLastTimestamp()
     *         (default 0 if not overridden by subclass)
     *
     * @note Thread-safe read: May be called from other threads
     * @note Provider dependent: Return value depends on generator implementation
     *
     * @see generator_->GetLastTimestamp() for the underlying call
     */
    std::chrono::nanoseconds GetLastGeneratorTimestamp() const {
        return generator_->GetLastTimestamp();
    }

    /**
     * @brief Factory method for creating the completion signal (pure virtual)
     *
     * Called when the generator is exhausted to create the notification
     * that will be emitted on Port 1. Subclasses MUST override to provide
     * their specific NotificationType with appropriate metadata.
     *
     * ## Semantic Contract
     *
     * The notification should include:
     * - Reason code indicating why production ended (exhausted, error, etc.)
     * - Producer name (from GetName())
     * - Timestamp when exhaustion occurred
     * - Total sample count (GetTotalSamplesGenerated())
     * - Custom message or context-specific data
     *
     * ## Implementation Example
     *
     * @code
     *   CompletionSignal CreateNotification() const override {
     *       return CompletionSignal(
     *           CompletionSignal::Reason::CSV_DATA_EXHAUSTED,
     *           GetName(),
     *           GetLastGeneratorTimestamp(),
     *           GetTotalSamplesGenerated(),
     *           "All samples produced successfully"
     *       );
     *   }
     * @endcode
     *
     * @return NotificationType instance with completion metadata
     *
     * @note Called only once per producer lifecycle (when generator exhausted)
     * @note Called from producer thread context
     * @note Must be const (queried during readonly state)
     *
     * @see CompletionSignal for standard notification type
     * @see GetName() for producer identification
     * @see GetTotalSamplesGenerated() for sample count
     * @see GetLastGeneratorTimestamp() for timing info
     */
    virtual NotificationType CreateNotification() const = 0;

    /**
     * @brief Get the sensor classification type (category)
     * 
     * Returns the sensor classification/category identifier used for data
     * routing, discovery, and integration regardless of data source.
     * 
     * Sensor classification is orthogonal to implementation:
     * - CSV sensor nodes (simulation) and real device sensor nodes both
     *   produce data of the same classification type
     * - This enables unified data routing and processing pipelines
     * 
     * Must be implemented by each sensor producer subclass.
     * 
     * Examples:
     *   - Accelerometer nodes (CSV or device) return ACCELEROMETER
     *   - Gyroscope nodes (CSV or device) return GYROSCOPE
     *   - Magnetometer nodes (CSV or device) return MAGNETOMETER
     *   - Barometric nodes (CSV or device) return BAROMETRIC
     *   - GPS nodes (CSV or device) return GPS_POSITION
     * 
     * @return Sensor classification type enum value
     * 
     * @see SensorClassificationTypeToString() to convert to string representation
     * @see StringToSensorClassificationType() to convert from string
     * @see SensorClassificationType.hpp for complete taxonomy
     * 
     * @note Used by CSVDataStreamManager for discovery and data dispatch
     * @note Used by any data processing pipeline for type-based routing
     */
    virtual sensors::SensorClassificationType GetSensorClassificationType() const  {
        return Classification;  
    }

    /**
     * @brief Get the sample production interval (customizable)
     *
     * Called before each sample production to determine how long to sleep
     * before the next sample. Default implementation returns the constant
     * sample_interval. Override to implement variable-rate sampling.
     *
     * ## Variable-Rate Sampling
     *
     * Subclasses can override to implement dynamic intervals:
     * @code
     *   std::chrono::microseconds GetNextSampleInterval() const override {
     *       // Increase interval gradually
     *       static size_t call_count = 0;
     *       return sample_interval * (1 + call_count++ / 100);
     *   }
     * @endcode
     *
     * This enables:
     * - Adaptive timing based on system load
     * - Ramp-up/ramp-down patterns
     * - Jitter injection for realistic testing
     *
     * @return Microseconds to sleep before next sample (default: sample_interval)
     *
     * @note Called before every sleep in Port 0 Produce()
     * @note Must return finite positive duration (zero allowed but not recommended)
     * @note Default implementation: `return sample_interval;`
     *
     * @see sample_interval member for the configured base interval
     */
    virtual std::chrono::microseconds GetNextSampleInterval() const {
        return sample_interval;
    }

    /**
     * @brief Cleanup and stop data production
     *
     * Prepares the producer for shutdown by:
     * 1. Clearing any pending notifications from notification_queue_
     * 2. Disabling the queue to prevent further enqueues
     * 3. Delegating to SourceNode::Stop() for thread termination
     *
     * Must be called before Join() to wait for graceful thread shutdown.
     * After Stop(), no more samples or signals will be produced.
     *
     * ## Shutdown Sequence
     *
     * @code
     *   producer->Stop();       // Signal threads to stop
     *   producer->Join();       // Wait for completion
     *   // Now safe to query final metrics or destroy
     * @endcode
     *
     * @note Clears notification_queue_ first (prevents pending signals)
     * @note Thread-safe: Safe to call from any thread
     * @note Idempotent: Safe to call multiple times
     *
     * @see SourceNode::Stop() for base implementation
     * @see Join() for waiting on thread completion
     */
    void Stop() override {
        notification_queue_.Clear();
        notification_queue_.Disable();
        NamedSourceNode<NodeType, ::graph::message::Message, NotificationType>::Stop();
    }

    /**
     * @brief Get total number of samples generated so far
     *
     * Returns the count of all successful Produce() calls from the generator,
     * including ignored samples. This is the raw production count before the
     * sample_ignore filter is applied.
     *
     * ## Semantics
     *
     * - Counts every successful generator->Produce() call
     * - Includes the first sample_ignore samples in the count
     * - Incremented before the ignore check is applied
     * - Available during sampling (increments in real-time)
     * - Reflects only successful produces (nullopt doesn't increment)
     *
     * Example with sample_ignore=3:
     * ```
     * Call 1: produces sample, total_samples_generated_ = 1 (ignored)
     * Call 2: produces sample, total_samples_generated_ = 2 (ignored)
     * Call 3: produces sample, total_samples_generated_ = 3 (ignored)
     * Call 4: produces sample, total_samples_generated_ = 4 (returned to Port 0)
     * Call 5: produces sample, total_samples_generated_ = 5 (returned to Port 0)
     * Call 6: generator exhausted, total_samples_generated_ = 5 (no increment)
     * ```
     *
     * @return Total production count (includes ignored samples)
     *
     * @note Thread-safe: Atomic integer, readable from any thread
     * @note Always >= sample_ignore_: First sample_ignore are always counted
     * @note Real-time: Updates while sampling is in progress
     *
     * For counting only delivered samples:
     * @code
     *   size_t delivered = GetTotalSamplesGenerated() - sample_ignore_;
     * @endcode
     *
     * @see sample_ignore_ member for the configured ignore count
     * @see GetLastGeneratorTimestamp() for timing of last sample
     * @see CreateNotification() passes this count to completion signal
     */
    size_t GetTotalSamplesGenerated() const {
        return total_samples_generated_;
    }

    protected:
        /// @brief Interval between samples (microseconds)
        /// Used by GetNextSampleInterval() to determine sleep duration
        std::chrono::microseconds sample_interval;
        
        /// @brief Number of initial samples to skip (ignore count)
        /// First sample_ignore_ samples are produced but not returned (nullopt)
        std::size_t sample_ignore_;
        
        /// @brief Logger for diagnostics and shutdown events
        /// Logs "Data exhausted" message when generator is exhausted
        log4cxx::LoggerPtr logger_ = log4cxx::Logger::getLogger("nodes.DataProducerWithNotification");
        
    private:
        /// @brief Data source generator (moved in constructor, never null after construction)
        std::unique_ptr<DataGenerator> generator_;
        
        /// @brief Timestamp when first Produce() was called
        /// Used to establish baseline for interval-based timing
        std::chrono::steady_clock::time_point start_time_;
        
        /// @brief Target time for next sample production
        /// Advanced by sample_interval on each Port 0 Produce() call
        /// Used with sleep_until() for precise microsecond timing
        std::chrono::steady_clock::time_point next_time_;
        
        /// @brief Total number of samples generated (including ignored ones)
        /// Incremented on each successful generator->Produce() call
        /// Accessible via GetTotalSamplesGenerated()
        size_t total_samples_generated_;
        
        /// @brief Queue for exactly-once completion signal delivery
        /// Thread-safe FIFO: enqueued when Port 0 exhausts, dequeued from Port 1
        /// Cleared and disabled on Stop() to prevent pending signals
        core::ActiveQueue<NotificationType> notification_queue_;
        
        /// @brief Default producer name for logging
        /// Used in log messages, overridable via SetName()
        std::string producer_name_ = "DataProducerWithNotification";
};

} // namespace graph

