// MIT License
/// @file core/RangesUtilities.hpp
/// @brief C++26 std::ranges adapters and helpers for modern algorithms

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

#include <ranges>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

// ===================================================================================
// Modern C++26 std::ranges Utilities and Adapters
// -----------------------------------------------------------------------------------
// Provides composable range views and algorithms for cleaner, more efficient code
// than traditional for-loops. Eliminates manual iterator management and intermediate
// vectors through lazy evaluation and view composition.
// ===================================================================================

namespace app::ranges {

/**
 * @brief Check if any element in range matches predicate (modern syntax)
 *
 * Cleaner alternative to std::any_of with ranges support.
 *
 * Usage:
 * ```cpp
 * auto has_error = AnyOf(metrics, [](const auto& m) { 
 *     return m.alert_level > 0; 
 * });
 * ```
 */
template<std::ranges::input_range R, typename Pred>
requires std::invocable<Pred, std::ranges::range_value_t<R>>
constexpr bool AnyOf(R&& range, Pred pred) {
    return std::ranges::any_of(range, pred);
}

/**
 * @brief Check if all elements in range match predicate
 *
 * Usage:
 * ```cpp
 * auto all_ready = AllOf(plugins, [](const auto& p) { 
 *     return p.IsCompliant(); 
 * });
 * ```
 */
template<std::ranges::input_range R, typename Pred>
requires std::invocable<Pred, std::ranges::range_value_t<R>>
constexpr bool AllOf(R&& range, Pred pred) {
    return std::ranges::all_of(range, pred);
}

/**
 * @brief Find first element matching predicate
 *
 * Returns iterator to first matching element or range.end() if none found.
 *
 * Usage:
 * ```cpp
 * auto it = FindIf(commands, [](const auto& cmd) { 
 *     return cmd.name == "help"; 
 * });
 * if (it != commands.end()) { }  // found
 * ```
 */
template<std::ranges::input_range R, typename Pred>
requires std::invocable<Pred, std::ranges::range_value_t<R>>
constexpr auto FindIf(R&& range, Pred pred) {
    return std::ranges::find_if(range, pred);
}

/**
 * @brief Count elements matching predicate
 *
 * Usage:
 * ```cpp
 * size_t critical_count = CountIf(metrics, [](const auto& m) { 
 *     return m.alert_level == 2; 
 * });
 * ```
 */
template<std::ranges::input_range R, typename Pred>
requires std::invocable<Pred, std::ranges::range_value_t<R>>
constexpr std::ranges::range_difference_t<R> CountIf(R&& range, Pred pred) {
    return std::ranges::count_if(range, pred);
}

/**
 * @brief Apply predicate to each element (for side effects)
 *
 * Useful for logging, updating state, etc.
 *
 * Usage:
 * ```cpp
 * ForEach(commands, [](auto& cmd) { cmd.last_used = now; });
 * ```
 */
template<std::ranges::input_range R, typename F>
requires std::invocable<F, std::ranges::range_value_t<R>>
constexpr void ForEach(R&& range, F func) {
    std::ranges::for_each(range, func);
}

/**
 * @brief Transform and collect results into container
 *
 * Collects results of transformation into a vector.
 *
 * Usage:
 * ```cpp
 * auto names = TransformTo<std::vector>(plugins, [](const auto& p) { 
 *     return p.name; 
 * });
 * ```
 */
/**
 * @class Container
 * @brief Container implementation for GraphX.
 */
template<template<typename> class Container, std::ranges::input_range R, typename F>
requires std::invocable<F, std::ranges::range_value_t<R>>
auto TransformTo(R&& range, F transform_fn) {
    using result_type = std::invoke_result_t<F, std::ranges::range_value_t<R>>;
    Container<result_type> result;
    
    for (auto&& elem : range) {
        result.push_back(transform_fn(elem));
    }
    return result;
}

/**
 * @brief Filter and collect matching elements
 *
 * Collects all elements matching predicate into a vector.
 *
 * Usage:
 * ```cpp
 * auto compliant = FilterTo<std::vector>(plugins, [](const auto& p) { 
 *     return p.IsCompliant(); 
 * });
 * ```
 */
template<template<typename> class Container, std::ranges::input_range R, typename Pred>
requires std::invocable<Pred, std::ranges::range_value_t<R>>
auto FilterTo(R&& range, Pred filter_fn) {
    using elem_type = std::ranges::range_value_t<R>;
    Container<elem_type> result;
    
    for (auto&& elem : range) {
        if (filter_fn(elem)) {
            result.push_back(elem);
        }
    }
    return result;
}

/**
 * @brief Case-insensitive substring match
 *
 * Useful for filtering commands and metrics by name pattern.
 *
 * Usage:
 * ```cpp
 * auto matching = FilterTo<std::vector>(commands, [pattern](const auto& cmd) {
 *     return ISubstringMatch(cmd.name, pattern);
 * });
 * ```
 */
inline bool ISubstringMatch(std::string_view text, std::string_view pattern) {
    // Convert to lowercase and check substring
/**
 * @brief Lower text.
 * @param text Parameter for lower text.
 * @return Result of the operation.
 */
    std::string lower_text(text);
/**
 * @brief Lower pattern.
 * @param pattern Parameter for lower pattern.
 * @return Result of the operation.
 */
    std::string lower_pattern(pattern);
    
    std::ranges::transform(lower_text, lower_text.begin(), ::tolower);
    std::ranges::transform(lower_pattern, lower_pattern.begin(), ::tolower);
    
    return lower_text.find(lower_pattern) != std::string::npos;
}

/**
 * @brief Filter by prefix (case-insensitive)
 *
 * Useful for command autocompletion and name-based filtering.
 *
 * Usage:
 * ```cpp
 * auto starting_with = FilterTo<std::vector>(commands, [prefix](const auto& cmd) {
 *     return StartsWithI(cmd.name, prefix);
 * });
 * ```
 */
inline bool StartsWithI(std::string_view text, std::string_view prefix) {
    if (prefix.length() > text.length()) return false;
    
    return std::ranges::equal(
        text | std::views::take(prefix.length()),
        prefix,
        [](unsigned char a, unsigned char b) {
            return ::tolower(a) == ::tolower(b);
        }
    );
}

/**
 * @brief Group consecutive elements by key function
 *
 * Advanced: creates a vector of groups where each group contains
 * consecutive elements with the same key.
 *
 * Usage:
 * ```cpp
 * auto by_type = GroupBy(metrics, [](const auto& m) { 
 *     return m.type; 
 * });
 * ```
 */
template<std::ranges::input_range R, typename KeyFn>
requires std::invocable<KeyFn, std::ranges::range_value_t<R>>
auto GroupBy(R&& range, KeyFn key_fn) {
    using elem_type = std::ranges::range_value_t<R>;
    using key_type = std::invoke_result_t<KeyFn, elem_type>;
    
    std::vector<std::pair<key_type, std::vector<elem_type>>> groups;
    
    for (auto&& elem : range) {
        auto key = key_fn(elem);
        
        if (groups.empty() || groups.back().first != key) {
            groups.push_back({key, {}});
        }
        groups.back().second.push_back(elem);
    }
    
    return groups;
}

}  // namespace app::ranges

