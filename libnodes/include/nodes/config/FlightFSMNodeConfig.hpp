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

#include "graph/GraphConfig.hpp"
#include "config/ConfigParser.hpp"
#include "avionics/config/FilterConfig.hpp"
#include <array>
#include <string>

namespace graph {

/**
 * Configuration for FlightFSMNode.
 * 
 * Controls the behavior of the flight state machine node, including:
 * - Initial flight mode
 * - Filter selection and parameters
 * - State buffer size for history
 * 
 * JSON Example:
 * @code
 * {
 *   "flight_mode": "AUTONOMOUS",
 *   "state_buffer_size": 2048,
 *   "filter": {
 *     "type": "Kalman",
 *     "params": {
 *       "q": 0.01,
 *       "r": 0.1,
 *       "p": 1.0
 *     }
 *   }
 * }
 * @endcode
 */
struct FlightFSMNodeConfig {
    /// Initial flight mode (IDLE, AUTONOMOUS, MANUAL, DEBUG, etc.)
    /// Must not be empty
    std::string flight_mode = "IDLE";
    
    /// Filter configuration for sensor data processing
    FilterConfig filter;
    
    /// Size of circular buffer for state history (must be > 0)
    int state_buffer_size = 1024;
    
    /**
     * Validate all parameters.
     * 
     * Checks:
     * - flight_mode is not empty
     * - filter is valid (delegates to FilterConfig::Validate())
     * - state_buffer_size is positive
     * 
     * @throws ConfigError if any parameter is invalid
     */
    void Validate() const {
        if (flight_mode.empty()) {
            throw ConfigError("flight_mode cannot be empty");
        }
        
        filter.Validate();
        
        if (state_buffer_size <= 0) {
            throw ConfigError("state_buffer_size must be positive, got: " + 
                            std::to_string(state_buffer_size));
        }
    }
    
    /**
     * Define schema metadata for this config.
     */
    static constexpr std::array<JsonField, 3> FIELDS{{
        {
            .name = "flight_mode",
            .type = JsonType::String,
            .required = true,
            .min = std::nullopt,
            .max = std::nullopt,
            .default_value = "IDLE",
            .enum_values = std::nullopt,
            .description = "Initial flight mode (IDLE, AUTONOMOUS, MANUAL, DEBUG, etc.)"
        },
        {
            .name = "filter",
            .type = JsonType::Object,
            .required = true,
            .min = std::nullopt,
            .max = std::nullopt,
            .default_value = std::nullopt,
            .enum_values = std::nullopt,
            .description = "Filter configuration (Kalman, MovingAverage, or None)"
        },
        {
            .name = "state_buffer_size",
            .type = JsonType::Integer,
            .required = true,
            .min = 1.0,
            .max = std::nullopt,
            .default_value = "1024",
            .enum_values = std::nullopt,
            .description = "Size of state history buffer (must be > 0)"
        }
    }};
    
    /**
     * Get the complete schema for this config type.
     */
    static constexpr JsonSchema Schema() {
        return {
            .title = "Flight FSM Node Config",
            .description = "Configuration for flight state machine node",
            .fields = FIELDS
        };
    }
};

/**
 * Parser specialization for FlightFSMNodeConfig.
 */
template<>
struct ConfigParser<FlightFSMNodeConfig> {
    /**
     * Parse JSON into FlightFSMNodeConfig.
     * 
     * Parses:
     * - flight_mode: string
     * - filter: nested FilterConfig object
     * - state_buffer_size: integer with default 1024
     * 
     * @param view JSON view containing configuration
     * @return Validated FlightFSMNodeConfig
     * @throws ConfigError if parsing or validation fails
     */
    static FlightFSMNodeConfig Parse(const JsonView& view) {
        FlightFSMNodeConfig cfg{
            .flight_mode = view.GetString("flight_mode", "IDLE"),
            .filter = ConfigParser<FilterConfig>::Parse(view.GetObject("filter")),
            .state_buffer_size = view.GetInt("state_buffer_size", 1024)
        };
        cfg.Validate();
        return cfg;
    }
};

}  // namespace graph
