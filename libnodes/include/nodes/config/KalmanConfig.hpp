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
 * Configuration for Kalman filter.
 * 
 * The Kalman filter uses three key parameters:
 * - q: Process noise covariance (how much the system can change unexpectedly)
 * - r: Measurement noise covariance (how much to trust measurements)
 * - p: Initial estimation error covariance (initial uncertainty in estimate)
 * 
 * All parameters must be positive. Typical values:
 * - q: 0.01 to 0.1 (smaller = trust the model more)
 * - r: 0.1 to 1.0 (smaller = trust measurements more)
 * - p: 0.1 to 10.0 (smaller = start with high confidence)
 * 
 * JSON Example:
 * @code
 * {
 *   "type": "Kalman",
 *   "params": {
 *     "q": 0.01,
 *     "r": 0.1,
 *     "p": 1.0
 *   }
 * }
 * @endcode
 */
struct KalmanConfig {
    /// Process noise covariance (must be > 0)
    float q = 0.01f;
    
    /// Measurement noise covariance (must be > 0)
    float r = 0.1f;
    
    /// Initial estimation error covariance (must be > 0)
    float p = 1.0f;
    
    /**
     * Validate all parameters.
     * 
     * @throws ConfigError if any parameter is invalid
     */
    void Validate() const {
        if (q <= 0.0f) {
            throw ConfigError("Kalman parameter 'q' must be positive, got: " + std::to_string(q));
        }
        if (r <= 0.0f) {
            throw ConfigError("Kalman parameter 'r' must be positive, got: " + std::to_string(r));
        }
        if (p <= 0.0f) {
            throw ConfigError("Kalman parameter 'p' must be positive, got: " + std::to_string(p));
        }
    }
    
    /**
     * Define schema metadata for this config.
     * 
     * Describes:
     * - All fields and their types
     * - Required vs optional
     * - Constraints (min values)
     * - Default values
     * - Descriptions
     */
    static constexpr std::array<JsonField, 3> FIELDS{{
        {
            .name = "q",
            .type = JsonType::Number,
            .required = true,
            .min = 0.0,
            .max = std::nullopt,
            .default_value = "0.01",
            .enum_values = std::nullopt,
            .description = "Process noise covariance (must be > 0)"
        },
        {
            .name = "r",
            .type = JsonType::Number,
            .required = true,
            .min = 0.0,
            .max = std::nullopt,
            .default_value = "0.1",
            .enum_values = std::nullopt,
            .description = "Measurement noise covariance (must be > 0)"
        },
        {
            .name = "p",
            .type = JsonType::Number,
            .required = true,
            .min = 0.0,
            .max = std::nullopt,
            .default_value = "1.0",
            .enum_values = std::nullopt,
            .description = "Initial estimation error covariance (must be > 0)"
        }
    }};
    
    /**
     * Get the complete schema for this config type.
     */
    static constexpr JsonSchema Schema() {
        return {
            .title = "Kalman Filter Config",
            .description = "Parameters for Kalman filter noise and estimation",
            .fields = FIELDS
        };
    }
};

/**
 * Parser specialization for KalmanConfig.
 * 
 * Extracts q, r, p from JSON and validates them.
 * 
 * Usage:
 * @code
 * auto json = nlohmann::json::parse(config_str);
 * auto kalman_cfg = ConfigParser<KalmanConfig>::Parse(JsonView(json));
 * @endcode
 */
template<>
struct ConfigParser<KalmanConfig> {
    /**
     * Parse JSON into KalmanConfig.
     * 
     * @param view JSON view containing configuration
     * @return Validated KalmanConfig
     * @throws ConfigError if parsing or validation fails
     */
    static KalmanConfig Parse(const JsonView& view) {
        KalmanConfig cfg{
            .q = view.GetFloat("q", 0.01f),
            .r = view.GetFloat("r", 0.1f),
            .p = view.GetFloat("p", 1.0f)
        };
        cfg.Validate();
        return cfg;
    }
};

}  // namespace graph
