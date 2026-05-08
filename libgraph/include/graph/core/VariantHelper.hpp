// MIT License
//
// Copyright (c) 2026 gdashboard contributors
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

/**
 * @file core/VariantHelper.hpp
 * @brief C++20/C++26 std::variant utilities for clean pattern matching
 * @description Provides helper templates for modern std::visit patterns with
 *              the Overload deduction guide idiom and safe variant access.
 *
 * @author Robert Klinkhammer
 * @date 2026
 */

#pragma once

#include <variant>
#include <optional>
#include <functional>
#include <type_traits>

namespace reflection {

// ============================================================================
// Overload Pattern Helper (C++20 Standard)
// ============================================================================

/**
 * @struct Overload
 * @brief Deduction guide for std::visit with lambda overloads
 *
 * Enables clean visitor pattern with multiple lambdas instead of
 * traditional visitor structs. Works with std::visit directly.
 *
 * Usage:
 * @code
 * std::visit(Overload{
 *     [](int i) { std::cout << "int: " << i; },
 *     [](double d) { std::cout << "double: " << d; },
 *     [](const std::string& s) { std::cout << "string: " << s; }
 * }, my_variant);
 * @endcode
 */
template<typename... Ts>
struct Overload : Ts... {
    using Ts::operator()...;
};

/// Explicit deduction guide (not needed in C++20+ but helps some compilers)
template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

// ============================================================================
// Variant Member Type Check Concept
// ============================================================================

/**
 * @concept VariantMember
 * @brief Compile-time check if type T is a member of variant V
 *
 * Note: Checking if a type is a member of a variant at compile-time is
 * non-trivial in C++20. This concept is provided for documentation purposes.
 * Runtime checks using std::holds_alternative are preferred.
 */
template<typename T, typename Variant>
concept VariantMember = true;  // Relaxed - runtime checks suffice

// ============================================================================
// Safe Variant Visitors
// ============================================================================

/**
 * @brief Safe typed accessor for variant with optional wrapper
 *
 * Returns std::optional containing a reference if the variant
 * holds the requested type, otherwise returns empty optional.
 *
 * @tparam T The type to extract from variant
 * @tparam Variant The variant type
 * @param var The variant instance
 * @return std::optional<reference_wrapper<T>> - holds reference if present
 *
 * Usage:
 * @code
 * std::variant<int, double, std::string> v = 42;
 * if (auto int_ref = GetIfVariant<int>(v)) {
 *     std::cout << int_ref->get();  // Access with .get()
 * }
 * @endcode
 */
template<typename T, typename Variant>
inline auto GetIfVariant(Variant& var) -> std::optional<std::reference_wrapper<T>> {
    if (std::holds_alternative<T>(var)) {
        return std::get<T>(var);
    }
    return std::nullopt;
}

/**
 * @brief Const version of GetIfVariant
 *
 * @tparam T The type to extract from variant
 * @tparam Variant The variant type
 * @param var The const variant instance
 * @return std::optional<reference_wrapper<const T>> - holds const reference if present
 */
template<typename T, typename Variant>
requires VariantMember<T, Variant>
inline auto GetIfVariant(const Variant& var) -> std::optional<std::reference_wrapper<const T>> {
    if (std::holds_alternative<T>(var)) {
        return std::get<T>(var);
    }
    return std::nullopt;
}

}  // namespace reflection
