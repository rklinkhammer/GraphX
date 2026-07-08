/**
 * @file AccelTypes.hpp
 * @brief Accel Types GPU acceleration support.
 *
 * @details Provides backend-neutral accelerator token and validation support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "graph/EdgeControl.hpp"

#include <array>
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace graph::gpu::accel {

constexpr std::size_t kMaxTensorRank = 8;

// Backend tag used to keep graph-facing payload contracts backend-neutral.
/**
 * @enum BackendKind
 * @brief Backend Kind values.
 *
 * @details Enumerates stable options or status values used by the libgpu API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class BackendKind : std::uint8_t {
    Unknown = 0,
    CPU,
    CUDA,
    SYCL,
    Metal
};

/**

 * @enum DataType

 * @brief Data Type values.

 *

 * @details Enumerates stable options or status values used by the libgpu API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

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

/**

 * @enum ReleasePolicy

 * @brief Release Policy values.

 *

 * @details Enumerates stable options or status values used by the libgpu API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

enum class ReleasePolicy : std::uint8_t {
    Manual = 0,
    AutoOnNodeCompletion,
    AutoOnGraphCompletion
};

/**

 * @enum CollectiveKind

 * @brief Collective Kind values.

 *

 * @details Enumerates stable options or status values used by the libgpu API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

enum class CollectiveKind : std::uint8_t {
    Unknown = 0,
    AllReduce,
    AllGather,
    ReduceScatter
};

/**

 * @struct TensorLayout

 * @brief Tensor Layout data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct TensorLayout {
    std::uint8_t rank{0};
    std::array<std::uint64_t, kMaxTensorRank> shape{};
    std::array<std::uint64_t, kMaxTensorRank> stride{};
};

/**

 * @struct DeviceBufferView

 * @brief Device Buffer View data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct DeviceBufferView {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t session_id{0};
    void* device_ptr{nullptr};
    std::uint64_t bytes{0};
    DataType dtype{DataType::Unknown};
    TensorLayout layout{};
    std::uint32_t device_id{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t ready_event{0};
};

/**

 * @struct HostPinnedBufferView

 * @brief Host Pinned Buffer View data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct HostPinnedBufferView {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t session_id{0};
    void* host_ptr{nullptr};
    std::uint64_t bytes{0};
    DataType dtype{DataType::Unknown};
    TensorLayout layout{};
    std::uint64_t allocator_id{0};
};

/**

 * @struct BufferLease

 * @brief Buffer Lease data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct BufferLease {
    std::uint64_t session_id{0};
    std::uint64_t pool_id{0};
    std::uint64_t allocation_id{0};
    ReleasePolicy release_policy{ReleasePolicy::Manual};
    DeviceBufferView device_view{};
    HostPinnedBufferView host_view{};
};

/**

 * @struct TransferTicket

 * @brief Transfer Ticket data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct TransferTicket {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t session_id{0};
    std::uint64_t transfer_id{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
    DeviceBufferView dst_device{};
    HostPinnedBufferView src_host{};
    DeviceBufferView src_device{};
    HostPinnedBufferView dst_host{};
};

/**

 * @struct KernelLaunchConfig

 * @brief Kernel Launch Config data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct KernelLaunchConfig {
    std::uint32_t grid_x{1};
    std::uint32_t grid_y{1};
    std::uint32_t grid_z{1};
    std::uint32_t block_x{1};
    std::uint32_t block_y{1};
    std::uint32_t block_z{1};
    std::uint32_t shared_mem_bytes{0};
};

/**

 * @struct KernelTicket

 * @brief Kernel Ticket data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct KernelTicket {
    BackendKind backend{BackendKind::Unknown};
    std::uint64_t session_id{0};
    std::uint64_t kernel_id{0};
    KernelLaunchConfig launch{};
    std::uint32_t arg_count{0};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
};

/**

 * @struct DeviceShardDescriptor

 * @brief Device Shard Descriptor data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct DeviceShardDescriptor {
    TensorLayout global_layout{};
    std::uint32_t shard_index{0};
    std::uint32_t shard_count{1};
    std::uint32_t owning_device_id{0};
    std::uint64_t element_offset{0};
    std::uint64_t element_length{0};
};

/**

 * @struct CollectiveTicket

 * @brief Collective Ticket data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct CollectiveTicket {
    BackendKind backend{BackendKind::Unknown};
    CollectiveKind kind{CollectiveKind::Unknown};
    std::uint64_t group_id{0};
    std::uint32_t rank{0};
    std::uint32_t world_size{1};
    std::uint64_t execution_queue_id{0};
    std::uint64_t completion_event{0};
};

/**
 * @struct ControlToken
 * @brief Backend-neutral control envelope for accelerated graph stages.
 *
 * @details Carries a domain-defined sidecar together with opaque accelerator
 * transport metadata. The sidecar owns domain identity and routing semantics.
 * Buffer views, leases, transfer tickets, and kernel tickets describe transport
 * state only and must not be used as domain identity.
 */
template <typename SidecarT>
struct ControlToken {
    using sidecar_type = SidecarT;

    std::uint64_t token_id{};
    graph::EdgeControl edge_control{};
    SidecarT sidecar{};
    BufferLease lease{};
    DeviceBufferView device_view{};
    HostPinnedBufferView host_view{};
    TransferTicket transfer_ticket{};
    KernelTicket kernel_ticket{};
    bool has_lease{false};
    bool has_device_view{false};
    bool has_host_view{false};
    bool has_transfer_ticket{false};
    bool has_kernel_ticket{false};
};

template <typename T>
struct IsControlToken : std::false_type {};

template <typename SidecarT>
struct IsControlToken<ControlToken<SidecarT>> : std::true_type {};

template <typename T>
inline constexpr bool IsControlTokenV =
    IsControlToken<std::remove_cvref_t<T>>::value;

template <typename T>
concept ControlTokenType = IsControlTokenV<T>;

template <typename T, typename SidecarT>
concept ControlTokenFor =
    ControlTokenType<T> &&
    std::same_as<typename std::remove_cvref_t<T>::sidecar_type,
                 std::remove_cvref_t<SidecarT>>;

} // namespace graph::gpu::accel
