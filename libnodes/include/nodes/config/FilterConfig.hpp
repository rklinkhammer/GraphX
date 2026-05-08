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
#include "avionics/config/KalmanConfig.hpp"
#include "avionics/config/MovingAverageConfig.hpp"
#include "core/VariantHelper.hpp"
#include <variant>

namespace graph {

/**
 * Polymorphic filter configuration.
 * 
 * Allows choosing between different filter types at runtime via JSON.
 * Uses std::variant to hold the actual configuration.
 * 
 * Supported filter types:
 * - Kalman: Kalman filter with noise parameters
 * - MovingAverage: Simple moving average filter
 * - None: No filtering
 * 
 * JSON Examples:
 * @code
 * // Kalman filter
 * {
 *   "type": "Kalman",
 *   "params": {
 *     "q": 0.01,
 *     "r": 0.1,
 *     "p": 1.0
 *   }
 * }
 * 
 * // Moving average filter
 * {
 *   "type": "MovingAverage",
 *   "params": {
 *     "window_size": 10
 *   }
 * }
 * 
 * // No filter
 * {
 *   "type": "None"
 * }
 * @endcode
 */
struct FilterConfig {
    /// Enumeration of available filter types
    enum class Type {
        Kalman,         ///< Kalman filter
        MovingAverage,  ///< Moving average filter
        None            ///< No filtering
    };
    
    /// Which filter type is selected
    Type type = Type::None;
    
    /// The actual configuration (variant of all possible config types)
    /// std::monostate for "None" type
    std::variant<std::monostate, KalmanConfig, MovingAverageConfig> params;
    
    /**
     * Validate the selected filter configuration.
     * 
     * Validates the active variant (the one actually in use).
     * Uses std::visit to call Validate() on the appropriate config type.
     * 
     * @throws ConfigError if validation fails
     */
    void Validate() const {
        struct ValidateVisitor {
            void operator()(const std::monostate&) const {
                // No validation needed for None type
            }
            void operator()(const KalmanConfig& cfg) const {
                cfg.Validate();
            }
            void operator()(const MovingAverageConfig& cfg) const {
                cfg.Validate();
            }
        };
        
        std::visit(ValidateVisitor{}, params);
    }
    
    /**
     * Check if a specific filter type is selected.
     * 
     * Convenience method for runtime type checking.
     */
    bool IsType(Type t) const { return type == t; }
    
    /**
     * Get Kalman config if selected, throw otherwise.
     *
     * Uses GetIfVariant helper for type-safe access with optional return.
     * Converts optional to exception for backward compatibility.
     *
     * @throws ConfigError if Kalman filter not selected
     */
    const KalmanConfig& GetKalmanConfig() const {
        auto cfg = reflection::GetIfVariant<KalmanConfig>(const_cast<decltype(params)&>(params));
        if (!cfg) {
            throw ConfigError("Kalman config requested but filter type is not Kalman");
        }
        return cfg->get();
    }

    /**
     * Get MovingAverage config if selected, throw otherwise.
     *
     * Uses GetIfVariant helper for type-safe access with optional return.
     * Converts optional to exception for backward compatibility.
     *
     * @throws ConfigError if MovingAverage filter not selected
     */
    const MovingAverageConfig& GetMovingAverageConfig() const {
        auto cfg = reflection::GetIfVariant<MovingAverageConfig>(const_cast<decltype(params)&>(params));
        if (!cfg) {
            throw ConfigError("MovingAverage config requested but filter type is not MovingAverage");
        }
        return cfg->get();
    }
    
    /**
     * Define schema metadata for this config.
     */
    static constexpr std::array<JsonField, 2> FIELDS{{
        {
            .name = "type",
            .type = JsonType::String,
            .required = true,
            .min = std::nullopt,
            .max = std::nullopt,
            .default_value = "None",
            .enum_values = std::nullopt,
            .description = "Filter type: 'Kalman', 'MovingAverage', or 'None'"
        },
        {
            .name = "params",
            .type = JsonType::Object,
            .required = false,
            .min = std::nullopt,
            .max = std::nullopt,
            .default_value = std::nullopt,
            .enum_values = std::nullopt,
            .description = "Filter-specific parameters (required if type is not 'None')"
        }
    }};
    
    /**
     * Get the complete schema for this config type.
     */
    static constexpr JsonSchema Schema() {
        return {
            .title = "Filter Config",
            .description = "Polymorphic filter configuration (Kalman, MovingAverage, or None)",
            .fields = FIELDS
        };
    }
};

/**
 * Parser specialization for FilterConfig.
 * 
 * Reads the "type" field to determine which parser to use,
 * then parses the "params" object using the appropriate parser.
 */
template<>
struct ConfigParser<FilterConfig> {
    /**
     * Parse JSON into FilterConfig.
     * 
     * Looks for:
     * - "type": string specifying "Kalman", "MovingAverage", or "None"
     * - "params": object with filter-specific parameters
     * 
     * @param view JSON view containing filter configuration
     * @return Validated FilterConfig
     * @throws ConfigError if type is unknown or parsing fails
     */
    static FilterConfig Parse(const JsonView& view) {
        FilterConfig cfg;
        
        // Get filter type
        std::string type_str = view.GetString("type", "None");
        
        if (type_str == "Kalman") {
            cfg.type = FilterConfig::Type::Kalman;
            cfg.params = ConfigParser<KalmanConfig>::Parse(view.GetObject("params"));
        } else if (type_str == "MovingAverage") {
            cfg.type = FilterConfig::Type::MovingAverage;
            cfg.params = ConfigParser<MovingAverageConfig>::Parse(view.GetObject("params"));
        } else if (type_str == "None") {
            cfg.type = FilterConfig::Type::None;
            cfg.params = std::monostate{};
        } else {
            throw ConfigError("Unknown filter type: " + type_str + 
                            ". Expected 'Kalman', 'MovingAverage', or 'None'");
        }
        
        // Validate the selected configuration
        cfg.Validate();
        
        return cfg;
    }
};

}  // namespace graph
