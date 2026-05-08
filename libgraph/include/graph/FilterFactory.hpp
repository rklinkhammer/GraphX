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

#include "avionics/config/FilterConfig.hpp"
#include <iostream>

namespace graph {

/**
 * Filter factory for creating filter instances from configuration.
 * 
 * Uses the std::visit pattern to dispatch based on the FilterConfig type
 * and create the appropriate filter implementation.
 * 
 * While this example uses placeholder filters, the factory pattern
 * makes it easy to integrate real filter implementations like:
 * - KalmanFilter<T>
 * - MovingAverageFilter<T>
 * - NoOpFilter<T>
 */
class FilterFactory {
public:
    /**
     * Create a filter based on configuration.
     * 
     * This is a placeholder that demonstrates how to handle each filter type.
     * In a real implementation, this would create actual filter instances.
     * 
     * @param config The filter configuration
     * @return Configuration summary for logging/debugging
     * 
     * Example for integrating real filters:
     * @code
     * struct FilterVisitor {
     *     std::unique_ptr<IFilter> operator()(const KalmanConfig& cfg) {
     *         return std::make_unique<KalmanFilter>(cfg.q, cfg.r, cfg.p);
     *     }
     *     std::unique_ptr<IFilter> operator()(const MovingAverageConfig& cfg) {
     *         return std::make_unique<MovingAverageFilter>(cfg.window_size);
     *     }
     *     std::unique_ptr<IFilter> operator()(const std::monostate&) {
     *         return std::make_unique<NoOpFilter>();
     *     }
     * };
     * 
     * auto filter = std::visit(FilterVisitor{}, config.params);
     * @endcode
     */
    static std::string CreateFilter(const FilterConfig& config) {
        struct FilterVisitor {
            std::string operator()(const std::monostate&) const {
                return "No filter (pass-through mode)";
            }
            std::string operator()(const KalmanConfig& cfg) const {
                return std::string("Kalman filter (q=") + std::to_string(cfg.q) + 
                       ", r=" + std::to_string(cfg.r) + 
                       ", p=" + std::to_string(cfg.p) + ")";
            }
            std::string operator()(const MovingAverageConfig& cfg) const {
                return std::string("MovingAverage filter (window=") + 
                       std::to_string(cfg.window_size) + ")";
            }
        };
        
        return std::visit(FilterVisitor{}, config.params);
    }
    
    /**
     * Get human-readable filter type name.
     */
    static std::string GetFilterTypeName(FilterConfig::Type type) {
        switch (type) {
            case FilterConfig::Type::Kalman:
                return "Kalman";
            case FilterConfig::Type::MovingAverage:
                return "MovingAverage";
            case FilterConfig::Type::None:
                return "None";
            default:
                return "Unknown";
        }
    }
};

}  // namespace graph
