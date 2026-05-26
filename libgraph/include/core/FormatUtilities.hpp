/**
 * @file FormatUtilities.hpp
 * @brief C++20 std::format utilities and helpers (Phase 5)
 * @author Robert Klinkhammer
 * @date May 7, 2026
 *
 * Centralized formatting utilities using std::format for cleaner, faster
 * string building throughout the dashboard project. Replaces manual string
 * concatenation, std::stringstream, and std::to_string patterns with
 * modern, type-safe formatting.
 *
 * ## Phase 5 Modernization
 *
 * This module is part of the Phase 5 C++26 modernization initiative:
 * - Replaces 80+ string building patterns
 * - Provides performance benefits (compile-time format validation)
 * - Improves code clarity and maintainability
 * - Follows C++20+ best practices
 *
 * ## Usage Patterns
 *
 * ### Before (Legacy)
 * @code
 *   std::string msg = "Hit rate: " + std::to_string(hit_rate) 
 *       + "%, adjusting from " + std::to_string(old)
 *       + " to " + std::to_string(new);
 * @endcode
 *
 * ### After (Modern, Phase 5)
 * @code
 *   std::string msg = app::format::MetricAdjustment(hit_rate, old, new);
 * @endcode
 *
 * ## Compile-Time Validation
 *
 * std::format validates format strings at compile-time:
 * @code
 *   std::format("Value: {}", value);        // ✅ Valid
 *   std::format("Value: {}", arg1, arg2);   // ❌ Too few placeholders (compile error!)
 * @endcode
 *
 * @see https://en.cppreference.com/w/cpp/utility/format
 */

#pragma once

#include <format>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace app::format {

/**
 * @brief Format a floating-point metric value with precision
 * @param value The metric value
 * @param precision Decimal places (default: 2)
 * @return Formatted string (e.g., "95.50")
 *
 * Phase 5: Replaces std::to_string(value) + manual string building
 * Benefit: Consistent precision across all metrics
 *
 * @code
 *   std::string s = ToMetricString(95.5);      // "95.50"
 *   std::string s = ToMetricString(0.333, 1);  // "0.3"
 * @endcode
 */
template<typename T>
[[nodiscard]] std::string ToMetricString(T value, int precision = 2) {
    return std::format("{:.{}f}", value, precision);
}

/**
 * @brief Format a percentage value with precision
 * @param value Percentage (0-100 or higher)
 * @param precision Decimal places (default: 1)
 * @return Formatted percentage (e.g., "95.5%")
 *
 * Phase 5: Common pattern for metrics display
 *
 * @code
 *   std::string s = ToPercentageString(95.5);    // "95.5%"
 *   std::string s = ToPercentageString(33.333);  // "33.3%"
 * @endcode
 */
template<typename T>
[[nodiscard]] std::string ToPercentageString(T value, int precision = 1) {
    return std::format("{:.{}f}%", value, precision);
}

/**
 * @brief Format a byte size with appropriate units (B, KB, MB, GB)
 * @param bytes Size in bytes
 * @return Human-readable size (e.g., "1.5 MB")
 *
 * Phase 5: Improves readability of memory metrics
 *
 * @code
 *   std::string s = FormatByteSize(1536);       // "1.5 KB"
 *   std::string s = FormatByteSize(1048576);    // "1.0 MB"
 * @endcode
 */
[[nodiscard]] inline std::string FormatByteSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    
    return std::format("{:.1f} {}", size, units[unit]);
}

/**
 * @brief Format an integer with thousand separators
 * @param value Integer value
 * @return Formatted string with commas (e.g., "1,234,567")
 *
 * Phase 5: Improves readability of large numbers
 *
 * @code
 *   std::string s = FormatWithCommas(1234567);  // "1,234,567"
 * @endcode
 */