// ============================================================================
// Common Range Composition Patterns (C++26 Modern Usage)
// ============================================================================
//
// **Pattern 1: Filter multiple conditions**
// ```cpp
// using namespace std::ranges;
// auto critical = commands 
//     | views::filter([](const auto& cmd) { return cmd.priority > 5; })
//     | views::filter([](const auto& cmd) { return !cmd.deprecated; });
// ```
//
// **Pattern 2: Transform filtered results**
// ```cpp
// auto names = plugins
//     | views::filter([](const auto& p) { return p.IsCompliant(); })
//     | views::transform([](const auto& p) { return p.name; });
// ```
//
// **Pattern 3: Early termination with any_of/all_of**
// ```cpp
// bool has_warnings = ranges::any_of(metrics, 
//     [](const auto& m) { return m.alert_level == 1; });
// ```
//
// **Pattern 4: Collect filtered results**
// ```cpp
// auto matching = metrics
//     | views::filter([pattern](const auto& m) { 
//         return ISubstringMatch(m.name, pattern); 
//     })
//     | ranges::to<std::vector>();
// ```
//
// **Benefits over traditional loops:**
// - **Lazy evaluation**: Views don't process data until consumed
// - **Composable**: Chain multiple operations expressively
// - **No temporaries**: Avoid intermediate vectors
// - **Performance**: Compiler optimizes range chains better
// - **Readable**: Algorithms explicit in code flow
// - **Type-safe**: Template instantiations provide compile-time checking
//
// **When to use**:
// - Filtering with multiple conditions
// - Transforming before filtering (or vice versa)
// - Counting/finding elements with predicates
// - Building results from filtered/transformed ranges
//
// **When NOT to use**:
// - Simple iteration for side effects (use ranges::for_each)
// - Single-element access (use find_if + bounds check)
// - Modifying elements in place (use ranges::sort, ranges::transform)
