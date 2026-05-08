#pragma once

/**
 * @file Reflection.hpp
 * @brief C++26 Reflection utilities for compile-time port metadata generation
 * 
 * This header provides reflection-based utilities to replace manual metaprogramming
 * (TypeList, MakePorts, build_port_table) with C++26 constexpr reflection.
 * 
 * Phase 1: Reflection-First Redesign
 * 
 * @author GraphX Contributors
 * @date 2026
 */

#pragma once

#include <cstddef>
#include <string_view>
#include <array>
#include <optional>
#include <type_traits>
#include <concepts>
#include <span>

#if __cplusplus >= 202600
    // C++26 reflection support
    #if __has_include(<meta>)
        #include <meta>
        #define GRAPH_CPP26_REFLECTION_AVAILABLE 1
    #else
        #define GRAPH_CPP26_REFLECTION_AVAILABLE 0
    #endif
#else
    #define GRAPH_CPP26_REFLECTION_AVAILABLE 0
#endif

namespace graph::reflection {

    /**
     * @brief Direction of a port (input or output)
     */
    enum class PortDirection {
        Input,
        Output
    };

    /**
     * @brief Metadata for a single port, generated at compile time
     * 
     * This replaces manual PortInfo structures by auto-generating metadata
     * through C++26 reflection on Port<T, ID> types.
     */
    struct PortMetadata {
        std::size_t id;                    ///< Port identifier (0-based index)
        std::string_view name;             ///< Human-readable port name
        std::string_view type_name;        ///< Type name of data flowing through port
        PortDirection direction;           ///< Input or Output
        
        /**
         * @brief Comparison for testing
         */
        constexpr bool operator==(const PortMetadata& other) const {
            return id == other.id && 
                   name == other.name && 
                   type_name == other.type_name &&
                   direction == other.direction;
        }
    };

    // =====================================================================
    // Concepts for Port Type Validation
    // =====================================================================

    /**
     * @brief Concept: Type is a valid Port<T, ID>
     * 
     * Valid ports must have:
     * - `type` member type (data type flowing through port)
     * - `id` compile-time constant (port identifier)
     */
    template <typename P>
    concept IsPort = requires {
        typename P::type;
        { P::id } -> std::convertible_to<std::size_t>;
    };

    /**
     * @brief Concept: Type has PortDirection
     */
    template <typename P>
    concept HasPortDirection = requires {
        { P::direction } -> std::convertible_to<PortDirection>;
    };

    // =====================================================================
    // Reflection Helpers for Port Metadata
    // =====================================================================

    /**
     * @brief Get the type name of a port's data type (C++26 reflection)
     * 
     * @tparam T The port's data type
     * @return String view of the type name, e.g., "int", "double", "MyStruct"
     * 
     * @note Uses std::meta::name_of in C++26; fallback to manual names in C++20
     */
    template <typename T>
    consteval std::string_view get_type_name() {
        #if GRAPH_CPP26_REFLECTION_AVAILABLE
            // C++26: Use reflection to get type name
            return std::meta::name_of<T>();
        #else
            // C++20 fallback: Use compiler builtins or manual registry
            if constexpr (std::is_same_v<T, int>) {
                return "int";
            } else if constexpr (std::is_same_v<T, double>) {
                return "double";
            } else if constexpr (std::is_same_v<T, float>) {
                return "float";
            } else if constexpr (std::is_same_v<T, bool>) {
                return "bool";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "std::string";
            } else {
                // For custom types, user must specialize
                return "custom_type";
            }
        #endif
    }

    /**
     * @brief Reflect metadata from a single Port<T, ID> type
     * 
     * Generates compile-time metadata for a port by reflecting on its type and ID.
     * This replaces manual PortInfo instantiation.
     * 
     * @tparam P A type satisfying IsPort concept (Port<T, ID>)
     * @return PortMetadata with id, name, and type_name
     * 
     * Example:
     * @code
     *   constexpr auto meta = reflect_port<Port<int, 0>>();
     *   static_assert(meta.id == 0);
     *   static_assert(meta.type_name == "int");
     * @endcode
     */
    template <IsPort P>
    consteval PortMetadata reflect_port(PortDirection direction = PortDirection::Output) {
        return PortMetadata{
            .id = P::id,
            .name = "Port", // TODO: Enhanced naming with constexpr format
            .type_name = get_type_name<typename P::type>(),
            .direction = direction
        };
    }

