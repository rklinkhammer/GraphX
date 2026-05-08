// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#ifndef GDASHBOARD_METRIC_HPP
#define GDASHBOARD_METRIC_HPP

#include <string>
#include <vector>
#include <deque>
#include <variant>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "core/VariantHelper.hpp"

// ============================================================================
// Data Structures
// ============================================================================

// Metric data type enumeration
enum class MetricType {
    INT,
    FLOAT,
    BOOL,
    STRING
};

/**
 * @brief Enhanced Metric structure with type support and event tracking
 * 
 * Stores a single metric value with its type, schema information, and event context.
 * Supports parsing from MetricsEvent.data with type inference from NodeMetricsSchema.
 * Maintains history of last 60 values with timestamps for trend analysis.
 */
struct Metric {
    std::string name;                                      // Metric name
    MetricType type = MetricType::STRING;                  // Data type
    std::variant<int, double, bool, std::string> value;    // Typed value
    std::string unit;                                      // Units (m, m/s, %, etc)
    int alert_level = 0;                                   // 0=ok, 1=warn, 2=critical
    std::string event_type;                                // Event this came from
    std::chrono::system_clock::time_point last_update;     // Timestamp of last update
    double confidence = 1.0;                               // Confidence 0.0-1.0 (for estimation metrics)
    
    // History tracking (circular buffer)
    static constexpr size_t HISTORY_SIZE = 60;             // 60 entries @ 100ms = 6 seconds
    std::deque<double> value_history;                      // Numeric history (for stats)
    std::deque<std::chrono::system_clock::time_point> 
        timestamp_history;                                 // Timestamps for each history entry
    
    // Default constructor
    Metric() : last_update(std::chrono::system_clock::now()) {}
    
    // Constructor for metric initialization
    Metric(const std::string& n, MetricType t, const std::variant<int, double, bool, std::string>& v,
           const std::string& u, int alert, const std::string& evt, 
           const std::chrono::system_clock::time_point& ts, double conf)
        : name(n), type(t), value(v), unit(u), alert_level(alert), 
          event_type(evt), last_update(ts), confidence(conf) {}
    
