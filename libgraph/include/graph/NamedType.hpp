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

/**
 * @file NamedType.hpp
 * @brief CRTP mixin for named type functionality
 * 
 * Provides compile-time and runtime type name information via CRTP.
 */

#pragma once

#include <string>
#include "core/TypeInfo.hpp"

namespace graph
{

    /**
     * @brief CRTP mixin providing named type functionality
     * @tparam Derived The derived class (using CRTP pattern)
     *
     * Provides a static type name and instance method to get the node type name.
     * Useful for runtime type identification and logging.
     */
    template <typename Derived>
    class NamedType {
    public:
        static const std::string& QualifiedTypeName() noexcept {
            static const std::string name = []{
                constexpr auto sv = type_name_const<Derived>();
                return std::string(sv);
                }();
            return name;
        }

        static const std::string& TypeName() noexcept {
            static const std::string name = []{
                constexpr auto sv = StripNamespace(type_name_const<Derived>());
                return std::string(sv);
                }();
            return name;
        }

        const std::string GetNodeTypeName() const noexcept { return TypeName(); }
        const std::string GetQualifiedTypeName() const noexcept { return QualifiedTypeName();  }
        
        const std::string GetName() const noexcept { return name_; }
        void SetName(std::string name) { name_ = std::move(name); }

    protected:
        explicit NamedType(std::string name = {}) : name_(std::move(name)) {}

    private:
        std::string name_;
    };

} // namespace graph

