/**
 * @file AccelTypes.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace graph::gpu::accel {

constexpr std::size_t kMaxTensorRank = 8;

// Backend tag used to keep graph-facing payload contracts backend-neutral.
enum class BackendKind : std::uint8_t {
    Unknown = 0,
    CUDA,
    SYCL,
    Metal
};

enum class DataType : std::uint8_t {
    Unknown = 0,
    Float16,
    Float32,
    Float64,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64
};

enum class ReleasePolicy : std::uint8_t {
    Manual = 0,
    AutoOnNodeCompletion,
    AutoOnGraphCompletion
};

enum class CollectiveKind : std::uint8_t {
    Unknown = 0,
    AllReduce,
    AllGather,
    ReduceScatter
};

struct TensorLayout {
    std::uint8_t rank{0};
    std::array<std::uint64_t, kMaxTensorRank> shape{};
    std::array<std::uint64_t, kMaxTensorRank> stride{};
};

struct DeviceBufferView {
    BackendKind backend{BackendKind::Unknown};
    void* device_ptr{nullptr};
    std::uint64_t bytes{0};
    DataType dtype{DataType::Unknown};
    TensorLayout layout{};
    std::uint32_t device_id{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t ready_event{0};
};

struct HostPinnedBufferView {
    BackendKind backend{BackendKind::Unknown};
    void* host_ptr{nullptr};
    std::uint64_t bytes{0};
    DataType dtype{DataType::Unknown};
    TensorLayout layout{};
    std::uint64_t allocator_id{0};
};

struct BufferLease {
    std::uint64_t pool_id{0};
    std::uint64_t allocation_id{0};
    ReleasePolicy release_policy{ReleasePolicy::Manual};
    DeviceBufferView device_view{};
    HostPinnedBufferView host_view{};
};

struct TransferTicket {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t transfer_id{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
    DeviceBufferView dst_device{};
    HostPinnedBufferView src_host{};
    DeviceBufferView src_device{};
    HostPinnedBufferView dst_host{};
};

struct KernelLaunchConfig {
    std::uint32_t grid_x{1};
    std::uint32_t grid_y{1};
    std::uint32_t grid_z{1};
    std::uint32_t block_x{1};
    std::uint32_t block_y{1};
    std::uint32_t block_z{1};
    std::uint32_t shared_mem_bytes{0};
};

struct KernelTicket {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t kernel_id{0};
    KernelLaunchConfig launch{};
    std::uint32_t arg_count{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
};

struct DeviceShardDescriptor {
    TensorLayout global_layout{};
    std::uint32_t shard_index{0};
    std::uint32_t shard_count{1};
    std::uint32_t owning_device_id{0};
    std::uint64_t element_offset{0};
    std::uint64_t element_length{0};
};

struct CollectiveTicket {
    BackendKind backend{BackendKind::Unknown};
    CollectiveKind kind{CollectiveKind::Unknown};
    std::uint64_t group_id{0};
    std::uint32_t rank{0};
    std::uint32_t world_size{1};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
};

} // namespace graph::gpu::accel