    /**
     * @brief Get value formatted as string with appropriate precision
     * Shows confidence metrics as percentage (e.g., "87.3%")
     * Uses modern std::visit pattern with Overload helper for type safety
     */
    std::string GetFormattedValue() const {
        return std::visit(reflection::Overload{
            // INT case
            [](int v) {
                return std::to_string(v);
            },
            // FLOAT/DOUBLE case
            [this](double v) {
                // Show confidence metrics as percentage
                if (name.find("confidence") != std::string::npos) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(1) << (v * 100.0) << "%";
                    return oss.str();
                }

                // Determine precision based on unit/name
                int precision = 2;  // default for altitudes, velocities
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(precision) << v;
                return oss.str();
            },
            // BOOL case
            [](bool v) {
                return v ? std::string("true") : std::string("false");
            },
            // STRING case
            [](const std::string& v) {
                return v;
            }
        }, value);
    }
    
    /**
     * @brief Add value to history (circular buffer)
     */
    void AddToHistory(double numeric_value) {
        value_history.push_back(numeric_value);
        timestamp_history.push_back(std::chrono::system_clock::now());
        
        // Maintain circular buffer size (60 entries)
        if (value_history.size() > HISTORY_SIZE) {
            value_history.pop_front();
            timestamp_history.pop_front();
        }
    }
    
    /**
     * @brief Get numeric value from current value (for history)
     * Uses modern std::visit pattern for type-safe conversion
     */
    double GetNumericValue() const {
        return std::visit(reflection::Overload{
            // INT case
            [](int v) -> double {
                return static_cast<double>(v);
            },
            // FLOAT/DOUBLE case
            [](double v) -> double {
                return v;
            },
            // BOOL case
            [](bool v) -> double {
                return v ? 1.0 : 0.0;
            },
            // STRING case
            [](const std::string& v) -> double {
                // Try to parse string as number
                try {
                    return std::stod(v);
                } catch (...) {
                    return 0.0;
                }
            }
        }, value);
    }
    
    /**
     * @brief Calculate minimum value from history
     */
    double GetHistoryMin() const {
        if (value_history.empty()) return 0.0;
        return *std::min_element(value_history.begin(), value_history.end());
    }
    
    /**
     * @brief Calculate maximum value from history
     */
    double GetHistoryMax() const {
        if (value_history.empty()) return 0.0;
        return *std::max_element(value_history.begin(), value_history.end());
    }
    
    /**
     * @brief Calculate average value from history
     */
    double GetHistoryAvg() const {
        if (value_history.empty()) return 0.0;
        double sum = 0.0;
        for (double v : value_history) {
            sum += v;
        }
        return sum / value_history.size();
    }
    
    /**
     * @brief Get history size
     */
    size_t GetHistorySize() const {
        return value_history.size();
    }
    
    /**
     * @brief Generate ASCII sparkline from history data
     * Uses 8-level Unicode bar characters: ▁▂▃▄▅▆▇█
     * Displays last N entries (up to 30 chars) normalized to value range
     * Returns empty string if insufficient history
     */
    std::string GenerateSparkline(size_t max_width = 30) const {
        if (value_history.size() < 2) return "";  // Need at least 2 points
        
        // Sparkline characters (8 levels)
        static constexpr const char* kBars[8] = {"▁","▂","▃","▄","▅","▆","▇","█"};
        
        // Determine how many data points to display (up to max_width)
        size_t display_count = std::min(max_width, value_history.size());
        size_t start_idx = value_history.size() - display_count;
        
        // Find min/max in the range to display
        double min_val = value_history[start_idx];
        double max_val = value_history[start_idx];
        for (size_t i = start_idx; i < value_history.size(); ++i) {
            min_val = std::min(min_val, value_history[i]);
            max_val = std::max(max_val, value_history[i]);
        }
        
        // Handle case where all values are the same
        double range = max_val - min_val;
        if (range < 0.0001) {
            // All same value - use middle bar
            std::string result;
            for (size_t i = 0; i < display_count; ++i) {
                result += "▄";
            }
            return result;
        }
        
        // Generate sparkline
        std::string result;
        for (size_t i = start_idx; i < value_history.size(); ++i) {
            // Normalize value to 0-7 range (8 levels)
            double normalized = (value_history[i] - min_val) / range;
            int level = static_cast<int>(normalized * 7.0);
            level = std::min(7, level);  // Clamp to 7
            result += kBars[level];
        }
        
        return result;
    }
    
    /**
     * @brief Generate ASCII-safe sparkline using simple ASCII characters
     * Uses 8-level ASCII pattern: ' ._-=~*+#'
     * Compatible with all terminals regardless of UTF-8 support
     * Same normalization as Unicode version
     */
    std::string GenerateSparklineASCII(size_t max_width = 30) const {
        if (value_history.size() < 2) return "";  // Need at least 2 points
        
        // ASCII-safe sparkline characters (8 levels)
        static constexpr const char* bars[9] = {" ",".","_","-","=","~","*","+","#"};

        // Determine how many data points to display (up to max_width)
        size_t display_count = std::min(max_width, value_history.size());
        size_t start_idx = value_history.size() - display_count;
        
        // Find min/max in the range to display
        double min_val = value_history[start_idx];
        double max_val = value_history[start_idx];
        for (size_t i = start_idx; i < value_history.size(); ++i) {
            min_val = std::min(min_val, value_history[i]);
            max_val = std::max(max_val, value_history[i]);
        }
        
        // Handle case where all values are the same
        double range = max_val - min_val;
        if (range < 0.0001) {
            // All same value - use middle character
            std::string result;
            for (size_t i = 0; i < display_count; ++i) {
                result += "-";
            }
            return result;
        }
        
        // Generate sparkline
        std::string result;
        for (size_t i = start_idx; i < value_history.size(); ++i) {
            // Normalize value to 0-8 range (9 chars total)
            double normalized = (value_history[i] - min_val) / range;
            int level = static_cast<int>(normalized * 8.0);
            level = std::min(8, level);  // Clamp to 8
            result += bars[level];
        }
        
        return result;
    }
};

/**
 * @brief Convert MetricType enum to string representation
 *
 * Centralizes MetricType→string conversion to eliminate code duplication.
 * Used by BuiltinCommands and other components for serialization.
 *
 * @param type The MetricType to convert
 * @return String representation: "int", "float", "bool", or "string"
 */
inline std::string MetricTypeToString(MetricType type) {
    switch (type) {
        case MetricType::INT:
            return "int";
        case MetricType::FLOAT:
            return "float";
        case MetricType::BOOL:
            return "bool";
        case MetricType::STRING:
            return "string";
    }
    return "unknown";
}

/**
 * @brief Convert string to MetricType enum
 *
 * Reverse of MetricTypeToString for parsing string representations.
 * Case-insensitive parsing for flexibility.
 *
 * @param type_str String representation (e.g., "int", "float", "bool", "string")
 * @return MetricType enum value (defaults to STRING if not recognized)
 */
inline MetricType StringToMetricType(const std::string& type_str) {
    if (type_str == "int" || type_str == "INT") {
        return MetricType::INT;
    } else if (type_str == "float" || type_str == "FLOAT" || type_str == "double" || type_str == "DOUBLE") {
        return MetricType::FLOAT;
    } else if (type_str == "bool" || type_str == "BOOL" || type_str == "boolean") {
        return MetricType::BOOL;
    } else {
        return MetricType::STRING;
    }
}

#endif // GDASHBOARD_METRIC_HPP
