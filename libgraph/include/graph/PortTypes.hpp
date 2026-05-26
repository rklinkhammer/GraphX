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
 * @file PortTypes.hpp
 * @brief Port type definitions and runtime reflection for the Active Graph framework
 * 
 * This header provides:
 * - Compile-time port machinery (TypeList, Port, MakePorts)
 * - Runtime port reflection (PortInfo, PortMetadata)
 * - Port direction enumeration
 * 
 * These types are used throughout the node framework to define typed input/output ports.
 */

#pragma once

#include <cstddef>
#include <string_view>
#include <string>
#include <array>
#include "core/TypeInfo.hpp"
#include "graph/PortSpec.hpp"

namespace graph
{

    // PortDirection is defined in PortSpec.hpp, no need to redefine it


    // ===================================================================================
    // Port / TypeList / MakePorts - Compile-time Type Machinery
    // -----------------------------------------------------------------------------------
    // This section provides compile-time tools for working with type lists and ports.
    //
    // TypeList<Ts...>: A parameter pack container used throughout the framework
    // Port<T, ID>: Associates a type T with a unique port identifier ID
    // MakePorts: Transforms TypeList<T1, T2, ...> into TypeList<Port<T1,0>, Port<T2,1>, ...>
    //
    // Example:
    //   TypeList<int, double> -> MakePorts -> TypeList<Port<int,0>, Port<double,1>>
    // ===================================================================================

    /**
     * @brief Container for a parameter pack of types
     * @tparam Ts Variadic template parameter pack
     *
     * TypeList is used throughout the framework to pass collections of types
     * at compile time without instantiating any values.
     */
    template <typename... Ts>
    struct TypeList
    {
    };

    // Helper to repeat a type N times
    template <typename T, std::size_t N, typename = std::make_index_sequence<N>>
    struct RepeatType;

    template <typename T, std::size_t N, std::size_t... Is>
    struct RepeatType<T, N, std::index_sequence<Is...>>
    {
        template <std::size_t>
        using type_identity = T;

        using type = TypeList<type_identity<Is>...>;
    };

    template <typename T, std::size_t N>
    using RepeatType_t = typename RepeatType<T, N>::type;

    template <typename List>
    struct MakePorts; // forward

    /**
     * @brief Associates a data type with a unique port identifier
     * @tparam T The data type that flows through this port
     * @tparam ID Unique compile-time port identifier (0-based index)
     *
     * Port serves as a compile-time tag that binds a data type to a specific
     * port number. This enables type-safe port connections and allows the
     * compiler to verify that connected ports have compatible types.
     */
    template <typename T, std::size_t ID>
    struct Port
    {
        using type = T;                       ///< The data type for this port
        static constexpr std::size_t id = ID; ///< The port identifier
    };

    template <typename... Ts>
    struct MakePorts<TypeList<Ts...>>
    {
    private:
        template <typename U, std::size_t ID>
        using PortT = Port<U, ID>;
        template <std::size_t... Is>
        static auto make(std::index_sequence<Is...>) -> TypeList<PortT<Ts, Is>...>;

    public:
        using type = decltype(make(std::index_sequence_for<Ts...>{}));
    };

    // ===================================================================================
    // Runtime Reflection - PortInfo and Port Tables
    // -----------------------------------------------------------------------------------
    // While port types are checked at compile time, runtime reflection is needed for:
    // - Dynamic graph construction and validation
    // - Debugging and visualization tools
    // - Error reporting with human-readable type names
    //
    // PortInfo provides runtime-accessible port metadata including:
    // - Port ID
    // - Type name (via compile-time string generation)
    // - Direction (Input or Output)
    // ===================================================================================

    /**
     * @brief Runtime descriptor for a node port
     *
     * Contains metadata about a port that can be accessed at runtime,
     * enabling dynamic inspection, validation, and debugging.
     */
    struct PortInfo
    {
        std::size_t id;             ///< Port identifier (matches compile-time Port<T,ID>::id)
        std::string_view type_name; ///< Human-readable type name (e.g., "int", "std::string")
        PortDirection direction;    ///< Whether this is an input or output port
    };

    template <typename P, PortDirection Dir>
    consteval PortInfo make_port_info()
    {
        return PortInfo{
            .id = P::id,
            .type_name = type_name_v<typename P::type>,
            .direction = Dir};
    }

    template <PortDirection Dir, typename... Ports>
    consteval auto build_port_table(TypeList<Ports...>)
    {
        return std::array{make_port_info<Ports, Dir>()...};
    }

    /**
     * @brief Port metadata for visualization serialization
     * 
     * Used by the reflection system to capture input/output port information
     * for JSON export and real-time visualization.
     */
    struct PortMetadata {
        std::size_t port_index;       ///< Port index (0-based)
        std::string payload_type;     // "AccelerometerData|GyroData" or "IqSample<float>"
        std::string direction;        ///< "input" or "output"
        std::string port_name;        ///< Type name (e.g., "int", "double")
    };

    // Forward declarations needed for MakePortMetadata
    namespace message { class Message; }
    
    template <typename Port>
    PortMetadata MakePortMetadata() {
        using T = typename Port::transport_type;

        // Check if it's a Message type
        if constexpr (requires { typename std::enable_if<std::is_same_v<T, message::Message>>::type; }) {
            return {
                Port::index,
                "Message", // Placeholder - actual implementation in Message.hpp
                Port::direction == PortDirection::Input ? "input" : "output",
                std::string(Port::name)
            };
        } else {
            return {
                Port::index,
                std::string(TypeName<T>()),                
                Port::direction == PortDirection::Input ? "input" : "output",
                std::string(Port::name)
            };
        }
    }

    template <typename Derived>
    struct HasPorts {
        template <typename T>
        static auto test(int) -> decltype(typename T::Ports{}, std::true_type{});

        template <typename>
        static std::false_type test(...);

        static constexpr bool value = decltype(test<Derived>(0))::value;
    };

} // namespace graph

