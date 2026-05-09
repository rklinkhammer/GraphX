// MIT License
/// @file core/TypeInfo.hpp
/// @brief TypeInfo interface and implementation

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

#include <string_view>
#include <cstdint>
#include <typeinfo>
#include "core/ReflectionHelper.hpp"

// ===================================================================================
// Utilities: compile-time type-name helper with C++26 Reflection-Ready Pattern
// -----------------------------------------------------------------------------------
// Uses compile-time type name extraction prepared for std::reflect migration.
// Supports all types including void, const-qualified, and container types.
// ===================================================================================

// Universal type name extraction (reflection-ready for all types)
template<typename T>
consteval std::string_view type_name_const() {
    // Use metadata extraction (works with reflection when available)
    // For now, uses compile-time type name extraction
    return reflection::ExtractTypeNameFromFunction<T>();
}

// Helper to trim whitespace from type names
consteval std::string_view rtrim(std::string_view sv) {
    while (!sv.empty()) {
        char c = sv.back();
        if (c == ']' || c == ' ' || c == '\t')
            sv.remove_suffix(1);
        else
            break;
    }
    return sv;
}

template <typename T>
consteval std::string_view type_name_const2() {
#if defined(__clang__) || defined(__GNUC__)
    constexpr std::string_view fn = __PRETTY_FUNCTION__;
    return fn;

#else
    return "type";
#endif
}

template <typename T>
inline constexpr std::string_view type_name_v = type_name_const<T>();

// ===================================================================================
// Message: type-erased, move-only container with small-object optimization (SSO)
// -----------------------------------------------------------------------------------
// - Stores small objects inline (SSO) up to SSO_SIZE bytes, otherwise heap allocates.
// - Non-copyable and movable only to avoid accidental costly copies.
// - Clients use Message::make<T>(value) to construct.
// ===================================================================================

struct TypeInfo {
    std::string_view name{};
    uint64_t hash{0};
    template<typename T>
    static TypeInfo create() {
        return { typeid(T).name(), static_cast<uint64_t>(typeid(T).hash_code()) };
    }
    bool is_valid() const noexcept { return hash != 0; }
};

constexpr std::string_view StripNamespace(std::string_view name)
{
    size_t pos = name.rfind("::");
    return (pos == std::string_view::npos)
        ? name
        : name.substr(pos + 2);
}

template <typename T>
constexpr std::string_view TypeName() {
    return StripNamespace(type_name_const<T>());
}