[[nodiscard]] inline std::string FormatWithCommas(uint64_t value) {
    std::string s = std::to_string(value);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

/**
 * @brief Format a timestamp to ISO 8601 format
 * @param tp Time point (system_clock)
 * @return ISO string (e.g., "2026-05-07 14:30:45")
 *
 * Phase 5: Consistent timestamp formatting
 *
 * @code
 *   auto now = std::chrono::system_clock::now();
 *   std::string s = FormatTimestamp(now);  // "2026-05-07 14:30:45"
 * @endcode
 */
[[nodiscard]] inline std::string FormatTimestamp(
    std::chrono::system_clock::time_point tp) {
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    return std::format("{:%Y-%m-%d %H:%M:%S}", sctp);
}

/**
 * @brief Format a duration to human-readable format
 * @param dur Duration (e.g., milliseconds, seconds)
 * @return Human-readable string (e.g., "5.2 s", "125 ms")
 *
 * Phase 5: Better duration display
 *
 * @code
 *   auto dur = std::chrono::milliseconds(5200);
 *   std::string s = FormatDuration(dur);  // "5.2 s"
 * @endcode
 */
template<typename Rep, typename Period>
[[nodiscard]] std::string FormatDuration(
    std::chrono::duration<Rep, Period> dur) {
    using namespace std::chrono;
    
    if (duration_cast<milliseconds>(dur).count() < 1000) {
        return std::format("{} ms", 
            duration_cast<milliseconds>(dur).count());
    } else if (duration_cast<seconds>(dur).count() < 60) {
        double secs = duration_cast<milliseconds>(dur).count() / 1000.0;
        return std::format("{:.1f} s", secs);
    } else {
        double mins = duration_cast<seconds>(dur).count() / 60.0;
        return std::format("{:.1f} m", mins);
    }
}

/**
 * @brief Format an error message with context
 * @param error_type Type/category of error
 * @param message Error description
 * @param line Optional line number for parse errors
 * @return Formatted error string
 *
 * Phase 5: Consistent error message formatting
 *
 * @code
 *   std::string s = FormatError("ParseError", "Invalid JSON");
 *   // "[ParseError] Invalid JSON"
 *
 *   std::string s = FormatError("ParseError", "Invalid JSON", 42);
 *   // "[ParseError] Invalid JSON at line 42"
 * @endcode
 */
[[nodiscard]] inline std::string FormatError(
    std::string_view error_type,
    std::string_view message,
    size_t line = 0) {
    if (line > 0) {
        return std::format("[{}] {} at line {}", error_type, message, line);
    }
    return std::format("[{}] {}", error_type, message);
}

/**
 * @brief Format a configuration error
 * @param field Field name that failed validation
 * @param reason Reason for validation failure
 * @param value Optional value that was invalid
 * @return Formatted error message
 *
 * Phase 5: Structured config error messages
 *
 * @code
 *   std::string s = FormatConfigError("metrics_height", "less than minimum", 5);
 *   // "Configuration error in metrics_height: less than minimum (value: 5)"
 * @endcode
 */
[[nodiscard]] inline std::string FormatConfigError(
    std::string_view field,
    std::string_view reason,
    int64_t value = 0) {
    if (value != 0) {
        return std::format("Configuration error in {}: {} (value: {})",
                         field, reason, value);
    }
    return std::format("Configuration error in {}: {}", field, reason);
}

/**
 * @brief Format a metric display with name, value, and unit
 * @param name Metric name
 * @param value Metric value
 * @param unit Unit of measurement (e.g., "ms", "bytes", "%")
 * @param precision Decimal places (default: 2)
 * @return Formatted metric (e.g., "Task Time: 5.20 ms")
 *
 * Phase 5: Standard metric display pattern
 *
 * @code
 *   std::string s = FormatMetric("Task Time", 5.2, "ms");
 *   // "Task Time: 5.20 ms"
 * @endcode
 */
template<typename T>
[[nodiscard]] std::string FormatMetric(
    std::string_view name,
    T value,
    std::string_view unit,
    int precision = 2) {
    return std::format("{}: {:.{}f} {}", name, value, precision, unit);
}

/**
 * @brief Format a capacity adjustment message
 * @param hit_rate Current hit rate percentage
 * @param old_capacity Previous capacity
 * @param new_capacity New capacity
 * @return Formatted message
 *
 * Phase 5: Specific pattern for AdaptiveCapacityMonitor logs
 *
 * @code
 *   std::string s = FormatCapacityAdjustment(95.5, 1000, 1500);
 *   // "Hit rate: 95.5%, adjusting capacity from 1000 to 1500"
 * @endcode
 */
[[nodiscard]] inline std::string FormatCapacityAdjustment(
    double hit_rate,
    size_t old_capacity,
    size_t new_capacity) {
    return std::format("Hit rate: {:.1f}%, adjusting capacity from {} to {}",
                      hit_rate, old_capacity, new_capacity);
}

/**
 * @brief Format a hexadecimal value
 * @param value Integer value to format
 * @param uppercase Use uppercase (A-F) instead of lowercase (a-f)
 * @param prefix Include "0x" prefix
 * @return Formatted hex string
 *
 * Phase 5: Cleaner hex formatting than manual string building
 *
 * @code
 *   std::string s = FormatHex(255);        // "0xff"
 *   std::string s = FormatHex(255, true);  // "0xFF"
 * @endcode
 */
template<typename T>
[[nodiscard]] std::string FormatHex(T value, bool uppercase = false) {
    if (uppercase) {
        return std::format("0x{:X}", value);
    } else {
        return std::format("0x{:x}", value);
    }
}

/**
 * @brief Format a thread ID or identifier
 * @param id Identifier value
 * @return Formatted ID string (e.g., "[ID: 0x123abc]")
 *
 * Phase 5: Consistent ID display in logs
 */
template<typename T>
[[nodiscard]] std::string FormatId(T id) {
    return std::format("[ID: 0x{:x}]", id);
}

/**
 * @brief Format a status message
 * @param status Status string
 * @param detail Optional additional detail
 * @return Formatted status (e.g., "Status: RUNNING (detail)")
 *
 * Phase 5: Consistent status display
 *
 * @code
 *   std::string s = FormatStatus("RUNNING");
 *   std::string s = FormatStatus("ERROR", "Connection timeout");
 * @endcode
 */
[[nodiscard]] inline std::string FormatStatus(
    std::string_view status,
    std::string_view detail = "") {
    if (detail.empty()) {
        return std::format("Status: {}", status);
    }
    return std::format("Status: {} ({})", status, detail);
}

}  // namespace app::format
