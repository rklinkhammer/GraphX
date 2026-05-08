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
 * @file PortSpec.hpp
 * @brief Port specification and metadata templates for compile-time port definitions
 *
 * Provides compile-time metaprogramming utilities for defining and introspecting
 * input and output port specifications. This includes:
 *
 * - **PortDirection enum**: Marks ports as Input or Output
 * - **PayloadList<Ts...>**: Parameter pack container for port payload types
 * - **PortSpec**: Compile-time port definition combining direction, transport, and payloads
 * - **TransportName()**: Constexpr type name extraction for transport types
 * - **PayloadListToString()**: Runtime conversion of payload types to pipe-delimited string
 *
 * Used throughout the framework for type-safe port definitions and reflection.
 *
 * @author GraphX Contributors
 * @date 2025-12-31
 * @version 1.0
 *
 * @see Nodes.hpp for usage in node port specifications
 * @see core/TypeInfo.hpp for type naming utilities
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include "core/TypeInfo.hpp"

namespace graph {

// ===================================================================================
// Port Direction Enumeration
// -----------------------------------------------------------------------------------
// Marks whether a port is an input or output port.
// ===================================================================================

/**
 * @enum PortDirection
 * @brief Indicates whether a port receives (Input) or produces (Output) data
 *
 * Used in PortSpec to define the direction of data flow through the port.
 */
enum class PortDirection {
    Input,   ///< Port receives data from connected nodes
    Output   ///< Port produces data to connected nodes
};

// ===================================================================================
// Payload Type List
// -----------------------------------------------------------------------------------
// Metaprogramming utility for storing a type list of payloads that can flow
// through a port. Each payload represents a distinct data type that may appear
// in a port message.
// ===================================================================================

/**
 * @struct PayloadList
 * @brief Compile-time container for a list of payload types
 *
 * Used as a template parameter in PortSpec to specify which data types
 * can flow through a port. The size is available as a compile-time constant.
 *
 * @tparam Ts Type list of payloads (can be empty for ports with no payloads)
 *
 * Example:
 * @code
 * using SensorPayloads = PayloadList<AccelerometerData, GyroData>;
 * static_assert(SensorPayloads::size == 2);
 * @endcode
 */
template <typename... Ts>
struct PayloadList {
    /// Number of payload types in the list
    static constexpr std::size_t size = sizeof...(Ts);
};

// ===================================================================================
// Port Specification
// -----------------------------------------------------------------------------------
// Complete compile-time definition of a port, including its index, transport type,
// direction, name, and payload types. All information is available at compile time
// for use in static analysis and type checking.
// ===================================================================================

/**
 * @struct PortSpec
 * @brief Compile-time specification of a port with complete metadata
 *
 * Captures all information needed to define a port at compile time:
 * - Unique index within the node's port collection
 * - Transport type (e.g., ActiveQueue<Message>)
 * - Direction (Input or Output)
 * - Human-readable name
 * - List of payload types that may flow through the port
 *
 * @tparam Index Zero-based port index within the node
 * @tparam TransportT Type of the transport mechanism (e.g., queue, callback)
 * @tparam Dir Direction of data flow (Input or Output)
 * @tparam Name Null-terminated string literal with the port's name
 * @tparam Payloads PayloadList of types that can flow through this port (default: empty)
 *
 * Example:
 * @code
 * using InputPort = PortSpec<
 *     0,
 *     ActiveQueue<Message>,
 *     PortDirection::Input,
 *     "in_data",
 *     PayloadList<AccelerometerData, GyroData>
 * >;
 *
 * static_assert(InputPort::index == 0);
 * static_assert(InputPort::direction == PortDirection::Input);
 * @endcode
 */
template<std::size_t Index, typename TransportT, PortDirection Dir,
    const char* Name, typename Payloads = PayloadList<>>
struct PortSpec {
    /// Port index (0-based)
    static constexpr std::size_t index = Index;
    
    /// Type of the transport mechanism (e.g., ActiveQueue<Message>)
    using transport_type = TransportT;
    
    /// List of payload types that can flow through this port
    using payloads = Payloads;
    
    /// Direction of data flow (Input or Output)
    static constexpr PortDirection direction = Dir;
    
    /// Null-terminated string literal with the port's human-readable name
    static constexpr const char* name = Name;
};

// ===================================================================================
// Type Name Utilities
// -----------------------------------------------------------------------------------
// Compile-time utilities for extracting and converting type names for reflection
// and runtime type inspection.
// ===================================================================================

/**
 * @brief Get the name of a transport type at compile time
 *
 * Uses the TypeInfo utilities to extract a human-readable name for the
 * transport type. Useful for generating reflection data and debug output.
 *
 * @tparam T Transport type (e.g., ActiveQueue<Message>)
 * @return Constexpr string view with the type name
 *
 * Example:
 * @code
 * constexpr auto name = TransportName<ActiveQueue<int>>();
 * @endcode
 */
template <typename T>
constexpr std::string_view TransportName() {
    return TypeName<T>();
}

// ===================================================================================
// Payload List String Conversion
// -----------------------------------------------------------------------------------
// Convert a compile-time PayloadList to a runtime string representation
// for reflection and introspection purposes. Empty lists return an empty string.
// ===================================================================================

/**
 * @brief Convert a PayloadList to a pipe-delimited string of type names
 *
 * Produces a human-readable string representation of all payload types in the list,
 * separated by pipe characters. Useful for serialization, logging, and reflection.
 *
 * @tparam Ts Type list of payloads
 * @param Empty parameter (type deduction only)
 * @return String with type names separated by '|', or empty string if no payloads
 *
 * Example:
 * @code
 * using Payloads = PayloadList<int, double, std::string>;
 * std::string types = PayloadListToString(Payloads());
 * // Result: "int|double|std::string"
 *
 * using EmptyPayloads = PayloadList<>;
 * std::string empty = PayloadListToString(EmptyPayloads());
 * // Result: ""
 * @endcode
 */
template <typename... Ts>
std::string PayloadListToString(PayloadList<Ts...>) {
    if constexpr (sizeof...(Ts) == 0) {
        // Empty payload list
        return {};
    } else {
        // Build comma-separated type names
        std::string out;
        ((out += std::string(TypeName<Ts>()) + "|"), ...);
        // Remove trailing pipe separator
        out.pop_back();
        return out;
    }
}

// template <typename Port>
// INode::PortMetadata ToMetadata() {
//     return {
//         Port::index,
//         PayloadListToString(typename Port::payloads{}),
//         Port::direction == PortDirection::Input ? "input" : "output",
//         std::string(Port::name)
//     };
// }

}  // namespace graph

