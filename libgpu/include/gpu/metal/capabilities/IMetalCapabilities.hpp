/**
 * @file IMetalCapabilities.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace graph::gpu::metal::capabilities {

/**
 * @class IMetalContextCapability
 * @brief IMetalContextCapability class.
 */
class IMetalContextCapability {
public:
    virtual ~IMetalContextCapability() = default;

/**
 * @brief Select device.
 * @param device_id Parameter for select device.
 * @return Result of the operation.
 */
    virtual bool SelectDevice(std::uint32_t device_id) = 0;
/**
 * @brief Current device.
 * @return Result of the operation.
 */
    virtual std::uint32_t CurrentDevice() const = 0;

/**
 * @brief Create command queue.
 * @return Result of the operation.
 */
    virtual std::uint64_t CreateCommandQueue() = 0;
/**
 * @brief Destroy command queue.
 * @param queue_id Parameter for destroy command queue.
 */
    virtual void DestroyCommandQueue(std::uint64_t queue_id) = 0;

/**
 * @brief Create event.
 * @return Result of the operation.
 */
    virtual std::uint64_t CreateEvent() = 0;
/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
    virtual void DestroyEvent(std::uint64_t event_id) = 0;
/**
 * @brief Is event complete.
 * @param event_id Parameter for is event complete.
 * @return Result of the operation.
 */
    virtual bool IsEventComplete(std::uint64_t event_id) const = 0;
/**
 * @brief Wait event.
 * @param event_id Parameter for wait event.
 * @param timeout_ms Parameter for wait event.
 * @return Result of the operation.
 */
    virtual bool WaitEvent(std::uint64_t event_id, std::uint64_t timeout_ms) = 0;
};

/**
 * @class IMetalSharedQueueCapability
 * @brief IMetalSharedQueueCapability class.
 */
class IMetalSharedQueueCapability {
public:
    virtual ~IMetalSharedQueueCapability() = default;

/**
 * @brief Get or create queue id.
 * @return Result of the operation.
 */
    virtual std::uint64_t GetOrCreateQueueId() = 0;
};

/**
 * @class MetalSharedQueueCapability
 * @brief Metal shared queue capability implementation for GraphX.
 */
class MetalSharedQueueCapability final : public IMetalSharedQueueCapability {
public:
    explicit MetalSharedQueueCapability(std::shared_ptr<IMetalContextCapability> context)
        : context_(std::move(context)) {}

    ~MetalSharedQueueCapability() override {
        std::scoped_lock lock(mutex_);
        if (queue_id_ != 0 && context_ != nullptr) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    std::uint64_t GetOrCreateQueueId() override {
        std::scoped_lock lock(mutex_);
        if (queue_id_ != 0) {
            return queue_id_;
        }
        if (context_ == nullptr) {
            return 0;
        }
        queue_id_ = context_->CreateCommandQueue();
        return queue_id_;
    }

private:
    std::shared_ptr<IMetalContextCapability> context_;
    std::uint64_t queue_id_{0};
    std::mutex mutex_;
};

/**
 * @class IMetalMemoryPoolCapability
 * @brief IMetalMemoryPoolCapability class.
 */
class IMetalMemoryPoolCapability {
public:
    virtual ~IMetalMemoryPoolCapability() = default;

    struct MemoryPoolSnapshot {
        std::uint64_t live_device_bytes{0};
        std::uint64_t live_shared_bytes{0};
        std::uint64_t live_host_bytes{0};
        std::uint64_t peak_device_bytes{0};
        std::uint64_t peak_shared_bytes{0};
        std::uint64_t peak_host_bytes{0};
        std::uint64_t allocation_count{0};
        std::uint64_t release_count{0};
    };

    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateHost(std::uint64_t bytes,
                              accel::BufferLease& out_lease) = 0;
/**
 * @brief Release.
 * @param lease Parameter for release.
 * @return Result of the operation.
 */
    virtual bool Release(const accel::BufferLease& lease) = 0;
    [[nodiscard]] virtual MemoryPoolSnapshot Snapshot() const = 0;
};

/**
 * @class IMetalTransferCapability
 * @brief IMetalTransferCapability class.
 */
class IMetalTransferCapability {
public:
    virtual ~IMetalTransferCapability() = default;

