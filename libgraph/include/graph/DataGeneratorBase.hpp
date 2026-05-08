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

namespace graph {

/**
 * @class DataGeneratorBase
 * @brief Generic data producer interface for generating samples of type SampleT
 * 
 * This abstract interface defines the contract for data generators that produce
 * sequential samples of a templated type. Generators can represent finite or
 * infinite data sources.
 * 
 * **Template Parameter:**
 * - SampleT: The data type produced by the generator
 * 
 * **Key Responsibilities:**
 * - Produce samples on demand via Produce()
 * - Track exhaustion state via IsExhausted()
 * - Provide timestamp information via GetLastTimestamp()
 * 
 * @tparam SampleT Data type produced by this generator
 * 
 * @note Subclasses should override virtual methods to implement specific generation logic
 * @see CSVDataGeneratorBase for CSV file-based data generation
 */
template<typename DataType>
class DataGeneratorBase {
public:
    /// Virtual destructor for proper cleanup of derived classes
    virtual ~DataGeneratorBase() = default;
    
    /// Produce next data sample
    /// @param index Sample ordinal for sequential reading
    /// @return Sample if available, std::nullopt if exhausted
    virtual std::optional<DataType> Produce(size_t index) = 0;
    
    /// Check if generator has been exhausted
    /// @return true if no more samples available; false otherwise
    /// @note Default implementation returns false (infinite source)
    virtual bool IsExhausted() const { return false; }
    
    /// Get the timestamp of the last produced sample
    /// @return Timestamp in nanosecond precision
    /// @note Default implementation returns zero nanoseconds
    virtual std::chrono::nanoseconds GetLastTimestamp() const { 
        return std::chrono::nanoseconds{0}; 
    }
};

} // namespace graph

