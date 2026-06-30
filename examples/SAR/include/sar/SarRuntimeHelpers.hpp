// SPDX-License-Identifier: MIT

/**
 * @file SarRuntimeHelpers.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>

namespace sar::runtime {

using SteadyClock = std::chrono::steady_clock;

inline std::uint64_t ElapsedUs(const SteadyClock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - start);
    const auto count = static_cast<std::uint64_t>(elapsed.count());
    return (count == 0u) ? 1u : count;
}

namespace detail {

inline std::uint64_t NextSyntheticCompletionEvent() {
    static std::atomic<std::uint64_t> next_event{1u};
    return next_event.fetch_add(1u, std::memory_order_relaxed);
}

inline void* SyntheticDeviceAddress(std::uint64_t byte_count,
                                    std::uint64_t sequence) noexcept {
    const auto token = ((byte_count + 1u) << 8u) | ((sequence + 1u) & 0xFFu);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}

} // namespace detail

inline graph::gpu::accel::HostPinnedBufferView MakeSyntheticHostView(
    graph::gpu::accel::HostPinnedBufferView view) noexcept {
    static std::byte storage{};
    view.host_ptr = &storage;
    return view;
}

inline graph::gpu::accel::DeviceBufferView MakeSyntheticDeviceView(
    graph::gpu::accel::DeviceBufferView view,
    std::uint64_t sequence) noexcept {
    view.device_ptr = detail::SyntheticDeviceAddress(view.bytes, sequence);
    view.ready_event = 0u;
    return view;
}

inline graph::gpu::accel::TransferTicket MakeSyntheticTransferTicket(
    graph::gpu::accel::TransferTicket ticket) {
    ticket.completion_event = detail::NextSyntheticCompletionEvent();
    return ticket;
}

inline graph::gpu::accel::KernelTicket MakeSyntheticKernelTicket(
    graph::gpu::accel::KernelTicket ticket) {
    ticket.completion_event = detail::NextSyntheticCompletionEvent();
    return ticket;
}

inline std::shared_ptr<SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SarDiagnosticsSinkNode") {
            continue;
        }
        return wrapper->GetNode<SarDiagnosticsSinkNode>();
    }

    return nullptr;
}

} // namespace sar::runtime
