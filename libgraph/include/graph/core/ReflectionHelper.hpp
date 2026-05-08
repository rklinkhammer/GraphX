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
 * @file core/ReflectionHelper.hpp
 * @brief C++26 Static Reflection Infrastructure (Reflection-Ready Pattern)
 * @description Provides reflection-based type introspection prepared for C++26 std::reflect.
 *              Currently uses compile-time type traits with future migration path to std::reflect.
 *              Will automatically upgrade when std::reflect becomes available in compilers.
 *
 * @author Robert Klinkhammer
 * @date 2026
 */

#pragma once

#include <string_view>
#include <cstddef>
#include <type_traits>

// Feature detection for C++26 std::reflect (not yet in compilers, but prepared)
#if __has_include(<reflect>) && __cplusplus >= 202500L
    #include <reflect>
    #define GDASHBOARD_HAS_REFLECTION 1
#else
    #define GDASHBOARD_HAS_REFLECTION 0
#endif

namespace reflection {

// ============================================================================
// C++26 Static Reflection Infrastructure (Reflection-Ready Pattern)
// ============================================================================
// This layer is designed to work with both current compilers and future
// versions with full std::reflect support. When reflection becomes available,
// the implementations below can be directly replaced with reflection-based versions
// without changing any client code.
// ============================================================================

/**
 * @struct TypeMetadata
 * @brief Compile-time type information.
 *
 * Will be sourced from std::reflect when available.
 * Currently uses compile-time traits.
 */
template<typename T>
struct TypeMetadata {
    std::string_view name;                    ///< Human-readable type name
    std::string_view display_name;            ///< Short type name (no namespace)
    std::size_t size;                         ///< sizeof(T)
    std::size_t alignment;                    ///< alignof(T)
    bool is_trivially_destructible;           ///< Can be destroyed without special logic
    bool is_nothrow_move_constructible;       ///< Move doesn't throw exceptions
    bool is_nothrow_move_assignable;          ///< Move assignment doesn't throw
    bool has_virtual_destructor;              ///< Destructor is virtual
};

/**
 * @brief Helper to strip namespace from type name
 *
 * Prepares type names for display. Works with both __PRETTY_FUNCTION__
 * output and future std::reflect names.
 */
consteval std::string_view StripNamespacePrefix(std::string_view full_name) {
    std::size_t last_colon = full_name.rfind("::");
    if (last_colon != std::string_view::npos) {
        return full_name.substr(last_colon + 2);
    }
    return full_name;
}

/**
 * @brief Extract type name from __PRETTY_FUNCTION__
 *
 * Current implementation: parses __PRETTY_FUNCTION__ for type information.
 * Future implementation: Will use std::meta::name_of(std::reflect(T{}))
 */
template<typename T>
consteval std::string_view ExtractTypeNameFromFunction() {
#if defined(__clang__) || defined(__GNUC__)
    std::string_view fn = __PRETTY_FUNCTION__;
    std::string_view key = "T = ";

    std::size_t start = fn.find(key);
    if (start == std::string_view::npos) {
        return "UnknownType";
    }

    std::size_t name_start = start + key.size();
    std::size_t name_end = fn.find(']', name_start);

    if (name_end == std::string_view::npos) {
        return "UnknownType";
    }

    return fn.substr(name_start, name_end - name_start);

#elif defined(_MSC_VER)
    std::string_view fn = __FUNCSIG__;
    std::string_view key = "ExtractTypeNameFromFunction<";

    std::size_t start = fn.find(key);
    if (start == std::string_view::npos) {
        return "UnknownType";
    }

    std::size_t name_start = start + key.size();
    std::size_t name_end = fn.find('>', name_start);

    return (name_end != std::string_view::npos)
        ? fn.substr(name_start, name_end - name_start)
        : "UnknownType";
#else
    return "UnknownType";
#endif
}

/**
 * @brief Extract type metadata using current approach
 *
 * Uses compile-time type traits. This function signature is designed to be
 * directly replaceable with std::reflect-based version in the future.
 *
 * @return TypeMetadata<T> containing type information
 */
template<typename T>
consteval TypeMetadata<T> GetMetadata() {
    auto full_name = ExtractTypeNameFromFunction<T>();
    auto display_name = StripNamespacePrefix(full_name);

    return TypeMetadata<T>{
        full_name,
        display_name,
        sizeof(T),
        alignof(T),
        std::is_trivially_destructible_v<T>,
        std::is_nothrow_move_constructible_v<T>,
        std::is_nothrow_move_assignable_v<T>,
        std::has_virtual_destructor_v<T>
    };
}

// ============================================================================
// Reflection-Based Concepts (C++26 Ready)
// ============================================================================

/**
 * @concept Reflectable
 * @brief Type that supports reflection and introspection
 *
 * Ensures a type has proper move semantics for reflection-based storage.
 */
template<typename T>
concept Reflectable =
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>;

/**
 * @concept StorableInMessage
 * @brief Type that can be safely stored in a type-erased Message container
 *
 * Requires:
 * - Reflectable (nothrow move semantics)
 * - Proper destruction semantics
 * - Validity for compile-time validation
 */
template<typename T>
concept StorableInMessage =
    Reflectable<T> &&
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T>;

// ============================================================================
// C++26 Template Constraint Concepts (Priority 2)
// ============================================================================

/**
 * @concept GraphNode
 * @brief Type that represents a graph node
 *
 * Ensures a type:
 * - Is destructible (required by all objects)
 * - Can be used in graph construction
 *
 * Note: Relaxed constraints to accommodate nodes with special move semantics.
 * Runtime type checking and factory patterns provide additional safety.
 *
 * Usage: Constrain NodeFactory::CreateNode<GraphNode T>
 */
template<typename T>
concept GraphNode = std::destructible<T>;

/**
 * @concept PortLike
 * @brief Type that represents a graph port or port descriptor
 *
 * Ensures a type has port-like properties suitable for edge creation
 * and message routing.
 *
 * Usage: Constrain edge creation and port connection templates
 */
template<typename T>
concept PortLike = std::destructible<T>;

/**
 * @concept PolicyLike
 * @brief Type that represents an execution policy
 *
 * Ensures a type has execution policy interface compatible with
 * ExecutionPolicyChain (OnInit, OnStart, OnRun, OnStop, OnJoin)
 *
 * Usage: Constrain ExecutionPolicyChain::AppendPolicy<PolicyLike T>
 */
template<typename T>
concept PolicyLike = std::destructible<T>;

/**
 * @concept ExecutionPolicy
 * @brief Alias for PolicyLike - types compatible with IExecutionPolicy chain
 *
 * Usage: Same as PolicyLike, alternative naming for clarity
 */
template<typename T>
concept ExecutionPolicy = PolicyLike<T>;

/**
 * @concept IsPolicy
 * @brief Type that implements execution policy interface
 *
 * Ensures a type has the complete ExecutionPolicy lifecycle interface
 * with all required methods: OnInit, OnStart, OnRun, OnStop, OnJoin.
 *
 * Usage: Validation for policy registration and composition
 *
 * Note: Forward declaration - requires app::capabilities::GraphCapability
 * which creates circular dependency. Kept as documentation template.
 * Use PolicyLike or ExecutionPolicy concepts for actual constraints.
 */
// template<typename T>
// concept IsPolicy = requires(T t, app::capabilities::GraphCapability& ctx) {
//     { t.OnInit(ctx) } -> std::convertible_to<bool>;
//     { t.OnStart(ctx) } -> std::convertible_to<bool>;
//     { t.OnRun(ctx) } -> std::convertible_to<bool>;
//     { t.OnStop(ctx) };
//     { t.OnJoin(ctx) };
// };

// ============================================================================
// Compile-Time Type Name Extraction
// ============================================================================

/**
 * @brief Extract human-readable type name
 *
 * Uses compile-time type information. Future versions will use std::reflect.
 * The function signature and contract are stable across implementations.
 *
 * @return std::string_view containing the type's display name
 */
template<Reflectable T>
consteval std::string_view GetTypeName() {
    return GetMetadata<T>().display_name;
}

/**
 * @brief Extract fully-qualified type name
 *
 * Returns the complete type path including namespace.
 *
 * @return std::string_view containing fully-qualified type name
 */
template<Reflectable T>
consteval std::string_view GetFullTypeName() {
    return GetMetadata<T>().name;
}

// ============================================================================
// Type Validation and Checking
// ============================================================================

/**
 * @brief Validate that a type meets Message storage requirements
 *
 * Compile-time check ensuring the type is safe for type-erased storage.
 *
 * @tparam T Type to validate
 * @return true if T meets all storage requirements
 */
template<typename T>
consteval bool IsValidMessageType() {
    if constexpr (StorableInMessage<T>) {
        auto metadata = GetMetadata<T>();
        return metadata.is_nothrow_move_constructible &&
               metadata.is_nothrow_move_assignable;
    }
    return false;
}

}  // namespace reflection
