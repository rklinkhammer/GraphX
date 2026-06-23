// SPDX-License-Identifier: MIT

/**
 * @file AccelTokenContracts.hpp
 * @brief Compile-time token contract concepts for GraphX node ports.
 */

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace graph {

namespace detail {

template <typename PortT, typename = void>
struct PortPayloadType {
    using type = std::remove_cvref_t<PortT>;
};

template <typename PortT>
struct PortPayloadType<PortT, std::void_t<typename std::remove_cvref_t<PortT>::type>> {
    using type = typename std::remove_cvref_t<PortT>::type;
};

template <typename PortT>
using PortPayloadTypeT = typename PortPayloadType<PortT>::type;

}  // namespace detail

template <typename T>
concept AccelControlTokenType = gpu::accel::ControlTokenType<T>;

template <typename T, typename SidecarT>
concept AccelControlTokenFor = gpu::accel::ControlTokenFor<T, SidecarT>;

template <typename NodeT, std::size_t PortIndex>
concept HasInputPortType = requires {
    typename NodeT::template InputPortType<PortIndex>;
};

template <typename NodeT, std::size_t PortIndex>
concept HasOutputPortType = requires {
    typename NodeT::template OutputPortType<PortIndex>;
};

template <typename NodeT, std::size_t PortIndex>
concept InputPortUsesAccelControlToken =
    HasInputPortType<NodeT, PortIndex> &&
    AccelControlTokenType<
        detail::PortPayloadTypeT<typename NodeT::template InputPortType<PortIndex>>>;

template <typename NodeT, std::size_t PortIndex>
concept OutputPortUsesAccelControlToken =
    HasOutputPortType<NodeT, PortIndex> &&
    AccelControlTokenType<
        detail::PortPayloadTypeT<typename NodeT::template OutputPortType<PortIndex>>>;

template <typename NodeT, std::size_t PortIndex, typename SidecarT>
concept InputPortUsesAccelControlTokenFor =
    HasInputPortType<NodeT, PortIndex> &&
    AccelControlTokenFor<
                         detail::PortPayloadTypeT<typename NodeT::template InputPortType<PortIndex>>,
                         SidecarT>;

template <typename NodeT, std::size_t PortIndex, typename SidecarT>
concept OutputPortUsesAccelControlTokenFor =
    HasOutputPortType<NodeT, PortIndex> &&
    AccelControlTokenFor<
                         detail::PortPayloadTypeT<typename NodeT::template OutputPortType<PortIndex>>,
                         SidecarT>;

template <typename NodeT, std::size_t PortIndex, typename ValueT>
concept InputPortTypeIs =
    HasInputPortType<NodeT, PortIndex> &&
    std::same_as<detail::PortPayloadTypeT<typename NodeT::template InputPortType<PortIndex>>,
                 std::remove_cvref_t<ValueT>>;

template <typename NodeT, std::size_t PortIndex, typename ValueT>
concept OutputPortTypeIs =
    HasOutputPortType<NodeT, PortIndex> &&
    std::same_as<detail::PortPayloadTypeT<typename NodeT::template OutputPortType<PortIndex>>,
                 std::remove_cvref_t<ValueT>>;

}  // namespace graph