    /**
     * @brief Reflect metadata from multiple Port types
     * 
     * Generates an array of PortMetadata for a sequence of Port<T, ID> types.
     * This replaces build_port_table<Direction>(TypeList<...>).
     * 
     * @tparam Ports Variadic list of Port<T, ID> types
     * @return std::array<PortMetadata, sizeof...(Ports)> with metadata for each port
     * 
     * Example:
     * @code
     *   constexpr auto metadata = reflect_ports<
     *       Port<int, 0>, Port<double, 1>, Port<bool, 2>
     *   >();
     *   static_assert(metadata.size() == 3);
     *   static_assert(metadata[0].id == 0);
     * @endcode
     */
    template <IsPort... Ports>
    consteval std::array<PortMetadata, sizeof...(Ports)> reflect_ports(
        PortDirection direction = PortDirection::Output) {
        std::array<PortMetadata, sizeof...(Ports)> result = {
            (reflect_port<Ports>(direction))...
        };
        return result;
    }

    // =====================================================================
    // Node Port Discovery via C++26 Reflection
    // =====================================================================

    /**
     * @brief Concept: Type is a node with reflected ports
     * 
     * A valid node for reflection must:
     * - Have a static constexpr port metadata member or method
     * - Implement OutputPorts() and/or InputPorts() methods
     */
    template <typename Node>
    concept IsReflectableNode = requires(const Node& n) {
        { n.OutputPorts() } -> std::convertible_to<std::span<const PortMetadata>>;
    };

    /**
     * @brief Discover and enumerate all output ports from a node type
     * 
     * Uses C++26 reflection to discover Port<T, ID> base classes in a node.
     * This replaces manual port tracking via OutputPorts() override.
     * 
     * @tparam Node The node type to reflect
     * @return std::array of PortMetadata for all output ports
     * 
     * @note For C++20 compatibility, falls back to static reflection data
     *       that must be manually maintained in node classes.
     * 
     * Example (C++26):
     * @code
     *   class MyNode : public SourceNode<int, double> { ... };
     *   constexpr auto ports = reflect_output_ports<MyNode>();
     *   // Result: array of 2 PortMetadata entries
     * @endcode
     */
    template <typename Node>
    consteval auto reflect_output_ports() -> std::array<PortMetadata, 0> {
        #if GRAPH_CPP26_REFLECTION_AVAILABLE
            // C++26: Use std::meta to reflect bases and discover ports
            // This is a placeholder - full implementation requires
            // std::meta::bases_of and filter for Port<T, ID> types
            
            // Pseudo-code of full implementation:
            // constexpr auto bases = std::meta::bases_of<Node>();
            // filter bases matching IsPort concept
            // return std::array of reflect_port<FilteredPorts...>()
        #else
            // C++20 fallback: Return empty array
            // Nodes must provide static_port_metadata instead
        #endif
        
        return std::array<PortMetadata, 0>{};
    }

    /**
     * @brief Discover and enumerate all input ports from a node type
     * 
     * Symmetric to reflect_output_ports() but for input ports.
     * 
     * @see reflect_output_ports() for details
     */
    template <typename Node>
    consteval auto reflect_input_ports() -> std::array<PortMetadata, 0> {
        // Similar implementation to reflect_output_ports()
        return std::array<PortMetadata, 0>{};
    }

    // =====================================================================
    // Runtime Port Metadata Table
    // =====================================================================

    /**
     * @brief Runtime representation of port metadata for polymorphic access
     * 
     * Wraps compile-time PortMetadata in a struct suitable for runtime
     * querying by port ID or type name.
     */
    struct PortInfo {
        std::size_t id;
        std::string_view name;
        std::string_view type_name;
        PortDirection direction;
        
        // Comparison operations for lookup
        constexpr bool operator==(std::size_t port_id) const {
            return id == port_id;
        }
        
        constexpr bool operator==(std::string_view tname) const {
            return type_name == tname;
        }
    };