    virtual bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2H(const accel::DeviceBufferView& src,
                            accel::HostPinnedBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;

    virtual bool EnqueueD2D(const accel::DeviceBufferView& src,
                            accel::DeviceBufferView& dst,
                            std::uint64_t queue_id,
                            accel::TransferTicket& out_ticket) = 0;
};

enum class MetalKernelSourceKind : std::uint8_t {
    Builtin = 0,
    InlineSource,
    MetallibPath,
};

enum class MetalKernelArgKind : std::uint8_t {
    DeviceBuffer = 0,
};

enum class MetalKernelArgAccess : std::uint8_t {
    ReadOnly = 0,
    WriteOnly,
    ReadWrite,
};

struct MetalKernelArgDescriptor {
    MetalKernelArgKind kind{MetalKernelArgKind::DeviceBuffer};
    MetalKernelArgAccess access{MetalKernelArgAccess::ReadWrite};
};

struct MetalKernelDispatchDescriptor {
    std::uint32_t default_grid_x{1};
    std::uint32_t default_grid_y{1};
    std::uint32_t default_grid_z{1};
    std::uint32_t default_block_x{1};
    std::uint32_t default_block_y{1};
    std::uint32_t default_block_z{1};
};

struct MetalKernelDescriptor {
    std::uint64_t kernel_id{0};
    std::string function_name{};
    MetalKernelSourceKind source_kind{MetalKernelSourceKind::Builtin};
    std::string source_payload{};
    std::vector<MetalKernelArgDescriptor> arg_layout{};
    MetalKernelDispatchDescriptor dispatch{};
};

/**
 * @class IMetalKernelCapability
 * @brief IMetalKernelCapability class.
 */
class IMetalKernelCapability {
public:
    virtual ~IMetalKernelCapability() = default;

    struct RegisteredKernelExecution {
        std::uint32_t arg_count{0};
        accel::KernelLaunchConfig dispatch{};
    };

    virtual bool RegisterKernel(std::uint64_t kernel_id,
                                std::string_view kernel_name) = 0;

    virtual bool TryGetRegisteredKernelExecution(
        std::uint64_t kernel_id,
        RegisteredKernelExecution& out_execution) const = 0;

    virtual bool Launch(const accel::KernelTicket& ticket,
                        void* const* args,
                        std::size_t arg_count) = 0;
};

/**
 * @class IMetalKernelDescriptorCapability
 * @brief IMetalKernelDescriptorCapability class.
 */
class IMetalKernelDescriptorCapability {
public:
    virtual ~IMetalKernelDescriptorCapability() = default;

/**
 * @brief Register kernel descriptor.
 * @param descriptor Parameter for register kernel descriptor.
 * @return Result of the operation.
 */
    virtual bool RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) = 0;
};

/**
 * @class IMetalTelemetryCapability
 * @brief IMetalTelemetryCapability class.
 */
class IMetalTelemetryCapability {
public:
    virtual ~IMetalTelemetryCapability() = default;

    struct TelemetrySnapshot {
        std::uint64_t transfer_samples{0};
        std::uint64_t kernel_samples{0};
        std::uint64_t error_count{0};
        std::uint64_t transfer_total_duration_ns{0};
        std::uint64_t kernel_total_duration_ns{0};
        std::uint64_t last_transfer_duration_ns{0};
        std::uint64_t last_kernel_duration_ns{0};
        std::uint64_t h2d_transfer_samples{0};
        std::uint64_t d2h_transfer_samples{0};
        std::uint64_t d2d_transfer_samples{0};
    };

    virtual void RecordTransfer(const accel::TransferTicket& ticket,
                                std::uint64_t duration_ns) = 0;
    virtual void RecordKernel(const accel::KernelTicket& ticket,
                              std::uint64_t duration_ns) = 0;
/**
 * @brief Increment error counter.
 * @param error_code Parameter for increment error counter.
 */
    virtual void IncrementErrorCounter(std::string_view error_code) = 0;
    [[nodiscard]] virtual TelemetrySnapshot Snapshot() const = 0;
};

/**
 * @class IMetalCollectiveCapability
 * @brief IMetalCollectiveCapability class.
 */
class IMetalCollectiveCapability {
public:
    virtual ~IMetalCollectiveCapability() = default;

    virtual bool AllReduce(accel::DeviceBufferView& in_out,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool AllGather(const accel::DeviceBufferView& input,
                           accel::DeviceBufferView& output,
                           const accel::CollectiveTicket& ticket) = 0;
    virtual bool ReduceScatter(const accel::DeviceBufferView& input,
                               accel::DeviceBufferView& output,
                               const accel::CollectiveTicket& ticket) = 0;
};

} // namespace graph::gpu::metal::capabilities
