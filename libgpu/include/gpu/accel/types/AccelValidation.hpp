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
           IsValidLayout(view.layout);
}

inline constexpr bool IsValidView(const HostPinnedBufferView& view) noexcept {
    return view.host_ptr != nullptr &&
           view.bytes > 0 &&
           IsKnownDataType(view.dtype) &&
           IsValidLayout(view.layout);
}

inline constexpr bool IsValidShardDescriptor(const DeviceShardDescriptor& shard) noexcept {
    return shard.shard_count > 0 &&
           shard.shard_index < shard.shard_count &&
           shard.element_length > 0 &&
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
