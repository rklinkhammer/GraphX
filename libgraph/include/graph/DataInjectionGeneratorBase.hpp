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
 * @file DataInjectionGeneratorBase.hpp
 * @brief Queue data generator for reading timestamped data from queue sources
 * @author GraphX Contributors
 * @date 2025
 * @license MIT
 */

#pragma once

#include <optional>
#include <chrono>
#include "graph/DataGeneratorBase.hpp"
#include "core/ActiveQueue.hpp"
#include "core/VariantHelper.hpp"

namespace graph {

/**
 * @class DataInjectionGeneratorBase
 * @brief Data generator that retrieves timestamped samples from an ActiveQueue
 * 
 * This class implements the DataGeneratorBase interface by consuming data from
 * an ActiveQueue<DataType>. It's typically used with data loaded from CSV files
 * or other sources into the queue.
 * 
 * **Key Features:**
 * - Dequeues samples from a shared queue
 * - Tracks the last produced sample and its timestamp
 * - Respects queue disabled state for exhaustion detection
 * - Thread-safe interaction with ActiveQueue
 * 
 * **Template Parameter:**
 * - DataType: Must have GetTimestamp() method returning std::chrono::nanoseconds
 * 
 * @tparam DataType Type of data samples in the queue
 * 
 * @note DataType is expected to inherit from or implement TimestampedData interface
 * @see DataGeneratorBase for interface definition
 * @see ActiveQueue for queue implementation details
 */
template<typename DataType, typename PayloadType>
class DataInjectionGeneratorBase : public DataGeneratorBase<DataType> {
public:
    /// Construct generator with reference to data queue
    /// @param queue Reference to ActiveQueue containing timestamped data samples
    explicit DataInjectionGeneratorBase(core::ActiveQueue<PayloadType>& queue) 
        : queue_(queue) {}
    
    /// Generate the next sample from the queue
    /// @param index Unused parameter (required by base class interface)
    /// @return Next data sample if available, std::nullopt if queue is empty
    /// @note Stores the produced sample for GetLastTimestamp() queries
    std::optional<DataType> Produce(size_t /*index*/) override {
        PayloadType sample;
        if (queue_.Dequeue(sample)) {
            auto data_ref = reflection::GetIfVariant<DataType>(sample);
            if (!data_ref) {
                return std::nullopt;  // Wrong type (should not happen)
            }
            last_sample_ = data_ref->get();
            return data_ref->get();
        } else {
            return std::nullopt; // Queue is empty or disabled
        }
    }
    
    /// Check if the generator has exhausted its samples
    /// @return true if the queue has been disabled; false otherwise
    /// @note Disabled queue indicates no more samples will be produced
    bool IsExhausted() const override {
        return !queue_.Enabled();
    }
    
    /// Get the timestamp of the last generated sample
    /// @return Nanosecond precision timestamp from the last sample
    /// @note Relies on DataType::GetTimestamp() for timestamp extraction
    std::chrono::nanoseconds GetLastTimestamp() const override {
        return last_sample_.GetTimestamp();
    }
    
private:
    /// Last sample produced by Produce() method
    DataType last_sample_;
    /// Reference to the queue containing data samples
    core::ActiveQueue<PayloadType>& queue_;
};

} // namespace graph

