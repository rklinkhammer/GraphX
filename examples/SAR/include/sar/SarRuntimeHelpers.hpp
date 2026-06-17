// SPDX-License-Identifier: MIT

/**
 * @file SarRuntimeHelpers.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "graph/GraphManager.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <atomic>
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

inline void* OpaqueHostPointer() noexcept {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1u));
}

inline std::uint64_t OpaqueReadyEventNotSignaled() noexcept {
    return 0u;
}

inline std::uint64_t NextOpaqueEventId() {
    static std::atomic<std::uint64_t> next_event{1u};
    return next_event.fetch_add(1u, std::memory_order_relaxed);
}

inline void* SyntheticDevicePointer(std::uint64_t byte_count,
                                    std::uint64_t sequence) noexcept {
    const auto token = ((byte_count + 1u) << 8u) | ((sequence + 1u) & 0xFFu);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}

inline void* SyntheticDevicePointer(
    const graph::gpu::accel::HostPinnedBufferView& input,
    std::uint64_t sequence) noexcept {
    return SyntheticDevicePointer(input.bytes, sequence);
}

inline void* SyntheticDevicePointer(
    const graph::gpu::accel::DeviceBufferView& input,
    std::uint64_t sequence) noexcept {
    return SyntheticDevicePointer(input.bytes, sequence);
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
