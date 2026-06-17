/**
 * @file AccelFormatting.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <ostream>

namespace graph::gpu::accel {

inline const char* ToString(BackendKind backend) noexcept {
    switch (backend) {
    case BackendKind::CUDA:
        return "CUDA";
    case BackendKind::SYCL:
        return "SYCL";
    case BackendKind::Metal:
        return "Metal";
    case BackendKind::Unknown:
    default:
        return "Unknown";
    }
}

inline const char* ToString(DataType dtype) noexcept {
    switch (dtype) {
    case DataType::Float16:
        return "Float16";
    case DataType::Float32:
        return "Float32";
    case DataType::Float64:
        return "Float64";
    case DataType::Int8:
        return "Int8";
    case DataType::UInt8:
        return "UInt8";
    case DataType::Int16:
        return "Int16";
    case DataType::UInt16:
        return "UInt16";
    case DataType::Int32:
        return "Int32";
    case DataType::UInt32:
        return "UInt32";
    case DataType::Int64:
        return "Int64";
    case DataType::UInt64:
        return "UInt64";
    case DataType::Unknown:
    default:
        return "Unknown";
    }
}

inline const char* ToString(CollectiveKind kind) noexcept {
    switch (kind) {
    case CollectiveKind::AllReduce:
        return "AllReduce";
    case CollectiveKind::AllGather:
        return "AllGather";
    case CollectiveKind::ReduceScatter:
        return "ReduceScatter";
    case CollectiveKind::Unknown:
    default:
        return "Unknown";
    }
}

inline std::ostream& operator<<(std::ostream& out, const TensorLayout& layout) {
    out << "rank=" << static_cast<unsigned>(layout.rank) << " shape=[";
    for (std::uint8_t index = 0; index < layout.rank; ++index) {
        if (index != 0) {
            out << ',';
        }
        out << layout.shape[index];
    }
    out << "] stride=[";
    for (std::uint8_t index = 0; index < layout.rank; ++index) {
        if (index != 0) {
            out << ',';
        }
        out << layout.stride[index];
    }
    out << ']';
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const DeviceBufferView& view) {
    out << "DeviceBufferView{" << ToString(view.backend)
        << ", device_id=" << view.device_id
        << ", bytes=" << view.bytes
        << ", dtype=" << ToString(view.dtype)
        << ", queue=" << view.execution_queue_id
        << ", event=" << view.ready_event << ", layout={" << view.layout << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const HostPinnedBufferView& view) {
    out << "HostPinnedBufferView{" << ToString(view.backend)
        << ", allocator_id=" << view.allocator_id
        << ", bytes=" << view.bytes
        << ", dtype=" << ToString(view.dtype)
        << ", layout={" << view.layout << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const BufferLease& lease) {
    out << "BufferLease{pool_id=" << lease.pool_id
        << ", allocation_id=" << lease.allocation_id
        << ", release_policy=" << static_cast<unsigned>(lease.release_policy)
        << ", device={" << lease.device_view << "}"
        << ", host={" << lease.host_view << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const TransferTicket& ticket) {
    out << "TransferTicket{" << ToString(ticket.backend)
        << ", transfer_id=" << ticket.transfer_id
        << ", queue=" << ticket.execution_queue_id
        << ", event=" << ticket.completion_event
        << ", src_host={" << ticket.src_host << "}"
        << ", dst_host={" << ticket.dst_host << "}"
        << ", src_device={" << ticket.src_device << "}"
        << ", dst_device={" << ticket.dst_device << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const KernelTicket& ticket) {
    out << "KernelTicket{" << ToString(ticket.backend)
        << ", kernel_id=" << ticket.kernel_id
        << ", queue=" << ticket.execution_queue_id
        << ", event=" << ticket.completion_event
        << ", arg_count=" << ticket.arg_count
        << ", launch={grid=" << ticket.launch.grid_x << 'x' << ticket.launch.grid_y << 'x'
        << ticket.launch.grid_z << ", block=" << ticket.launch.block_x << 'x'
        << ticket.launch.block_y << 'x' << ticket.launch.block_z
        << ", shared_mem_bytes=" << ticket.launch.shared_mem_bytes << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const DeviceShardDescriptor& shard) {
    out << "DeviceShardDescriptor{shard_index=" << shard.shard_index
        << ", shard_count=" << shard.shard_count
        << ", owning_device_id=" << shard.owning_device_id
        << ", element_offset=" << shard.element_offset
        << ", element_length=" << shard.element_length
        << ", layout={" << shard.global_layout << "}}";
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const CollectiveTicket& ticket) {
    out << "CollectiveTicket{" << ToString(ticket.backend)
        << ", kind=" << ToString(ticket.kind)
        << ", group_id=" << ticket.group_id
        << ", rank=" << ticket.rank
        << ", world_size=" << ticket.world_size
        << ", queue=" << ticket.execution_queue_id
        << ", event=" << ticket.completion_event << "}";
    return out;
}

} // namespace graph::gpu::accel