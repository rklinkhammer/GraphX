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

#include "config/Config.hpp"
#include "config/ConfigParser.hpp"
#include <array>

namespace graph {

/**
 * Configuration for moving average filter.
 * 
 * A simple filter that averages the last N samples.
 * 
 * Parameters:
 * - window_size: Number of samples to average (must be > 0)
 * 
 * Typical values:
 * - 5: Light smoothing, quick response
 * - 10: Moderate smoothing
 * - 20: Heavy smoothing, slower response
 * 
 * JSON Example:
 * @code
 * {
 *   "type": "MovingAverage",
 *   "params": {
 *     "window_size": 10
 *   }
 * }
 * @endcode
 */
struct MovingAverageConfig {
    /// Number of samples to average (must be > 0)
    int window_size = 5;
    
    /**
     * Validate all parameters.
     * 
     * @throws ConfigError if any parameter is invalid
     */
    void Validate() const {
        if (window_size <= 0) {
            throw ConfigError("MovingAverage parameter 'window_size' must be positive, got: " + 
                            std::to_string(window_size));
        }
    }
    
    /**
     * Define schema metadata for this config.
     */
    static constexpr std::array<JsonField, 1> FIELDS{{
        {
            .name = "window_size",
            .type = JsonType::Integer,
            .required = true,
            .min = 1.0,
            .max = std::nullopt,
            .default_value = "5",
            .enum_values = std::nullopt,
            .description = "Number of samples to average (must be > 0)"
        }
    }};
    
    /**
     * Get the complete schema for this config type.
     */
    static constexpr JsonSchema Schema() {
        return {
            .title = "Moving Average Filter Config",
            .description = "Parameters for moving average filter",
            .fields = FIELDS
        };
    }
};

/**
 * Parser specialization for MovingAverageConfig.
 */
template<>
struct ConfigParser<MovingAverageConfig> {
    /**
     * Parse JSON into MovingAverageConfig.
     * 
     * @param view JSON view containing configuration
     * @return Validated MovingAverageConfig
     * @throws ConfigError if parsing or validation fails
     */
    static MovingAverageConfig Parse(const JsonView& view) {
        MovingAverageConfig cfg{
            .window_size = view.GetInt("window_size", 5)
        };
        cfg.Validate();
        return cfg;
    }
};

}  // namespace graph
