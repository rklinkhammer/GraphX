/**
 * @file AccelValidation.hpp
 * @brief Accel Validation GPU acceleration support.
 *
 * @details Provides backend-neutral accelerator token and validation support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

namespace graph::gpu::accel {

inline constexpr bool IsKnownDataType(DataType dtype) noexcept {
    return dtype != DataType::Unknown;
}

inline constexpr bool IsValidLayout(const TensorLayout& layout) noexcept {
    if (layout.rank > kMaxTensorRank) {
        return false;
    }

    for (std::uint8_t i = 0; i < layout.rank; ++i) {
        if (layout.shape[i] == 0 || layout.stride[i] == 0) {
            return false;
        }
    }

    return true;
}

inline constexpr bool IsValidView(const DeviceBufferView& view) noexcept {
    return view.backend != BackendKind::Unknown &&
           view.device_ptr != nullptr &&
           view.bytes > 0 &&
           IsKnownDataType(view.dtype) &&
           /**
            * @brief Reports whether Is Valid Layout is true.
            *
            * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
            * @return Method-specific result, status, or produced value when the signature provides one.
            */
           IsValidLayout(view.layout);
}

inline constexpr bool IsValidView(const HostPinnedBufferView& view) noexcept {
    return view.host_ptr != nullptr &&
           view.bytes > 0 &&
           IsKnownDataType(view.dtype) &&
           /**
            * @brief Reports whether Is Valid Layout is true.
            *
            * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
            * @return Method-specific result, status, or produced value when the signature provides one.
            */
           IsValidLayout(view.layout);
}

inline constexpr bool IsValidLease(const BufferLease& lease) noexcept {
    const bool has_device_view = IsValidView(lease.device_view);
    const bool has_host_view = IsValidView(lease.host_view);
    return lease.pool_id != 0 &&
           lease.allocation_id != 0 &&
           (has_device_view || has_host_view);
}

inline constexpr bool IsValidTransferTicket(const TransferTicket& ticket) noexcept {
    const bool has_h2d = IsValidView(ticket.src_host) && IsValidView(ticket.dst_device);
    const bool has_d2h = IsValidView(ticket.src_device) && IsValidView(ticket.dst_host);
    const bool has_d2d = IsValidView(ticket.src_device) && IsValidView(ticket.dst_device);
    return ticket.backend != BackendKind::Unknown &&
           ticket.transfer_id != 0 &&
           ticket.execution_queue_id != 0 &&
           ticket.completion_event != 0 &&
           (has_h2d || has_d2h || has_d2d);
}

inline constexpr bool IsValidShardDescriptor(const DeviceShardDescriptor& shard) noexcept {
    return shard.shard_count > 0 &&
           shard.shard_index < shard.shard_count &&
           shard.element_length > 0 &&
           /**
            * @brief Reports whether Is Valid Layout is true.
            *
            * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
            * @return Method-specific result, status, or produced value when the signature provides one.
            */
           IsValidLayout(shard.global_layout);
}

inline constexpr bool IsValidCollectiveTicket(const CollectiveTicket& ticket) noexcept {
    return ticket.backend != BackendKind::Unknown &&
           ticket.kind != CollectiveKind::Unknown &&
           ticket.world_size > 0 &&
           ticket.rank < ticket.world_size;
}

inline constexpr bool IsValidKernelTicket(const KernelTicket& ticket) noexcept {
    return ticket.backend != BackendKind::Unknown &&
           ticket.kernel_id != 0 &&
           ticket.launch.grid_x > 0 &&
           ticket.launch.grid_y > 0 &&
           ticket.launch.grid_z > 0 &&
           ticket.launch.block_x > 0 &&
           ticket.launch.block_y > 0 &&
           ticket.launch.block_z > 0;
}

} // namespace graph::gpu::accel
