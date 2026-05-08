// MIT License
/// @file core/Expected.hpp
/// @brief C++23 std::expected<T, E> wrapper for error handling

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

#pragma once

#include <expected>
#include <utility>
#include <optional>
#include <type_traits>

// ===================================================================================
// Error Handling: C++23 std::expected<T, E> Wrapper
// -----------------------------------------------------------------------------------
// Provides type-safe, exception-free error handling for C++26
// Replaces try-catch-return patterns with composable error types
// ===================================================================================

namespace app::error {

/**
 * @brief Generic error result type for operations
 * 
 * Replaces try-catch patterns with type-safe error codes:
 * ```cpp
 * std::expected<void, OperationError> result = DoSomething();
 * if (!result) {
 *     auto err = result.error();
 *     LOG_ERROR("Operation failed: " << static_cast<int>(err));
 * }
 * ```
 */
template<typename T = void, typename E = int>
using Result = std::expected<T, E>;

/**
 * @brief Helper: Create an error result
 * 
 * Usage:
 * ```cpp
 * enum class MyError { Failed };
 * std::expected<int, MyError> GetValue() {
 *     return Err(MyError::Failed);
 * }
 * ```
 */
template<typename E>
constexpr std::unexpected<E> Err(E&& error) {
    return std::unexpected<E>(std::forward<E>(error));
}

}  // namespace app::error

// Convenience type aliases for common error types
namespace app::error {

/// Result with string error message
template<typename T = void>
using StringResult = Result<T, std::string>;

/// Result with integer error code
template<typename T = void>
using ErrorCodeResult = Result<T, int>;

/// Result with void return and string error
using VoidResult = Result<void, std::string>;

}  // namespace app::error

// ============================================================================
// Usage Guide for std::expected<T, E> in C++23+
// ============================================================================
//
// std::expected<T, E> provides type-safe error handling without exceptions.
// It's available as a member function, so you can chain operations directly:
//
// **Basic usage:**
// ```cpp
// std::expected<int, std::string> result = Parse(str);
// if (result) {
//     std::cout << "Value: " << *result << std::endl;
// } else {
//     std::cerr << "Error: " << result.error() << std::endl;
// }
// ```
//
// **Chaining with and_then():**
// ```cpp
// auto result = Parse(str)
//     .and_then([](int x) { return Validate(x); })
//     .and_then([](int x) { return Store(x); });
// ```
//
// **Transforming values with transform():**
// ```cpp
// auto doubled = Parse(str)
//     .transform([](int x) { return x * 2; });
// ```
//
// **Providing fallback with or_else():**
// ```cpp
// auto result = Parse(str)
//     .or_else([](auto err) { 
//         LOG_ERROR("Parse failed, using default");
//         return std::expected(42);
//     });
// ```
//
// **Extracting value safely:**
// ```cpp
// int value = result.value_or(0);  // Default to 0 if error
// ```
//
// See: https://en.cppreference.com/w/cpp/utility/expected

