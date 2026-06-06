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

class IMetalContextCapability {
public:
    virtual ~IMetalContextCapability() = default;

    virtual bool SelectDevice(std::uint32_t device_id) = 0;
    virtual std::uint32_t CurrentDevice() const = 0;

    virtual std::uint64_t CreateCommandQueue() = 0;
    virtual void DestroyCommandQueue(std::uint64_t queue_id) = 0;

    virtual std::uint64_t CreateEvent() = 0;
    virtual void DestroyEvent(std::uint64_t event_id) = 0;
    virtual bool IsEventComplete(std::uint64_t event_id) const = 0;
    virtual bool WaitEvent(std::uint64_t event_id, std::uint64_t timeout_ms) = 0;
};

class IMetalSharedQueueCapability {
public:
    virtual ~IMetalSharedQueueCapability() = default;

    virtual std::uint64_t GetOrCreateQueueId() = 0;
};

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

class IMetalMemoryPoolCapability {
public:
    virtual ~IMetalMemoryPoolCapability() = default;

    virtual bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                                accel::BufferLease& out_lease) = 0;
    virtual bool AllocateHost(std::uint64_t bytes,
                              accel::BufferLease& out_lease) = 0;
    virtual bool Release(const accel::BufferLease& lease) = 0;
};

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

class IMetalKernelDescriptorCapability {
public:
    virtual ~IMetalKernelDescriptorCapability() = default;

    virtual bool RegisterKernelDescriptor(const MetalKernelDescriptor& descriptor) = 0;
};

class IMetalTelemetryCapability {
public:
    virtual ~IMetalTelemetryCapability() = default;

    virtual void RecordTransfer(const accel::TransferTicket& ticket,
                                std::uint64_t duration_ns) = 0;
    virtual void RecordKernel(const accel::KernelTicket& ticket,
                              std::uint64_t duration_ns) = 0;
    virtual void IncrementErrorCounter(std::string_view error_code) = 0;
};

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