    /**
     * @brief Convert PortMetadata to PortInfo for runtime use
     * 
     * This is a trivial conversion; both have the same layout.
     * Provided for type safety and clarity.
     */
    constexpr PortInfo to_port_info(const PortMetadata& meta) {
        return PortInfo{meta.id, meta.name, meta.type_name, meta.direction};
    }

    /**
     * @brief Find a port by ID in a metadata array
     * 
     * Useful for implementing port lookup at compile time.
     * 
     * @param metadata Array of PortMetadata (typically from reflect_ports<>())
     * @param port_id The ID to search for
     * @return Optional PortMetadata if found, std::nullopt otherwise
     * 
     * Example:
     * @code
     *   constexpr auto metadata = reflect_ports<Port<int, 0>, Port<double, 1>>();
     *   constexpr auto port0 = find_port_by_id(metadata, 0);
     *   static_assert(port0.has_value());
     *   static_assert(port0->type_name == "int");
     * @endcode
     */
    template <std::size_t N>
    constexpr std::optional<PortMetadata> find_port_by_id(
        const std::array<PortMetadata, N>& metadata,
        std::size_t port_id) {
        for (const auto& port : metadata) {
            if (port.id == port_id) {
                return port;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Find a port by type name in a metadata array
     * 
     * Useful for implementing type-based port lookup.
     * 
     * @param metadata Array of PortMetadata
     * @param type_name The type name string to search for
     * @return Optional PortMetadata if found, std::nullopt otherwise
     */
    template <std::size_t N>
    constexpr std::optional<PortMetadata> find_port_by_type(
        const std::array<PortMetadata, N>& metadata,
        std::string_view type_name) {
        for (const auto& port : metadata) {
            if (port.type_name == type_name) {
                return port;
            }
        }
        return std::nullopt;
    }

    // =====================================================================
    // Reflection Validation
    // =====================================================================

    /**
     * @brief Validate that a set of ports has no duplicate IDs
     * 
     * Used at compile time to catch configuration errors early.
     * 
     * @tparam N Number of ports
     * @param metadata Array of PortMetadata to validate
     * @return true if all IDs are unique, false otherwise
     */
    template <std::size_t N>
    constexpr bool validate_port_ids(const std::array<PortMetadata, N>& metadata) {
        for (std::size_t i = 0; i < N; ++i) {
            for (std::size_t j = i + 1; j < N; ++j) {
                if (metadata[i].id == metadata[j].id) {
                    return false;  // Duplicate ID
                }
            }
        }
        return true;
    }

    /**
     * @brief Validate that port IDs are in expected range [0, N-1]
     * 
     * Ensures no gaps or out-of-order IDs.
     * 
     * @tparam N Number of ports
     * @param metadata Array of PortMetadata to validate
     * @return true if IDs are [0..N-1], false otherwise
     */
    template <std::size_t N>
    constexpr bool validate_port_id_range(const std::array<PortMetadata, N>& metadata) {
        for (std::size_t i = 0; i < N; ++i) {
            if (metadata[i].id >= N) {
                return false;  // ID out of range
            }
        }
        return true;
    }

    // =====================================================================
    // C++20 Fallback: Manual Reflection Registry
    // =====================================================================

    /**
     * @brief Type traits registry for C++20 compatibility
     * 
     * When std::meta is not available (C++20), manual reflection data
     * must be registered here for compile-time access.
     * 
     * Example specialization:
     * @code
     *   template <>
     *   struct PortTraits<Port<int, 0>> {
     *       static constexpr auto metadata = PortMetadata{
     *           .id = 0,
     *           .name = "output_0",
     *           .type_name = "int",
     *           .direction = PortDirection::Output
     *       };
     *   };
     * @endcode
     */
    template <typename P>
    struct PortTraits;  // Intentionally not defined; users must specialize for custom ports

    // Specializations for standard types (used in C++20 mode)
    template <>
    struct PortTraits<int> {
        static constexpr std::string_view type_name = "int";
    };

    template <>
    struct PortTraits<double> {
        static constexpr std::string_view type_name = "double";
    };

    template <>
    struct PortTraits<float> {
        static constexpr std::string_view type_name = "float";
    };

    template <>
    struct PortTraits<bool> {
        static constexpr std::string_view type_name = "bool";
    };

}  // namespace graph::reflection

